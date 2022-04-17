include_HEADERS = include/color.h include/colorscreen.h include/imagedata.h include/matrix.h include/render-fast.h include/render.h include/render-interpolate.h include/render-superposeimg.h include/screen.h include/scr-to-img.h 
#noinst_LIBRARIES = libcolorscreen.a
#libcolorscreen_a_SOURCES = render.C  render-fast.C  render-interpolate.C  screen.C  scr-to-img.C
lib_LTLIBRARIES = libcolorscreen.la
libcolorscreen_la_SOURCES = render.C  render-fast.C  render-interpolate.C  screen.C  scr-to-img.C imagedata.C loadsave.C 
libcolorscreen_la_LDFLAGS = -version-info 1:1:1
if JAVA_HOME_SET
  libcolorscreen_la_SOURCES += cz_cuni_mff_java_digitalColoring_interfaceWithC_ImageData.C
endif

noinst_PROGRAMS=matrix
matrix_SOURCES=matrix.C

cz_cuni_mff_java_digitalColoring_interfaceWithC_ImageData.o: cz_cuni_mff_java_digitalColoring_interfaceWithC_ImageData.h

cz_cuni_mff_java_digitalColoring_interfaceWithC_ImageData.lo: cz_cuni_mff_java_digitalColoring_interfaceWithC_ImageData.h

cz_cuni_mff_java_digitalColoring_interfaceWithC_ImageData.h: ImageData.java
	javac -cp . -h . $<
