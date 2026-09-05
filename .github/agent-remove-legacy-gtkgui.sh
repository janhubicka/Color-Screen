#!/bin/bash
set -euo pipefail

BASE_SHA=6bc267640e06141029c0bf447bef8d7974c9c3d8
REVIEW_BRANCH=agent/remove-legacy-gtkgui
WORK_BRANCH=agent/remove-legacy-gtkgui-work

git config user.name "Color-Screen agent"
git config user.email "46065755+janhubicka@users.noreply.github.com"
git config core.fileMode false

sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  autoconf autoconf-archive automake build-essential git gzip \
  libexiv2-dev libfftw3-dev libgsl-dev liblcms2-dev libopenjp2-7-dev \
  libpng-dev libraw-dev libtiff-dev libtool libturbojpeg0-dev libzip-dev \
  pkg-config qt6-base-dev qt6-tools-dev qt6-tools-dev-tools \
  qt6-translations-l10n libqt6svg6-dev tar xauth xvfb xz-utils

existing=$(git ls-remote origin refs/tags/gtkgui | awk '{print $1}')
if test "$existing" != "$BASE_SHA"; then
  echo "gtkgui tag must point at $BASE_SHA, found ${existing:-<missing>}" >&2
  exit 1
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

# Commit the intended source-level changes temporarily. Regenerate Autotools
# metadata only to discover GTK-specific generated hunks; this avoids
# committing unrelated churn from a newer Automake/Autoconf on the runner.
git add -A
git commit -m "temporary source GTK removal"
SOURCE_SHA=$(git rev-parse HEAD)
autoreconf -fiv

git diff "$SOURCE_SHA" -- configure ':(glob)**/Makefile.in' > /tmp/autoreconf-generated.diff
python3 - <<'PY'
from pathlib import Path
import re

lines = Path("/tmp/autoreconf-generated.diff").read_text().splitlines(True)
target = re.compile(
    r"gtkgui|colorscreen-gtk|gtkimageviewer|gtk\+?-?2\.0|GTKGUI_BIN|RDYNAMIC|src/gtkgui",
    re.IGNORECASE)
out = []
i = 0
selected_hunks = 0
while i < len(lines):
    if not lines[i].startswith("diff --git "):
        i += 1
        continue
    header_start = i
    i += 1
    while i < len(lines) and not lines[i].startswith("@@ ") and not lines[i].startswith("diff --git "):
        i += 1
    header = lines[header_start:i]
    hunks = []
    while i < len(lines) and not lines[i].startswith("diff --git "):
        if not lines[i].startswith("@@ "):
            i += 1
            continue
        hunk_start = i
        i += 1
        while i < len(lines) and not lines[i].startswith("@@ ") and not lines[i].startswith("diff --git "):
            i += 1
        hunk = lines[hunk_start:i]
        if target.search("".join(hunk)):
            hunks.extend(hunk)
            selected_hunks += 1
    if hunks:
        out.extend(header)
        out.extend(hunks)

if not out or selected_hunks < 4:
    raise SystemExit(f"unexpectedly found only {selected_hunks} GTK-specific generated hunks")
Path("/tmp/generated-gtk.patch").write_text("".join(out))
print(f"Selected {selected_hunks} GTK-specific generated Autotools hunks")
PY

git reset --hard "$SOURCE_SHA"
git clean -fdx
git apply --check /tmp/generated-gtk.patch
git apply /tmp/generated-gtk.patch

# Source/documentation changes must be whitespace-clean. Some newer Automake
# generated assignments carry trailing spaces, so do not lint generated
# Makefile.in files here.
git diff --check -- . ':(exclude,glob)**/Makefile.in'
sh build-aux/check-generated-build-metadata.sh

echo "Final cleanup diff summary before build:"
git diff --stat "$BASE_SHA"

# These two workflow files are intentionally updated after this commit through
# the GitHub API, because the Actions token may not push workflow modifications.
if git grep -n -i -E 'gtkgui|colorscreen-gtk|gtkimageviewer|gtk\+?-?2\.0|libgtk2|enable-gtkgui|disable-gtkgui|GTKGUI_BIN' -- . \
     ':(exclude).github/workflows/build-ubuntu.yml' \
     ':(exclude).github/workflows/sanitizers.yml'; then
  echo "Legacy GTK GUI references remain outside the two pending workflow edits" >&2
  exit 1
fi
if git grep -n -E 'RDYNAMIC' -- configure Makefile.in src images examples testsuite; then
  echo "GTK-only RDYNAMIC metadata remains" >&2
  exit 1
fi

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

cd "$GITHUB_WORKSPACE"
rm -rf build-qt
git add -A
git commit --amend -m "build: remove legacy GTK GUI" -m "Remove the deprecated GTK2 frontend and bundled gtkimageviewer sources now that the Qt6 GUI is the maintained graphical interface.

Drop the gtkgui configure option, generated Autotools metadata, packaging and Ubuntu image GTK2 dependencies, stale Windows build instructions, and GTK-specific documentation/source comments. Refresh the top-level README around the maintained Qt GUI.

The final pre-removal tree is preserved by the gtkgui tag.

Validated by regenerating GTK-related Autotools metadata, checking generated Qt build metadata, configuring and building on Ubuntu without GTK2 development packages, and running the multi-window Qt smoke tests."

git push origin HEAD:"$REVIEW_BRANCH"
git push origin --delete "$WORK_BRANCH" || true
