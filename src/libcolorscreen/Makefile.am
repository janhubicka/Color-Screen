include_HEADERS = include/color.h include/colorscreen.h include/imagedata.h include/matrix.h include/render-fast.h include/render.h include/render-to-scr.h include/screen.h include/scr-to-img.h  include/dllpublic.h include/scr-detect.h include/render-scr-detect.h include/spectrum-to-xyz.h include/patches.h include/progress-info.h include/sensitivity.h include/precomputed-function.h include/solver.h include/mesh.h include/base.h include/bitmap.h include/tiff-writer.h include/stitch.h include/backlight-correction.h include/analyze-dufay.h include/analyze-paget.h include/analyze-base.h include/lens-correction.h include/tone-curve.h include/finetune.h include/spline.h include/histogram.h include/colorscreen-config.h
lib_LTLIBRARIES = libcolorscreen.la
libcolorscreen_la_SOURCES = render.C render-to-scr.C render-fast.C render-interpolate.C screen.C scr-to-img.C imagedata.C loadsave.C render-tile.C scr-detect.C render-scr-detect.C spectrum-to-xyz.C patches.C progress-info.C render-to-file.C color.C sensitivity.C solver.C mesh.C scr-detect-geometry.C analyze-dufay.C analyze-paget.C screen-map.C analyze-base.C tiff-writer.C backlight-correction.C stitch-image.C stitch-project.C icc.C render-parameters.C mapalloc.C parse-captureone-lcc.C dufaycolor.C wratten.C spectrum.C spectrum-dyes.C spectrum-illuminants.C spectrum-responses.C tone-curve.C lens-warp-correction.C matrix-profile.C scr-detect-colors.C finetune.C
EXTRA_DIST = lru-cache.h analyze-base-worker.h gaussian-blur.h icc-srgb.h  render-diff.h render-tile.h sharpen.h wratten.h dj_fft.h gsl-utils.h loadsave.h mapalloc.h render-interpolate.h render-to-file.h spectrum-dyes.h dufaycolor.h icc.h nmsimplex.h render-superposeimg.h screen-map.h spectrum.h

if RENDER_EXTRA
nodist_libcolorscreen_la_SOURCES = render-extra/render-extra.C
endif
# -no-undefined is needed in order to build DLL on Windows.
libcolorscreen_la_LDFLAGS = -version-info 1:1:1 $(WIN_NO_UNDEFINED)
libcolorscreen_la_CXXFLAGS = -fvisibility=hidden -DLIBCOLORSCREEN

noinst_PROGRAMS=unittests
unittests_SOURCES=unittests.C
