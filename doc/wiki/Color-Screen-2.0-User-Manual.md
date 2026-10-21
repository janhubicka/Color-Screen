# Color-Screen 2.0 — user manual (development draft)

Color-Screen reconstructs color photographs from scans of historical color-screen originals. Version 2.0 uses a Qt GUI; older Java GUI instructions belong to the [1.0 archive](Color-Screen-1.0-Documentation.md).

## Install and open

Download the rolling **current** prerelease from https://github.com/janhubicka/Color-Screen/releases/tag/current. It is not the final 2.0 release. Windows offers a general ucrt64 installer or portable ZIP; CPU-specific znver2/znver4 builds require compatible hardware. On macOS unpack the application ZIP. The macOS bundle is not notarized.

Open the scan in the Qt GUI. Keep the original scan available: parameter files describe processing settings, not the image pixels themselves.

## Suggested workflow

1. Load the scan and inspect its orientation and usable image area.
2. In **Process**, configure **Screen** before optional **Contact copy** processing.
3. Identify the historical screen type and adjust registration/alignment.
4. Inspect reconstructed color, tone and sharpness at a useful zoom.
5. Save parameters to a `.cspar` file; export the rendered image separately.

A parameter file can refer to its source image. If the image has moved, the application may ask you to locate it. The parameter file does not embed the source image.

## Large scans and memory

The image/cache subsystem is designed for very large scans. Preferences include a shared cache memory budget and scratch directory; the cache statistics view reports cache use. Temporary mapped allocations can use the configured scratch directory. Cache limits do not guarantee an absolute process-memory cap, because active renderers may retain their own references.

## File types

- `.cspar`: current JSON processing parameters; see [format reference](Color-Screen-2.0-Parameter-Format.md).
- `.par`: legacy parameter file, retained for reading.
- `.csprj`: stitching project. New projects use JSON; historical version-1 text files remain readable. See [project format](Color-Screen-2.0-Project-Format.md).

This manual is a source-aligned development draft, not a guarantee that every GUI label is final. GUI testing and screenshots should be used to correct terminology before the 2.0 release.
