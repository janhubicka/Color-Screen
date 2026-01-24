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
pkg_check_modules(FFTW3 fftw3)

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
)

set(SOURCES
    main.cpp
    MainWindow.cpp
    ImageWidget.cpp
    NavigationView.cpp
    Renderer.cpp
    ParameterState.cpp
    LinearizationPanel.cpp
    ParameterPanel.cpp
    TilePreviewPanel.cpp
    SharpnessPanel.cpp
    ScreenPanel.cpp
    ColorPanel.cpp
    MTFChartWidget.cpp
    SpectraChartWidget.cpp
    CIEChartWidget.cpp
    ThamesAnimation.cpp
    PagetAnimation.cpp
    GeometryPanel.cpp
    GeometrySolverWorker.cpp
    DetectScreenWorker.cpp
    FinetuneWorker.cpp
    WorkerBase.cpp
    TaskQueue.cpp
    TaskQueue.cpp
    SubtitleOverlay.cpp
    CoordinateTransformer.cpp
)

set(HEADERS
    MainWindow.h
    ImageWidget.h
    Renderer.h
    NavigationView.h
    LinearizationPanel.h
    ParameterPanel.h
    TilePreviewPanel.h
    SharpnessPanel.h
    ScreenPanel.h
    ColorPanel.h
    MTFChartWidget.h
    SpectraChartWidget.h
    CIEChartWidget.h
    ThamesAnimation.h
    PagetAnimation.h
    GeometryPanel.h
    GeometrySolverWorker.h
    DetectScreenWorker.h
    FinetuneWorker.h
    WorkerBase.h
    TaskQueue.h
    SubtitleOverlay.h
    SmartSpinBox.h
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
    jpeg
    turbojpeg
    m
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(colorscreen-qt PRIVATE OpenMP::OpenMP_CXX)
endif()
