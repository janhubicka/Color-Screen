#!/bin/sh
# first install MSYS2 packaging of mingw64 from https://www.mingw-w64.org/downloads/#msys2
# then run:
 
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-gtk2 mingw-w64-x86_64-libtiff mingw-w64-x86_64-libjpeg-turbo git make diffutils automake autoconf mingw-w64-x86_64-pkg-config vim gdb libtool mingw-w64-x86_64-libraw mingw-w64-x86_64-lcms  mingw-w64-x86_64-libzip mingw-w64-x86_64-gsl

# it is requires to exit and restart msys32
git clone https://github.com/janhubicka/Color-Screen.git ColorScreen
mkdir ColorScreen-build
cd ColorScreen-build/
../ColorScreen/configure --prefix ~/ColorScreen-install --enable-gtkgui
make -j24
make install
