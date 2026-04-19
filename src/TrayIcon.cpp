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

    constexpr int margin = 4;
    constexpr int termW  = 5;
    QRectF body(margin, margin + 6, S - 2 * margin - termW, S - 2 * margin - 12);
    QRectF terminal(body.right(), body.center().y() - 5, termW, 10);

    p.setPen(QPen(Qt::white, 2.5));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(body, 4, 4);
    p.drawRoundedRect(terminal, 2, 2);

    constexpr double inset = 3.0;
    QRectF inner(body.left() + inset, body.top() + inset,
                 body.width() - 2 * inset, body.height() - 2 * inset);
    double fillW = inner.width() * pct / 100.0;
    QRectF fillRect(inner.left(), inner.top(), fillW, inner.height());
    p.setPen(Qt::NoPen);
    p.setBrush(fillColor);
    p.drawRoundedRect(fillRect, 2, 2);

    if (charging) {
        QPainterPath bolt;
        double cx = body.center().x();
        double cy = body.center().y();
        bolt.moveTo(cx + 2, cy - 14);
        bolt.lineTo(cx - 4, cy + 1);
        bolt.lineTo(cx + 1, cy + 1);
        bolt.lineTo(cx - 2, cy + 14);
        bolt.lineTo(cx + 4, cy - 1);
        bolt.lineTo(cx - 1, cy - 1);
        bolt.closeSubpath();

        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawPath(bolt);
    }

    QFont font;
    font.setPixelSize(14);
    font.setBold(true);
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(QRectF(0, S - 18, S, 18), Qt::AlignCenter,
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

    constexpr int margin = 4;
    constexpr int termW  = 5;
    QRectF body(margin, margin + 6, S - 2 * margin - termW, S - 2 * margin - 12);
    QRectF terminal(body.right(), body.center().y() - 5, termW, 10);

    QColor gray(0x9E, 0x9E, 0x9E);
    p.setPen(QPen(gray, 2.5));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(body, 4, 4);
    p.drawRoundedRect(terminal, 2, 2);

    p.setPen(QPen(QColor(0xEF, 0x53, 0x50), 3));
    p.drawLine(QPointF(body.left() + 6, body.top() + 6),
               QPointF(body.right() - 6, body.bottom() - 6));
    p.drawLine(QPointF(body.right() - 6, body.top() + 6),
               QPointF(body.left() + 6, body.bottom() - 6));

    p.end();
    return QIcon(pixmap);
}
