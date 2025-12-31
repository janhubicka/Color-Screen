include_HEADERS = include/color.h include/colorscreen.h include/imagedata.h include/matrix.h include/scr-to-img.h  include/dllpublic.h include/scr-detect-parameters.h include/spectrum-to-xyz.h include/progress-info.h include/sensitivity.h include/precomputed-function.h include/mesh.h include/base.h include/tiff-writer.h include/stitch.h include/lens-correction.h include/tone-curve.h include/finetune.h include/histogram.h include/colorscreen-config.h include/dufaycolor.h  include/wratten.h include/screen-map.h  include/paget.h include/render-type-parameters.h include/render-parameters.h include/solver-parameters.h include/detect-regular-screen-parameters.h include/scr-to-img-parameters.h include/lens-warp-correction-parameters.h include/backlight-correction-parameters.h include/scanner-blur-correction-parameters.h include/strips.h
lib_LTLIBRARIES = libcolorscreen.la
libcolorscreen_la_SOURCES = render.C render-to-scr.C render-fast.C render-interpolate.C screen.C scr-to-img.C imagedata.C loadsave.C render-tile.C scr-detect.C render-scr-detect.C spectrum-to-xyz.C patches.C progress-info.C render-to-file.C color.C sensitivity.C solver.C mesh.C scr-detect-geometry.C analyze-dufay.C analyze-paget.C analyze-strips.C screen-map.C analyze-base.C tiff-writer.C backlight-correction.C stitch-image.C stitch-project.C icc.C render-parameters.C mapalloc.C parse-captureone-lcc.C dufaycolor.C wratten.C spectrum.C spectrum-dyes.C spectrum-illuminants.C spectrum-responses.C tone-curve.C lens-warp-correction.C matrix-profile.C scr-detect-colors.C finetune.C homography.C gsl-utils.C scanner-blur-correction.C simulate.C has-regular-screen.C deconvolute.C mtf.C
EXTRA_DIST = lru-cache.h analyze-base-worker.h gaussian-blur.h icc-srgb.h  render-diff.h render-tile.h sharpen.h gsl-utils.h loadsave.h mapalloc.h render-interpolate.h render-to-file.h spectrum-dyes.h icc.h nmsimplex.h render-superposeimg.h spectrum.h analyze-dufay.h analyze-paget.h analyze-strips.h analyze-base.h  bitmap.h render-fast.h spline.h screen.h render-scr-detect.h patches.h render-to-scr.h render.h solver.h scr-detect.h backlight-correction.h mem-luminosity.h homography.h simulate.h deconvolute.h mtf.h

if RENDER_EXTRA
nodist_libcolorscreen_la_SOURCES = render-extra/render-extra.C
endif
# -no-undefined is needed in order to build DLL on Windows.
libcolorscreen_la_LDFLAGS = -version-info 2:0:0 $(WIN_NO_UNDEFINED)
libcolorscreen_la_CXXFLAGS = -fvisibility=hidden -DLIBCOLORSCREEN

noinst_PROGRAMS=unittests
if static_link
  unittests_LDFLAGS = -static
  unittests_CXXFLAGS = -static
endif
unittests_LDADD = libcolorscreen.la 
unittests_SOURCES=unittests.C
