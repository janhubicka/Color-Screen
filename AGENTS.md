# Project Overview for AI Agents

This project, **Color-Screen**, is a high-performance color screening and image processing application. It utilizes a modular C++ architecture with a focus on color accuracy and optimized rendering.

## Project Core
- **Language:** C++ (Standard: C++17)
- **Build System:** GNU Autotools (`autoconf`, `automake`, `libtool`)
- **Primary Goal:** Implement highly optimized tools to reconstruct colors from digitized early color photographs

### Build Environment
- **Primary Build Directory**: Rebuilding should always be performed in the `build-qt` subdirectory to keep the source tree clean.
- **Compiler Requirements**: Supports GCC (14/15) and Clang.
- **Parallelism**: Uses **OpenMP** for multi-threaded performance. Parallelism is highly encouraged, especially in `libcolorscreen`.
- **Dependencies**: Qt6, FFmpeg, Lensfun, GSL, etc.

### Recommended Build Process

Always build out-of-tree in the `build-qt` directory:

```bash
cd build-qt
# If you need a fresh start:
# rm -rf * 
# ../configure [flags]
```

### Correct Configure Flags

To ensure optimal performance and compatibility (especially with GCC 15 + Qt6), use the following configuration:

```bash
CXXFLAGS="-Ofast -march=native -Wall -g" \
CFLAGS="-Ofast -march=native -Wall -g" \
../configure --prefix=$HOME/Color-Screen-install --enable-qtgui --enable-maintainer-mode --prefix=/home/jan/barveni-bin --enable-gtkgui 
```

### Checking Mode

Configuring with `--enable-checking` defines the `COLORSCREEN_CHECKING` macro. Heavyweight consistency checks and additional tests should be guarded by this macro to keep production builds fast.

### Compilation

Use parallel builds to speed up the process:

```bash
make -j$(nproc)
make install-strip
```

### Testing

The project includes a comprehensive testsuite covering `libcolorscreen` features. Tests are invoked using:

```bash
make -j$(nproc) check
```

- **Unit Tests**: Implemented in `src/libcolorscreen/unittests.C`.
- **CLI Tests**: The command-line tool `colorscreen` can be tested using scripts located in the `testsuite/` subdirectory.

### Continuous Integration

The project uses GitHub Actions for automated testing. Workflows are defined in `.github/workflows/` and cover:
- **Ubuntu**: Linux builds and tests.
- **macOS**: Apple Silicon and Intel builds.
- **Windows**: MSYS2/MinGW-w64 builds.

#### Selecting CI on agent pull requests

Pull requests run the full CI set by default. For a focused iteration on a
same-repository branch, put one or more CI selectors in the pull-request body.
As soon as any `[ci:...]` token is present, the primary PR workflows run only
the matching lanes. Fork pull requests ignore selectors and always run full CI.

Workflow selectors:

- `[ci:all]` - explicitly run every primary CI workflow.
- `[ci:ubuntu]` - all Ubuntu build, distcheck, and checking jobs.
- `[ci:macos]` - the regular macOS build/package/checking workflow.
- `[ci:windows]` - the GCC/MSYS2 Windows build matrix.
- `[ci:windows-clang]` - the Clang/MSYS2 Windows build matrix.
- `[ci:sanitizers]` - all dedicated sanitizer jobs.

For narrower Ubuntu or sanitizer work, use:

- `[ci:ubuntu-build]`, `[ci:ubuntu-distcheck]`, or
  `[ci:ubuntu-checking]`.
- `[ci:tsan]`, `[ci:macos-sanitizers]`, or `[ci:windows-asan]`.

Selectors can be combined. For example, a PR body containing

```text
[ci:ubuntu-checking]
[ci:windows-asan]
```

runs only the Ubuntu ASan/UBSan checking matrix and the Windows ASan job from
the primary PR workflows. The Ubuntu CI-image workflow remains controlled by
its own narrow path filters.

Selectors apply only to `pull_request` events. Pushes to `main` and release
branches still run their normal complete workflows. The main platform
workflows also support `workflow_dispatch`; Ubuntu and the sanitizer workflow
provide boolean inputs for selecting their major job groups when running
manually.

Use focused selectors while diagnosing a platform-specific failure or when the
user explicitly wants a narrow CI run. Before requesting merge, normally make
sure the final head revision has had full CI coverage: remove the selectors
before the final PR synchronization, or manually run all skipped workflows.
Do not invent selector names: an unknown `[ci:...]` token still activates
selective mode and can therefore skip every primary job on a same-repository PR.

PR workflows also use concurrency cancellation. A new commit to the same PR
cancels older in-progress/queued runs of that workflow; do not rely on results
from a superseded run.

## Agent container build and test setup

The ChatGPT/agent execution container is normally Debian 13 (trixie), amd64.
It may not have root access, the development packages needed by Color-Screen,
or reliable direct access to GitHub/Debian mirrors. A reproducible core-library
build can still be made by keeping both source and dependencies in private
prefixes.

### Obtaining source and dependencies

Prefer an exact source revision and record its commit SHA before testing. If
normal network access works, a normal checkout/archive is fine. If direct
container networking cannot reach GitHub, use a temporary GitHub Actions job as
a transport: archive the exact revision (or run `make dist` when appropriate),
upload it as an Actions artifact, and download that artifact into the container.
Do not review or merge temporary transport branches/workflows; the review branch
must contain only the intended project changes.

For Debian 13/trixie amd64 core builds, the dependency bundle used successfully
by agents contains the development packages and matching runtimes for:

- FFTW: `libfftw3-dev`, `libfftw3-bin`, `libfftw3-long3`, `libfftw3-quad3`
- GSL: `libgsl-dev`, `libgsl28`, `libgslcblas0`
- TurboJPEG: `libturbojpeg0-dev`, `libturbojpeg0`
- LittleCMS: `liblcms2-dev`, `liblcms2-2`
- LibRaw: `libraw-dev`, `libraw23t64`
- libzip: `libzip-dev`, `libzip5`, `zipcmp`, `zipmerge`, `ziptool`
- Exiv2: `libexiv2-dev`, `libexiv2-28`, `libexiv2-data`
- INI reader runtime needed by the packaged Exiv2 dependency:
  `libinireader0`

A convenient way to create the bundle is a temporary Debian 13 GitHub Actions
runner using `apt-get download` for the packages above and uploading the
resulting `.deb` files as an artifact. On a non-Debian host, the same bundle
can be produced without `apt` by downloading the Debian trixie `Packages.xz`
indexes, resolving the package filenames, and downloading the referenced
`.deb` files. Keep the bundle together with the exact source revision when
reproducibility matters.

The agent container does not need root privileges to install this bundle.
Extract every package into a private prefix, for example:

```bash
DEPS=/tmp/colorscreen-build/deps/local
mkdir -p "$DEPS"
for deb in /path/to/debs/*.deb; do
  dpkg-deb -x "$deb" "$DEPS"
done
```

Then point compilation, `pkg-config`, and runtime linking at that prefix:

```bash
export DEPS=/tmp/colorscreen-build/deps/local
export PKG_CONFIG_PATH="$DEPS/usr/lib/x86_64-linux-gnu/pkgconfig:$DEPS/usr/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export CPPFLAGS="-I$DEPS/usr/include${CPPFLAGS:+ $CPPFLAGS}"
export LDFLAGS="-L$DEPS/usr/lib/x86_64-linux-gnu -Wl,-rpath,$DEPS/usr/lib/x86_64-linux-gnu${LDFLAGS:+ $LDFLAGS}"
export LD_LIBRARY_PATH="$DEPS/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

If a Debian package was built against an INI-reader SONAME not otherwise
available in the container, an ABI-compatible private `libinih.so.1` shim has
been used successfully for test-only builds. Prefer the real Debian runtime
package (`libinireader0`) whenever available.

### Core build used by agents

For correctness work that does not require the GUI, use an out-of-tree build
with checking enabled. Keep OpenMP enabled: some core code still calls OpenMP
entry points directly, so `--disable-openmp` is not a supported test
configuration.

A conservative validation build is:

```bash
mkdir -p /tmp/colorscreen-build/build-o2
cd /tmp/colorscreen-build/build-o2
CFLAGS="-O2 -g -Wall -Wextra" \
CXXFLAGS="-O2 -g -Wall -Wextra" \
/path/to/Color-Screen/configure \
  --enable-checking --disable-shared --enable-static --disable-static-link
make -j"$(nproc)"
```

The optimized build that most closely exercises production assumptions is:

```bash
mkdir -p /tmp/colorscreen-build/build-ofast
cd /tmp/colorscreen-build/build-ofast
CFLAGS="-Ofast -march=native -g -Wall" \
CXXFLAGS="-Ofast -march=native -g -Wall" \
/path/to/Color-Screen/configure \
  --enable-checking --disable-shared --enable-static --disable-static-link
make -j"$(nproc)"
```

GCC is the most reliable compiler in this private-prefix setup because it has a
working OpenMP runtime. Clang validation is useful when `omp.h`/libomp are
available, but lack of the OpenMP development files is an environment problem,
not a reason to disable OpenMP in Color-Screen.

### Agent-side test procedure

Run focused tests first while developing, then the complete unit binary:

```bash
./src/libcolorscreen/unittests warp lens_correction 1d_homography
top_srcdir=/path/to/Color-Screen ./src/libcolorscreen/unittests
```

The second command should run all registered unit groups. Also use
`make -j"$(nproc)" check` when the reconstructed source/build tree contains the
generated testsuite Makefiles. For changes affecting CLI/rendering paths, run
the relevant scripts in `testsuite/` in addition to the unit binary.

Repeat important numerical tests with both `-O2` and `-Ofast`. Fast-math has
historically exposed real portability/correctness issues, so an `-O2` pass
alone is not enough for sensitive numerical code.

Some integration tests, especially full finetune runs, can exceed the command
execution window of the agent container. Run as much as possible locally, say
exactly where execution stopped, and leave the uninterrupted long run to the
repository CI rather than claiming a complete local pass.

## Git and pull-request workflow for agents

Substantial agent changes should be prepared on a clean feature branch created
from the current `main`, never directly on `main`. Before committing, inspect
the exact diff and make sure the branch contains only the intended files; keep
temporary dependency/source-transport workflows and staging artifacts off the
review branch.

When the user authorizes publication, the preferred sequence is:

1. create the feature branch from the current `main` commit;
2. apply and test the intended changes;
3. commit only the reviewed files;
4. push the feature branch;
5. open a **draft** pull request against `main`;
6. inspect GitHub Actions results and report failures precisely.

Do not merge a pull request unless the user explicitly asks for the merge.
If a matching PR already exists, update that PR/branch instead of opening a
duplicate.

Commit messages should be relatively detailed. Use a concise subsystem-style
subject (for example `lens: validate Adobe DNG reference geometry`) followed by
a body that gives an overview of the changes and, importantly, their rationale.
Call out compatibility decisions, non-obvious invariants, deliberately deferred
work, and the tests that were run. Avoid vague one-line commit messages for
nontrivial patches: the commit should remain useful months later without
requiring the reader to reconstruct the motivation from the diff.

## Repository Structure

- `src/libcolorscreen/`: Core rendering and processing library.
- `src/libcolorscreen/include`: Public API of the library.
- `src/colorscreen`: Command line utility accessing main functions of the library.
- `src/qtgui/`: Qt6-based graphical user interface. [See Developer Docs](.agents/qtgui.md)
- `src/gtkgui/`: Legacy GTK-based interface (if enabled) to be deprecated soon.
- `testsuite/`: Unit tests and verification suites (Check tests/Makefile.am for test registration).
- `m4/`: Autoconf macros

## Coding Style

The project uses different coding styles for its components:

- **`src/libcolorscreen/`**: Follows the **GNU coding style**. C++ files uses .C extensions
- **`src/colorscreen/`**: Follows the **GNU coding style**. C++ files uses .C extensions
- **`src/gtkgui/`**: Follows the **GNU coding style**. C++ files uses .C extensions
- **`src/qtgui/`**: Follows the **Qt-like style**. C++ files uses .cpp extension.
- Every function should have comment what it does.  In GNU style sections it should explain all function parameters in upper case
- Every global class and enum should also have comment

- **Memory**: Use RAII and Smart Pointers (`std::unique_ptr`, `std::shared_ptr`). Raw new/delete should only be used when necessary to interface legacy code.

## Joy and entertaiment

- After larger achivements we can do celebratory animations similar to `src/qtgui/JolyAnimation.cpp`

## Common Troubleshooting

- **Linker Errors**: If static linking fails, try adding `--disable-static-link` to the configure flags.
- **Dependencies**: Ensure `pkg-config` can find development headers for `lcms2`, `libzip`, `libraw`, `libtiff`, and `fftw3`.
