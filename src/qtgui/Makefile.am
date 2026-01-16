bin_PROGRAMS = colorscreen-qt

colorscreen_qt_SOURCES = \
    main.cpp \
    MainWindow.cpp \
    ImageWidget.cpp \
    Renderer.cpp \
    NavigationView.cpp \
    ParameterState.cpp \
    LinearizationPanel.cpp \
    ParameterPanel.cpp \
    TilePreviewPanel.cpp \
    SharpnessPanel.cpp \
    ColorPanel.cpp \
    MTFChartWidget.cpp

if static_link
  AM_LDFLAGS = -static
  AM_CXXFLAGS = -DLIBCOLORSCREEN -static
endif

# MOC Generation
# We need to explicitly run moc on headers that have Q_OBJECT
MOC_SOURCES = \
    moc_MainWindow.cpp \
    moc_ImageWidget.cpp \
    moc_Renderer.cpp \
    moc_NavigationView.cpp \
    moc_LinearizationPanel.cpp \
    moc_ParameterPanel.cpp \
    moc_TilePreviewPanel.cpp \
    moc_SharpnessPanel.cpp \
    moc_ColorPanel.cpp \
    moc_SmartSpinBox.cpp \
    moc_MTFChartWidget.cpp

EXTRA_DIST = ImageWidget.h LinearizationPanel.h MainWindow.h MTFChartWidget.h NavigationView.h ParameterPanel.h ParameterState.h Renderer.h SharpnessPanel.h SmartSpinBox.h TilePreviewPanel.h ColorPanel.h

# Add moc files to the build
nodist_colorscreen_qt_SOURCES = $(MOC_SOURCES)

# Rules to generate moc files
moc_%.cpp: %.h
	$(MOC) $< -o $@

# Cleanup generated files
CLEANFILES = $(MOC_SOURCES)

# Compiler flags
colorscreen_qt_CXXFLAGS = -std=c++17 $(Qt6_CFLAGS) -I$(top_srcdir)/src/libcolorscreen/include/

# Linker flags
colorscreen_qt_LDADD = \
    $(top_builddir)/src/libcolorscreen/libcolorscreen.la \
    $(Qt6_LIBS)

# Ensure moc is run before compilation
BUILT_SOURCES = $(MOC_SOURCES)
