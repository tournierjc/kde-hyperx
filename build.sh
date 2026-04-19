#!/bin/bash
set -euo pipefail

SRCDIR="$(cd "$(dirname "$0")" && pwd)"
BUILDDIR="${SRCDIR}/build"

mkdir -p "${BUILDDIR}"

MOC=/usr/lib/qt6/moc
CXX="${CXX:-g++}"
CXXFLAGS="-std=c++17 -fPIC -O2 $(pkg-config --cflags Qt6Core Qt6Gui Qt6Widgets hidapi-hidraw)"
LDFLAGS="$(pkg-config --libs Qt6Core Qt6Gui Qt6Widgets hidapi-hidraw)"

echo "==> Running MOC..."
${MOC} "${SRCDIR}/src/HyperXDevice.h" -o "${BUILDDIR}/moc_HyperXDevice.cpp"
${MOC} "${SRCDIR}/src/TrayIcon.h"     -o "${BUILDDIR}/moc_TrayIcon.cpp"

echo "==> Compiling..."
${CXX} ${CXXFLAGS} -c "${SRCDIR}/src/main.cpp"           -o "${BUILDDIR}/main.o"
${CXX} ${CXXFLAGS} -c "${SRCDIR}/src/HyperXDevice.cpp"   -o "${BUILDDIR}/HyperXDevice.o"
${CXX} ${CXXFLAGS} -c "${SRCDIR}/src/TrayIcon.cpp"       -o "${BUILDDIR}/TrayIcon.o"
${CXX} ${CXXFLAGS} -c "${BUILDDIR}/moc_HyperXDevice.cpp" -o "${BUILDDIR}/moc_HyperXDevice.o"
${CXX} ${CXXFLAGS} -c "${BUILDDIR}/moc_TrayIcon.cpp"     -o "${BUILDDIR}/moc_TrayIcon.o"

echo "==> Linking..."
${CXX} \
  "${BUILDDIR}/main.o" \
  "${BUILDDIR}/HyperXDevice.o" \
  "${BUILDDIR}/TrayIcon.o" \
  "${BUILDDIR}/moc_HyperXDevice.o" \
  "${BUILDDIR}/moc_TrayIcon.o" \
  ${LDFLAGS} \
  -o "${BUILDDIR}/kde-hyperx"

echo "==> Built: ${BUILDDIR}/kde-hyperx"
