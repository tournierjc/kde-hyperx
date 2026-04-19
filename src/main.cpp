#include <QApplication>
#include <QThread>

#include "HyperXDevice.h"
#include "TrayIcon.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("kde-hyperx"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QApplication::setQuitOnLastWindowClosed(false);

    TrayIcon tray;

    QThread deviceThread;
    auto *device = new HyperXDevice();
    device->moveToThread(&deviceThread);

    QObject::connect(&deviceThread, &QThread::started,  device, &HyperXDevice::run);
    QObject::connect(&deviceThread, &QThread::finished, device, &QObject::deleteLater);

    QObject::connect(device, &HyperXDevice::batteryChanged,
                     &tray,  &TrayIcon::updateBattery, Qt::QueuedConnection);
    QObject::connect(device, &HyperXDevice::chargingChanged,
                     &tray,  &TrayIcon::updateCharging, Qt::QueuedConnection);
    QObject::connect(device, &HyperXDevice::deviceConnected,
                     &tray,  &TrayIcon::updateConnected, Qt::QueuedConnection);
    QObject::connect(device, &HyperXDevice::deviceDisconnected,
                     &tray,  &TrayIcon::updateDisconnected, Qt::QueuedConnection);
    QObject::connect(device, &HyperXDevice::muteChanged,
                     &tray,  &TrayIcon::updateMute, Qt::QueuedConnection);

    QObject::connect(&app, &QApplication::aboutToQuit, [&]() {
        device->stop();
        deviceThread.quit();
        deviceThread.wait(3000);
    });

    tray.show();
    deviceThread.start();

    return app.exec();
}
