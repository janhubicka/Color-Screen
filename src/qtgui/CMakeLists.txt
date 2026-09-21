cmake_minimum_required(VERSION 3.16)

project(colorscreen-qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Svg)
find_package(OpenMP)

# Dependencies based on configure.ac
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSL REQUIRED gsl)
pkg_check_modules(TIFF REQUIRED libtiff-4) # or libtiff
pkg_check_modules(LCMS2 REQUIRED lcms2)
pkg_check_modules(LIBRAW libraw)
pkg_check_modules(LIBZIP libzip)
pkg_check_modules(FFTW3 REQUIRED fftw3)
pkg_check_modules(EXIV2 REQUIRED exiv2)
pkg_check_modules(OPENJPEG REQUIRED libopenjp2)
pkg_check_modules(LIBPNG REQUIRED libpng)

set(LIBCOLORSCREEN_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../libcolorscreen")
set(LIBCOLORSCREEN_BUILD_DIR "${CMAKE_SOURCE_DIR}/../../build/src/libcolorscreen")

include_directories(
    ${LIBCOLORSCREEN_SOURCE_DIR}
    ${LIBCOLORSCREEN_SOURCE_DIR}/include
    ${LIBCOLORSCREEN_BUILD_DIR}
    ${LIBCOLORSCREEN_BUILD_DIR}/include
    ${GSL_INCLUDE_DIRS}
    ${TIFF_INCLUDE_DIRS}
    ${LCMS2_INCLUDE_DIRS}
    ${LIBRAW_INCLUDE_DIRS}
    ${LIBZIP_INCLUDE_DIRS}
    ${FFTW3_INCLUDE_DIRS}
    ${EXIV2_INCLUDE_DIRS}
    ${OPENJPEG_INCLUDE_DIRS}
    ${LIBPNG_INCLUDE_DIRS}
)

set(SOURCES
    main.cpp
    DocumentLifecycleSmoke.cpp
    WorkspaceChurnSmoke.cpp
    ColorScreenApplication.cpp
    WorkspaceWindow.cpp
    ImageViewWindow.cpp
    MainWindow.cpp
    MainWindowAnalysis.cpp
    BackgroundThreadRegistry.cpp
    DocumentProgressController.cpp
    FileRenderController.cpp
    OneShotOperationController.cpp
    InitialSetupGuideDialog.cpp
    ImageWidget.cpp
    BacklightChartWidget.cpp
    Renderer.cpp
    NavigationView.cpp
    ParameterState.cpp
    CapturePanel.cpp
    ParameterPanel.cpp
    TilePreviewPanel.cpp
    SharpnessPanel.cpp
    AdaptiveSharpeningDialog.cpp
    MTFFitDialog.cpp
    SlantedEdgeDialog.cpp
    ScreenPanel.cpp
    GeometryPanel.cpp
    GeometrySolverWorker.cpp
    DetectScreenWorker.cpp
    FlatFieldWorker.cpp
    FinetuneWorker.cpp
    FinetuneMisregisteredWorker.cpp
    WorkerBase.cpp
    ContactCopyPanel.cpp
    HDCurveWidget.cpp
    InteractiveChartWidget.cpp
    ToneCurveWidget.cpp
    ColorPanel.cpp
    MTFChartWidget.cpp
    DeformationChartWidget.cpp
    SpectraChartWidget.cpp
    CIEChartWidget.cpp
    FinetuneImagesPanel.cpp
    ThamesAnimation.cpp
    PagetAnimation.cpp
    JolyAnimation.cpp
    HurleyAnimation.cpp
    TaskQueue.cpp
    HistogramWorker.cpp
    FlowLayout.cpp
    MultiLineTabWidget.cpp
    Logging.cpp
    SubtitleOverlay.cpp
    CoordinateTransformer.cpp
    ScalableImageLabel.cpp
    FocusAnalysisWorker.cpp
    AdaptiveSharpeningWorker.cpp
    AdaptiveSharpeningChart.cpp
    RenderDialog.cpp
    ProfilePanel.cpp
    ColorOptimizerWorker.cpp
    CoordinateOptimizationWorker.cpp
    TilesPanel.cpp
    ImageLayerPanel.cpp
    MeasureDialog.cpp
    resources.qrc
)

set(HEADERS
    ColorScreenApplication.h
    DocumentLifecycleSmoke.h
    WorkspaceChurnSmoke.h
    WorkspaceWindow.h
    ImageViewWindow.h
    ImageWidget.h
    CapturePanel.h
    MainWindow.h
    BackgroundThreadRegistry.h
    DocumentProgressController.h
    FileRenderController.h
    OneShotOperationController.h
    ChangeParametersCommand.h
    InitialSetupGuideDialog.h
    MTFChartWidget.h
    DeformationChartWidget.h
    FinetuneImagesPanel.h
    BacklightChartWidget.h
    ColorUtils.h
    NavigationView.h
    ParameterPanel.h
    ParameterState.h
    Renderer.h
    SynchronizedRunnable.h
    SharpnessPanel.h
    SmartSpinBox.h
    TilePreviewPanel.h
    ScreenPanel.h
    GeometryPanel.h
    GeometrySolverWorker.h
    DetectScreenWorker.h
    FlatFieldWorker.h
    FinetuneWorker.h
    FinetuneMisregisteredWorker.h
    WorkerBase.h
    ContactCopyPanel.h
    HDCurveWidget.h
    InteractiveChartWidget.h
    ToneCurveWidget.h
    ColorPanel.h
    SpectraChartWidget.h
    CIEChartWidget.h
    ThamesAnimation.h
    PagetAnimation.h
    JolyAnimation.h
    HurleyAnimation.h
    TaskQueue.h
    SubtitleOverlay.h
    Logging.h
    AdaptiveSharpeningWorker.h
    AdaptiveSharpeningParameters.h
    AdaptiveSharpeningChart.h
    AdaptiveSharpeningDialog.h
    RenderDialog.h
    ProfilePanel.h
    CoordinateOptimizationWorker.h
    ColorOptimizerWorker.h
    CoordinateTransformer.h
    FlowLayout.h
    FocusAnalysisWorker.h
    HistogramWorker.h
    MultiLineTabWidget.h
    ScalableImageLabel.h
    TilesPanel.h
    ImageLayerPanel.h
    MeasureDialog.h
    MTFFitDialog.h
    SlantedEdgeDialog.h
)

add_executable(colorscreen-qt
    ${SOURCES}
    ${HEADERS}
)

target_link_libraries(colorscreen-qt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Svg
    ${LIBCOLORSCREEN_BUILD_DIR}/.libs/libcolorscreen.a
    ${GSL_LIBRARIES}
    ${TIFF_LIBRARIES}
    ${LCMS2_LIBRARIES}
    ${LIBRAW_LIBRARIES}
    ${LIBZIP_LIBRARIES}
    ${FFTW3_LIBRARIES}
    ${EXIV2_LIBRARIES}
    ${OPENJPEG_LIBRARIES}
    ${LIBPNG_LIBRARIES}
    jpeg
    turbojpeg
    m
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(colorscreen-qt PRIVATE OpenMP::OpenMP_CXX)
endif()
