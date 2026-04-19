#!/bin/bash
set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"

echo "==> Removing binary..."
sudo rm -f "${PREFIX}/bin/kde-hyperx"

echo "==> Removing udev rules..."
sudo rm -f /etc/udev/rules.d/99-hyperx-headset.rules
sudo udevadm control --reload-rules

echo "==> Removing desktop entries..."
rm -f "${HOME}/.local/share/applications/kde-hyperx.desktop"
rm -f "${HOME}/.config/autostart/kde-hyperx-autostart.desktop"

echo "==> Done. kde-hyperx has been uninstalled."
