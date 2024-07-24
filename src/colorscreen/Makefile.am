bin_PROGRAMS=colorscreen
if static_link
  AM_LDFLAGS = -static -all-static
  AM_CXXFLAGS = -DLIBCOLORSCREEN -static -all-static
endif
LDADD = ../libcolorscreen/libcolorscreen.la 
colorscreen_SOURCES=colorscreen.C
