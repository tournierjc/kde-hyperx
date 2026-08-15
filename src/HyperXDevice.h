#pragma once

#include <QObject>
#include <hidapi.h>
#include <atomic>
#include <cstdint>
#include <vector>

class HyperXDevice : public QObject {
    Q_OBJECT

public:
    explicit HyperXDevice(QObject *parent = nullptr);
    ~HyperXDevice() override;

    static constexpr uint16_t VENDOR_ID            = 0x0951;
    static constexpr uint16_t PID_CLOUD_FLIGHT_OLD = 0x16C4;
    static constexpr uint16_t PID_CLOUD_FLIGHT_NEW = 0x1723;

    // Vendor collections that accept the battery poll.
    // 0xFF53 is documented by srn/hyperx-cloud-flight-wireless; this dongle
    // exposes usage 0x0303 on page 0xFF90 instead.
    static constexpr uint16_t USAGE_PAGE_STATUS     = 0xFF53;
    static constexpr uint16_t USAGE_PAGE_STATUS_ALT = 0xFF90;
    static constexpr uint16_t USAGE_STATUS          = 0x0303;

    // Reverse-engineered threshold: voltage above this = headset is charging
    static constexpr uint16_t VOLTAGE_CHARGING_THRESHOLD = 0x100B;

    static constexpr int READ_TIMEOUT_MS          = 100;
    static constexpr int BATTERY_POLL_INTERVAL_MS = 30000;
    static constexpr int RECONNECT_INTERVAL_MS    = 5000;

    // Exponential moving average weight for voltage smoothing (0..1).
    // Lower = smoother but slower to react.  0.3 at 30 s polls ≈ 1.5 min
    // effective time-constant — good enough for battery level.
    static constexpr float EMA_ALPHA = 0.3f;

    // Headset powered on (not merely that the USB dongle is present).
    bool isConnected() const { return m_headsetAlive.load(); }
    bool isDonglePresent() const { return m_dongleOpen.load(); }
    int  batteryPercent() const { return m_batteryPercent.load(); }
    bool isCharging() const { return m_charging.load(); }
    bool isMuted() const { return m_muted.load(); }

public slots:
    void run();
    void stop();

signals:
    void batteryChanged(int percent);
    void chargingChanged(bool charging);
    void deviceConnected();
    void deviceDisconnected();
    void muteChanged(bool muted);

private:
    bool tryConnect();
    void closeDongle();
    void closeHandles();
    void requestBattery();
    void processResponse(const uint8_t *data, int length);
    void notifyHeadsetOn();
    void notifyHeadsetOff();

    // Maps Cloud Flight open-circuit voltage (mV) to 0-100%. Charging rail
    // voltages (>= 0x100B) are filtered out before this is called.
    static float estimateBatteryLevel(uint16_t voltage);

    std::vector<hid_device *> m_handles;
    hid_device *m_statusHandle = nullptr;
    bool m_statusIdentified = false;

    std::atomic<int>  m_batteryPercent{-1};
    std::atomic<bool> m_charging{false};
    std::atomic<bool> m_dongleOpen{false};
    std::atomic<bool> m_headsetAlive{false};
    std::atomic<bool> m_muted{false};
    std::atomic<bool> m_running{false};

    float m_smoothedVoltage = 0.0f;
};
