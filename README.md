# Color-Screen
Color-Screen is a software tool designed to digitally align viewing screens
with scans of negatives and transparencies created using additive color screen
processes. See [our
wiki](https://github.com/janhubicka/Color-Screen/wiki) for details.

Color-Screen began as a simplistic solution developed for the 2013 exhibition
at the [Šechtl and Vošeček Museum of
Photography](http://sechtl-vosecek.ucw.cz/en/).  The exhibition showcased
photographs from the [Matson (G. Eric and Edith) Photograph
Collection](https://www.loc.gov/pictures/collection/matpc/colony.html),
digitized by the Library of Congress. During the preparation process, Mark
Jacobs identified rare scans of early color negatives using the Finlay color
process. To address these unique negatives, Jan Hubička quickly created a
rendering tool that would later become Color-Screen.

## Prequisities
Color-Screen can be built using recent versions of either GCC or Clang
compilers.

For optimal performance, OpenMP support in your compiler is recommended. The
default Clang compiler included with Xcode does not support OpenMP. However,
you can install OpenMP separately for Xcode's Clang.

Building the main library and command-line utility requires the following
additional libraries:

 - [libtiff](http://www.libtiff.org/)
 - [libjpeg-turbo](https://libjpeg-turbo.org/)
 - [libzip](https://libzip.org/)
 - [libgsl](https://www.gnu.org/software/gsl/)
 - [libraw](https://www.libraw.org/)
 - [liblcms2](https://www.littlecms.com/)

To build a GTK2 based gui, GTK2 and Glade libraries are needed. Note that the
GTK2 gui is deprecated and new Java based [Color-Screen
GUI](https://gitlab.mff.cuni.cz/kimroval/Color-Screen-GUI) is being developed
by Linda Kimrová

## Installation

### Linux

On typical Linux distribution it is enough to do the following.

	./configure --prefix=<where_to_install>
	make
	make install

To build the gui use use addition `--enable-gtkgui` option to the configure
script. `make examples` will download some sample images from the Library
of Congress webpages and produce color renderings.

### Windows

Easiest way to install on Windows is to use [MSYS2](https://www.msys2.org/).
Then start MSYS2 WINGW64 from Windows start menu.  Install all necessary
packages

    pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-gtk2 mingw-w64-x86_64-libtiff mingw-w64-x86_64-libjpeg-turbo git make diffutils automake autoconf mingw-w64-x86_64-pkg-config vim gdb libtool mingw-w64-x86_64-libraw mingw-w64-x86_64-lcms  mingw-w64-x86_64-libzip mingw-w64-x86_64-gsl 

Then close the terminal emulation window and open MSYS2 MINGW64 again
(this is necessary to get `PATH` set up).  Now Color-Screen can be built
in standard way.

    git clone https://github.com/janhubicka/Color-Screen.git Color-Screen
    mkdir Color-Screen-build
    cd Color-Screen-build/
    ../Color-Screen/configure --prefix=~/Color-Screen-install --enable-gtkgui
    make
    make install 

As a result native Color-Screen library, GTK gui and command line utilities
will be built.  Note that `~` does not point to Windows home directory, but to
a home directory in msys2 tree.


To make the package stand-alone it is necessary to copy
all DLL libraries to `~/Color-Screen-install/bin/`. To figure out what libraries
are necessary use 

    ldd libcolorscreen0.dll

This will print all libraries used. Copy all DLL files from mingw64 subdirectory
to bin subdirectory.

## Usage

There are three programs installed. 

`colorscreen` is a command line utility to render into tiff files. See
`colorscreen --help` for usage information.

`colorscreen-gtk` is a (deprecated) GTK based gui application. Invoke it
with `colorscreen-gtk <scan file>`. Scan must be either in tiff or jpeg file
format.

Jan Hubicka (hubicka@ucw.cz)
