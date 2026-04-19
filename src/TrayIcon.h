#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QIcon>
#include <QPixmap>
#include <QPainter>

class TrayIcon : public QObject {
    Q_OBJECT

public:
    explicit TrayIcon(QObject *parent = nullptr);

    void show();

public slots:
    void updateBattery(int percent);
    void updateCharging(bool charging);
    void updateConnected();
    void updateDisconnected();
    void updateMute(bool muted);

private:
    void drawHeadsetOutline(QPainter &p, const QColor &color) const;
    QIcon renderBatteryIcon(int percent, bool charging) const;
    QIcon renderDisconnectedIcon() const;
    void refreshIcon();
    void refreshTooltip();

    QSystemTrayIcon *m_tray;
    QMenu *m_menu;
    QAction *m_batteryAction;
    QAction *m_statusAction;
    QAction *m_muteAction;

    int m_percent = -1;
    bool m_charging = false;
    bool m_connected = false;
    bool m_muted = false;
};
