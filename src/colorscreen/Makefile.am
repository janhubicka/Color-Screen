bin_PROGRAMS=colorscreen
if static_link
  AM_LDFLAGS = -static
  AM_CXXFLAGS = -DLIBCOLORSCREEN -static
endif
LDADD = ../libcolorscreen/libcolorscreen.la
if HOST_WIN32
  LDADD += -lshell32
endif
colorscreen_SOURCES=colorscreen.C
