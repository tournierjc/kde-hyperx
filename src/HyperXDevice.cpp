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
            if (m_headsetAlive.load()) {
                requestBattery();
            }
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
            m_headsetAlive.store(true);
            m_smoothedVoltage = 0.0f;
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
    m_headsetAlive.store(false);
    m_batteryPercent.store(-1);
    m_charging.store(false);
    m_muted.store(false);
    m_smoothedVoltage = 0.0f;

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
                m_headsetAlive.store(true);
                m_connected.store(true);
                m_smoothedVoltage = 0.0f;
                requestBattery();
                emit deviceConnected();
            } else if (data[1] == 0x03) {
                // Headset powered off (dongle stays alive — don't close handle)
                m_headsetAlive.store(false);
                m_batteryPercent.store(-1);
                m_charging.store(false);
                m_smoothedVoltage = 0.0f;
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
    // data[3] encodes charge state:
    //   0x0e / 0x0f → discharging (voltage = data[3]<<8 | data[4])
    //   0x10        → charging in progress (data[4] >= 20) or fully charged (data[4] < 20)
    //   0x11        → charging (some firmware revisions)
    // Refs: HeadsetControl, kondinskis/hyperx-cloud-flight, srn/hyperx-cloud-flight-wireless
    if (length == 20 || length == 0x0F) {
        if (!m_headsetAlive.load()) return;

        bool isCharging = (data[3] == 0x10 || data[3] == 0x11);

        // Fallback: voltage threshold (catches edge cases across firmware variants)
        uint16_t voltage = static_cast<uint16_t>((data[3] << 8) | data[4]);
        if (!isCharging && voltage > VOLTAGE_CHARGING_THRESHOLD) {
            isCharging = true;
        }

        if (isCharging) {
            if (!m_charging.load()) {
                m_charging.store(true);
                emit chargingChanged(true);
            }
            // During charging the reported voltage is the charger rail (~4.1 V+),
            // not the battery's open-circuit voltage — keep the last known
            // percentage from before charging started.
            return;
        }

        // Not charging — voltage is meaningful, use polynomial estimate
        bool wasCharging = m_charging.exchange(false);
        if (wasCharging) {
            emit chargingChanged(false);
        }

        float fv = static_cast<float>(voltage);
        if (m_smoothedVoltage < 1.0f) {
            m_smoothedVoltage = fv;
        } else {
            m_smoothedVoltage = EMA_ALPHA * fv + (1.0f - EMA_ALPHA) * m_smoothedVoltage;
        }

        int level = std::clamp(
            static_cast<int>(std::roundf(estimateBatteryLevel(
                static_cast<uint16_t>(std::roundf(m_smoothedVoltage))))),
            0, 100);

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
