include_HEADERS = include/color.h include/colorscreen.h include/imagedata.h include/matrix.h include/render-fast.h include/render.h include/render-to-scr.h include/screen.h include/scr-to-img.h  include/dllpublic.h include/scr-detect.h include/render-scr-detect.h include/spectrum-to-xyz.h include/patches.h include/progress-info.h include/sensitivity.h include/precomputed-function.h include/solver.h include/mesh.h include/base.h include/bitmap.h include/tiff-writer.h include/stitch.h include/backlight-correction.h include/analyze-dufay.h include/analyze-paget.h include/analyze-base.h include/lens-correction.h include/tone-curve.h
#noinst_LIBRARIES = libcolorscreen.a
#libcolorscreen_a_SOURCES = render.C  render-fast.C  render-interpolate.C  screen.C  scr-to-img.C
lib_LTLIBRARIES = libcolorscreen.la
libcolorscreen_la_SOURCES = render.C  render-to-scr.C render-fast.C  render-interpolate.C  screen.C  scr-to-img.C imagedata.C loadsave.C render-tile.C scr-detect.C render-scr-detect.C spectrum-to-xyz.C patches.C progress-info.C render-to-file.C color.C sensitivity.C solver.C mesh.C scr-detect-geometry.C analyze-dufay.C analyze-paget.C screen-map.C analyze-base.C tiff-writer.C backlight-correction.C stitch-image.C stitch-project.C icc.C render-parameters.C mapalloc.C parse-captureone-lcc.C dufaycolor.C wratten.C spectrum.C spectrum-dyes.C spectrum-illuminants.C spectrum-responses.C tone-curve.C lens-warp-correction.C matrix-profile.C scr-detect-colors.C  finetune.C

if RENDER_EXTRA
libcolorscreen_la_SOURCES += render-extra/render-extra.C
endif
# -no-undefined is needed in order to build DLL on Windows.
libcolorscreen_la_LDFLAGS = -version-info 1:1:1 $(WIN_NO_UNDEFINED)
libcolorscreen_la_CXXFLAGS = -fvisibility=hidden
#if JAVA_HOME_SET
#  libcolorscreen_la_SOURCES += cz_cuni_mff_java_digitalColoring_interfaceWithC_ImageData.C
#endif

noinst_PROGRAMS=matrix
matrix_SOURCES=matrix.C

cz_cuni_mff_java_digitalColoring_interfaceWithC_ImageData.C: cz_cuni_mff_java_digitalColoring_interfaceWithC_ImageData.h

cz_cuni_mff_java_digitalColoring_interfaceWithC_ImageData.h: ImageData.java
	javac -cp . -h . $<
