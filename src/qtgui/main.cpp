#include "ColorScreenApplication.h"
#include "MainWindow.h"
#include "MultiLineTabWidget.h"
#include "ImageViewWindow.h"
#include "ImageWidget.h"
#include "SharpnessPanel.h"
#include "ToneCurveWidget.h"
#include "CoordinateTransformer.h"
#include "DocumentLifecycleSmoke.h"
#include "FlatFieldWorker.h"
#include "WorkspaceChurnSmoke.h"
#include "WorkspaceWindow.h"
#include "progress-info.h"

#include <QAction>
#include <QCheckBox>
#include <QColor>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDockWidget>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTabBar>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QToolBar>
#include <QTemporaryDir>
#include <QTransform>
#include <QUndoStack>

#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace {

/** Tone-curve wrapper exposing the protected plot mapping to the smoke probe. */
class PointerSmokeToneCurve final : public ToneCurveWidget {
public:
  using ToneCurveWidget::ToneCurveWidget;
  QPointF plotPoint(double x, double y) const { return plotToWidget(x, y); }
};

/** Deliver one synthetic mouse event using Qt6's local/global constructor. */
void sendPointerSmokeEvent(QWidget &target, QEvent::Type type, QPointF pos,
                           Qt::MouseButton button, Qt::MouseButtons buttons,
                           Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QMouseEvent event(type, pos, target.mapToGlobal(pos.toPoint()), button,
                    buttons, modifiers);
  QCoreApplication::sendEvent(&target, &event);
}

/** Deliver one synthetic key event to the focused canvas smoke widget. */
void sendKeySmokeEvent(QWidget &target, QEvent::Type type, int key) {
  QKeyEvent event(type, key, Qt::NoModifier);
  QCoreApplication::sendEvent(&target, &event);
}

/** Exercise beta-critical non-rendering UI/document invariants. */
bool runBetaInvariantSmoke() {
  auto fail = [](const char *reason) {
    qCritical() << "Beta invariant smoke failed:" << reason;
    return false;
  };

  // Logical tab availability must not depend on whether an ancestor is
  // currently mapped. The host deliberately remains hidden for the whole
  // probe, reproducing inspector reparent/detach transitions.
  QWidget hiddenHost;
  MultiLineTabWidget tabs(&hiddenHost);
  const int firstTab = tabs.addTab(new QWidget, QStringLiteral("First"));
  const int secondTab = tabs.addTab(new QWidget, QStringLiteral("Second"));
  const int thirdTab = tabs.addTab(new QWidget, QStringLiteral("Third"));
  hiddenHost.hide();

  tabs.setCurrentIndex(secondTab);
  if (tabs.currentIndex() != secondTab)
    return fail("hidden ancestor blocked logical tab selection");

  tabs.setTabVisible(firstTab, false);
  tabs.setTabVisible(secondTab, false);
  if (tabs.currentIndex() != thirdTab)
    return fail("hidden current tab did not fall back while ancestor was hidden");

  tabs.setCurrentIndex(firstTab);
  if (tabs.currentIndex() != thirdTab)
    return fail("programmatic selection entered an explicitly hidden tab");

  tabs.setTabVisible(secondTab, true);
  tabs.setCurrentIndex(secondTab);
  if (tabs.currentIndex() != secondTab)
    return fail("logically visible tab stayed unavailable under hidden ancestor");

  // Exercise the real MainWindow -> ChangeParametersCommand -> QUndoStack
  // path. Two updates from one stable parameter key must coalesce, while an
  // adjacent update from a different key must remain a separate user action
  // even when the human-visible Undo description is deliberately identical.
  QTemporaryDir recovery;
  if (!recovery.isValid())
    return fail("could not create temporary recovery directory");

  const FlatFieldAnalysisResult missingFlatField = FlatFieldWorker::analyze(
      recovery.filePath(QStringLiteral("missing-flat-field-reference.tif")),
      QString(), 1.0, colorscreen::image_data::demosaic_none, nullptr);
  if (missingFlatField.success || missingFlatField.cancelled ||
      missingFlatField.error.isEmpty() || missingFlatField.correction)
    return fail("flat-field helper did not report a missing reference cleanly");

  MainWindow window(recovery.path());
  QUndoStack *undoStack = window.findChild<QUndoStack *>();
  if (!undoStack)
    return fail("document undo stack was not found");
  undoStack->clear();

  const ParameterState baseline = window.documentStateSnapshot();
  ParameterState gamma1 = baseline;
  gamma1.rparams.gamma = baseline.rparams.gamma + 0.1;
  window.applySharedDocumentState(gamma1, QStringLiteral("Smoke adjustment"),
                                  QStringLiteral("smoke.capture.gamma"));

  ParameterState gamma2 = gamma1;
  gamma2.rparams.gamma = gamma1.rparams.gamma + 0.1;
  window.applySharedDocumentState(gamma2, QStringLiteral("Smoke adjustment"),
                                  QStringLiteral("smoke.capture.gamma"));
  if (undoStack->count() != 1 || window.documentStateSnapshot() != gamma2)
    return fail("same-key parameter updates did not merge into one undo step");

  ParameterState rotated = gamma2;
  rotated.scrToImg.final_rotation = gamma2.scrToImg.final_rotation + 1.0;
  window.applySharedDocumentState(rotated, QStringLiteral("Smoke adjustment"),
                                  QStringLiteral("smoke.output.rotation"));
  if (undoStack->count() != 2 || window.documentStateSnapshot() != rotated)
    return fail("different parameter keys merged despite identical Undo text");

  undoStack->undo();
  if (window.documentStateSnapshot() != gamma2)
    return fail("first undo did not restore the second merged gamma state");
  undoStack->undo();
  if (window.documentStateSnapshot() != baseline)
    return fail("merged gamma undo did not restore the original state");
  undoStack->redo();
  undoStack->redo();
  if (window.documentStateSnapshot() != rotated)
    return fail("redo did not restore both independent user actions");

  return true;
}

/** Exercise gesture interruption invariants without loading or rendering an image. */
bool runPointerInteractionSmoke() {
  auto fail = [](const char *reason) {
    qCritical() << "Pointer interaction smoke failed:" << reason;
    return false;
  };
  auto samePoint = [](const colorscreen::point_t &a,
                      const colorscreen::point_t &b) {
    return a.x == b.x && a.y == b.y;
  };

  ImageWidget image;
  image.resize(320, 240);
  colorscreen::render_parameters rparams;
  colorscreen::scr_to_img_parameters scrToImg;
  colorscreen::scr_detect_parameters detect;
  colorscreen::render_type_parameters renderType;
  colorscreen::solver_parameters solver;
  scrToImg.center = {100, 100};
  scrToImg.coordinate1 = {20, 0};
  scrToImg.coordinate2 = {0, 20};
  solver.add_point({100, 100}, {0, 0}, colorscreen::solver_parameters::red);
  image.setImage({}, &rparams, &scrToImg, &detect, &renderType, &solver);
  image.setShowRegistrationPoints(true);

  // A stale pan flag must not mutate the viewport after Qt reports no button.
  int viewChanges = 0;
  QObject::connect(&image, &ImageWidget::viewStateChanged, &image,
                   [&viewChanges](QRectF, double) { ++viewChanges; });
  image.setInteractionMode(ImageWidget::PanMode);
  sendPointerSmokeEvent(image, QEvent::MouseButtonPress, {40, 40},
                        Qt::LeftButton, Qt::LeftButton);
  const int beforeLostPan = viewChanges;
  sendPointerSmokeEvent(image, QEvent::MouseMove, {80, 40}, Qt::NoButton,
                        Qt::NoButton);
  if (viewChanges != beforeLostPan)
    return fail("lost left release continued panning");

  // Registration points are live-edited during a drag; a button-less move must
  // close the transaction before changing the point.
  int pointStarts = 0;
  int pointCompletions = 0;
  QObject::connect(&image, &ImageWidget::pointManipulationStarted, &image,
                   [&pointStarts]() { ++pointStarts; });
  QObject::connect(&image, &ImageWidget::pointsChanged, &image,
                   [&pointCompletions]() { ++pointCompletions; });
  image.setInteractionMode(ImageWidget::SelectMode);
  const colorscreen::point_t originalPoint = solver.points[0].img;
  const QPointF pointWidget = image.imageToWidget(originalPoint);
  sendPointerSmokeEvent(image, QEvent::MouseButtonPress, pointWidget,
                        Qt::LeftButton, Qt::LeftButton);
  sendPointerSmokeEvent(image, QEvent::MouseMove, pointWidget + QPointF(40, 40),
                        Qt::NoButton, Qt::NoButton);
  if (pointStarts != 1 || !samePoint(solver.points[0].img, originalPoint) ||
      pointCompletions != 0)
    return fail("registration drag survived a lost left release");

  // Switching tools discards an unfinished measurement; returning to Measure
  // must not let a later release commit the old gesture.
  int measurements = 0;
  const QMetaObject::Connection measurementCountConnection =
      QObject::connect(&image, &ImageWidget::distanceMeasured, &image,
                       [&measurements](colorscreen::point_t,
                                       colorscreen::point_t) {
                         ++measurements;
                       });
  image.setInteractionMode(ImageWidget::MeasureMode);
  sendPointerSmokeEvent(image, QEvent::MouseButtonPress, {20, 20},
                        Qt::LeftButton, Qt::LeftButton);
  image.setInteractionMode(ImageWidget::PanMode);
  image.setInteractionMode(ImageWidget::MeasureMode);
  sendPointerSmokeEvent(image, QEvent::MouseButtonRelease, {60, 60},
                        Qt::LeftButton, Qt::NoButton);
  if (measurements != 0)
    return fail("measurement leaked across a tool switch");

  // Measurement accepts two independent clicks, so the pointer may move (and
  // the real UI may zoom) between anchors without holding a button.
  colorscreen::point_t measuredStart {0, 0};
  colorscreen::point_t measuredEnd {0, 0};
  QObject::disconnect(measurementCountConnection);
  QObject::connect(&image, &ImageWidget::distanceMeasured, &image,
                   [&measurements, &measuredStart, &measuredEnd](
                       colorscreen::point_t p1, colorscreen::point_t p2) {
                     ++measurements;
                     measuredStart = p1;
                     measuredEnd = p2;
                   });
  measurements = 0;
  image.setInteractionMode(ImageWidget::PanMode);
  image.setInteractionMode(ImageWidget::MeasureMode);
  sendPointerSmokeEvent(image, QEvent::MouseButtonPress, {20, 30},
                        Qt::LeftButton, Qt::LeftButton);
  sendPointerSmokeEvent(image, QEvent::MouseButtonRelease, {20, 30},
                        Qt::LeftButton, Qt::NoButton);
  sendPointerSmokeEvent(image, QEvent::MouseMove, {90, 70}, Qt::NoButton,
                        Qt::NoButton);
  if (measurements != 0)
    return fail("first measurement click committed prematurely");
  sendPointerSmokeEvent(image, QEvent::MouseButtonPress, {90, 70},
                        Qt::LeftButton, Qt::LeftButton);
  sendPointerSmokeEvent(image, QEvent::MouseButtonRelease, {90, 70},
                        Qt::LeftButton, Qt::NoButton);
  if (measurements != 1 || !samePoint(measuredStart, {20, 30}) ||
      !samePoint(measuredEnd, {90, 70}))
    return fail("two-click measurement did not commit both anchors");

  // Capture One-style temporary Hand: holding Space must pan without
  // switching tools or discarding the first click of a precision operation.
  measurements = 0;
  image.setInteractionMode(ImageWidget::PanMode);
  image.setInteractionMode(ImageWidget::MeasureMode);
  sendPointerSmokeEvent(image, QEvent::MouseButtonPress, {30, 40},
                        Qt::LeftButton, Qt::LeftButton);
  sendPointerSmokeEvent(image, QEvent::MouseButtonRelease, {30, 40},
                        Qt::LeftButton, Qt::NoButton);
  sendKeySmokeEvent(image, QEvent::KeyPress, Qt::Key_Space);
  const int beforeTemporaryPan = viewChanges;
  sendPointerSmokeEvent(image, QEvent::MouseButtonPress, {80, 80},
                        Qt::LeftButton, Qt::LeftButton);
  sendPointerSmokeEvent(image, QEvent::MouseMove, {110, 95}, Qt::NoButton,
                        Qt::LeftButton);
  sendPointerSmokeEvent(image, QEvent::MouseButtonRelease, {110, 95},
                        Qt::LeftButton, Qt::NoButton);
  sendKeySmokeEvent(image, QEvent::KeyRelease, Qt::Key_Space);
  if (viewChanges <= beforeTemporaryPan || measurements != 0 ||
      image.interactionMode() != ImageWidget::MeasureMode)
    return fail("Space-hand pan changed or committed the active measure tool");

  // Right-click is a quick cancellation path for a pending first anchor.
  sendPointerSmokeEvent(image, QEvent::MouseButtonPress, {110, 95},
                        Qt::RightButton, Qt::RightButton);
  sendPointerSmokeEvent(image, QEvent::MouseButtonRelease, {110, 95},
                        Qt::RightButton, Qt::NoButton);
  sendPointerSmokeEvent(image, QEvent::MouseButtonPress, {50, 60},
                        Qt::LeftButton, Qt::LeftButton);
  sendPointerSmokeEvent(image, QEvent::MouseButtonRelease, {50, 60},
                        Qt::LeftButton, Qt::NoButton);
  if (measurements != 0)
    return fail("right-click did not cancel the pending measurement anchor");
  sendPointerSmokeEvent(image, QEvent::MouseButtonPress, {50, 60},
                        Qt::RightButton, Qt::RightButton);
  sendPointerSmokeEvent(image, QEvent::MouseButtonRelease, {50, 60},
                        Qt::RightButton, Qt::NoButton);

  // Temporary area tools accept the same click-move-click interaction while
  // preserving the existing drag-to-select shortcut.
  int areas = 0;
  QRect selectedArea;
  QObject::connect(&image, &ImageWidget::areaSelected, &image,
                   [&areas, &selectedArea](QRect area) {
                     ++areas;
                     selectedArea = area;
                   });
  image.setInteractionMode(ImageWidget::GenericAreaMode);
  sendPointerSmokeEvent(image, QEvent::MouseButtonPress, {25, 35},
                        Qt::LeftButton, Qt::LeftButton);
  sendPointerSmokeEvent(image, QEvent::MouseButtonRelease, {25, 35},
                        Qt::LeftButton, Qt::NoButton);
  sendPointerSmokeEvent(image, QEvent::MouseMove, {100, 85}, Qt::NoButton,
                        Qt::NoButton);
  if (areas != 0)
    return fail("first area click committed prematurely");
  sendPointerSmokeEvent(image, QEvent::MouseButtonPress, {100, 85},
                        Qt::LeftButton, Qt::LeftButton);
  sendPointerSmokeEvent(image, QEvent::MouseButtonRelease, {100, 85},
                        Qt::LeftButton, Qt::NoButton);
  if (areas != 1 || selectedArea != QRect(QPoint(25, 35), QPoint(100, 85)))
    return fail("two-click area selection did not commit both corners");

  // A regular screen with zero-vector sentinels must still allow manual
  // bootstrap: center click, neighboring +X green-dot click, then ordinary edit.
  ImageWidget bootstrapImage;
  bootstrapImage.resize(320, 240);
  colorscreen::scr_to_img_parameters bootstrapGeometry;
  bootstrapGeometry.type = colorscreen::Paget;
  colorscreen::solver_parameters bootstrapSolver;
  bootstrapImage.setImage({}, &rparams, &bootstrapGeometry, &detect, &renderType,
                          &bootstrapSolver);
  bootstrapImage.setInteractionMode(ImageWidget::SetCenterMode);
  if (bootstrapImage.screenCoordinateSetupStage() !=
      ImageWidget::ScreenCoordinateSetupStage::NeedCenter)
    return fail("unconfigured regular screen did not enter center bootstrap");

  sendPointerSmokeEvent(bootstrapImage, QEvent::MouseButtonPress, {80, 90},
                        Qt::LeftButton, Qt::LeftButton);
  sendPointerSmokeEvent(bootstrapImage, QEvent::MouseButtonRelease, {80, 90},
                        Qt::LeftButton, Qt::NoButton);
  if (!samePoint(bootstrapGeometry.center, {0, 0}) ||
      bootstrapImage.screenCoordinateSetupStage() !=
          ImageWidget::ScreenCoordinateSetupStage::NeedXAxis)
    return fail("first screen-coordinate click was not kept pending");
  sendPointerSmokeEvent(bootstrapImage, QEvent::MouseButtonPress, {80, 90},
                        Qt::RightButton, Qt::RightButton);
  sendPointerSmokeEvent(bootstrapImage, QEvent::MouseButtonRelease, {80, 90},
                        Qt::RightButton, Qt::NoButton);
  if (bootstrapImage.screenCoordinateSetupStage() !=
      ImageWidget::ScreenCoordinateSetupStage::NeedCenter)
    return fail("right-click did not cancel screen-coordinate bootstrap");
  sendPointerSmokeEvent(bootstrapImage, QEvent::MouseButtonPress, {80, 90},
                        Qt::LeftButton, Qt::LeftButton);
  sendPointerSmokeEvent(bootstrapImage, QEvent::MouseButtonRelease, {80, 90},
                        Qt::LeftButton, Qt::NoButton);
  sendPointerSmokeEvent(bootstrapImage, QEvent::MouseMove, {104, 96},
                        Qt::NoButton, Qt::NoButton);
  if (!samePoint(bootstrapGeometry.center, {0, 0}) ||
      !samePoint(bootstrapGeometry.coordinate1, {0, 0}))
    return fail("live screen-axis preview mutated document geometry");

  sendPointerSmokeEvent(bootstrapImage, QEvent::MouseButtonPress, {104, 96},
                        Qt::LeftButton, Qt::LeftButton);
  sendPointerSmokeEvent(bootstrapImage, QEvent::MouseButtonRelease, {104, 96},
                        Qt::LeftButton, Qt::NoButton);
  if (!colorscreen::screen_geometry_configured_p(bootstrapGeometry) ||
      !samePoint(bootstrapGeometry.center, {80, 90}) ||
      !samePoint(bootstrapGeometry.coordinate1, {24, 6}) ||
      !samePoint(bootstrapGeometry.coordinate2, {-6, 24}) ||
      bootstrapImage.screenCoordinateSetupStage() !=
          ImageWidget::ScreenCoordinateSetupStage::Editing)
    return fail("second screen-coordinate click did not create a valid basis");

  const colorscreen::point_t bootstrapCenter = bootstrapGeometry.center;
  sendPointerSmokeEvent(bootstrapImage, QEvent::MouseButtonPress, {80, 90},
                        Qt::LeftButton, Qt::LeftButton);
  sendPointerSmokeEvent(bootstrapImage, QEvent::MouseMove, {90, 95},
                        Qt::NoButton, Qt::LeftButton);
  sendPointerSmokeEvent(bootstrapImage, QEvent::MouseButtonRelease, {90, 95},
                        Qt::LeftButton, Qt::NoButton);
  if (samePoint(bootstrapGeometry.center, bootstrapCenter))
    return fail("manual bootstrap did not transition to normal center dragging");

  // Coordinate-system editing has a begin/end undo transaction. Lost releases
  // and tool switches must close it exactly once without applying a phantom
  // move; a normal right-button drag must still work.
  int coordinateStarts = 0;
  int coordinateFinishes = 0;
  QObject::connect(&image, &ImageWidget::coordinateSystemManipulationStarted,
                   &image, [&coordinateStarts]() { ++coordinateStarts; });
  QObject::connect(&image, &ImageWidget::coordinateSystemManipulationFinished,
                   &image, [&coordinateFinishes]() { ++coordinateFinishes; });
  image.setInteractionMode(ImageWidget::SetCenterMode);
  const colorscreen::point_t originalAxis = scrToImg.coordinate1;
  sendPointerSmokeEvent(image, QEvent::MouseButtonPress, {120, 100},
                        Qt::RightButton, Qt::RightButton);
  sendPointerSmokeEvent(image, QEvent::MouseMove, {140, 100}, Qt::NoButton,
                        Qt::NoButton);
  if (!samePoint(scrToImg.coordinate1, originalAxis) ||
      coordinateStarts != 1 || coordinateFinishes != 1)
    return fail("coordinate drag did not settle after a lost release");

  sendPointerSmokeEvent(image, QEvent::MouseButtonPress, {120, 100},
                        Qt::RightButton, Qt::RightButton);
  image.setInteractionMode(ImageWidget::PanMode);
  if (coordinateStarts != 2 || coordinateFinishes != 2)
    return fail("tool switch left a coordinate transaction open");

  image.setInteractionMode(ImageWidget::SetCenterMode);
  sendPointerSmokeEvent(image, QEvent::MouseButtonPress, {120, 100},
                        Qt::RightButton, Qt::RightButton);
  sendPointerSmokeEvent(image, QEvent::MouseMove, {140, 100}, Qt::NoButton,
                        Qt::RightButton);
  sendPointerSmokeEvent(image, QEvent::MouseButtonRelease, {140, 100},
                        Qt::RightButton, Qt::NoButton);
  if (samePoint(scrToImg.coordinate1, originalAxis) ||
      coordinateStarts != 3 || coordinateFinishes != 3)
    return fail("normal coordinate drag was broken by gesture hardening");

  PointerSmokeToneCurve chart;
  chart.resize(320, 240);
  const double oldMinX = chart.minX();
  const double oldMaxX = chart.maxX();
  const QPointF chartCenter = chart.plotPoint(0, 0);
  sendPointerSmokeEvent(chart, QEvent::MouseButtonPress, chartCenter,
                        Qt::RightButton, Qt::RightButton);
  sendPointerSmokeEvent(chart, QEvent::MouseMove, chartCenter + QPointF(30, 0),
                        Qt::NoButton, Qt::NoButton);
  if (chart.minX() != oldMinX || chart.maxX() != oldMaxX)
    return fail("chart panning survived a lost right release");

  // Right-button panning is a plot gesture, not a margin gesture.
  sendPointerSmokeEvent(chart, QEvent::MouseButtonPress, {5, 5},
                        Qt::RightButton, Qt::RightButton);
  sendPointerSmokeEvent(chart, QEvent::MouseMove, {50, 50}, Qt::NoButton,
                        Qt::RightButton);
  sendPointerSmokeEvent(chart, QEvent::MouseButtonRelease, {50, 50},
                        Qt::RightButton, Qt::NoButton);
  if (chart.minX() != oldMinX || chart.maxX() != oldMaxX)
    return fail("chart margin started a pan gesture");

  chart.setCoordinateType(ToneCurveWidget::CoordinateType::Linear);
  chart.setToneCurve(colorscreen::tone_curve::tone_curve_custom,
                     {{0, 0}, {0.5, 0.5}, {1, 1}});
  int curveChanges = 0;
  QObject::connect(&chart, &ToneCurveWidget::controlPointsChanged, &chart,
                   [&curveChanges](const auto &) { ++curveChanges; });
  const QPointF middlePoint = chart.plotPoint(0.5, 0.5);
  sendPointerSmokeEvent(chart, QEvent::MouseButtonPress, middlePoint,
                        Qt::LeftButton, Qt::LeftButton);
  sendPointerSmokeEvent(chart, QEvent::MouseButtonRelease, middlePoint,
                        Qt::RightButton, Qt::LeftButton);
  sendPointerSmokeEvent(chart, QEvent::MouseMove,
                        middlePoint + QPointF(15, -15), Qt::NoButton,
                        Qt::LeftButton);
  if (curveChanges != 1)
    return fail("secondary release ended a left tone-curve drag");
  sendPointerSmokeEvent(chart, QEvent::MouseButtonRelease,
                        middlePoint + QPointF(15, -15), Qt::LeftButton,
                        Qt::NoButton);

  chart.setToneCurve(colorscreen::tone_curve::tone_curve_custom,
                     {{0, 0}, {0.5, 0.5}, {1, 1}});
  curveChanges = 0;
  const QPointF resetMiddlePoint = chart.plotPoint(0.5, 0.5);
  sendPointerSmokeEvent(chart, QEvent::MouseButtonPress, resetMiddlePoint,
                        Qt::LeftButton, Qt::LeftButton);
  sendPointerSmokeEvent(chart, QEvent::MouseMove,
                        resetMiddlePoint + QPointF(25, -25), Qt::NoButton,
                        Qt::NoButton);
  if (curveChanges != 0)
    return fail("tone-curve point drag survived a lost left release");

  return true;
}

} // namespace

/** Start the Qt GUI, restore any crashed document session, and open every
    positional image argument in an independent MainWindow.  */
int main(int argc, char *argv[]) {
  // Enable plugin diagnostics before QApplication initializes platform plugins.
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--debug-qt") == 0) {
      qputenv("QT_DEBUG_PLUGINS", "1");
      qputenv("QT_LOGGING_RULES", "*=true");
      break;
    }
  }

  ColorScreenApplication app(argc, argv);
  QApplication::setOrganizationName("ColorScreen");
  QApplication::setOrganizationDomain("colorscreen.org");
  QApplication::setApplicationName("colorscreen-qt");
  QApplication::setApplicationVersion("1.1");

  // Use INI files on every platform so settings are easy to inspect and move.
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QApplication::setStyle(QStyleFactory::create("Fusion"));
  QApplication::setWindowIcon(QIcon(":/images/icon.svg"));

  QCommandLineParser parser;
  parser.setApplicationDescription("ColorScreen Qt GUI");
  parser.addHelpOption();
  parser.addVersionOption();

  QCommandLineOption debugOption("debug-qt", "Enable Qt plugin debugging");
  parser.addOption(debugOption);

  QCommandLineOption smokeTestOption(
      "smoke-test", "Run for N ms and exit (for CI smoke testing)", "ms",
      "5000");
  parser.addOption(smokeTestOption);

  QCommandLineOption expectedWindowsOption(
      "smoke-test-expect-windows",
      "Fail smoke testing unless exactly N document windows were created",
      "count");
  parser.addOption(expectedWindowsOption);

  QCommandLineOption expectedTabsOption(
      "smoke-test-expect-tabs",
      "Fail smoke testing unless exactly N image tabs were created", "count");
  parser.addOption(expectedTabsOption);

  QCommandLineOption detachReattachOption(
      "smoke-test-detach-reattach",
      "Exercise detaching and reattaching the active image document");
  parser.addOption(detachReattachOption);

  QCommandLineOption closeToEmptyTabOption(
      "smoke-test-close-to-empty-tab",
      "Close all documents and require the empty workspace shell to disappear");
  parser.addOption(closeToEmptyTabOption);

  QCommandLineOption expectedTabBarOption(
      "smoke-test-expect-tabbar",
      "Fail smoke testing unless the document tab bar is visible or hidden",
      "state");
  parser.addOption(expectedTabBarOption);

  QCommandLineOption mdiArrangementOption(
      "smoke-test-mdi-arrangements",
      "Exercise tile, cascade, and tabbed QMdiArea presentations");
  parser.addOption(mdiArrangementOption);

  QCommandLineOption tileActivationStableOption(
      "smoke-test-tile-activation-stable",
      "Require activating a tiled document to keep every tile in place");
  parser.addOption(tileActivationStableOption);

  QCommandLineOption menuOrderOption(
      "smoke-test-menu-order",
      "Require Registration, Window, Help as the final three top-level menus");
  parser.addOption(menuOrderOption);

  QCommandLineOption toolbarOrderOption(
      "smoke-test-toolbar-before-tabs",
      "Require the active document toolbar to be above the document tab bar");
  parser.addOption(toolbarOrderOption);

  QCommandLineOption dragDetachOption(
      "smoke-test-tab-drag-detach",
      "Exercise dragging a document tab out into a detached window");
  parser.addOption(dragDetachOption);

  QCommandLineOption tabbedFillOption(
      "smoke-test-tabbed-fills-workspace",
      "Require the active tabbed MDI document to fill the document viewport");
  parser.addOption(tabbedFillOption);

  QCommandLineOption globalStatusBarOption(
      "smoke-test-global-statusbar",
      "Require every attached tab to share the workspace status bar");
  parser.addOption(globalStatusBarOption);

  QCommandLineOption userVisibleProgressOption(
      "smoke-test-user-visible-progress",
      "Exercise dedicated Cancel/Stop rows for long-running tasks");
  parser.addOption(userVisibleProgressOption);

  QCommandLineOption newViewOption(
      "smoke-test-new-view",
      "Create a secondary view and verify shared image and independent render mode");
  parser.addOption(newViewOption);

  QCommandLineOption windowLifetimeOption(
      "smoke-test-window-lifetime",
      "Exercise peer-view lifetime, detached-window lifetime, and one-tab UI");
  parser.addOption(windowLifetimeOption);

  QCommandLineOption slantedReferenceOption(
      "smoke-test-slanted-reference",
      "Open the current image as a separate slanted-edge reference view");
  parser.addOption(slantedReferenceOption);

  QCommandLineOption documentLifecycleOption(
      "smoke-test-document-lifecycle",
      "Exercise staged Save/Discard/Cancel document close workflows");
  parser.addOption(documentLifecycleOption);

  QCommandLineOption workspaceChurnOption(
      "smoke-test-workspace-churn",
      "Exercise staged multi-document/view MDI and detached-window churn");
  parser.addOption(workspaceChurnOption);

  QCommandLineOption timeReportOption(
      "time-report", "Enable internal time reporting of tasks");
  parser.addOption(timeReportOption);

  parser.addPositionalArgument("image", "Image file(s) to open.", "[image...]");
  parser.process(app);

  // Every Qt smoke invocation first checks low-level pointer gesture
  // interruption semantics. It needs no image fixture or worker thread.
  if (parser.isSet(smokeTestOption) && !runPointerInteractionSmoke())
    return 20;
  // Keep beta-critical undo and logical-tab invariants in the same ordinary
  // smoke lane so every supported Qt platform exercises them.
  if (parser.isSet(smokeTestOption) && !runBetaInvariantSmoke())
    return 21;

  if (parser.isSet(workspaceChurnOption) &&
      (parser.isSet(documentLifecycleOption) ||
       parser.isSet(detachReattachOption) ||
       parser.isSet(closeToEmptyTabOption) ||
       parser.isSet(expectedTabBarOption) ||
       parser.isSet(mdiArrangementOption) ||
       parser.isSet(tileActivationStableOption) ||
       parser.isSet(menuOrderOption) || parser.isSet(toolbarOrderOption) ||
       parser.isSet(dragDetachOption) || parser.isSet(tabbedFillOption) ||
       parser.isSet(globalStatusBarOption) ||
       parser.isSet(userVisibleProgressOption) || parser.isSet(newViewOption) ||
       parser.isSet(windowLifetimeOption) ||
       parser.isSet(slantedReferenceOption))) {
    qCritical() << "--smoke-test-workspace-churn must run without other "
                   "action smoke options";
    return 18;
  }

  if (parser.isSet(documentLifecycleOption) &&
      (parser.isSet(workspaceChurnOption) ||
       parser.isSet(detachReattachOption) ||
       parser.isSet(closeToEmptyTabOption) ||
       parser.isSet(expectedTabBarOption) ||
       parser.isSet(mdiArrangementOption) ||
       parser.isSet(tileActivationStableOption) ||
       parser.isSet(menuOrderOption) || parser.isSet(toolbarOrderOption) ||
       parser.isSet(dragDetachOption) || parser.isSet(tabbedFillOption) ||
       parser.isSet(globalStatusBarOption) ||
       parser.isSet(userVisibleProgressOption) || parser.isSet(newViewOption) ||
       parser.isSet(windowLifetimeOption) ||
       parser.isSet(slantedReferenceOption))) {
    qCritical() << "--smoke-test-document-lifecycle must run without other "
                   "action smoke options";
    return 19;
  }

  // Smoke tests need an explicit shutdown turn so queued widget destruction and
  // QtConcurrent work can be drained before sanitizers inspect process state.
  if (parser.isSet(smokeTestOption) || parser.isSet(closeToEmptyTabOption) ||
      parser.isSet(windowLifetimeOption) ||
      parser.isSet(documentLifecycleOption))
    app.setQuitOnLastWindowClosed(false);

  if (parser.isSet(timeReportOption))
    colorscreen::time_report = true;

  // Set icon search paths and theme for packaged Windows/macOS applications.
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
  QStringList paths = QIcon::themeSearchPaths();
  const QString appDir = QCoreApplication::applicationDirPath();
  paths.prepend(appDir + "/../share/icons");
  paths.prepend(appDir + "/share/icons");
  QIcon::setThemeSearchPaths(paths);
  QIcon::setThemeName("Adwaita");
#endif

  QPalette darkPalette;
  darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
  darkPalette.setColor(QPalette::WindowText, Qt::white);
  darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
  darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
  darkPalette.setColor(QPalette::ToolTipBase, QColor(53, 53, 53));
  darkPalette.setColor(QPalette::ToolTipText, Qt::white);
  darkPalette.setColor(QPalette::Text, Qt::white);
  darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
  darkPalette.setColor(QPalette::ButtonText, Qt::white);
  darkPalette.setColor(QPalette::BrightText, Qt::red);
  darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
  darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
  darkPalette.setColor(QPalette::HighlightedText, Qt::black);
  darkPalette.setColor(QPalette::Mid, QColor(45, 45, 45));
  darkPalette.setColor(QPalette::Dark, QColor(35, 35, 35));
  darkPalette.setColor(QPalette::Light, QColor(65, 65, 65));
  darkPalette.setColor(QPalette::Disabled, QPalette::Text,
                       QColor(127, 127, 127));
  darkPalette.setColor(QPalette::Disabled, QPalette::WindowText,
                       QColor(127, 127, 127));
  darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText,
                       QColor(127, 127, 127));
  darkPalette.setColor(QPalette::Disabled, QPalette::Highlight,
                       QColor(80, 80, 80));
  darkPalette.setColor(QPalette::Disabled, QPalette::HighlightedText,
                       QColor(127, 127, 127));
  app.setPalette(darkPalette);

  // Smoke tests must be deterministic and must not consume a developer's
  // pending recovery session when run locally.
  const bool restoredSession =
      !parser.isSet(smokeTestOption) && app.restoreRecoverySession();
  QStringList images = parser.positionalArguments();
  if (parser.isSet(smokeTestOption) && parser.isSet(newViewOption) &&
      images.size() >= 2 &&
      QFileInfo(images.at(0)).absoluteFilePath() ==
          QFileInfo(images.at(1)).absoluteFilePath()) {
    const QString monochromeFixture = QFileInfo(images.front()).absolutePath() +
                                      QStringLiteral("/checkerboard.tif");
    if (QFileInfo::exists(monochromeFixture)) {
      qInfo() << "New View render smoke replacing duplicate RGB fixture with"
              << monochromeFixture;
      images[1] = monochromeFixture;
    }
  }
  if (!images.isEmpty())
    app.openFiles(images, nullptr, parser.isSet(smokeTestOption));
  else if (!restoredSession && app.documentWindows().isEmpty())
    app.createDocumentWindow();

  if (parser.isSet(expectedWindowsOption)) {
    bool converted = false;
    const int expected = parser.value(expectedWindowsOption).toInt(&converted);
    QTimer::singleShot(0, &app, [&app, expected, converted]() {
      const int actual = app.documentWindows().size();
      if (!converted || expected < 0 || actual != expected) {
        qCritical() << "Smoke test expected" << expected
                    << "document windows but created" << actual;
        app.exit(2);
      }
    });
  }

  if (parser.isSet(expectedTabsOption)) {
    bool converted = false;
    const int expected = parser.value(expectedTabsOption).toInt(&converted);
    QTimer::singleShot(0, &app, [&app, expected, converted]() {
      const int actual = app.tabCount();
      if (!converted || expected < 0 || actual != expected) {
        qCritical() << "Smoke test expected" << expected
                    << "document tabs but created" << actual;
        app.exit(3);
      }
    });
  }

  if (parser.isSet(detachReattachOption)) {
    QTimer::singleShot(0, &app, [&app]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      MainWindow *document = workspace ? workspace->currentDocument() : nullptr;
      const int documentsBefore = app.documentWindows().size();
      const int tabsBefore = app.tabCount();
      if (!document || tabsBefore < 1) {
        qCritical() << "Smoke test has no active tab to detach";
        app.exit(4);
        return;
      }
      app.detachDocument(document);
      if (app.documentWindows().size() != documentsBefore ||
          app.tabCount() != tabsBefore - 1 || document->parentWidget()) {
        qCritical() << "Smoke test detach did not preserve the live document";
        app.exit(4);
        return;
      }
      app.attachDocument(document);
      if (app.documentWindows().size() != documentsBefore ||
          app.tabCount() != tabsBefore ||
          !workspace->containsDocument(document)) {
        qCritical() << "Smoke test reattach did not restore the same document";
        app.exit(4);
      }
    });
  }

  if (parser.isSet(closeToEmptyTabOption)) {
    QTimer::singleShot(50, &app, [&app]() {
      const QList<MainWindow *> documents = app.documentWindows();
      for (MainWindow *document : documents)
        document->close();
      QTimer::singleShot(100, &app, [&app]() {
        WorkspaceWindow *workspace = app.workspaceWindow();
        if (!app.documentWindows().isEmpty() || !app.viewWindows().isEmpty() ||
            app.tabCount() != 0 || (workspace && workspace->isVisible())) {
          qCritical() << "Smoke test left an empty application workspace";
          app.exit(5);
          return;
        }
        app.exit(0);
      });
    });
  }

  if (parser.isSet(expectedTabBarOption)) {
    const QString expected = parser.value(expectedTabBarOption).trimmed().toLower();
    QTimer::singleShot(100, &app, [&app, expected]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      const bool valid = expected == QStringLiteral("visible") ||
                         expected == QStringLiteral("hidden");
      const bool wantedVisible = expected == QStringLiteral("visible");
      const bool actualVisible = workspace && workspace->isTabBarVisible();
      if (!valid || actualVisible != wantedVisible) {
        qCritical() << "Smoke test expected document tab bar" << expected
                    << "but visibility was" << actualVisible;
        app.exit(6);
      }
    });
  }

  if (parser.isSet(mdiArrangementOption)) {
    QTimer::singleShot(100, &app, [&app]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      if (!workspace || workspace->tabCount() < 2) {
        qCritical() << "MDI arrangement smoke test requires two documents";
        app.exit(7);
        return;
      }
      workspace->tileDocuments();
      auto *mdiArea = workspace->findChild<QMdiArea *>(
          QStringLiteral("documentMdiArea"));
      const QList<QMdiSubWindow *> tiledWindows =
          mdiArea ? mdiArea->subWindowList() : QList<QMdiSubWindow *>();
      if (workspace->isTabbedView() || tiledWindows.size() < 2 ||
          !tiledWindows[0]->geometry().isValid() ||
          !tiledWindows[1]->geometry().isValid() ||
          !tiledWindows[0]->geometry().intersected(
              tiledWindows[1]->geometry()).isEmpty()) {
        qCritical() << "Tile Documents did not create distinct non-overlapping"
                       " subwindows";
        app.exit(7);
        return;
      }
      workspace->cascadeDocuments();
      const QList<QMdiSubWindow *> cascadedWindows =
          mdiArea ? mdiArea->subWindowList() : QList<QMdiSubWindow *>();
      if (workspace->isTabbedView() || cascadedWindows.size() < 2 ||
          cascadedWindows[0]->pos() == cascadedWindows[1]->pos()) {
        qCritical() << "Cascade Documents did not create offset subwindows";
        app.exit(7);
        return;
      }
      workspace->showTabbedDocuments();
      if (!workspace->isTabbedView()) {
        qCritical() << "Tabbed Documents did not restore tabbed mode";
        app.exit(7);
      }
    });
  }

  if (parser.isSet(tileActivationStableOption)) {
    QTimer::singleShot(150, &app, [&app]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      auto *mdiArea = workspace
                          ? workspace->findChild<QMdiArea *>(
                                QStringLiteral("documentMdiArea"))
                          : nullptr;
      if (!workspace || !mdiArea || workspace->tabCount() < 2) {
        qCritical() << "Tile activation stability test requires two documents";
        app.exit(11);
        return;
      }
      workspace->tileDocuments();
      const QList<QMdiSubWindow *> windows =
          mdiArea->subWindowList(QMdiArea::CreationOrder);
      if (windows.size() < 2) {
        app.exit(11);
        return;
      }
      QMdiSubWindow *first = windows[0];
      QMdiSubWindow *second = windows[1];
      const QPoint firstPosition = first->pos();
      const QPoint secondPosition = second->pos();
      QMdiSubWindow *target =
          mdiArea->activeSubWindow() == first ? second : first;
      mdiArea->setActiveSubWindow(target);
      if (mdiArea->activeSubWindow() != target ||
          first->pos() != firstPosition || second->pos() != secondPosition) {
        qCritical() << "Activating a tiled document moved or swapped tiles";
        app.exit(11);
        return;
      }

      // Do not leave later smoke checks in a tiled workspace.
      workspace->showTabbedDocuments();
    });
  }

  if (parser.isSet(menuOrderOption)) {
    QTimer::singleShot(250, &app, [&app]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      const QList<QAction *> actions =
          workspace && workspace->menuBar() ? workspace->menuBar()->actions()
                                            : QList<QAction *>();
      QStringList menus;
      for (QAction *action : actions)
        menus.append(action ? action->text().remove(QLatin1Char('&'))
                            : QString());
      const int registration = menus.indexOf(QStringLiteral("Registration"));
      const int window = menus.indexOf(QStringLiteral("Window"));
      const int help = menus.indexOf(QStringLiteral("Help"));
      if (registration < 0 || window != registration + 1 ||
          help != window + 1 || help != menus.size() - 1) {
        qCritical() << "Unexpected top-level menu order:" << menus;
        app.exit(12);
      }
    });
  }

  if (parser.isSet(toolbarOrderOption)) {
    QTimer::singleShot(100, &app, [&app]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      auto *mdiArea = workspace
                          ? workspace->findChild<QMdiArea *>(
                                QStringLiteral("documentMdiArea"))
                          : nullptr;
      auto *tabBar = mdiArea
                         ? mdiArea->findChild<QTabBar *>(
                               QString(), Qt::FindDirectChildrenOnly)
                         : nullptr;
      auto *toolbar = workspace
                          ? workspace->findChild<QToolBar *>(
                                QStringLiteral("MainToolbar"),
                                Qt::FindDirectChildrenOnly)
                          : nullptr;
      if (!tabBar || !toolbar ||
          toolbar->mapToGlobal(toolbar->rect().bottomLeft()).y() >=
              tabBar->mapToGlobal(tabBar->rect().topLeft()).y()) {
        qCritical() << "Document tabs are not below the shared toolbar";
        app.exit(8);
      }
    });
  }

  if (parser.isSet(dragDetachOption)) {
    QTimer::singleShot(250, &app, [&app]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      auto *mdiArea = workspace
                          ? workspace->findChild<QMdiArea *>(
                                QStringLiteral("documentMdiArea"))
                          : nullptr;
      auto *tabBar = mdiArea
                         ? mdiArea->findChild<QTabBar *>(
                               QString(), Qt::FindDirectChildrenOnly)
                         : nullptr;
      MainWindow *document = workspace ? workspace->currentDocument() : nullptr;
      const int tabsBefore = app.tabCount();
      if (!tabBar || !document || tabsBefore < 2 || !tabBar->isVisible()) {
        qCritical() << "Tab drag smoke test requires two visible document tabs";
        app.exit(9);
        return;
      }

      const int index = tabBar->currentIndex();
      const QPoint localStart = tabBar->tabRect(index).center();
      const QPoint globalStart = tabBar->mapToGlobal(localStart);
      const QPoint localOutside(-80, tabBar->height() + 80);
      const QPoint globalOutside = tabBar->mapToGlobal(localOutside);

      QMouseEvent press(QEvent::MouseButtonPress, QPointF(localStart),
                        QPointF(globalStart), Qt::LeftButton, Qt::LeftButton,
                        Qt::NoModifier);
      QCoreApplication::sendEvent(tabBar, &press);
      QMouseEvent move(QEvent::MouseMove, QPointF(localOutside),
                       QPointF(globalOutside), Qt::NoButton, Qt::LeftButton,
                       Qt::NoModifier);
      QCoreApplication::sendEvent(tabBar, &move);
      QMouseEvent release(QEvent::MouseButtonRelease, QPointF(localOutside),
                          QPointF(globalOutside), Qt::LeftButton,
                          Qt::NoButton, Qt::NoModifier);
      QCoreApplication::sendEvent(tabBar, &release);

      QPointer<MainWindow> guardedDocument(document);
      QTimer::singleShot(100, &app, [&app, workspace, guardedDocument,
                                     tabsBefore]() {
        if (!guardedDocument || app.tabCount() != tabsBefore - 1 ||
            guardedDocument->parentWidget()) {
          qCritical() << "Dragging a document tab did not detach it";
          app.exit(9);
          return;
        }
        app.attachDocument(guardedDocument);
        if (app.tabCount() != tabsBefore ||
            !workspace->containsDocument(guardedDocument)) {
          qCritical() << "Dragged document did not reattach intact";
          app.exit(9);
        }
      });
    });
  }

  if (parser.isSet(tabbedFillOption)) {
    QTimer::singleShot(200, &app, [&app]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      if (!workspace || workspace->tabCount() < 2) {
        qCritical() << "Tabbed fill smoke test requires two documents";
        app.exit(10);
        return;
      }
      workspace->showTabbedDocuments();
      QTimer::singleShot(0, &app, [&app, workspace]() {
        auto *mdiArea = workspace->findChild<QMdiArea *>(
            QStringLiteral("documentMdiArea"));
        QMdiSubWindow *active = mdiArea ? mdiArea->activeSubWindow() : nullptr;
        if (!mdiArea || !active || !workspace->isTabbedView()) {
          qCritical() << "Tabbed fill smoke test has no active tabbed subwindow";
          app.exit(10);
          return;
        }

        const QSize viewportSize = mdiArea->viewport()->size();
        const QSize documentSize = active->size();
        if (!(active->windowState() & Qt::WindowMaximized) ||
            documentSize.width() * 10 < viewportSize.width() * 9 ||
            documentSize.height() * 10 < viewportSize.height() * 9) {
          qCritical() << "Active tabbed document does not fill MDI viewport"
                      << "document" << documentSize << "viewport"
                      << viewportSize << "state" << active->windowState();
          app.exit(10);
        }
      });
    });
  }

  if (parser.isSet(globalStatusBarOption)) {
    QTimer::singleShot(200, &app, [&app]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      MainWindow *document = workspace ? workspace->currentDocument() : nullptr;
      QStatusBar *workspaceStatus = workspace ? workspace->statusBar() : nullptr;
      QWidget *progress = document ? document->workspaceStatusWidget() : nullptr;
      if (!workspaceStatus || !document || !progress ||
          !workspaceStatus->isAncestorOf(progress) ||
          document->statusBar() != workspaceStatus ||
          document->standaloneStatusBar()->isVisible()) {
        qCritical() << "Active document is not using the shared window status bar";
        app.exit(11);
        return;
      }

      MainWindow *inactiveDocument = nullptr;
      for (MainWindow *candidate : app.documentWindows()) {
        if (!candidate || !workspace->containsDocument(candidate))
          continue;
        if (candidate->statusBar() != workspaceStatus ||
            candidate->standaloneStatusBar()->isVisible() ||
            !workspaceStatus->isAncestorOf(candidate->workspaceStatusWidget())) {
          qCritical() << "Attached document has a private status bar";
          app.exit(11);
          return;
        }
        if (candidate != document && !inactiveDocument)
          inactiveDocument = candidate;
      }
      for (ImageViewWindow *view : app.viewWindows()) {
        if (view && workspace->containsView(view) &&
            (view->statusBar() != workspaceStatus ||
             view->standaloneStatusBar()->isVisible())) {
          qCritical() << "Attached secondary view has a private status bar";
          app.exit(11);
          return;
        }
      }

      const QString marker = QStringLiteral("workspace-status-smoke");
      document->statusBar()->showMessage(marker);
      QCoreApplication::processEvents();
      if (workspaceStatus->currentMessage() != marker) {
        qCritical() << "Document status message did not reach shared status bar";
        app.exit(11);
        return;
      }

      if (inactiveDocument) {
        const QString inactiveMarker =
            QStringLiteral("workspace-status-inactive-tab-smoke");
        inactiveDocument->statusBar()->showMessage(inactiveMarker);
        QCoreApplication::processEvents();
        if (workspaceStatus->currentMessage() != inactiveMarker) {
          qCritical() << "Inactive tab did not share the window status bar";
          app.exit(11);
          return;
        }
      }
      workspaceStatus->clearMessage();
    });
  }

  if (parser.isSet(userVisibleProgressOption)) {
    auto startProgressSmoke = std::make_shared<std::function<void(int)>>();
    const std::weak_ptr<std::function<void(int)>> weakStartProgressSmoke =
        startProgressSmoke;
    *startProgressSmoke =
        [&app, weakStartProgressSmoke](int attemptsLeft) {
      WorkspaceWindow *workspace = app.workspaceWindow();
      const QList<MainWindow *> documents = app.documentWindows();
      bool loaded = documents.size() >= 2;
      for (MainWindow *document : documents) {
        if (document && !document->currentImageFile().isEmpty() &&
            !document->sharedImageData()) {
          loaded = false;
          break;
        }
      }
      if (!workspace || !loaded) {
        if (attemptsLeft > 0) {
          if (auto retry = weakStartProgressSmoke.lock()) {
            QTimer::singleShot(100, &app, [retry, attemptsLeft]() {
              (*retry)(attemptsLeft - 1);
            });
            return;
          }
        }
        qCritical()
            << "User-visible progress smoke test requires two ready documents";
        app.exit(13);
        return;
      }

      MainWindow *cancelDocument = documents[0];
      MainWindow *stopDocument = documents[1];
      auto cancelProgress = std::make_shared<colorscreen::progress_info>();
      cancelProgress->set_task("cancel smoke task", 100);
      cancelProgress->set_progress(25);
      auto stopProgress = std::make_shared<colorscreen::progress_info>();
      stopProgress->set_task("stop smoke task", 100);
      stopProgress->set_progress(50);

      cancelDocument->addUserVisibleProgress(
          cancelProgress, QStringLiteral("Visible Cancel Task"));
      stopDocument->addUserVisibleProgress(
          stopProgress, QStringLiteral("Visible Stop Task"),
          ProgressAction::Stop);
      QCoreApplication::processEvents();

      QWidget *cancelContainer =
          cancelDocument->workspaceUserVisibleStatusWidget();
      QWidget *stopContainer = stopDocument->workspaceUserVisibleStatusWidget();
      QStatusBar *workspaceStatus = workspace->statusBar();
      QWidget *taskStack = workspace->findChild<QWidget *>(
          QStringLiteral("WorkspaceUserVisibleProgressStack"));
      if (!cancelContainer || !stopContainer || !workspaceStatus || !taskStack ||
          !taskStack->isAncestorOf(cancelContainer) ||
          !taskStack->isAncestorOf(stopContainer) ||
          workspaceStatus->isAncestorOf(cancelContainer) ||
          workspaceStatus->isAncestorOf(stopContainer) ||
          cancelContainer->isHidden() || stopContainer->isHidden()) {
        qCritical() << "User-visible progress did not remain in the dedicated "
                       "task strip above the status bar";
        app.exit(13);
        return;
      }

      const int statusHeight = workspaceStatus->height();

      // Switching the active image must not hide long tasks belonging to the
      // other document.
      workspace->activateDocument(cancelDocument);
      QCoreApplication::processEvents();
      if (!taskStack->isAncestorOf(cancelContainer) ||
          !taskStack->isAncestorOf(stopContainer) ||
          workspaceStatus->height() != statusHeight ||
          cancelContainer->isHidden() || stopContainer->isHidden()) {
        qCritical() << "User-visible progress disappeared after tab switch or "
                       "changed the one-line status bar height";
        app.exit(13);
        return;
      }

      // Transient work belongs to the workspace, not the selected tab. Start
      // work in the second document while the first remains active and require
      // the shared status line to present it after the normal display delay.
      auto transientProgress = std::make_shared<colorscreen::progress_info>();
      transientProgress->set_task("inactive document transient smoke task", 100);
      transientProgress->set_progress(10);
      stopDocument->addProgress(transientProgress);
      QThread::msleep(350);
      QCoreApplication::processEvents();
      QWidget *workspaceProgress = workspace->findChild<QWidget *>(
          QStringLiteral("WorkspaceProgressArea"));
      if (!workspaceProgress || workspaceProgress->isHidden() ||
          workspace->currentDocument() != cancelDocument ||
          workspace->displayedProgressDocument() != stopDocument ||
          !workspaceProgress->isAncestorOf(stopDocument->workspaceStatusWidget()) ||
          workspaceStatus->height() != statusHeight ||
          !taskStack->isAncestorOf(cancelContainer) ||
          !taskStack->isAncestorOf(stopContainer)) {
        qCritical() << "Inactive document transient progress was not presented globally";
        app.exit(13);
        return;
      }
      stopDocument->removeProgress(transientProgress);
      QCoreApplication::processEvents();
      if (workspace->currentDocument() != cancelDocument ||
          workspaceStatus->height() != statusHeight) {
        qCritical() << "Transient progress exit changed tab or status-bar height";
        app.exit(13);
        return;
      }

      const QList<QWidget *> cancelRows = cancelContainer->findChildren<QWidget *>(
          QStringLiteral("UserVisibleProgressRow"), Qt::FindDirectChildrenOnly);
      const QList<QWidget *> stopRows = stopContainer->findChildren<QWidget *>(
          QStringLiteral("UserVisibleProgressRow"), Qt::FindDirectChildrenOnly);
      if (cancelRows.size() != 1 || stopRows.size() != 1) {
        qCritical() << "Expected one dedicated row per user-visible task";
        app.exit(13);
        return;
      }

      QPushButton *cancelButton = cancelRows.front()->findChild<QPushButton *>();
      QPushButton *stopButton = stopRows.front()->findChild<QPushButton *>();
      if (!cancelButton || !stopButton || cancelButton->text() != "Cancel" ||
          stopButton->text() != "Stop" ||
          cancelButton->property("progressAction").toString() !=
              QStringLiteral("cancel") ||
          stopButton->property("progressAction").toString() !=
              QStringLiteral("stop")) {
        qCritical() << "Dedicated progress rows have incorrect actions";
        app.exit(13);
        return;
      }

      // Reproduce the real Stop path with the task owner as the current tab.
      // Removing the focused task row must not make QMdiArea fall back to the
      // first document.
      workspace->activateDocument(stopDocument);
      QCoreApplication::processEvents();
      if (workspace->currentDocument() != stopDocument) {
        qCritical() << "Could not activate Stop task owner before termination";
        app.exit(13);
        return;
      }

      // Reproduce a mouse Stop click. Dedicated task controls must not accept
      // mouse focus, otherwise focusing a workspace-global dock can make
      // QMdiArea select another child before the click handler even runs.
      if (stopButton->focusPolicy() != Qt::TabFocus) {
        qCritical() << "Dedicated Stop button unexpectedly accepts mouse focus";
        app.exit(13);
        return;
      }
      stopDocument->primaryImageWidget()->setFocus(Qt::OtherFocusReason);
      QCoreApplication::processEvents();
      if (workspace->currentDocument() != stopDocument) {
        qCritical() << "Focusing Stop task image changed the active document";
        app.exit(13);
        return;
      }
      stopButton->click();
      QCoreApplication::processEvents();
      if (!stopProgress->pool_cancel()) {
        qCritical() << "Stop progress action did not request termination";
        app.exit(13);
        return;
      }
      if (workspace->currentDocument() != stopDocument) {
        qCritical() << "Pressing Stop changed the active document";
        app.exit(13);
        return;
      }
      stopDocument->removeProgress(stopProgress);
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QCoreApplication::processEvents();
      if (workspace->currentDocument() != stopDocument) {
        qCritical() << "Stopping a task changed the active document";
        app.exit(13);
        return;
      }

      cancelButton->click();
      if (!cancelProgress->pool_cancel()) {
        qCritical() << "Cancel progress action did not request termination";
        app.exit(13);
        return;
      }
      cancelDocument->removeProgress(cancelProgress);
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QCoreApplication::processEvents();
      if (workspace->currentDocument() != stopDocument) {
        qCritical() << "Removing another document's task changed the active document";
        app.exit(13);
        return;
      }
        };
    QTimer::singleShot(250, &app, [startProgressSmoke]() {
      (*startProgressSmoke)(80);
    });
  }

  // The combined CI command runs New View and slanted-reference checks in one
  // process. Track completion explicitly instead of relying on wall-clock
  // delays that processEvents() can overtake on slow or instrumented builds.
  const auto newViewSmokeDone =
      std::make_shared<bool>(!parser.isSet(newViewOption));
  const auto slantedReferenceSmokeDone =
      std::make_shared<bool>(!parser.isSet(slantedReferenceOption));
  const auto workspaceChurnSmokeDone =
      std::make_shared<bool>(!parser.isSet(workspaceChurnOption));
  const auto documentLifecycleSmokeDone =
      std::make_shared<bool>(!parser.isSet(documentLifecycleOption));
  const bool completionManagedSmoke =
      parser.isSet(smokeTestOption) &&
      (parser.isSet(newViewOption) || parser.isSet(slantedReferenceOption) ||
       parser.isSet(workspaceChurnOption) ||
       parser.isSet(documentLifecycleOption)) &&
      !parser.isSet(userVisibleProgressOption) &&
      !parser.isSet(windowLifetimeOption) &&
      !parser.isSet(closeToEmptyTabOption);
  const auto maybeFinishStructuredSmoke =
      std::make_shared<std::function<void()>>();
  *maybeFinishStructuredSmoke =
      [&app, newViewSmokeDone, slantedReferenceSmokeDone,
       workspaceChurnSmokeDone, documentLifecycleSmokeDone,
       completionManagedSmoke]() {
        if (completionManagedSmoke && *newViewSmokeDone &&
            *slantedReferenceSmokeDone && *workspaceChurnSmokeDone &&
            *documentLifecycleSmokeDone)
          QTimer::singleShot(0, &app, [&app]() { app.quit(); });
      };

  if (parser.isSet(newViewOption)) {
    auto startNewViewSmoke = std::make_shared<std::function<void(int)>>();
    const std::weak_ptr<std::function<void(int)>> weakStartNewViewSmoke =
        startNewViewSmoke;
    *startNewViewSmoke =
        [&app, newViewSmokeDone, maybeFinishStructuredSmoke,
         weakStartNewViewSmoke](int attemptsLeft) {
      const QList<MainWindow *> documents = app.documentWindows();
      MainWindow *source = nullptr;
      MainWindow *monochromeSource = nullptr;
      for (MainWindow *document : documents) {
        const auto scan = document ? document->sharedImageData() : nullptr;
        if (!scan)
          continue;
        if (scan->has_rgb() && !source)
          source = document;
        if (!scan->has_rgb() && !monochromeSource)
          monochromeSource = document;
      }
      if (!source || !monochromeSource) {
        if (attemptsLeft > 0) {
          if (auto retry = weakStartNewViewSmoke.lock()) {
            QTimer::singleShot(100, &app, [retry, attemptsLeft]() {
              (*retry)(attemptsLeft - 1);
            });
            return;
          }
        }
        qCritical() << "New View render-mode smoke timed out waiting for both "
                       "RGB and monochrome scans to finish loading";
        app.exit(14);
        return;
      }

      const auto sourceScan = source->sharedImageData();
      const auto monochromeScan = monochromeSource->sharedImageData();
      const auto sourceType = source->viewRenderTypeParameters().type;

      auto installRenderSmokeState = [](MainWindow *document, const auto &scan,
                                        bool correctionProfile) {
        ParameterState state = document->documentStateSnapshot();
        // Keep the smoke fixture physically consistent with the capture-aware
        // GUI mode filter. RGB Dufay scans carry a historical screen; the
        // monochrome checkerboard is an ordinary no-screen image used only for
        // modes that remain applicable without RGB screen detection.
        state.rparams.capture_type =
            scan->has_rgb()
                ? colorscreen::render_parameters::capture_transparency_with_screen
                : colorscreen::render_parameters::capture_plain_image;
        state.scrToImg.type = colorscreen::Dufay;
        state.scrToImg.center = {(colorscreen::coord_t)scan->width / 2,
                                 (colorscreen::coord_t)scan->height / 2};
        state.scrToImg.coordinate1 = {8, 0};
        state.scrToImg.coordinate2 = {0, 8};
        state.scrToImg.final_rotation = 12.5;
        state.scrToImg.final_mirror = true;
        if (correctionProfile) {
          // Near-identity but non-default profile: deliberately expose
          // all correction-profile modes in this regression smoke.
          state.rparams.profiled_red = {0.98, 0.01, 0.01};
          state.rparams.profiled_green = {0.01, 0.98, 0.01};
          state.rparams.profiled_blue = {0.01, 0.01, 0.98};
        }
        // Smoke-only setup must not dirty the document or add undo state.
        document->applyState(state);
        return state;
      };

      ParameterState coordinateState =
          installRenderSmokeState(source, sourceScan, true);
      ParameterState monochromeCoordinateState =
          installRenderSmokeState(monochromeSource, monochromeScan, false);
      if (!coordinateState.rparams.has_correction_profile()) {
        qCritical()
            << "RGB render-mode smoke failed to install correction profile";
        app.exit(14);
        return;
      }
      auto basePresentation = coordinateState.rparams;
      basePresentation.scan_rotation = 0;
      basePresentation.scan_mirror = false;
      basePresentation.scan_crop.set = false;
      CoordinateTransformer finalBase(sourceScan.get(), basePresentation,
                                      &coordinateState.scrToImg,
                                      colorscreen::render_final_coordinates);
      auto changedPresentation = basePresentation;
      changedPresentation.scan_rotation = 1;
      changedPresentation.scan_mirror = true;
      changedPresentation.scan_crop.set = true;
      changedPresentation.scan_crop.x = sourceScan->width / 4;
      changedPresentation.scan_crop.y = sourceScan->height / 4;
      changedPresentation.scan_crop.width = sourceScan->width / 2;
      changedPresentation.scan_crop.height = sourceScan->height / 2;
      CoordinateTransformer finalChanged(sourceScan.get(), changedPresentation,
                                         &coordinateState.scrToImg,
                                         colorscreen::render_final_coordinates);
      const auto baseRange = finalBase.getRenderCrop();
      const auto changedRange = finalChanged.getRenderCrop();
      const colorscreen::point_t probe = coordinateState.scrToImg.center;
      const auto baseProbe = finalBase.scanToTransformedCrop(probe);
      const auto changedProbe = finalChanged.scanToTransformedCrop(probe);
      if (finalBase.getTransformedCropSize() !=
              finalChanged.getTransformedCropSize() ||
          baseRange.x != changedRange.x || baseRange.y != changedRange.y ||
          baseRange.width != changedRange.width ||
          baseRange.height != changedRange.height ||
          qAbs(baseProbe.x - changedProbe.x) > 1e-9 ||
          qAbs(baseProbe.y - changedProbe.y) > 1e-9) {
        qCritical() << "Scan presentation leaked into final coordinates";
        app.exit(14);
        return;
      }

      // Mouse tools and overlays use CoordinateTransformer, while the rendered
      // scan uses QImage::transformed for quarter-turn/mirror presentation.
      // Compare a real marker pixel so the two paths cannot drift by one pixel
      // (the old edge-coordinate math mapped 270-degree rotation one pixel
      // below the rendered pixel at high zoom).
      {
        constexpr int probeWidth = 7;
        constexpr int probeHeight = 5;
        constexpr int probeX = 1;
        constexpr int probeY = 2;
        colorscreen::image_data probeScan;
        if (!probeScan.set_dimensions(probeWidth, probeHeight, true, false)) {
          qCritical() << "Could not allocate coordinate-transform probe image";
          app.exit(14);
          return;
        }

        for (int rotation = 0; rotation < 4; ++rotation) {
          for (bool mirror : {false, true}) {
            QImage markerImage(probeWidth, probeHeight, QImage::Format_RGB32);
            markerImage.fill(Qt::black);
            markerImage.setPixelColor(probeX, probeY, Qt::red);
            QTransform imageTransform;
            if (rotation)
              imageTransform.rotate(rotation * 90.0);
            if (mirror)
              imageTransform.scale(-1, 1);
            const QImage transformed = markerImage.transformed(imageTransform);

            QPoint expected(-1, -1);
            for (int y = 0; y < transformed.height(); ++y)
              for (int x = 0; x < transformed.width(); ++x)
                if (transformed.pixelColor(x, y) == QColor(Qt::red))
                  expected = QPoint(x, y);
            if (expected.x() < 0) {
              qCritical() << "QImage marker disappeared during coordinate smoke";
              app.exit(14);
              return;
            }

            colorscreen::render_parameters presentation;
            presentation.scan_rotation = rotation;
            presentation.scan_mirror = mirror;
            CoordinateTransformer transformer(
                &probeScan, presentation, nullptr,
                colorscreen::render_scan_coordinates);
            const colorscreen::point_t scanPoint = {probeX, probeY};
            const colorscreen::point_t mapped =
                transformer.scanToTransformedCrop(scanPoint);
            const colorscreen::point_t roundTrip =
                transformer.transformedToScanCrop(
                    {(colorscreen::coord_t)expected.x(),
                     (colorscreen::coord_t)expected.y()});
            if (qAbs(mapped.x - expected.x()) > 1e-9 ||
                qAbs(mapped.y - expected.y()) > 1e-9 ||
                qAbs(roundTrip.x - probeX) > 1e-9 ||
                qAbs(roundTrip.y - probeY) > 1e-9) {
              qCritical() << "Mouse/render pixel-center transform mismatch"
                          << "rotation" << rotation << "mirror" << mirror
                          << "mapped" << mapped.x << mapped.y
                          << "rendered" << expected;
              app.exit(14);
              return;
            }
          }
        }
      }

      auto renderModeAvailable = [](const ParameterState &state,
                                    const auto &scan,
                                    colorscreen::render_type_t type) {
        const auto &prop =
            colorscreen::render_type_properties[static_cast<int>(type)];
        if (prop.flags & colorscreen::render_type_property::HIDE_IN_GUI)
          return false;

        // Mirror MainWindow::updateModeMenu() and
        // ImageViewWindow::rebuildModeList() exactly. In particular, a
        // geometric Dufay mapping alone does not make screen reconstruction
        // modes valid for a capture explicitly classified as non-screen.
        const auto capture =
            scan ? state.rparams.get_capture_type(scan.get())
                 : colorscreen::render_parameters::capture_unknown;
        const bool hasScreenCapture =
            colorscreen::render_parameters::capture_has_screen_p(capture);
        const bool supportsScreenDetection =
            colorscreen::render_parameters::capture_supports_screen_detection_p(
                capture);
        if ((prop.flags &
             colorscreen::render_type_property::NEEDS_SCR_TO_IMG) &&
            (!hasScreenCapture ||
             !colorscreen::screen_geometry_configured_p(state.scrToImg)))
          return false;
        if ((prop.flags &
             colorscreen::render_type_property::USES_SCR_DETECT) &&
            (!supportsScreenDetection ||
             !colorscreen::screen_present_p(state.scrToImg.type)))
          return false;
        if ((prop.flags & colorscreen::render_type_property::NEEDS_RGB) &&
            (!scan || !scan->has_rgb()))
          return false;
        if ((prop.flags &
             colorscreen::render_type_property::NEEDS_CORRECTION_PROFILE) &&
            !state.rparams.has_correction_profile())
          return false;
        return true;
      };

      auto exerciseRenderApi = [&renderModeAvailable](
                                   const char *label, const auto &scan,
                                   const ParameterState &state) {
        std::vector<unsigned char> pixels(2 * 2 * 3);
        for (colorscreen::render_coordinate_space coordinates :
             {colorscreen::render_scan_coordinates,
              colorscreen::render_final_coordinates}) {
          for (int i = 0; i < colorscreen::render_type_max; ++i) {
            const auto type = static_cast<colorscreen::render_type_t>(i);
            if (!renderModeAvailable(state, scan, type))
              continue;
            const auto &prop = colorscreen::render_type_properties[i];
            const char *coordinateName =
                coordinates == colorscreen::render_scan_coordinates ? "scan"
                                                                    : "screen";
            qInfo() << "Render-mode API smoke" << label << coordinateName
                    << prop.pretty_name;

            colorscreen::render_type_parameters apiRender;
            apiRender.type = type;
            apiRender.color = scan->has_rgb();
            colorscreen::tile_parameters apiTile;
            apiTile.pixels = pixels.data();
            apiTile.rowstride = 2 * 3;
            apiTile.pixelbytes = 3;
            apiTile.width = 2;
            apiTile.height = 2;
            apiTile.pos =
                coordinates == colorscreen::render_scan_coordinates
                    ? colorscreen::point_t{(colorscreen::coord_t)scan->width /
                                                   2 -
                                               1,
                                           (colorscreen::coord_t)scan->height /
                                                   2 -
                                               1}
                    : colorscreen::point_t{0, 0};
            apiTile.step = 1;
            auto apiRparams = state.rparams;
            auto apiScrToImg = state.scrToImg;
            auto apiDetect = state.detect;
            if (!colorscreen::render_tile(*scan, apiScrToImg, apiDetect,
                                          apiRparams, apiRender, apiTile,
                                          coordinates, nullptr)) {
              qCritical() << "Render-mode API smoke failed" << label
                          << coordinateName << prop.pretty_name;
              return false;
            }
          }
        }
        return true;
      };

      if (!exerciseRenderApi("RGB", sourceScan, coordinateState) ||
          !exerciseRenderApi("monochrome", monochromeScan,
                             monochromeCoordinateState)) {
        app.exit(14);
        return;
      }
      const int previousTabs = app.tabCount();
      ImageViewWindow *view = app.createViewWindow(source);
      QPointer<MainWindow> guardedSource(source);
      QPointer<ImageViewWindow> guardedView(view);
      WorkspaceWindow *workspace = app.workspaceWindow();
      if (!view || view->sourceDocument() != source ||
          view->sharedImageData() != sourceScan ||
          app.documentWindows().size() != documents.size() ||
          !workspace || !workspace->containsView(view) ||
          app.tabCount() != previousTabs + 1 || view->isWindow()) {
        qCritical() << "New View was not added as a shared-image workspace tab";
        app.exit(14);
        return;
      }

      QWidget *inspector = view->workspaceInspectorWidget();
      QDockWidget *workspaceInspector = workspace->findChild<QDockWidget *>(
          QStringLiteral("DocumentControlsDock"));
      if (!inspector || inspector != source->workspaceInspectorWidget() ||
          !inspector->findChild<QWidget *>(QStringLiteral("ConfigTabs")) ||
          !workspaceInspector || workspaceInspector->isHidden() ||
          !workspaceInspector->isAncestorOf(inspector) ||
          source->inspectorImageWidget() != view->imageWidget()) {
        qCritical() << "New View does not present the owning document's full "
                       "inspector and Navigation/panel controls";
        app.exit(14);
        return;
      }

      // Ordinary New Views must expose the same document-editing chrome as the
      // primary presentation while keeping render/color/coordinate controls
      // view-local. In particular Edit and Registration may not disappear.
      QStringList viewMenus;
      for (QAction *action : view->menuBar()->actions())
        viewMenus << QString(action->text()).remove('&');
      const QStringList expectedViewMenus = {QStringLiteral("File"),
                                             QStringLiteral("Edit"),
                                             QStringLiteral("View"),
                                             QStringLiteral("Registration"),
                                             QStringLiteral("Window"),
                                             QStringLiteral("Help")};
      if (viewMenus != expectedViewMenus) {
        qCritical() << "New View menu chrome differs from an ordinary document"
                    << viewMenus;
        app.exit(14);
        return;
      }

      QMenu *viewFileMenu = nullptr;
      QMenu *viewRegistrationMenu = nullptr;
      for (QAction *action : view->menuBar()->actions()) {
        const QString menuName = QString(action->text()).remove('&');
        if (menuName == QStringLiteral("File"))
          viewFileMenu = action->menu();
        else if (menuName == QStringLiteral("Registration"))
          viewRegistrationMenu = action->menu();
      }

      QAction *detectedPatchCentersAction = nullptr;
      if (viewRegistrationMenu) {
        for (QAction *action : viewRegistrationMenu->actions())
          if (action && action->objectName() ==
                            QStringLiteral("DetectedPatchCentersAction")) {
            detectedPatchCentersAction = action;
            break;
          }
      }
      if (!detectedPatchCentersAction ||
          detectedPatchCentersAction->isEnabled()) {
        qCritical() << "New View Registration menu is missing the disabled "
                       "auto-detected patch-center diagnostic";
        app.exit(14);
        return;
      }
      QStringList viewFileActions;
      if (viewFileMenu) {
        for (QAction *action : viewFileMenu->actions())
          if (action && !action->isSeparator())
            viewFileActions << QString(action->text()).remove('&');
      }
      if (!viewFileMenu ||
          !viewFileActions.contains(QStringLiteral("Save Parameters")) ||
          !viewFileActions.contains(QStringLiteral("Exit")) ||
          !viewFileActions.contains(QStringLiteral("Close View")) ||
          viewFileActions.contains(QStringLiteral("Close Window"))) {
        qCritical() << "New View File menu does not mirror document commands"
                    << viewFileActions;
        app.exit(14);
        return;
      }

      QCheckBox *viewColorToggle = nullptr;
      bool hasSelectTool = false;
      bool hasAddPointTool = false;
      if (QToolBar *toolbar = view->workspaceToolBar()) {
        for (QCheckBox *checkBox : toolbar->findChildren<QCheckBox *>())
          if (checkBox->text() == QObject::tr("Color"))
            viewColorToggle = checkBox;
        for (QAction *action : toolbar->actions()) {
          if (action->text() == QObject::tr("Select"))
            hasSelectTool = true;
          if (action->text() == QObject::tr("Add Point"))
            hasAddPointTool = true;
        }
      }
      if (!viewColorToggle ||
          (sourceScan->has_rgb() && viewColorToggle->isHidden()) ||
          !hasSelectTool || !hasAddPointTool) {
        qCritical() << "New View toolbar is missing ordinary document controls";
        app.exit(14);
        return;
      }

      // One-shot canvas tools belong to the document operation, not to the
      // canvas that happened to be active when they were armed. Verify that
      // distance and area tools migrate to another ordinary view of the same
      // loaded image and leave the old view inert.
      workspace->activateDocument(source);
      QCoreApplication::processEvents();
      if (!QMetaObject::invokeMethod(source, "onMeasureRequested",
                                     Qt::DirectConnection) ||
          source->primaryImageWidget()->interactionMode() !=
              ImageWidget::MeasureMode) {
        qCritical() << "Could not arm Measure in the primary view";
        app.exit(14);
        return;
      }
      workspace->activateView(view);
      QCoreApplication::processEvents();
      if (source->inspectorImageWidget() != view->imageWidget() ||
          view->imageWidget()->interactionMode() != ImageWidget::MeasureMode ||
          source->primaryImageWidget()->interactionMode() !=
              ImageWidget::PanMode) {
        qCritical() << "Measure tool did not follow the active ordinary view";
        app.exit(14);
        return;
      }
      view->imageWidget()->setInteractionMode(ImageWidget::PanMode);

      workspace->activateDocument(source);
      QCoreApplication::processEvents();
      if (!QMetaObject::invokeMethod(source, "onCropRequested",
                                     Qt::DirectConnection) ||
          source->primaryImageWidget()->interactionMode() !=
              ImageWidget::CropMode) {
        qCritical() << "Could not arm Crop in the primary view";
        app.exit(14);
        return;
      }
      workspace->activateView(view);
      QCoreApplication::processEvents();
      if (source->inspectorImageWidget() != view->imageWidget() ||
          view->imageWidget()->interactionMode() != ImageWidget::CropMode ||
          source->primaryImageWidget()->interactionMode() !=
              ImageWidget::PanMode) {
        qCritical() << "Area-selection tool did not follow the active ordinary view";
        app.exit(14);
        return;
      }
      if (!QMetaObject::invokeMethod(source, "onCropRequested",
                                     Qt::DirectConnection) ||
          view->imageWidget()->interactionMode() != ImageWidget::PanMode) {
        qCritical() << "Could not cancel transferred Crop tool";
        app.exit(14);
        return;
      }

      app.detachView(view);
      QCoreApplication::processEvents();
      if (!guardedSource || !guardedView) {
        qCritical() << "New View disappeared while detaching";
        app.exit(14);
        return;
      }
      guardedView->activateWindow();
      QCoreApplication::processEvents();
      if (!guardedSource || !guardedView) {
        qCritical() << "New View disappeared while activating detached view";
        app.exit(14);
        return;
      }
      QDockWidget *detachedInspector = guardedView->findChild<QDockWidget *>(
          QStringLiteral("SecondaryDocumentControlsDock"));
      if (!view->isWindow() || !detachedInspector ||
          detachedInspector->isHidden() ||
          !detachedInspector->isAncestorOf(inspector) ||
          source->inspectorImageWidget() != view->imageWidget()) {
        qCritical() << "Detached New View did not keep the full document panels";
        app.exit(14);
        return;
      }

      app.attachView(view);
      QCoreApplication::processEvents();
      if (!guardedSource || !guardedView) {
        qCritical() << "New View disappeared while reattaching";
        app.exit(14);
        return;
      }
      if (!workspace->containsView(guardedView) ||
          !workspaceInspector->isAncestorOf(inspector) ||
          source->inspectorImageWidget() != view->imageWidget()) {
        qCritical() << "Reattached New View did not restore shared panels";
        app.exit(14);
        return;
      }

      bool hasIconOnlyZoom = false;
      bool hasIconOnlyRotation = false;
      if (QToolBar *toolbar = view->workspaceToolBar()) {
        for (QAction *action : toolbar->actions()) {
          const QString actionText =
              QString(action->text()).remove(QLatin1Char('&'));
          if (actionText == QObject::tr("Zoom In") && !action->icon().isNull())
            hasIconOnlyZoom = true;
          if (actionText == QObject::tr("Rotate Right") && !action->icon().isNull())
            hasIconOnlyRotation = true;
        }
      }
      if (!hasIconOnlyZoom || !hasIconOnlyRotation) {
        qCritical() << "New View toolbar does not use the standard image-view icons";
        app.exit(14);
        return;
      }

      if (!source->primaryImageWidget() ||
          source->primaryImageWidget()->coordinateSpace() !=
              colorscreen::render_scan_coordinates ||
          !view->setCoordinateSpace(colorscreen::render_final_coordinates) ||
          view->coordinateSpace() != colorscreen::render_final_coordinates ||
          source->primaryImageWidget()->coordinateSpace() !=
              colorscreen::render_scan_coordinates) {
        qCritical() << "New View Scan/Screen coordinate selection is not independent";
        app.exit(14);
        return;
      }

      // Moving the shared inspector between primary, detached, and attached
      // presentations above can leave queued panel refreshes behind. Drain them
      // first, then install the deterministic renderer fixture at the point of
      // use so the GUI mode list and the API sweep exercise the same live state.
      QCoreApplication::processEvents();
      coordinateState = installRenderSmokeState(source, sourceScan, true);

      auto exerciseViewModes =
          [&renderModeAvailable](const char *label, ImageViewWindow *target,
                                 const ParameterState &state,
                                 const auto &scan) {
            for (colorscreen::render_coordinate_space coordinates :
                 {colorscreen::render_scan_coordinates,
                  colorscreen::render_final_coordinates}) {
              if (!target->setCoordinateSpace(coordinates) ||
                  target->coordinateSpace() != coordinates) {
                qCritical()
                    << "GUI render-mode smoke could not select coordinate space"
                    << label << (int)coordinates;
                return false;
              }
              for (int i = 0; i < colorscreen::render_type_max; ++i) {
                const auto type = static_cast<colorscreen::render_type_t>(i);
                if (!renderModeAvailable(state, scan, type))
                  continue;
                const auto &prop = colorscreen::render_type_properties[i];
                qInfo() << "GUI render-mode smoke" << label
                        << (coordinates == colorscreen::render_scan_coordinates
                                ? "scan"
                                : "screen")
                        << prop.pretty_name;
                if (!target->setRenderType(type) ||
                    target->renderType() != type) {
                  qCritical() << "GUI render-mode smoke could not select mode"
                              << label << prop.pretty_name;
                  return false;
                }
              }
            }
            return true;
          };

      if (!exerciseViewModes("RGB", view, coordinateState, sourceScan) ||
          source->viewRenderTypeParameters().type != sourceType) {
        qCritical()
            << "RGB New View render/coordinate modes are not independent";
        app.exit(14);
        return;
      }

      monochromeCoordinateState =
          installRenderSmokeState(monochromeSource, monochromeScan, false);
      ImageViewWindow *monochromeView = app.createViewWindow(monochromeSource);
      QPointer<ImageViewWindow> guardedMonochromeView(monochromeView);
      if (!monochromeView ||
          monochromeView->sourceDocument() != monochromeSource ||
          monochromeView->sharedImageData() != monochromeScan ||
          !exerciseViewModes("monochrome", monochromeView,
                             monochromeCoordinateState, monochromeScan) ||
          !app.closeView(monochromeView)) {
        qCritical() << "Monochrome New View render-mode smoke failed";
        app.exit(14);
        return;
      }
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QCoreApplication::processEvents();
      if (guardedMonochromeView ||
          app.documentWindows().size() != documents.size()) {
        qCritical()
            << "Monochrome render-mode smoke left a secondary view behind";
        app.exit(14);
        return;
      }

      colorscreen::render_type_t alternate = sourceType;
      bool changed = false;
      for (int i = 0; i < colorscreen::render_type_max; ++i) {
        const auto candidate = static_cast<colorscreen::render_type_t>(i);
        if (candidate != sourceType &&
            renderModeAvailable(coordinateState, sourceScan, candidate) &&
            view->setRenderType(candidate)) {
          alternate = candidate;
          changed = true;
          break;
        }
      }
      if (!changed || view->renderType() != alternate ||
          source->viewRenderTypeParameters().type != sourceType) {
        qCritical() << "New View render mode is not independent";
        app.exit(14);
        return;
      }
      // The primary presentation is a peer of New View. Closing it must keep
      // the logical document and shared image alive until this final view also
      // closes. This simultaneously covers destruction of one loaded document
      // while another independent image remains open.
      const int documentCount = documents.size();
      if (source->close() || !guardedSource || !guardedView ||
          app.isDocumentPresentationOpen(source) ||
          workspace->containsDocument(source) ||
          !workspace->containsView(view) || view->sourceDocument() != source ||
          view->sharedImageData() != sourceScan) {
        qCritical() << "Closing the primary view destroyed or detached its peers";
        app.exit(14);
        return;
      }

      if (!app.closeView(view)) {
        qCritical() << "Closing the final peer view was rejected unexpectedly";
        app.exit(14);
        return;
      }

      auto checkFinalPeerClose =
          std::make_shared<std::function<void(int)>>();
      const std::weak_ptr<std::function<void(int)>> weakCheckFinalPeerClose =
          checkFinalPeerClose;
      *checkFinalPeerClose =
          [&app, guardedSource, guardedView, documentCount, newViewSmokeDone,
           maybeFinishStructuredSmoke, weakCheckFinalPeerClose](int attemptsLeft) {
            if (guardedSource || guardedView ||
                app.documentWindows().size() != documentCount - 1) {
              if (attemptsLeft > 0) {
                if (auto retry = weakCheckFinalPeerClose.lock()) {
                  QTimer::singleShot(50, &app, [retry, attemptsLeft]() {
                    (*retry)(attemptsLeft - 1);
                  });
                  return;
                }
              }
              qCritical() << "Final peer view did not close its document owner";
              app.exit(14);
              return;
            }
            *newViewSmokeDone = true;
            (*maybeFinishStructuredSmoke)();
          };
      QTimer::singleShot(0, &app, [checkFinalPeerClose]() {
        (*checkFinalPeerClose)(40);
      });
    };
    QTimer::singleShot(0, &app, [startNewViewSmoke]() {
      (*startNewViewSmoke)(300);
    });
  }

  if (parser.isSet(workspaceChurnOption)) {
    startWorkspaceChurnSmoke(
        app, [workspaceChurnSmokeDone, maybeFinishStructuredSmoke]() {
          *workspaceChurnSmokeDone = true;
          (*maybeFinishStructuredSmoke)();
        });
  }

  if (parser.isSet(documentLifecycleOption)) {
    startDocumentLifecycleSmoke(
        app, [documentLifecycleSmokeDone, maybeFinishStructuredSmoke]() {
          *documentLifecycleSmokeDone = true;
          (*maybeFinishStructuredSmoke)();
        });
  }

  if (parser.isSet(windowLifetimeOption)) {
    auto startWindowLifetime =
        std::make_shared<std::function<void(int)>>();
    const std::weak_ptr<std::function<void(int)>> weakStartWindowLifetime =
        startWindowLifetime;
    *startWindowLifetime = [&app, weakStartWindowLifetime](int attemptsLeft) {
      const QList<MainWindow *> documents = app.documentWindows();
      WorkspaceWindow *workspace = app.workspaceWindow();
      if (documents.size() != 1 || !documents.front()->sharedImageData() ||
          !workspace || app.tabCount() != 1 || !workspace->isTabBarVisible()) {
        if (attemptsLeft > 0) {
          if (auto retry = weakStartWindowLifetime.lock()) {
            QTimer::singleShot(250, &app, [retry, attemptsLeft]() {
              (*retry)(attemptsLeft - 1);
            });
            return;
          }
        }
        qCritical() << "Window lifetime smoke test requires one standard tab";
        app.exit(16);
        return;
      }

      MainWindow *source = documents.front();
      app.detachDocument(source);
      QCoreApplication::processEvents();
      if (workspace->isVisible() || !source->isVisible() ||
          !source->isWindow() || app.tabCount() != 0) {
        qCritical() << "Detaching the sole image left a useless workspace shell";
        app.exit(16);
        return;
      }

      app.attachDocument(source);
      QCoreApplication::processEvents();
      if (!workspace->isVisible() || !workspace->containsDocument(source) ||
          app.tabCount() != 1 || !workspace->isTabBarVisible()) {
        qCritical() << "Reattaching the sole image did not restore a standard tab";
        app.exit(16);
        return;
      }

      ImageViewWindow *view = app.createViewWindow(source, true);
      if (!view || !view->isWindow() || !view->isVisible()) {
        qCritical() << "Could not create detached peer view";
        app.exit(16);
        return;
      }

      QPointer<MainWindow> guardedSource(source);
      QPointer<ImageViewWindow> guardedView(view);
      if (!workspace->close()) {
        qCritical() << "Workspace close was rejected with a detached peer";
        app.exit(16);
        return;
      }
      QCoreApplication::processEvents();
      if (workspace->isVisible() || !guardedSource || !guardedView ||
          !guardedView->isVisible() ||
          app.isDocumentPresentationOpen(guardedSource)) {
        qCritical() << "Closing the workspace also closed its detached peer";
        app.exit(16);
        return;
      }

      if (!app.closeView(guardedView)) {
        qCritical() << "Final detached peer did not accept close";
        app.exit(16);
        return;
      }

      auto checkFinalDetachedClose =
          std::make_shared<std::function<void(int)>>();
      const std::weak_ptr<std::function<void(int)>>
          weakCheckFinalDetachedClose = checkFinalDetachedClose;
      *checkFinalDetachedClose =
          [&app, workspace, guardedSource, guardedView,
           weakCheckFinalDetachedClose](int attemptsLeft) {
            // WA_DeleteOnClose is intentionally asynchronous.  In particular,
            // ASan can make the Windows event loop reach this check before Qt
            // has handled the queued widget destruction.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents();
            if (guardedSource || guardedView ||
                !app.documentWindows().isEmpty() ||
                !app.viewWindows().isEmpty() ||
                (workspace && workspace->isVisible())) {
              if (attemptsLeft > 0) {
                if (auto retry = weakCheckFinalDetachedClose.lock()) {
                  QTimer::singleShot(50, &app, [retry, attemptsLeft]() {
                    (*retry)(attemptsLeft - 1);
                  });
                  return;
                }
              }
              qCritical()
                  << "Last application window did not release its document";
              app.exit(16);
              return;
            }
            app.exit(0);
          };
      QTimer::singleShot(0, &app, [checkFinalDetachedClose]() {
        (*checkFinalDetachedClose)(40);
      });
    };
    QTimer::singleShot(300, &app, [startWindowLifetime]() {
      (*startWindowLifetime)(40);
    });
  }

  if (parser.isSet(slantedReferenceOption)) {
    auto startReferenceSmoke =
        std::make_shared<std::function<void(int)>>();
    const std::weak_ptr<std::function<void(int)>> weakStartReferenceSmoke =
        startReferenceSmoke;
    *startReferenceSmoke =
        [&app, newViewSmokeDone, slantedReferenceSmokeDone,
         maybeFinishStructuredSmoke, weakStartReferenceSmoke](int attemptsLeft) {
      if (!*newViewSmokeDone) {
        if (attemptsLeft <= 0) {
          qCritical() << "Slanted reference smoke test timed out waiting for "
                         "New View to finish";
          app.exit(15);
          return;
        }
        if (auto retry = weakStartReferenceSmoke.lock()) {
          QTimer::singleShot(100, &app, [retry, attemptsLeft]() {
            (*retry)(attemptsLeft - 1);
          });
          return;
        }
        qCritical() << "Slanted reference smoke retry expired unexpectedly";
        app.exit(15);
        return;
      }

      const QList<MainWindow *> documents = app.documentWindows();
      MainWindow *source = nullptr;
      for (MainWindow *document : documents) {
        if (document && app.isDocumentPresentationOpen(document) &&
            !document->currentImageFile().isEmpty()) {
          source = document;
          break;
        }
      }
      if (!source || !source->sharedImageData()) {
        if (attemptsLeft > 0) {
          if (auto retry = weakStartReferenceSmoke.lock()) {
            QTimer::singleShot(100, &app, [retry, attemptsLeft]() {
              (*retry)(attemptsLeft - 1);
            });
            return;
          }
        }
        qCritical() << "Slanted reference smoke test requires a visible loaded image";
        app.exit(15);
        return;
      }
      const int documentCount = documents.size();
      const int tabCount = app.tabCount();
      QPointer<MainWindow> guardedSource(source);
      QPointer<ImageViewWindow> guardedReference(
          app.createSlantedEdgeReference(source, source->currentImageFile()));
      ImageViewWindow *reference = guardedReference.data();
      if (!reference) {
        qCritical() << "Could not create slanted-edge reference view";
        app.exit(15);
        return;
      }

      auto checkReference =
          std::make_shared<std::function<void(int)>>();
      const std::weak_ptr<std::function<void(int)>> weakCheckReference =
          checkReference;
      *checkReference = [&app, guardedSource, guardedReference, documentCount,
                         tabCount, slantedReferenceSmokeDone,
                         maybeFinishStructuredSmoke,
                         weakCheckReference](int attemptsLeft) {
        MainWindow *source = guardedSource.data();
        ImageViewWindow *reference = guardedReference.data();
        if (!source || !reference) {
          qCritical() << "Slanted-edge reference disappeared while waiting for its image";
          app.exit(15);
          return;
        }
        if (!reference->sharedImageData() && attemptsLeft > 0) {
          if (auto retry = weakCheckReference.lock()) {
            QTimer::singleShot(250, &app, [retry, attemptsLeft]() {
              (*retry)(attemptsLeft - 1);
            });
            return;
          }
        }

        WorkspaceWindow *workspace = app.workspaceWindow();
        if (!reference || !reference->isSlantedEdgeReference() ||
            reference->sourceDocument() != source ||
            !reference->sharedImageData() ||
            reference->sharedImageData() == source->sharedImageData() ||
            app.documentWindows().size() != documentCount || !workspace ||
            !workspace->containsView(reference) ||
            app.tabCount() != tabCount + 1 ||
            !reference->workspaceInspectorWidget() ||
            !reference->workspaceInspectorWidget()->findChild<QWidget *>(
                QStringLiteral("SlantedEdgeNavigation")) ||
            !reference->workspaceInspectorWidget()->findChild<QWidget *>(
                QStringLiteral("SlantedEdgeReferenceTabs"))) {
          qCritical() << "Slanted-edge reference did not remain a specialized "
                         "shared-parameter workspace view";
          app.exit(15);
          return;
        }

        if (!reference->setRenderType(colorscreen::render_type_original) ||
            !reference->setRenderType(colorscreen::render_type_image_layer) ||
            reference->setRenderType(colorscreen::render_type_interpolated) ||
            reference->setCoordinateSpace(colorscreen::render_final_coordinates)) {
          qCritical() << "Slanted-edge reference exposes unexpected render modes";
          app.exit(15);
          return;
        }

        // Reproduce reference-panel detachment while source and reference are
        // visible as MDI tiles.  The section must use the actual top-level
        // presentation host, exactly like every other ParameterPanel section.
        workspace->tileDocuments();
        workspace->activateView(reference);
        QCoreApplication::processEvents();
        SharpnessPanel *sharpness =
            reference->workspaceInspectorWidget()->findChild<SharpnessPanel *>();
        QWidget *mtfChart = sharpness ? sharpness->getMTFChartWidget() : nullptr;
        QWidget *mtfSection = mtfChart ? mtfChart->parentWidget() : nullptr;
        QPushButton *detachMtf = nullptr;
        if (mtfSection) {
          for (QPushButton *button : mtfSection->findChildren<QPushButton *>()) {
            if (button && button->text() == QStringLiteral("Detach")) {
              detachMtf = button;
              break;
            }
          }
        }
        if (!sharpness || !mtfChart || !mtfSection || !detachMtf) {
          qCritical() << "Could not locate reference MTF detachable section";
          app.exit(15);
          return;
        }

        // Teardown can be queued by another presentation operation while this
        // check yields to Qt. Never retain raw child pointers across that turn.
        QPointer<SharpnessPanel> guardedSharpness(sharpness);
        QPointer<QWidget> guardedMtfChart(mtfChart);
        QPointer<QWidget> guardedMtfSection(mtfSection);
        QPointer<QPushButton> guardedDetachMtf(detachMtf);
        guardedDetachMtf->click();
        QCoreApplication::processEvents();
        if (!guardedReference || !guardedSharpness || !guardedMtfChart ||
            !guardedMtfSection || !guardedDetachMtf) {
          qCritical() << "Reference MTF section disappeared during detach";
          app.exit(15);
          return;
        }

        QMainWindow *mtfHost =
            qobject_cast<QMainWindow *>(guardedMtfSection->window());
        QDockWidget *mtfDock = nullptr;
        if (mtfHost) {
          for (QDockWidget *candidate : mtfHost->findChildren<QDockWidget *>()) {
            if (candidate && candidate->property("detachablePanel").toBool() &&
                candidate->property("detachableTitle").toString() ==
                    QStringLiteral("MTF Chart") &&
                candidate->isAncestorOf(guardedMtfChart.data())) {
              mtfDock = candidate;
              break;
            }
          }
        }
        if (!mtfDock || !mtfDock->isVisible() || !mtfDock->isFloating() ||
            !mtfDock->widget() ||
            !mtfDock->isAncestorOf(guardedMtfChart.data())) {
          qCritical() << "Reference MTF chart disappeared instead of detaching";
          app.exit(15);
          return;
        }
        QPointer<QDockWidget> guardedMtfDock(mtfDock);
        mtfDock->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        if (!guardedReference || !guardedMtfChart || !guardedDetachMtf) {
          qCritical() << "Reference MTF section disappeared during reattach";
          app.exit(15);
          return;
        }
        if (guardedMtfDock && guardedMtfDock->widget()) {
          qCritical() << "Generic MTF dock retained its content after close";
          app.exit(15);
          return;
        }
        QWidget *referenceInspector =
            guardedReference->workspaceInspectorWidget();
        if (!referenceInspector ||
            !referenceInspector->isAncestorOf(guardedMtfChart.data()) ||
            guardedDetachMtf->text() != QStringLiteral("Detach")) {
          qCritical() << "Reference MTF chart did not reattach after dock close";
          app.exit(15);
          return;
        }

        // Exercise the same implementation in two unrelated document panels.
        workspace->activateDocument(source);
        QCoreApplication::processEvents();
        auto exerciseDetachable = [workspace, source](const QString &title) {
          QWidget *inspector = source->workspaceInspectorWidget();
          QWidget *section = nullptr;
          for (QWidget *candidate : inspector->findChildren<QWidget *>()) {
            if (candidate->objectName() == QStringLiteral("DetachableSection") &&
                candidate->property("detachableTitle").toString() == title) {
              section = candidate;
              break;
            }
          }
          QPushButton *button = section
                                    ? section->findChild<QPushButton *>(
                                          QStringLiteral("DetachableSectionButton"))
                                    : nullptr;
          QWidget *content = nullptr;
          if (section) {
            for (QWidget *candidate : section->findChildren<QWidget *>()) {
              if (candidate->property("detachableContentTitle").toString() ==
                  title) {
                content = candidate;
                break;
              }
            }
          }
          if (!section || !button || !content)
            return false;
          button->click();
          QCoreApplication::processEvents();
          QDockWidget *dock = nullptr;
          for (QDockWidget *candidate : workspace->findChildren<QDockWidget *>()) {
            if (candidate->property("detachablePanel").toBool() &&
                candidate->property("detachableTitle").toString() == title &&
                candidate->isAncestorOf(content)) {
              dock = candidate;
              break;
            }
          }
          if (!dock || !dock->isVisible() || !dock->isFloating() ||
              button->text() != QStringLiteral("Reattach"))
            return false;
          QPointer<QDockWidget> guardedDock(dock);
          dock->close();
          QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
          QCoreApplication::processEvents();
          return (!guardedDock || !guardedDock->widget()) &&
                 inspector->isAncestorOf(content) &&
                 button->text() == QStringLiteral("Detach");
        };
        if (!exerciseDetachable(QStringLiteral("H&D Curve")) ||
            !exerciseDetachable(QStringLiteral("Backlight"))) {
          qCritical() << "Unrelated panels do not share the generic detach lifecycle";
          app.exit(15);
          return;
        }
        workspace->activateView(reference);
        QCoreApplication::processEvents();
        workspace->showTabbedDocuments();
        workspace->activateView(reference);
        QCoreApplication::processEvents();

        const auto beforeReload = reference->sharedImageData();
        app.reloadSlantedEdgeReferences(source);
        auto checkReload = std::make_shared<std::function<void(int)>>();
        const std::weak_ptr<std::function<void(int)>> weakCheckReload =
            checkReload;
        *checkReload = [&app, guardedSource, guardedReference, beforeReload,
                        slantedReferenceSmokeDone, maybeFinishStructuredSmoke,
                        weakCheckReload](int attemptsLeft) {
          MainWindow *source = guardedSource.data();
          ImageViewWindow *reference = guardedReference.data();
          if (!source || !reference) {
            qCritical() << "Slanted-edge reference disappeared during reload";
            app.exit(15);
            return;
          }
          if (reference->sharedImageData() == beforeReload &&
              attemptsLeft > 0) {
            if (auto retry = weakCheckReload.lock()) {
              QTimer::singleShot(250, &app, [retry, attemptsLeft]() {
                (*retry)(attemptsLeft - 1);
              });
              return;
            }
          }
          if (reference->sharedImageData() == beforeReload) {
            qCritical() << "Slanted-edge reference did not reload";
            app.exit(15);
            return;
          }

          // Creating the reference must already have persisted it. Replay that
          // recovery metadata while the original remains open; this should
          // create one additional specialized reference view. In a real crash
          // recovery the document starts with no secondary views.
          if (app.restoreSlantedEdgeReferencesFromRecovery(source) != 1) {
            qCritical() << "Slanted-edge recovery metadata did not recreate "
                           "the recorded reference";
            app.exit(15);
            return;
          }

          QList<ImageViewWindow *> references;
          for (ImageViewWindow *view : app.viewWindows()) {
            if (view && view->sourceDocument() == source &&
                view->isSlantedEdgeReference())
              references.append(view);
          }
          WorkspaceWindow *workspace = app.workspaceWindow();
          if (references.size() != 2 || !workspace) {
            qCritical() << "Recovery did not recreate exactly one reference";
            app.exit(15);
            return;
          }
          for (ImageViewWindow *view : references) {
            if (view->referenceFile() != source->currentImageFile() ||
                !workspace->containsView(view)) {
              qCritical() << "Recovered slanted-edge reference has wrong "
                             "file or presentation";
              app.exit(15);
              return;
            }
          }
          for (ImageViewWindow *view : references)
            app.closeView(view);
          *slantedReferenceSmokeDone = true;
          (*maybeFinishStructuredSmoke)();
        };
        (*checkReload)(28);
      };
      (*checkReference)(28);
    };
    QTimer::singleShot(350, &app, [startReferenceSmoke]() {
      (*startReferenceSmoke)(80);
    });
  }

  if (parser.isSet(smokeTestOption)) {
    bool converted = false;
    int duration = parser.value(smokeTestOption).toInt(&converted);
    if (!converted || duration <= 0)
      duration = 5000;
    // New View and slanted-reference checks manipulate shared presentation
    // serially. Give their watchdog enough room under instrumentation, but
    // successful structured checks quit immediately when they are complete.
    if (completionManagedSmoke && parser.isSet(newViewOption) &&
        parser.isSet(slantedReferenceOption))
      duration = qMax(duration, 60000);
    if (completionManagedSmoke && parser.isSet(workspaceChurnOption))
      duration = qMax(duration, 60000);
    if (completionManagedSmoke && parser.isSet(documentLifecycleOption))
      duration = qMax(duration, 60000);
    qDebug() << "Smoke Test Mode: watchdog is" << duration << "ms";
    QTimer::singleShot(
        duration, &app,
        [&app, completionManagedSmoke, newViewSmokeDone,
         slantedReferenceSmokeDone, workspaceChurnSmokeDone,
         documentLifecycleSmokeDone]() {
          if (completionManagedSmoke) {
            if (!*newViewSmokeDone || !*slantedReferenceSmokeDone ||
                !*workspaceChurnSmokeDone || !*documentLifecycleSmokeDone) {
              qCritical()
                  << "Structured GUI smoke test timed out before completion";
              app.exit(17);
              return;
            }
            app.quit();
            return;
          }

          // Ordinary smoke tests can leave Qt MDI mode-switch and activation
          // work queued. Begin document teardown while the event loop is still
          // running, then give deferred close processing one turn before quit.
          app.closeAllDocumentWindows();
          QTimer::singleShot(0, &app, [&app]() { app.quit(); });
        });
  }

  // WorkspaceWindow deliberately survives Close while detached peer windows
  // exist. Keep an explicit owner in main() so the hidden workspace cannot
  // outlive QApplication teardown.
  QPointer<WorkspaceWindow> workspaceOwner(app.workspaceWindow());
  const int exitCode = app.exec();

  if (parser.isSet(smokeTestOption)) {
    // A smoke test can exit while a slanted-reference QtConcurrent load is
    // finishing or while WA_DeleteOnClose widgets are queued for deletion.
    // Drain both before LeakSanitizer inspects process state.
    app.closeAllDocumentWindows();
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }

  delete workspaceOwner.data();
  return exitCode;
}
