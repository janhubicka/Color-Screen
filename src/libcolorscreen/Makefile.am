include_HEADERS = include/color.h include/colorscreen.h include/imagedata.h include/matrix.h include/render-fast.h include/render.h include/render-to-scr.h include/screen.h include/scr-to-img.h  include/dllpublic.h include/scr-detect.h include/render-scr-detect.h include/spectrum-to-xyz.h include/patches.h include/progress-info.h include/sensitivity.h include/precomputed-function.h
#noinst_LIBRARIES = libcolorscreen.a
#libcolorscreen_a_SOURCES = render.C  render-fast.C  render-interpolate.C  screen.C  scr-to-img.C
lib_LTLIBRARIES = libcolorscreen.la
libcolorscreen_la_SOURCES = render.C  render-to-scr.C render-fast.C  render-interpolate.C  screen.C  scr-to-img.C imagedata.C loadsave.C render-tile.C scr-detect.C render-scr-detect.C spectrum-to-xyz.C patches.C progress-info.C render-to-file.C color.C sensitivity.C
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
