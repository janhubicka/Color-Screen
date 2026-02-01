bin_PROGRAMS = colorscreen-qt

colorscreen_qt_SOURCES = \
    main.cpp \
    MainWindow.cpp \
    ImageWidget.cpp \
    BacklightChartWidget.cpp \
    Renderer.cpp \
    NavigationView.cpp \
    ParameterState.cpp \
    CapturePanel.cpp \
    ParameterPanel.cpp \
    TilePreviewPanel.cpp \
    SharpnessPanel.cpp \
    ScreenPanel.cpp \
    GeometryPanel.cpp \
    GeometrySolverWorker.cpp \
    DetectScreenWorker.cpp \
    FlatFieldWorker.cpp \
    FinetuneWorker.cpp \
    WorkerBase.cpp \
    ColorPanel.cpp \
    MTFChartWidget.cpp \
    DeformationChartWidget.cpp \
    SpectraChartWidget.cpp \
    CIEChartWidget.cpp \
    FinetuneImagesPanel.cpp \
    ThamesAnimation.cpp \
    PagetAnimation.cpp \
    JolyAnimation.cpp \
    TaskQueue.cpp \
    Logging.cpp \
    SubtitleOverlay.cpp \
    CoordinateTransformer.cpp \
    ScalableImageLabel.cpp \
    FocusAnalysisWorker.cpp



# MOC Generation
# We need to explicitly run moc on headers that have Q_OBJECT
MOC_SOURCES = \
    moc_MainWindow.cpp \
    moc_ImageWidget.cpp \
    moc_BacklightChartWidget.cpp \
    moc_Renderer.cpp \
    moc_NavigationView.cpp \
    moc_CapturePanel.cpp \
    moc_ParameterPanel.cpp \
    moc_TilePreviewPanel.cpp \
    moc_SharpnessPanel.cpp \
    moc_ScreenPanel.cpp \
    moc_GeometryPanel.cpp \
    moc_GeometrySolverWorker.cpp \
    moc_DetectScreenWorker.cpp \
    moc_FlatFieldWorker.cpp \
    moc_FinetuneWorker.cpp \
    moc_WorkerBase.cpp \
    moc_ColorPanel.cpp \
    moc_SmartSpinBox.cpp \
    moc_MTFChartWidget.cpp \
    moc_DeformationChartWidget.cpp \
    moc_SpectraChartWidget.cpp \
    moc_CIEChartWidget.cpp \
    moc_FinetuneImagesPanel.cpp \
    moc_ThamesAnimation.cpp \
    moc_PagetAnimation.cpp \
    moc_JolyAnimation.cpp \
    moc_TaskQueue.cpp \
    moc_ScalableImageLabel.cpp \
    moc_FocusAnalysisWorker.cpp

EXTRA_DIST = ImageWidget.h CapturePanel.h MainWindow.h MTFChartWidget.h DeformationChartWidget.h FinetuneImagesPanel.h BacklightChartWidget.h ColorUtils.h NavigationView.h ParameterPanel.h ParameterState.h Renderer.h SharpnessPanel.h SmartSpinBox.h TilePreviewPanel.h ScreenPanel.h GeometryPanel.h GeometrySolverWorker.h DetectScreenWorker.h FlatFieldWorker.h FinetuneWorker.h WorkerBase.h ColorPanel.h SpectraChartWidget.h CIEChartWidget.h ThamesAnimation.h PagetAnimation.h JolyAnimation.h TaskQueue.h SubtitleOverlay.h Logging.h colorscreen.rc icons/hand.svg icons/arrow.svg icons/plus.svg icons/crosshair.svg icons/rotate-left.svg icons/rotate-right.svg icons/zoom-in.svg icons/zoom-out.svg icons/zoom-100.svg icons/zoom-fit.svg icons/mirror.svg

# Add moc files to the build
nodist_colorscreen_qt_SOURCES = $(MOC_SOURCES) qrc_resources.cpp

# Rules to generate moc files
moc_%.cpp: %.h
	$(MOC) $< -o $@

# Rule to generate resource file (RCC is set by configure)
qrc_resources.cpp: resources.qrc ../../images/icon.svg icons/hand.svg icons/arrow.svg icons/arrow.svg  icons/plus.svg   icons/rotate-right.svg  icons/zoom-fit.svg icons/zoom-out.svg icons/crosshair.svg  icons/rotate-left.svg  icons/zoom-100.svg      icons/zoom-in.svg icons/mirror.svg
	$(RCC) $< -o $@ 

# Cleanup generated files
CLEANFILES = $(MOC_SOURCES) qrc_resources.cpp

# Compiler flags
colorscreen_qt_CXXFLAGS = -std=c++17 $(Qt6_CFLAGS) -I$(top_srcdir)/src/libcolorscreen/include/
colorscreen_qt_LDFLAGS = $(GUI_LDFLAGS)
if static_link
  colorscreen_qt_CXXFLAGS += -DLIBCOLORSCREEN -static
  colorscreen_qt_LDFLAGS += -static
endif

# Linker flags
colorscreen_qt_LDADD = \
    $(top_builddir)/src/libcolorscreen/libcolorscreen.la \
    $(Qt6_LIBS)

# Windows Resource File
if HOST_WIN32
colorscreen_qt_LDADD += colorscreen_res.o
endif

colorscreen_res.o: colorscreen.rc
	$(WINDRES) $< -o $@

# Ensure moc and rcc are run before compilation

