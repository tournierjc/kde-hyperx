#include "TrayIcon.h"

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
    , m_tray(new QSystemTrayIcon(this))
    , m_menu(new QMenu())
{
    m_batteryAction = m_menu->addAction(QStringLiteral("Battery: --"));
    m_batteryAction->setEnabled(false);

    m_statusAction = m_menu->addAction(QStringLiteral("Off"));
    m_statusAction->setEnabled(false);

    m_muteAction = m_menu->addAction(QStringLiteral("Mic: --"));
    m_muteAction->setEnabled(false);

    m_menu->addSeparator();
    m_menu->addAction(QStringLiteral("Quit"), qApp, &QApplication::quit);

    m_tray->setContextMenu(m_menu);
    m_tray->setToolTip(QStringLiteral("HyperX Cloud Flight — Off"));
    m_tray->setIcon(renderDisconnectedIcon());
}

TrayIcon::~TrayIcon()
{
    // QSystemTrayIcon does not take ownership of the context menu.
    m_tray->setContextMenu(nullptr);
    delete m_menu;
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
                                    : QStringLiteral("On Battery"));
}

void TrayIcon::updateConnected()
{
    m_connected = true;
    m_statusAction->setText(
        m_charging ? QStringLiteral("Charging") : QStringLiteral("On Battery"));
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
    m_statusAction->setText(QStringLiteral("Off"));
    m_muteAction->setText(QStringLiteral("Mic: --"));
    refreshIcon();
    refreshTooltip();
}

void TrayIcon::updateMute(bool muted)
{
    m_muted = muted;
    m_muteAction->setText(muted ? QStringLiteral("Mic: Muted")
                                : QStringLiteral("Mic: Active"));
    refreshIcon();
    refreshTooltip();
}

void TrayIcon::refreshIcon()
{
    m_tray->setIcon(
        m_connected ? renderBatteryIcon(m_percent, m_charging, m_muted)
                    : renderDisconnectedIcon());
}

void TrayIcon::refreshTooltip()
{
    if (!m_connected) {
        m_tray->setToolTip(QStringLiteral("HyperX Cloud Flight — Off"));
        return;
    }

    const QString status = m_charging ? QStringLiteral("Charging")
                                     : QStringLiteral("On Battery");
    QString tip;
    if (m_percent >= 0) {
        tip = QStringLiteral("HyperX Cloud Flight\nBattery: %1% — %2")
                  .arg(m_percent).arg(status);
    } else {
        tip = QStringLiteral("HyperX Cloud Flight — %1").arg(status);
    }
    if (m_muted) {
        tip += QStringLiteral("\nMic: Muted");
    }
    m_tray->setToolTip(tip);
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

void TrayIcon::drawMutedBadge(QPainter &p) const
{
    constexpr int S = 64;
    const QRectF badge(S - 24, 2, 22, 22);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xEF, 0x53, 0x50));
    p.drawEllipse(badge);

    // Microphone capsule
    const QRectF mic(badge.center().x() - 3.5, badge.top() + 4, 7, 10);
    p.setBrush(Qt::white);
    p.drawRoundedRect(mic, 3.5, 3.5);

    QPen stem(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(stem);
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(badge.center().x(), mic.bottom()),
               QPointF(badge.center().x(), badge.bottom() - 4));
    p.drawLine(QPointF(badge.center().x() - 4, badge.bottom() - 4),
               QPointF(badge.center().x() + 4, badge.bottom() - 4));

    // Slash through the mic so the muted state reads at tray size
    p.setPen(QPen(QColor(0xC62828), 2.4, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(badge.topLeft() + QPointF(5, 5), badge.bottomRight() - QPointF(5, 5));
}

QIcon TrayIcon::renderBatteryIcon(int percent, bool charging, bool muted) const
{
    constexpr int S = 64;
    QPixmap pixmap(S, S);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    const bool known = percent >= 0;
    const int pct = known ? std::clamp(percent, 0, 100) : 0;

    QColor fillColor;
    if (charging)          fillColor = QColor(0x42, 0xA5, 0xF5);
    else if (!known)       fillColor = QColor(0x9E, 0x9E, 0x9E);
    else if (pct > 60)     fillColor = QColor(0x66, 0xBB, 0x6A);
    else if (pct >= 25)    fillColor = QColor(0xFF, 0xCA, 0x28);
    else                   fillColor = QColor(0xEF, 0x53, 0x50);

    drawHeadsetOutline(p, fillColor);

    QRectF leftCup(6, 28, 16, 22);
    QRectF rightCup(S - 22, 28, 16, 22);

    if (known) {
        const double fillH = leftCup.height() * pct / 100.0;
        const double fillY = leftCup.bottom() - fillH;

        p.setPen(Qt::NoPen);
        p.setBrush(fillColor);
        p.setClipRect(QRectF(0, fillY, S, S));
        p.drawRoundedRect(leftCup.adjusted(1, 1, -1, -1), 3, 3);
        p.drawRoundedRect(rightCup.adjusted(1, 1, -1, -1), 3, 3);
        p.setClipping(false);
    }

    if (charging) {
        QPainterPath bolt;
        const double cx = S / 2.0;
        const double cy = 36.0;
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

    const QString label = known ? QStringLiteral("%1%").arg(pct)
                                : QStringLiteral("--");
    const QRectF textRect(0, S - 20, S, 20);

    QFont font;
    font.setPixelSize(18);
    font.setBold(true);
    p.setFont(font);

    // Outline so the label stays readable on both light and dark panels.
    p.setPen(QColor(0, 0, 0, 200));
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            p.drawText(textRect.translated(dx, dy), Qt::AlignCenter, label);
        }
    }
    p.setPen(Qt::white);
    p.drawText(textRect, Qt::AlignCenter, label);

    if (muted) {
        drawMutedBadge(p);
    }

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
