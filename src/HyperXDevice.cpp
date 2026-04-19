#include "HyperXDevice.h"

#include <QDebug>
#include <QThread>
#include <array>
#include <cstring>

HyperXDevice::HyperXDevice(QObject *parent)
    : QObject(parent)
{
    hid_init();
}

HyperXDevice::~HyperXDevice()
{
    stop();
    disconnect();
    hid_exit();
}

void HyperXDevice::run()
{
    m_running.store(true);

    QElapsedTimer batteryTimer;
    batteryTimer.start();

    QElapsedTimer reconnectTimer;
    reconnectTimer.start();

    while (m_running.load()) {
        if (!m_connected.load()) {
            if (reconnectTimer.elapsed() >= RECONNECT_INTERVAL_MS || !m_device) {
                if (tryConnect()) {
                    requestBattery();
                    batteryTimer.restart();
                }
                reconnectTimer.restart();
            } else {
                QThread::msleep(500);
            }
            continue;
        }

        uint8_t buf[32] = {};
        int bytes = hid_read_timeout(m_device, buf, sizeof(buf), READ_TIMEOUT_MS);

        if (bytes < 0) {
            qWarning() << "[HyperXDevice] read error, disconnecting";
            disconnect();
            continue;
        }

        if (bytes > 0) {
            processResponse(buf, bytes);
        }

        if (batteryTimer.elapsed() >= BATTERY_POLL_INTERVAL_MS) {
            requestBattery();
            batteryTimer.restart();
        }
    }
}

void HyperXDevice::stop()
{
    m_running.store(false);
}

bool HyperXDevice::tryConnect()
{
    static constexpr uint16_t pids[] = { PID_CLOUD_FLIGHT_NEW, PID_CLOUD_FLIGHT_OLD };

    for (uint16_t pid : pids) {
        hid_device *dev = hid_open(VENDOR_ID, pid, nullptr);
        if (dev) {
            hid_set_nonblocking(dev, 1);
            m_device = dev;
            m_connected.store(true);
            qInfo() << "[HyperXDevice] connected, PID:" << Qt::hex << pid;
            emit deviceConnected();
            return true;
        }
    }
    return false;
}

void HyperXDevice::disconnect()
{
    if (m_device) {
        hid_close(m_device);
        m_device = nullptr;
    }

    bool wasConnected = m_connected.exchange(false);
    m_batteryPercent.store(-1);
    m_charging.store(false);
    m_muted.store(false);

    if (wasConnected) {
        qInfo() << "[HyperXDevice] disconnected";
        emit deviceDisconnected();
    }
}

void HyperXDevice::requestBattery()
{
    if (!m_device) return;

    // Reverse-engineered Cloud Flight battery trigger: 0x21 0xFF 0x05
    // (from HeadsetControl + kondinskis/hyperx-cloud-flight)
    std::array<uint8_t, 20> packet = {};
    packet[0] = 0x21;
    packet[1] = 0xFF;
    packet[2] = 0x05;

    if (hid_write(m_device, packet.data(), packet.size()) < 0) {
        qWarning() << "[HyperXDevice] failed to send battery request";
    }
}

void HyperXDevice::processResponse(const uint8_t *data, int length)
{
    // Protocol: 2 bytes = power/mute events
    if (length == 2) {
        if (data[0] == 0x64) {
            if (data[1] == 0x01) {
                m_connected.store(true);
                requestBattery();
            } else if (data[1] == 0x03) {
                // Headset powered off (dongle stays alive — don't close handle)
                emit deviceDisconnected();
            }
        } else if (data[0] == 0x65) {
            bool muted = (data[1] == 0x04);
            if (muted != m_muted.load()) {
                m_muted.store(muted);
                emit muteChanged(muted);
            }
        }
        return;
    }

    if (length == 5) return; // volume events — irrelevant for tray

    // Protocol: 20 or 15 bytes = battery response
    // Bytes [3..4] are big-endian voltage; above VOLTAGE_CHARGING_THRESHOLD = charging
    if (length == 20 || length == 0x0F) {
        uint16_t voltage = static_cast<uint16_t>((data[3] << 8) | data[4]);

        if (voltage > VOLTAGE_CHARGING_THRESHOLD) {
            if (!m_charging.load()) {
                m_charging.store(true);
                emit chargingChanged(true);
            }
            int prev = m_batteryPercent.load();
            if (prev < 0) {
                m_batteryPercent.store(100);
                emit batteryChanged(100);
            }
            return;
        }

        bool wasCharging = m_charging.exchange(false);
        if (wasCharging) {
            emit chargingChanged(false);
        }

        int level = std::clamp(
            static_cast<int>(std::roundf(estimateBatteryLevel(voltage))), 0, 100);

        int prev = m_batteryPercent.load();
        m_batteryPercent.store(level);
        if (level != prev) {
            emit batteryChanged(level);
        }
    }
}

// Polynomial curve from HeadsetControl — maps raw voltage to 0-100%.
// Coefficients derived from Logitech G633/G933/935 battery characterisation,
// adapted for Cloud Flight voltage range 3300-4200 mV.
float HyperXDevice::estimateBatteryLevel(uint16_t voltage)
{
    if (voltage <= 3648)
        return 0.00125f * voltage;

    if (voltage > 3975)
        return 100.0f;

    double v = static_cast<double>(voltage);
    return static_cast<float>(
        0.00000002547505 * std::pow(v, 4)
      - 0.0003900299     * std::pow(v, 3)
      + 2.238321         * std::pow(v, 2)
      - 5706.256         * v
      + 5452299.0
    );
}
