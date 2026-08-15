#include "HyperXDevice.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QThread>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_map>

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

        std::unordered_map<std::string, hid_device *> byPath;

        for (hid_device_info *cur = devs; cur; cur = cur->next) {
            if (!cur->path) {
                continue;
            }

            hid_device *dev = nullptr;
            auto it = byPath.find(cur->path);
            if (it == byPath.end()) {
                dev = hid_open_path(cur->path);
                if (!dev) {
                    continue;
                }
                hid_set_nonblocking(dev, 1);
                m_handles.push_back(dev);
                byPath.emplace(cur->path, dev);
            } else {
                dev = it->second;
            }

            // Status collection accepts the 0x21 0xFF 0x05 battery poll.
            if (cur->usage == USAGE_STATUS
                || cur->usage_page == USAGE_PAGE_STATUS
                || cur->usage_page == USAGE_PAGE_STATUS_ALT) {
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
        const uint16_t voltage = static_cast<uint16_t>((data[3] << 8) | data[4]);
        const bool isCharging = chargeFlag || voltage > VOLTAGE_CHARGING_THRESHOLD;

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
            qInfo() << "[HyperXDevice] battery" << level << "%  voltage" << voltage << "mV";
            emit batteryChanged(level);
        }
    }
}

// Cloud Flight reports millivolts in bytes 3–4 (~3600 empty, ~4075 full,
// ~4400 on the charger). The Logitech G933 polynomial previously used here
// saturates at 3975 mV, which is a normal resting voltage for this pack, so
// the tray stuck at 100% for most of the upper range.
float HyperXDevice::estimateBatteryLevel(uint16_t voltage)
{
    static constexpr struct {
        uint16_t mv;
        float pct;
    } kMap[] = {
        {3600, 0.0f},
        {3650, 5.0f},
        {3700, 12.0f},
        {3750, 23.0f},
        {3800, 38.0f},
        {3850, 52.0f},
        {3900, 65.0f},
        {3950, 78.0f},
        {4000, 90.0f},
        {4050, 97.0f},
        {4075, 100.0f},
    };

    if (voltage <= kMap[0].mv) {
        return kMap[0].pct;
    }

    constexpr size_t n = sizeof(kMap) / sizeof(kMap[0]);
    for (size_t i = 1; i < n; ++i) {
        if (voltage <= kMap[i].mv) {
            const float span = static_cast<float>(kMap[i].mv - kMap[i - 1].mv);
            const float t = static_cast<float>(voltage - kMap[i - 1].mv) / span;
            return kMap[i - 1].pct + t * (kMap[i].pct - kMap[i - 1].pct);
        }
    }
    return 100.0f;
}
