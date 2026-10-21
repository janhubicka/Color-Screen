# Color-Screen 2.0 — user manual

**For the Qt GUI in the 2.0 development series.** The older Java/GTK interface
and historical command-line instructions belong to the
[Color-Screen 1.0 archive](Color-Screen-1.0-Documentation).
The [parameter-file reference](Color-Screen-2.0-Parameter-Format) and
[stitch-project reference](Color-Screen-2.0-Project-Format) describe the new
JSON files separately.

Color-Screen reconstructs colour information from scans of early additive
colour-screen photographs. It is a non-destructive editor: the scan and
processing parameters are separate, and rendering produces a separate output
image.

## Installing and starting

Development installers are available from the
[`current` prerelease](https://github.com/janhubicka/Color-Screen/releases/tag/current).
This is **not yet the final 2.0 release**.

- **Windows:** Choose the ordinary `ucrt64` installer for a general x86-64
  computer. The portable ZIP works without an installer if you extract the
  complete archive and start `bin/colorscreen-qt.exe`. The specialised
  `znver2` and `znver4` packages require compatible processors.
- **macOS:** Unpack the macOS ZIP and launch `Color-Screen.app`. Development
  builds are ad-hoc signed, not Developer-ID notarized; macOS may require
  explicitly allowing the application to open.
- **Linux:** Build from source with `--enable-qtgui` as described in the
  [repository README](https://github.com/janhubicka/Color-Screen#building-from-source-code).

The GUI executable is called `colorscreen-qt`.

## Opening scans, parameters and projects

Choose **File → Open Image or Document…** (usually Ctrl+O). Select a scan or
multiple scans to open independent documents. Supported inputs include common
TIFF/JPEG/PNG/JPEG 2000 images and supported camera RAW formats.

You may also open a saved processing document (`.cspar`) or a stitching
project (`.csprj`). A `.cspar` file can remember its source-image path and
basic source identity, but it **does not contain the pixels**. If a linked scan
has moved, select its new location when prompted. Review a warning about
different image dimensions or file sizes before accepting a replacement.
A failed image open leaves the previous document intact.

A stitching project similarly refers to its individual scanned tiles. Keep
the source images alongside the project or preserve their relative directory
layout when moving it.

## Understanding the workspace

The application can have several documents open at once. Each document has
its own image, processing settings, Undo/Redo history, and rendering and
analysis jobs. The document tabs can be reordered or detached; **Window →
Arrange Images** can bring detached images back into the workspace or tile
and cascade several views.

The **image canvas** is for inspecting the scan, registration and recovered
image. The **right inspector** exposes processing stages and specialist
controls. The **Workflow** summary describes the current processing state
and gives the next useful action. When a recommendation has an unambiguous
destination, **Open stage** navigates to the matching inspector panel.
A collapsed specialist section can be reopened with its header; folding
sections is a display preference, not a change to the image.

The **View** menu contains zoom, fit-to-screen, scan/final coordinate
presentation and an independent **Gamut Warning** overlay. Gamut warnings
are diagnostics for the canvas, not permanent image adjustments.

## Recommended first-time workflow

The exact controls depend on the capture and historical photographic
process. A practical sequence is:

1. **Digital capture.** Confirm the kind of digital scan and its orientation,
   resolution and (for RAW inputs) demosaicing. Select the *object crop* so the
   scanned photograph and its surroundings remain available where needed.
   Then use **Photographic image area** to identify just the photograph,
   excluding surrounding border, bindings or tapes as appropriate.
2. **Image layer.** Use the captured image channels that are physically
   available. Ordinary colour scans and native monochrome/infrared captures
   are not interchangeable. An RGB-mixer view is a simulation when no true
   scalar source channel exists.
3. **Process → Screen.** Select the actual historical screen type, then
   configure its process-specific properties. **Screen is the first Process
   subtab**; **Contact copy** is optional and should usually be considered
   afterward, when simulating a photographic copy or negative-to-positive
   darkroom response.
4. **Registration.** Find or refine the screen alignment. For regular screen
   geometries this usually means screen detection, inspection of control
   points and fitting geometry. Review misregistered regions and outliers
   before accepting a fit. A stochastic screen (such as an Autochrome-like
   process) follows a different recovery path and does not use the same
   regular-lattice geometry fit.
5. **Reconstruction and sharpening.** Inspect reconstructed colour and
   detail at representative zoom levels. Use sharpening and **Capture MTF**
   measurements when the scan's optical transfer or desired detail requires
   them. Measurements and their source provenance belong to the processing
   document; transient fit diagnostics do not.
6. **Colour, tone and profile.** Adjust viewing colour, white balance,
   brightness/saturation and tone curve, then use profile calibration when
   suitable references or calibration spots exist.
7. **Save and export.** Save the editable parameters separately, then
   render a final image with the required output colour profile and image
   geometry.

The **Workflow** summary provides context-aware next steps, so you need not
perform every specialist operation for every photograph.

## Registration and geometry controls

Registration points are image-processing measurements, not decorative
overlays. **Registration** menu actions can show points, show detected patch
centres, select/deselect points, remove or prune misplaced points, and
optimise geometry. When a geometry fit fails, the previous accepted mapping
is retained; examine point coverage, outliers and fit settings before
trying again.

Use **View → Fit to Screen** or zoom controls to inspect the result. Depending
on scan type, the coordinate selector can display original scan coordinates
or the corrected screen/final view. These are different coordinate systems,
not two different source images.

## Saving processing parameters

**File → Save Parameters** (Ctrl+S) saves the current document's settings.
**File → Save Parameters As…** (usually Ctrl+Shift+S) selects a new destination.
The native file is a plain UTF-8 JSON **`.cspar`, schema 3**, which can
optionally include a link to its source image. Historical plain-text
`.par` files remain readable and can be migrated, but the native 2.0 format
is recommended for new work.

Parameters include the screen geometry, processing choices, persistent MTF
and colour/tone settings. They do **not** include the source scan, current
window layout, cache preferences, or the display's output colour profile.
Do not treat a `.cspar` file as a backup of the image.

**File → Save Reproducibility Report…** exports a human-readable processing
summary useful for scientific records and sharing workflow details.

For manual JSON editing, consult the
[format reference](Color-Screen-2.0-Parameter-Format). The reader checks its
schema strictly; do not assume arbitrary added fields are accepted.

## Rendering to an image file

Choose **File → Render…** (Ctrl+E). The Render to File dialog chooses the
destination, output rendering mode, colour/encoding settings, and output
profile. You may render in original scan geometry or corrected screen
geometry where supported. The photographic image-area selection can also
affect the crop of a rendered image.

**Output ICC profile, transfer/gamma and per-view gamut warnings are
render/view options**, not permanent document calibration. Saving a different
export does not need to change the underlying `.cspar` processing values.
A gamut-warning overlay is not baked into ordinary exports.

## Stitching projects

The native **`.csprj` project is JSON schema 2**, with an array of tiles
referencing external scans. Each tile records its image path, stitching
position/size/shift, screen type and geometry. It does not embed the original
pixel data or an entire copy of the `.cspar` capture/colour/MTF settings.
Older projects beginning with
`color_screen_stitch_project_version: 1` remain readable. Newly saved
projects use JSON. See the [project file format](Color-Screen-2.0-Project-Format).

The CLI `colorscreen stitch` can construct a project, and the Qt GUI can
open the resulting `.csprj` as an image document. Large projects load
tiles progressively using the same core cache as individual images.

## Preferences, scratch space and memory

Use **Edit → Preferences…** for application-wide settings:

- **Performance:** Let the image cache choose an automatic memory budget, or
  configure a custom share (10–85%). The budget is shared across core
  caches and RAW-source resources; it is a **soft cache limit**, not a
  guaranteed upper bound on process memory while active images/renderers
  retain pixel buffers.
- **Scratch disks:** Set a writable temporary directory for future
  memory-mapped scratch allocations. The default is the platform temporary
  directory; on some systems that location may use RAM-backed storage.
- **Easter eggs:** Enable or disable the optional idle animations.

Click **OK** to apply validated changes or **Cancel** to discard them.
Preferences are global and are **not** stored in `.cspar` or `.csprj`.

Choose **Help → Cache Statistics…** for a read-only report on available
memory, the current budget, and core caches. Entries are ordered primarily
by retained size, then entry count. A large scan may use more total memory
than the cache-owned figures show.

## Recovery and common problems

- **An image or tile cannot be found:** Locate the moved source file, or
  restore the relative directory layout from the original project.
- **The replacement image does not match:** Check the dimensions/file
  identity warning; do not silently substitute a different scan.
- **Registration looks wrong:** Verify capture orientation, screen type,
  control-point coverage and fit settings before sharpening.
- **The image looks unexpectedly dark or inverted:** Check the capture's
  interpretation, historical process, and whether optional Contact copy
  simulation was enabled.
- **Memory or scratch use is excessive:** Inspect Help → Cache Statistics…,
  change the cache percentage or scratch directory in Preferences, and
  remember that currently displayed large scans also occupy memory.
- **Work is interrupted:** The GUI maintains per-document crash-recovery
  state. Reopen the recovered document and verify both image association
  and parameters before overwriting a saved version.

This manual describes the current **development** interface. Menu and panel
labels can still be refined during 2.0 GUI acceptance testing.
