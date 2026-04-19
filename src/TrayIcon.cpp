#include "TrayIcon.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QFont>

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
    , m_tray(new QSystemTrayIcon(this))
    , m_menu(new QMenu())
{
    m_batteryAction = m_menu->addAction(QStringLiteral("Battery: --"));
    m_batteryAction->setEnabled(false);

    m_statusAction = m_menu->addAction(QStringLiteral("Disconnected"));
    m_statusAction->setEnabled(false);

    m_muteAction = m_menu->addAction(QStringLiteral("Mic: --"));
    m_muteAction->setEnabled(false);

    m_menu->addSeparator();
    m_menu->addAction(QStringLiteral("Quit"), qApp, &QApplication::quit);

    m_tray->setContextMenu(m_menu);
    m_tray->setToolTip(QStringLiteral("HyperX Cloud Flight — Disconnected"));
    m_tray->setIcon(renderDisconnectedIcon());
}

void TrayIcon::show()
{
    m_tray->show();
}

void TrayIcon::updateBattery(int percent)
{
    m_percent = percent;
    refreshIcon();
    refreshTooltip();
    m_batteryAction->setText(
        percent >= 0 ? QStringLiteral("Battery: %1%").arg(percent)
                     : QStringLiteral("Battery: --"));
}

void TrayIcon::updateCharging(bool charging)
{
    m_charging = charging;
    refreshIcon();
    refreshTooltip();
    m_statusAction->setText(charging ? QStringLiteral("Charging")
                                    : QStringLiteral("Discharging"));
}

void TrayIcon::updateConnected()
{
    m_connected = true;
    m_statusAction->setText(
        m_charging ? QStringLiteral("Charging") : QStringLiteral("Discharging"));
    refreshIcon();
    refreshTooltip();
}

void TrayIcon::updateDisconnected()
{
    m_connected = false;
    m_percent = -1;
    m_charging = false;
    m_muted = false;
    m_batteryAction->setText(QStringLiteral("Battery: --"));
    m_statusAction->setText(QStringLiteral("Disconnected"));
    m_muteAction->setText(QStringLiteral("Mic: --"));
    refreshIcon();
    refreshTooltip();
}

void TrayIcon::updateMute(bool muted)
{
    m_muted = muted;
    m_muteAction->setText(muted ? QStringLiteral("Mic: Muted")
                                : QStringLiteral("Mic: Active"));
}

void TrayIcon::refreshIcon()
{
    m_tray->setIcon(
        m_connected ? renderBatteryIcon(m_percent, m_charging)
                    : renderDisconnectedIcon());
}

void TrayIcon::refreshTooltip()
{
    if (!m_connected) {
        m_tray->setToolTip(QStringLiteral("HyperX Cloud Flight — Disconnected"));
        return;
    }

    QString status = m_charging ? QStringLiteral("Charging") : QStringLiteral("Discharging");
    if (m_percent >= 0) {
        m_tray->setToolTip(
            QStringLiteral("HyperX Cloud Flight\nBattery: %1% — %2")
                .arg(m_percent).arg(status));
    } else {
        m_tray->setToolTip(
            QStringLiteral("HyperX Cloud Flight — %1").arg(status));
    }
}

void TrayIcon::drawHeadsetOutline(QPainter &p, const QColor &color) const
{
    constexpr int S = 64;
    QPen pen(color, 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    QPainterPath headband;
    headband.moveTo(10, 32);
    headband.cubicTo(10, 6, S - 10, 6, S - 10, 32);
    p.drawPath(headband);

    QRectF leftCup(6, 28, 16, 22);
    QRectF rightCup(S - 22, 28, 16, 22);
    p.drawRoundedRect(leftCup, 4, 4);
    p.drawRoundedRect(rightCup, 4, 4);
}

QIcon TrayIcon::renderBatteryIcon(int percent, bool charging) const
{
    constexpr int S = 64;
    QPixmap pixmap(S, S);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    int pct = std::clamp(percent, 0, 100);

    QColor fillColor;
    if (charging)       fillColor = QColor(0x42, 0xA5, 0xF5);
    else if (pct > 60)  fillColor = QColor(0x66, 0xBB, 0x6A);
    else if (pct > 25)  fillColor = QColor(0xFF, 0xCA, 0x28);
    else                fillColor = QColor(0xEF, 0x53, 0x50);

    drawHeadsetOutline(p, fillColor);

    QRectF leftCup(6, 28, 16, 22);
    QRectF rightCup(S - 22, 28, 16, 22);

    double fillH = leftCup.height() * pct / 100.0;
    double fillY = leftCup.bottom() - fillH;

    p.setPen(Qt::NoPen);
    p.setBrush(fillColor);
    p.setClipRect(QRectF(0, fillY, S, S));
    p.drawRoundedRect(leftCup.adjusted(1, 1, -1, -1), 3, 3);
    p.drawRoundedRect(rightCup.adjusted(1, 1, -1, -1), 3, 3);
    p.setClipping(false);

    if (charging) {
        QPainterPath bolt;
        double cx = S / 2.0;
        double cy = 36.0;
        bolt.moveTo(cx + 2, cy - 8);
        bolt.lineTo(cx - 3, cy + 1);
        bolt.lineTo(cx + 1, cy + 1);
        bolt.lineTo(cx - 2, cy + 9);
        bolt.lineTo(cx + 3, cy - 1);
        bolt.lineTo(cx - 1, cy - 1);
        bolt.closeSubpath();

        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawPath(bolt);
    }

    QFont font;
    font.setPixelSize(13);
    font.setBold(true);
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(QRectF(0, S - 14, S, 14), Qt::AlignCenter,
               QStringLiteral("%1%").arg(pct));

    p.end();
    return QIcon(pixmap);
}

QIcon TrayIcon::renderDisconnectedIcon() const
{
    constexpr int S = 64;
    QPixmap pixmap(S, S);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    drawHeadsetOutline(p, QColor(0x9E, 0x9E, 0x9E));

    p.setPen(QPen(QColor(0xEF, 0x53, 0x50), 3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(18, 20), QPointF(46, 48));
    p.drawLine(QPointF(46, 20), QPointF(18, 48));

    p.end();
    return QIcon(pixmap);
}
