#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${PREFIX:-/usr/local}"

echo "==> Building..."
"${SCRIPT_DIR}/build.sh"

echo "==> Installing binary to ${PREFIX}/bin/"
sudo install -Dm755 "${SCRIPT_DIR}/build/kde-hyperx" "${PREFIX}/bin/kde-hyperx"

echo "==> Installing udev rules..."
sudo install -Dm644 "${SCRIPT_DIR}/udev/99-hyperx-headset.rules" /etc/udev/rules.d/99-hyperx-headset.rules
sudo udevadm control --reload-rules
sudo udevadm trigger

echo "==> Installing desktop entries..."
install -Dm644 "${SCRIPT_DIR}/desktop/kde-hyperx.desktop" \
    "${HOME}/.local/share/applications/kde-hyperx.desktop"

mkdir -p "${HOME}/.config/autostart"
install -Dm644 "${SCRIPT_DIR}/desktop/kde-hyperx-autostart.desktop" \
    "${HOME}/.config/autostart/kde-hyperx-autostart.desktop"

echo "==> Done. Run 'kde-hyperx' or log out/in for autostart."
