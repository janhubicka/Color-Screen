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

# Installing Color-Screen
## Binary packages
We provide binary packages for Windows (x86) and MacOS (aarch64) at
our [wiki page](https://github.com/janhubicka/Color-Screen/wiki#software-packages).
We are happy to build more configurations if that seem useful.

## Building from source code
### Prequisities
Color-Screen can be built using recent versions of either GCC or Clang
compilers.  For optimal performance, OpenMP support in your compiler is
recommended. 

Building the main library and command-line utility requires the following
additional libraries:

 - [libtiff](http://www.libtiff.org/)
 - [libjpeg-turbo](https://libjpeg-turbo.org/)
 - [libzip](https://libzip.org/)
 - [libgsl](https://www.gnu.org/software/gsl/)
 - [libraw](https://www.libraw.org/)
 - [liblcms2](https://www.littlecms.com/)
 - [libfftw3](https://www.fftw.org/)

If you wish to develop colorscreen, additional packages are recommended

 - [autoconf](https://www.gnu.org/software/autoconf/)
 - [automake](https://www.gnu.org/software/automake/)
 - [autoconf-archive](https://www.gnu.org/s/autoconf-archive/Downloads.html)
 - [libtool](https://www.gnu.org/software/libtool/)

To build a QT based gui, QT6 libraries are needed. 

### Building on Linux (and other UNIX-like systems)

On typical Linux distribution it is enough to do the following.

	CXXFLAGS="-Ofast -flto" CFLAGS="$CXXFLAGS" ./configure --prefix=<where_to_install>
	make
	make install-strip

To build the gui use use addition `--enable-qtgui` option to the configure
script.

For better performance, if you are going to use the binary on the same CPU
as you are building it, add `-march=native` to `CXXFLAGS`.  This will enable use
of extended instruction set of your CPU.

### Building on Windows

Easiest way to install on Windows is to use [MSYS2](https://www.msys2.org/).
Then start MSYS2 WINGW64 from Windows start menu.  Install all necessary
packages

    pacman -S make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libtiff mingw-w64-ucrt-x86_64-libjpeg-turbo git mingw-w64-ucrt-x86_64-pkg-config mingw-w64-ucrt-x86_64-libraw mingw-w64-ucrt-x86_64-lcms2  mingw-w64-ucrt-x86_64-libzip mingw-w64-ucrt-x86_64-gsl diffutils autoconf-archive mingw-w64-ucrt-x86_64-autotools  mingw-w64-ucrt-x86_64-fftw mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-tools mingw-w64-ucrt-x86_64-adwaita-icon-theme mingw-w64-ucrt-x86_64-qt6-svg mingw-w64-ucrt-x86_64-nsis mingw-w64-ucrt-x86_64-imagemagick mingw-w64-ucrt-x86_64-exiv2


Then close the terminal emulation window and open MSYS2 MINGW64 again
(this is necessary to get `PATH` set up).  Now Color-Screen can be built
in standard way.

    git clone https://github.com/janhubicka/Color-Screen.git Color-Screen
    mkdir Color-Screen-build
    cd Color-Screen-build/
    CXXFLAGS="-Ofast -flto=auto" CFLAGS="$CXXFLAGS" LDFLAGS="-Wl,--stack,16777216" ../Color-Screen/configure --prefix=~/Color-Screen-install --enable-qtgui
    make
    make install-strip

For better performance, if you are going to use the binary on the same CPU
as you are building it, add `-march=native` to `CXXFLAGS`.  This will enable use
of extended instruction set of your CPU.

As a result native Color-Screen library, GTK gui and command line utilities
will be built.  Note that `~` does not point to Windows home directory, but to
a home directory in msys2 tree.


To make the package stand-alone it is necessary to copy
all DLL libraries to `~/Color-Screen-install/bin/`. To figure out what libraries
are necessary use 

    ldd libcolorscreen0.dll

This will print all libraries used. Copy all DLL files from mingw64 subdirectory
to bin subdirectory.

### Building on MacOS

Install Xcode to obtain the C++ compiler (clang).  Xcode version of clang has
OpenMP for multithreading disabled.  For Color–Screen to run smoothly and
faster, install (using homebrew) the `libomp` package together with the other
required packages listed above:

    CXXFLAGS="-Ofast -flto -I/opt/homebrew/include -I/opt/homebrew/opt/libomp/include -Xclang=-fopenmp" \
    LDFLAGS="-L/opt/homebrew/lib -L/opt/homebrew/opt/libomp/lib -lomp" \
    ./configure --prefix=<where_to_install> --disable-openmp
    make
    make install-strip

This works around the disabled OpenMP support.

### Wonderful examples

Once colorscreen is built, `make examples` will download some sample images
from the Library of Congress webpages and produce color renderings.

# Usage

There are two programs installed. 

`colorscreen` is a command line utility to render into tiff files. See
`colorscreen --help` and its
[wiki](https://github.com/janhubicka/Color-Screen/wiki/colorscreen) for usage
information.

`colorscreen-qt` is a QT6 based GUI application. 

Finally you may try to build also `colorscreen-gtk` which is a deprecated GTK
based gui application. Invoke it with `colorscreen-gtk <scan file>`. Scan must
be either in tiff or jpeg file format.

Jan Hubička (hubicka@ucw.cz)
