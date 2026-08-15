#include "HyperXDevice.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QThread>
#include <algorithm>
#include <array>
#include <cmath>

HyperXDevice::HyperXDevice(QObject *parent)
    : QObject(parent)
{
    if (hid_init() != 0) {
        qWarning() << "[HyperXDevice] hid_init failed";
    }
}

HyperXDevice::~HyperXDevice()
{
    stop();
    closeDongle();
    hid_exit();
}

void HyperXDevice::run()
{
    m_running.store(true);

    QElapsedTimer batteryTimer;
    batteryTimer.start();

    // Invalid until the first connect attempt so we try immediately, then
    // wait RECONNECT_INTERVAL_MS between retries (avoids a busy-loop while
    // the dongle is unplugged — m_handles is empty whenever we are disconnected).
    QElapsedTimer reconnectTimer;

    while (m_running.load()) {
        if (!m_dongleOpen.load()) {
            if (!reconnectTimer.isValid()
                || reconnectTimer.elapsed() >= RECONNECT_INTERVAL_MS) {
                if (tryConnect()) {
                    requestBattery();
                    batteryTimer.restart();
                }
                reconnectTimer.start();
            }
            QThread::msleep(READ_TIMEOUT_MS);
            continue;
        }

        bool readError = false;
        bool gotData = false;
        for (hid_device *dev : m_handles) {
            uint8_t buf[32] = {};
            int bytes = hid_read_timeout(dev, buf, sizeof(buf), 0);
            if (bytes < 0) {
                qWarning() << "[HyperXDevice] read error, disconnecting";
                readError = true;
                break;
            }
            if (bytes > 0) {
                gotData = true;
                processResponse(buf, bytes);
            }
        }

        if (readError) {
            closeDongle();
            reconnectTimer.invalidate();
            continue;
        }

        if (!gotData) {
            QThread::msleep(READ_TIMEOUT_MS);
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

void HyperXDevice::closeHandles()
{
    for (hid_device *dev : m_handles) {
        hid_close(dev);
    }
    m_handles.clear();
    m_statusHandle = nullptr;
    m_statusIdentified = false;
}

bool HyperXDevice::tryConnect()
{
    static constexpr uint16_t pids[] = { PID_CLOUD_FLIGHT_NEW, PID_CLOUD_FLIGHT_OLD };

    for (uint16_t pid : pids) {
        hid_device_info *devs = hid_enumerate(VENDOR_ID, pid);
        if (!devs) {
            continue;
        }

        for (hid_device_info *cur = devs; cur; cur = cur->next) {
            hid_device *dev = hid_open_path(cur->path);
            if (!dev) {
                continue;
            }
            hid_set_nonblocking(dev, 1);
            m_handles.push_back(dev);

            // Status collection (usage page 0xFF53 / usage 0x0303) is the one
            // that accepts the 0x21 0xFF 0x05 battery poll. Other interfaces
            // carry power/mute/volume events.
            if (cur->usage_page == USAGE_PAGE_STATUS
                && (cur->usage == USAGE_STATUS || cur->usage == 0)) {
                m_statusHandle = dev;
                m_statusIdentified = true;
            }
        }
        hid_free_enumeration(devs);

        if (m_handles.empty()) {
            continue;
        }

        m_dongleOpen.store(true);
        // Headset power is unknown until a battery report or 0x64 0x01 event.
        m_headsetAlive.store(false);
        m_smoothedVoltage = 0.0f;
        qInfo() << "[HyperXDevice] dongle open, PID:" << Qt::hex << pid
                << "interfaces:" << Qt::dec << m_handles.size();
        return true;
    }
    return false;
}

void HyperXDevice::closeDongle()
{
    const bool hadDongle = m_dongleOpen.exchange(false);
    closeHandles();
    notifyHeadsetOff();
    if (hadDongle) {
        qInfo() << "[HyperXDevice] dongle closed";
    }
}

void HyperXDevice::notifyHeadsetOn()
{
    const bool wasAlive = m_headsetAlive.exchange(true);
    if (!wasAlive) {
        qInfo() << "[HyperXDevice] headset on";
        emit deviceConnected();
    }
}

void HyperXDevice::notifyHeadsetOff()
{
    const bool wasAlive = m_headsetAlive.exchange(false);
    m_batteryPercent.store(-1);
    m_charging.store(false);
    m_muted.store(false);
    m_smoothedVoltage = 0.0f;
    if (wasAlive) {
        qInfo() << "[HyperXDevice] headset off";
        emit deviceDisconnected();
    }
}

void HyperXDevice::requestBattery()
{
    if (m_handles.empty()) {
        return;
    }

    // Reverse-engineered Cloud Flight battery trigger: 0x21 0xFF 0x05
    // (from HeadsetControl + kondinskis/hyperx-cloud-flight)
    std::array<uint8_t, 20> packet = {};
    packet[0] = 0x21;
    packet[1] = 0xFF;
    packet[2] = 0x05;

    auto writeTo = [&](hid_device *dev) -> bool {
        return hid_write(dev, packet.data(), packet.size()) >= 0;
    };

    bool ok = false;
    if (m_statusIdentified && m_statusHandle) {
        ok = writeTo(m_statusHandle);
    } else {
        // usage_page is often 0 on older hidapi/hidraw — write every interface
        for (hid_device *dev : m_handles) {
            if (writeTo(dev)) {
                ok = true;
            }
        }
    }

    if (!ok) {
        qWarning() << "[HyperXDevice] failed to send battery request";
    }
}

void HyperXDevice::processResponse(const uint8_t *data, int length)
{
    // Protocol: 2 bytes = power/mute events
    if (length == 2) {
        if (data[0] == 0x64) {
            if (data[1] == 0x01) {
                notifyHeadsetOn();
                m_smoothedVoltage = 0.0f;
                requestBattery();
            } else if (data[1] == 0x03) {
                // Headset powered off (dongle stays alive — don't close handles)
                notifyHeadsetOff();
            }
        } else if (data[0] == 0x65) {
            const bool muted = (data[1] == 0x04);
            if (muted != m_muted.load()) {
                m_muted.store(muted);
                emit muteChanged(muted);
            }
        }
        return;
    }

    if (length == 5) {
        return; // volume events — irrelevant for tray
    }

    // Protocol: 20 or 15 bytes = battery response
    // data[3] encodes charge state:
    //   0x0e / 0x0f → discharging (voltage = data[3]<<8 | data[4])
    //   0x10        → charging in progress (data[4] >= 20) or fully charged (data[4] < 20)
    //   0x11        → charging (some firmware revisions)
    // Refs: HeadsetControl, kondinskis/hyperx-cloud-flight, srn/hyperx-cloud-flight-wireless
    if (length == 20 || length == 0x0F) {
        // A battery report means the headset is powered on.
        notifyHeadsetOn();

        const bool chargeFlag = (data[3] == 0x10 || data[3] == 0x11);
        const bool fullyCharged = chargeFlag && data[4] < 20;

        uint16_t voltage = static_cast<uint16_t>((data[3] << 8) | data[4]);
        bool isCharging = chargeFlag || voltage > VOLTAGE_CHARGING_THRESHOLD;

        if (fullyCharged) {
            if (!m_charging.load()) {
                m_charging.store(true);
                emit chargingChanged(true);
            }
            if (m_batteryPercent.load() != 100) {
                m_batteryPercent.store(100);
                emit batteryChanged(100);
            }
            return;
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

        const bool wasCharging = m_charging.exchange(false);
        if (wasCharging) {
            emit chargingChanged(false);
        }

        const float fv = static_cast<float>(voltage);
        if (m_smoothedVoltage < 1.0f) {
            m_smoothedVoltage = fv;
        } else {
            m_smoothedVoltage = EMA_ALPHA * fv + (1.0f - EMA_ALPHA) * m_smoothedVoltage;
        }

        const int level = std::clamp(
            static_cast<int>(std::roundf(estimateBatteryLevel(
                static_cast<uint16_t>(std::roundf(m_smoothedVoltage))))),
            0, 100);

        const int prev = m_batteryPercent.load();
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

    const double v = static_cast<double>(voltage);
    return static_cast<float>(
        0.00000002547505 * std::pow(v, 4)
      - 0.0003900299     * std::pow(v, 3)
      + 2.238321         * std::pow(v, 2)
      - 5706.256         * v
      + 5452299.0
    );
}
