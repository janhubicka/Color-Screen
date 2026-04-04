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
make check
```

- **Unit Tests**: Implemented in `src/libcolorscreen/unittests.C`.
- **CLI Tests**: The command-line tool `colorscreen` can be tested using scripts located in the `testsuite/` subdirectory.

### Continuous Integration

The project uses GitHub Actions for automated testing. Workflows are defined in `.github/workflows/` and cover:
- **Ubuntu**: Linux builds and tests.
- **macOS**: Apple Silicon and Intel builds.
- **Windows**: MSYS2/MinGW-w64 builds.

## Repository Structure

- `src/libcolorscreen/`: Core rendering and processing library.
- `src/libcolorscreen/include`: Public API of the library.
- `src/colorscreen`: Command line utility accessing main functions of the library.
- `src/qtgui/`: Qt6-based graphical user interface.
- `src/gtkgui/`: Legacy GTK-based interface (if enabled) to be deprecated soon.
- `testsuite/`: Unit tests and verification suites (Check tests/Makefile.am for test registration).
- `m4/`: Autoconf macros

## Coding Style

The project uses different coding styles for its components:

- **`src/libcolorscreen/`**: Follows the **GNU coding style**. C++ files uses .C extensions
- **`src/colorscreen/`**: Follows the **GNU coding style**. C++ files uses .C extensions
- **`src/gtkgui/`**: Follows the **GNU coding style**. C++ files uses .C extensions
- **`src/qtgui/`**: Does **not** follow the GNU coding style (uses a more standard Qt-like style). C++ files uses .cpp extension.

- **Memory**: Use RAII and Smart Pointers (`std::unique_ptr`, `std::shared_ptr`). Raw new/delete is prohibited.

## Common Troubleshooting

- **Linker Errors**: If static linking fails, try adding `--disable-static-link` to the configure flags.
- **Dependencies**: Ensure `pkg-config` can find development headers for `lcms2`, `libzip`, `libraw`, `libtiff`, and `fftw3`.
