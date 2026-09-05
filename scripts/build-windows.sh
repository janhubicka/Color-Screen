#!/bin/sh
# Build Color-Screen from an MSYS2 UCRT64 shell.
set -eu

pacman -S --needed \
  make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libtiff \
  mingw-w64-ucrt-x86_64-libjpeg-turbo git mingw-w64-ucrt-x86_64-pkg-config \
  mingw-w64-ucrt-x86_64-libraw mingw-w64-ucrt-x86_64-lcms2 \
  mingw-w64-ucrt-x86_64-libzip mingw-w64-ucrt-x86_64-gsl diffutils \
  autoconf-archive mingw-w64-ucrt-x86_64-autotools mingw-w64-ucrt-x86_64-fftw \
  mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-tools \
  mingw-w64-ucrt-x86_64-qt6-svg mingw-w64-ucrt-x86_64-adwaita-icon-theme \
  mingw-w64-ucrt-x86_64-exiv2 mingw-w64-ucrt-x86_64-openjpeg2 \
  mingw-w64-ucrt-x86_64-libpng

# Restart the UCRT64 shell after the first package installation so PATH is refreshed.
git clone https://github.com/janhubicka/Color-Screen.git ColorScreen
mkdir -p ColorScreen-build
cd ColorScreen-build
export CXXFLAGS="-Ofast -flto=auto"
export CFLAGS="$CXXFLAGS"
export LDFLAGS="-Wl,--stack,16777216"
../ColorScreen/configure --prefix="$HOME/ColorScreen-install" --enable-qtgui
make -j"$(nproc)"
make install-strip
