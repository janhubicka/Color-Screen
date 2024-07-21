bin_PROGRAMS=colorscreen
if static_link
  AM_LDFLAGS = -static
  colorscreen_CXXFLAGS = -DLIBCOLORSCREEN
endif
LDADD = ../libcolorscreen/libcolorscreen.la 
colorscreen_SOURCES=colorscreen.C
