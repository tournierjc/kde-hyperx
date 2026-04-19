#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <hidapi.h>
#include <atomic>
#include <cstdint>
#include <cmath>

class HyperXDevice : public QObject {
    Q_OBJECT

public:
    explicit HyperXDevice(QObject *parent = nullptr);
    ~HyperXDevice() override;

    static constexpr uint16_t VENDOR_ID            = 0x0951;
    static constexpr uint16_t PID_CLOUD_FLIGHT_OLD = 0x16C4;
    static constexpr uint16_t PID_CLOUD_FLIGHT_NEW = 0x1723;

    // Reverse-engineered threshold: voltage above this = headset is charging
    static constexpr uint16_t VOLTAGE_CHARGING_THRESHOLD = 0x100B;

    static constexpr int READ_TIMEOUT_MS          = 100;
    static constexpr int BATTERY_POLL_INTERVAL_MS = 30000;
    static constexpr int RECONNECT_INTERVAL_MS    = 5000;

    // Exponential moving average weight for voltage smoothing (0..1).
    // Lower = smoother but slower to react.  0.3 at 30 s polls ≈ 1.5 min
    // effective time-constant — good enough for battery level.
    static constexpr float EMA_ALPHA = 0.3f;

    bool isConnected() const { return m_connected.load(); }
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
    void disconnect();
    void requestBattery();
    void processResponse(const uint8_t *data, int length);

    // Polynomial curve fitting from HeadsetControl (derived from voltage→percent
    // characterisation of the Cloud Flight battery). Not documented by HyperX.
    static float estimateBatteryLevel(uint16_t voltage);

    hid_device *m_device = nullptr;

    std::atomic<int>  m_batteryPercent{-1};
    std::atomic<bool> m_charging{false};
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_headsetAlive{false};
    std::atomic<bool> m_muted{false};
    std::atomic<bool> m_running{false};

    float m_smoothedVoltage = 0.0f;
};
