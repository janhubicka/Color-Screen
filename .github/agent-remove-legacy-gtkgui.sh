#!/bin/bash
set -euo pipefail

BASE_SHA=6bc267640e06141029c0bf447bef8d7974c9c3d8
REVIEW_BRANCH=agent/remove-legacy-gtkgui
WORK_BRANCH=agent/remove-legacy-gtkgui-work

git config user.name "Color-Screen agent"
git config user.email "46065755+janhubicka@users.noreply.github.com"

sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  autoconf autoconf-archive automake build-essential ccache git gzip \
  libexiv2-dev libfftw3-dev libgsl-dev liblcms2-dev libopenjp2-7-dev \
  libpng-dev libraw-dev libtiff-dev libtool libturbojpeg0-dev libzip-dev \
  pkg-config qt6-base-dev qt6-tools-dev qt6-tools-dev-tools \
  qt6-translations-l10n libqt6svg6-dev tar xauth xvfb xz-utils

existing=$(git ls-remote origin refs/tags/gtkgui | awk '{print $1}')
if test -n "$existing"; then
  if test "$existing" != "$BASE_SHA"; then
    echo "gtkgui tag already exists at unexpected commit $existing" >&2
    exit 1
  fi
else
  git tag gtkgui "$BASE_SHA"
  git push origin refs/tags/gtkgui
fi

git reset --hard "$BASE_SHA"
git rm -r src/gtkgui gtkimageviewer-0.9.3

python3 - <<'PY'
from pathlib import Path
import re
import textwrap


def replace(path, old, new):
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"expected text not found in {path}: {old!r}")
    p.write_text(text.replace(old, new))


p = Path("configure.ac")
text = p.read_text()
text, n = re.subn(
    r"\n# Needed only for gtkgui\nAC_DEFUN\(\[AC_LINK_EXPORT_DYNAMIC\],.*?\nAC_LINK_EXPORT_DYNAMIC\n",
    "\n", text, count=1, flags=re.S)
if n != 1:
    raise SystemExit("failed to remove GTK-only RDYNAMIC probe from configure.ac")
text, n = re.subn(
    r"\nAC_ARG_ENABLE\(\[gtkgui\],.*?\nfi\n",
    "\n", text, count=1, flags=re.S)
if n != 1:
    raise SystemExit("failed to remove gtkgui configure option")
text = text.replace(
    "[do not link colorscreen statically into colorscreen and colorscreen-gtk binaries]",
    "[do not link colorscreen statically into command-line and GUI binaries]")
text = text.replace(
    'AM_CONDITIONAL([gtkgui], [test x"$enable_gtkgui" == x"yes"])\n', "")
text = text.replace(
    "testsuite/Makefile src/gtkgui/Makefile src/qtgui/Makefile",
    "testsuite/Makefile src/qtgui/Makefile")
text = text.replace(
    "src/libcolorscreen/config.h src/gtkgui/config.h src/libcolorscreen/include/colorscreen-config.h",
    "src/libcolorscreen/config.h src/libcolorscreen/include/colorscreen-config.h")
if "gtkgui" in text or "GTKGUI_BIN" in text:
    raise SystemExit("configure.ac still contains GTK GUI configuration")
p.write_text(text)

Path("src/Makefile.am").write_text(
    "SUBDIRS = libcolorscreen colorscreen\n\n"
    "if qtgui\n"
    "  SUBDIRS += qtgui\n"
    "endif\n")

replace(
    "AGENTS.md",
    '../configure --prefix=$HOME/Color-Screen-install --enable-qtgui --enable-maintainer-mode --prefix=/home/jan/barveni-bin --enable-gtkgui \n',
    '../configure --prefix=$HOME/Color-Screen-install --enable-qtgui --enable-maintainer-mode\n')
replace(
    "AGENTS.md",
    "- `src/gtkgui/`: Legacy GTK-based interface (if enabled) to be deprecated soon.\n",
    "")
replace(
    "AGENTS.md",
    "- **`src/gtkgui/`**: Follows the **GNU coding style**. C++ files uses .C extensions\n",
    "")

replace("README.md", "### Prequisities", "### Prerequisites")
replace(
    "README.md",
    "To build a QT based gui, QT6 libraries are needed. \n",
    "To build the maintained Qt-based GUI, Qt 6 libraries are needed.\n")
replace(
    "README.md",
    "To build the gui use use addition `--enable-qtgui` option to the configure\nscript.\n",
    "To build the GUI, add the `--enable-qtgui` option to the configure script.\n")
replace(
    "README.md",
    "As a result native Color-Screen library, GTK gui and command line utilities\nwill be built.  Note that `~` does not point to Windows home directory, but to\na home directory in msys2 tree.\n",
    "As a result the native Color-Screen library, Qt GUI, and command-line utility\nwill be built. Note that `~` does not point to the Windows home directory, but\nto a home directory in the MSYS2 tree.\n")
replace(
    "README.md",
    "\nFinally you may try to build also `colorscreen-gtk` which is a deprecated GTK\nbased gui application. Invoke it with `colorscreen-gtk <scan file>`. Scan must\nbe in tiff, jpeg, jpeg2000 or png file format.\n",
    "")

Path("scripts/build-windows.sh").write_text(textwrap.dedent("""\
    #!/bin/sh
    # Build Color-Screen from an MSYS2 UCRT64 shell.
    set -eu

    pacman -S --needed \\
      make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libtiff \\
      mingw-w64-ucrt-x86_64-libjpeg-turbo git mingw-w64-ucrt-x86_64-pkg-config \\
      mingw-w64-ucrt-x86_64-libraw mingw-w64-ucrt-x86_64-lcms2 \\
      mingw-w64-ucrt-x86_64-libzip mingw-w64-ucrt-x86_64-gsl diffutils \\
      autoconf-archive mingw-w64-ucrt-x86_64-autotools mingw-w64-ucrt-x86_64-fftw \\
      mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-tools \\
      mingw-w64-ucrt-x86_64-qt6-svg mingw-w64-ucrt-x86_64-adwaita-icon-theme \\
      mingw-w64-ucrt-x86_64-exiv2 mingw-w64-ucrt-x86_64-openjpeg2 \\
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
    """))

replace("os/linux/control", ", libgtk2.0-0", "")
replace(".github/ci/ubuntu.Dockerfile", "        libgtk2.0-dev \\\n", "")
replace(
    ".github/workflows/build-ubuntu.yml",
    "./configure --enable-checking --disable-static-link --enable-gtkgui --enable-qtgui",
    "./configure --enable-checking --disable-static-link --enable-qtgui")
replace(
    ".github/workflows/sanitizers.yml",
    "./configure --enable-checking --disable-static-link \\\n          --disable-gtkgui --enable-qtgui",
    "./configure --enable-checking --disable-static-link --enable-qtgui")

replace(
    "src/qtgui/DetectScreenWorker.cpp",
    "  // Setup detection parameters (based on gtkgui.C:755)\n",
    "  // Set up parameters for the regular-screen detector.\n")
replace(
    "src/qtgui/ImageWidget.cpp",
    "  // Draw profile spots: always while in AddPointMode (like gtkgui.C's color_profiling mode),\n"
    "  // or when the show_profile_spots flag is set.\n",
    "  // Draw profile spots while adding points or when their overlay is enabled.\n")
replace(
    "src/qtgui/ImageWidget.cpp",
    "    // Use stitch mapped coords if relevant (same logic as gtkgui.C)\n",
    "    // Use stitch-mapped coordinates for stitched captures.\n")
replace(
    "src/qtgui/MainWindow.h",
    "  // Using std::shared_ptr or just direct members.\n"
    "  // Given the library usage in gtkgui, direct members are fine.\n",
    "  // These parameter objects are document-local, so direct members are appropriate.\n")
replace(
    "src/libcolorscreen/render-fast.C",
    "/* Render preview for GTKGUI.  To be replaced by render_tile later.\n",
    "/* Render a fast preview.  To be replaced by render_tile later.\n")
PY

autoreconf -fiv
sh build-aux/check-generated-build-metadata.sh

if git grep -n -i -E 'gtkgui|colorscreen-gtk|gtkimageviewer|gtk\+\-2\.0|libgtk2|enable-gtkgui|disable-gtkgui|GTKGUI_BIN'; then
  echo "Legacy GTK GUI references remain in the active tree" >&2
  exit 1
fi

git add -A
git commit -m "build: remove legacy GTK GUI" -m "Remove the deprecated GTK2 frontend and bundled gtkimageviewer sources now that the Qt6 GUI is the maintained graphical interface.

Drop the gtkgui configure option, generated Autotools metadata, packaging and CI GTK dependencies, and stale build/documentation references. Reword surviving source comments so maintained code no longer points at the removed implementation.

The final pre-removal tree is preserved separately by the gtkgui tag.

Tests prepared here: regenerated Autotools files, generated-build-metadata check, an out-of-tree Qt/checking build without installing GTK2 development packages, Qt smoke tests, and make check."
git push origin HEAD:"$REVIEW_BRANCH"

mkdir build-qt
cd build-qt
CFLAGS="-O2 -g -Wall -Wextra" CXXFLAGS="-O2 -g -Wall -Wextra" \
  ../configure --enable-qtgui --enable-checking \
  --disable-shared --enable-static --disable-static-link
make -j"$(nproc)"

image="$GITHUB_WORKSPACE/testsuite/dufaycolor_nikon_coolsan9000ED_4000DPI_raw.tif"
QT_QPA_PLATFORM=offscreen ./src/qtgui/colorscreen-qt \
  --smoke-test 10000 --smoke-test-expect-windows 2 \
  --smoke-test-tile-activation-stable --smoke-test-menu-order \
  --smoke-test-new-view --smoke-test-slanted-reference \
  "$image" "$image"
QT_QPA_PLATFORM=offscreen ./src/qtgui/colorscreen-qt \
  --smoke-test 30000 --smoke-test-expect-windows 2 \
  --smoke-test-document-lifecycle \
  "$image" "$image"
make -j"$(nproc)" check

cd "$GITHUB_WORKSPACE"
git push origin --delete "$WORK_BRANCH" || true
