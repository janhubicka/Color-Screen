#include <algorithm>
#include "../libcolorscreen/include/imagedata.h"
#include "GeometryPanel.h"
#include "BackgroundThreadRegistry.h"
#include "ColorScreenApplication.h"
#include "CapturePanel.h"
#include "ColorPanel.h"
#include "ContactCopyPanel.h"
#include "MainWindow.h"
#include "MultiLineTabWidget.h"
#include "ParameterPanel.h"
#include "ProfilePanel.h"
#include "ImageViewWindow.h"
#include "ImageWidget.h"
#include "InitialSetupGuideDialog.h"
#include "SharpnessPanel.h"
#include "ToneCurveWidget.h"
#include "TilePreviewPanel.h"
#include "TilesPanel.h"
#include "CoordinateTransformer.h"
#include "CoordinateOptimizationWorker.h"
#include "DocumentLifecycleSmoke.h"
#include "DetectScreenWorker.h"
#include "FocusAnalysisWorker.h"
#include "FinetuneWorker.h"
#include "FlatFieldWorker.h"
#include "WorkspaceChurnSmoke.h"
#include "WorkspaceWindow.h"
#include "progress-info.h"
#include "../libcolorscreen/include/stitch.h"

#include <QAction>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStringList>
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

#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace {

/** Tone-curve wrapper exposing the protected plot mapping to the smoke probe. */
class PointerSmokeToneCurve final : public ToneCurveWidget {
public:
  using ToneCurveWidget::ToneCurveWidget;
  QPointF plotPoint(double x, double y) const { return plotToWidget(x, y); }
};

/** Expose checkbox helpers so their common enabled/applicable contract can be
    checked without relying on a particular processing panel. */
class CheckboxSemanticsProbe final : public ParameterPanel {
public:
  using ParameterPanel::ParameterPanel;
  using ParameterPanel::addCheckboxParameter;
  using ParameterPanel::addCheckboxWithReset;
  using ParameterPanel::addCorrelatedRGBParameter;
  using ParameterPanel::addEnumParameter;
  using ParameterPanel::addSeparator;
  using ParameterPanel::addSlider;
  using ParameterPanel::addSliderParameter;
  using ParameterPanel::addSliderParameterControls;
  using ParameterPanel::setParameterApplicability;
};

/** Exercise dynamic tile-definition rebuilds without starting render work. */
class TileDefinitionProbe final : public TilePreviewPanel {
public:
  TileDefinitionProbe(StateGetter stateGetter, StateSetter stateSetter,
                      ImageGetter imageGetter)
      : TilePreviewPanel(std::move(stateGetter), std::move(stateSetter),
                         std::move(imageGetter), nullptr, false) {}

  void initialize() { setupTiles(QStringLiteral("Tile definition probe")); }
  void useAlternateDefinitions() {
    m_alternate = true;
    rebuildTiles();
  }

protected:
  std::vector<std::pair<colorscreen::render_screen_tile_type, QString>>
  getTileTypes() const override {
    if (m_alternate)
      return {{colorscreen::corrected_backlight_screen,
               QStringLiteral("After A")},
              {colorscreen::corrected_detail_screen,
               QStringLiteral("After B")}};
    return {{colorscreen::original_screen, QStringLiteral("Before A")},
            {colorscreen::corrected_full_screen,
             QStringLiteral("Before B")}};
  }

  bool shouldUpdateTiles(const ParameterState &) override { return false; }
  bool isTileRenderingEnabled(const ParameterState &) const override {
    return false;
  }
  bool requiresScan() const override { return false; }

private:
  bool m_alternate = false;
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

/** Exercise deadlock-safe joining of ad-hoc document worker threads. */
bool backgroundThreadRegistryShutdownSmoke() {
  BackgroundThreadRegistry registry;
  QThread workerThread;
  QThread *guiThread = QThread::currentThread();
  QObject *guiContext = QCoreApplication::instance();
  std::atomic<bool> enteredBlockingCall{false};
  std::atomic<bool> callbackServiced{false};
  std::atomic<bool> workerResumedAfterCallback{false};

  // Install all functors before start(): constructing an invokeMethod lambda
  // on the worker publishes its captures through Qt's event queue, whose
  // synchronization is not visible to TSan in the uninstrumented system Qt.
  // QThread::started is emitted on the worker, in connection order. The direct
  // slots touch only atomics; the middle slot still blocks on a GUI MetaCall,
  // so shutdown must service that call rather than merely wait for the thread.
  QObject::connect(
      &workerThread, &QThread::started, &workerThread,
      [&enteredBlockingCall]() {
        enteredBlockingCall.store(true, std::memory_order_release);
      },
      Qt::DirectConnection);
  QObject::connect(
      &workerThread, &QThread::started, guiContext,
      [&callbackServiced, guiThread]() {
        callbackServiced.store(QThread::currentThread() == guiThread,
                               std::memory_order_release);
      },
      Qt::BlockingQueuedConnection);
  QObject::connect(
      &workerThread, &QThread::started, &workerThread,
      [&callbackServiced, &workerResumedAfterCallback]() {
        workerResumedAfterCallback.store(
            callbackServiced.load(std::memory_order_acquire),
            std::memory_order_release);
      },
      Qt::DirectConnection);

  registry.track(&workerThread);
  workerThread.start();

  for (int i = 0;
       i < 1000 && !enteredBlockingCall.load(std::memory_order_acquire); ++i)
    QThread::msleep(1);

  if (!enteredBlockingCall.load(std::memory_order_acquire)) {
    qCritical() << "Background-thread registry smoke worker did not start";
    registry.shutdown(guiContext);
    return false;
  }

  registry.shutdown(guiContext);
  if (workerThread.isRunning()) {
    qCritical() << "Background-thread registry did not join its worker";
    return false;
  }
  if (!callbackServiced.load(std::memory_order_acquire)) {
    qCritical() << "Background-thread registry did not service blocking MetaCall";
    return false;
  }
  if (!workerResumedAfterCallback.load(std::memory_order_acquire)) {
    qCritical()
        << "Background-thread registry worker did not resume after GUI callback";
    return false;
  }
  if (!registry.empty()) {
    qCritical() << "Background-thread registry retained joined workers";
    return false;
  }
  return true;
}

/** Verify that section preferences stay independent of document state. */
bool parameterSectionPreferencesSmoke() {
  auto fail = [](const char *reason) {
    qCritical() << "Section-preference smoke failed:" << reason;
    return false;
  };

  // Touch only a unique test subtree, including on an early failure.
  QTemporaryDir temporary;
  if (!temporary.isValid())
    return fail("could not allocate unique settings keys");
  const QString keyRoot = QStringLiteral("smoke.%1")
      .arg(QFileInfo(temporary.path()).fileName());
  const QString settingsRoot =
      QStringLiteral("inspector/sections/") + keyRoot;
  struct SettingsCleanup {
    QString root;
    /** Remove only preferences belonging to this smoke invocation. */
    ~SettingsCleanup() { QSettings().remove(root); }
  } cleanup{settingsRoot};
  const QString firstKey = keyRoot + QStringLiteral("/first");
  const QString secondKey = keyRoot + QStringLiteral("/second");
  const QString firstSetting = settingsRoot + QStringLiteral("/first/expanded");
  const QString secondSetting = settingsRoot + QStringLiteral("/second/expanded");

  ParameterState state;
  int documentEdits = 0;
  auto getState = [&state]() { return state; };
  auto setState = [&state, &documentEdits](const ParameterState &next,
                                          const QString &, const QString &) {
    state = next;
    ++documentEdits;
  };
  auto noImage = []() { return std::shared_ptr<colorscreen::image_data>(); };
  auto addRow = [](CheckboxSemanticsProbe &panel) {
    QCheckBox *field = panel.addCheckboxParameter(
        QStringLiteral("Saved value"),
        [](const ParameterState &s) { return s.rparams.scan_mirror; },
        [](ParameterState &s, bool value) { s.rparams.scan_mirror = value; });
    return field->parentWidget();
  };

  {
    CheckboxSemanticsProbe panel(getState, setState, noImage, nullptr, false);
    QToolButton *first = panel.addSeparator(QStringLiteral("Repeated title"),
                                           firstKey);
    QWidget *firstRow = addRow(panel);
    QToolButton *second = panel.addSeparator(QStringLiteral("Repeated title"),
                                            secondKey);
    QWidget *secondRow = addRow(panel);
    bool rowApplicable = false;
    panel.setParameterApplicability(secondRow,
        [&rowApplicable](const ParameterState &) { return rowApplicable; });
    panel.updateUI();
    if (!first->isChecked() || !second->isChecked() || firstRow->isHidden()
        || !secondRow->isHidden())
      return fail("initial folding or row applicability is incorrect");
    if (QSettings().contains(firstSetting) || QSettings().contains(secondSetting))
      return fail("initial refresh persisted a choice the user did not make");
    if (first->property("sectionKey").toString() != firstKey
        || first->property("parameterKey").isValid()
        || first->accessibleName() != QStringLiteral("Repeated title"))
      return fail("section identity/accessibility was confused with parameters");

    first->click();
    panel.updateUI();
    if (!firstRow->isHidden() || first->arrowType() != Qt::RightArrow
        || QSettings().value(firstSetting, true).toBool()
        || QSettings().contains(secondSetting))
      return fail("user folding did not persist independently by stable key");

    // Repeated captions and inapplicable rows must not share fold state.
    second->click();
    second->click();
    panel.updateUI();
    if (!secondRow->isHidden() || !second->isChecked()
        || !QSettings().value(secondSetting, false).toBool())
      return fail("expansion resurrected an inapplicable row");
    rowApplicable = true;
    panel.updateUI();
    if (secondRow->isHidden())
      return fail("applicable row did not return in an expanded section");

    // Programmatic changes and a hidden/reparented inspector are not choices
    // to copy into subsequently opened documents.
    first->setChecked(true);
    QWidget *firstGroup = first->parentWidget()->parentWidget();
    bool sectionApplicable = false;
    panel.setParameterApplicability(firstGroup,
        [&sectionApplicable](const ParameterState &) {
          return sectionApplicable;
        });
    panel.updateUI();
    if (!firstGroup->isHidden() || !first->isChecked()
        || QSettings().value(firstSetting, true).toBool())
      return fail("programmatic folding/applicability overwrote the preference");
    sectionApplicable = true;
    panel.updateUI();
    if (firstGroup->isHidden() || firstRow->isHidden())
      return fail("whole-section applicability lost local expansion state");
  }

  {
    CheckboxSemanticsProbe restored(getState, setState, noImage, nullptr, false);
    QToolButton *first = restored.addSeparator(QStringLiteral("Renamed title"),
                                              firstKey);
    QWidget *firstRow = addRow(restored);
    restored.updateUI();
    if (first->isChecked() || !firstRow->isHidden()
        || first->arrowType() != Qt::RightArrow)
      return fail("recreated/renamed section did not restore collapsed state");
    QWidget *lateRow = addRow(restored);
    state.rparams.scan_mirror = true;
    restored.updateUI();
    if (!lateRow->isHidden() || !firstRow->isHidden())
      return fail("late rows or document refresh escaped a collapsed section");

    QToolButton *second = restored.addSeparator(QStringLiteral("Renamed title"),
                                               secondKey);
    QWidget *secondRow = addRow(restored);
    QToolButton *unkeyed = restored.addSeparator(QStringLiteral("Renamed title"));
    QWidget *unkeyedRow = addRow(restored);
    restored.updateUI();
    if (!second->isChecked() || secondRow->isHidden()
        || !unkeyed->isChecked() || unkeyedRow->isHidden())
      return fail("same-caption or unkeyed sections inherited another key");

    const QStringList settingsBefore = QSettings().allKeys();
    unkeyed->click();
    restored.updateUI();
    if (!unkeyedRow->isHidden() || QSettings().allKeys() != settingsBefore)
      return fail("unkeyed folding wrote application settings");

    first->click();
    restored.updateUI();
    if (firstRow->isHidden() || lateRow->isHidden()
        || !QSettings().value(firstSetting, false).toBool())
      return fail("explicit re-expansion did not replace the saved choice");
  }

  CheckboxSemanticsProbe reopened(getState, setState, noImage, nullptr, false);
  QToolButton *first = reopened.addSeparator(QStringLiteral("Third title"),
                                            firstKey);
  QWidget *row = addRow(reopened);
  reopened.updateUI();
  if (!first->isChecked() || row->isHidden())
    return fail("latest user expansion was not restored");
  if (documentEdits != 0)
    return fail("section preferences entered the document/Undo setter");
  return true;
}

/** Exercise persisted folding in representative real processing panels. */
bool colorSectionPreferencesSmoke() {
  auto fail = [](const QString &reason) {
    qCritical() << "Processing-panel section smoke failed:" << reason;
    return false;
  };

  // Real panels use their production keys. Isolate the complete QSettings
  // identity instead of overwriting any existing operator preferences. This
  // probe runs before document windows are created and never pumps GUI events.
  QTemporaryDir temporary;
  if (!temporary.isValid())
    return fail(QStringLiteral("could not allocate isolated settings identity"));
  struct SettingsIdentityGuard {
    QString organization = QCoreApplication::organizationName();
    QString domain = QCoreApplication::organizationDomain();
    QString application = QCoreApplication::applicationName();
    /** Remove the probe's settings and restore the application's identity. */
    ~SettingsIdentityGuard() {
      QSettings settings;
      settings.clear();
      settings.sync();
      QCoreApplication::setOrganizationName(organization);
      QCoreApplication::setOrganizationDomain(domain);
      QCoreApplication::setApplicationName(application);
    }
  } settingsGuard;
  const QString identity = QStringLiteral("ColorScreenSmoke-%1")
      .arg(QFileInfo(temporary.path()).fileName());
  QCoreApplication::setOrganizationName(identity);
  QCoreApplication::setOrganizationDomain(identity + QStringLiteral(".invalid"));
  QCoreApplication::setApplicationName(QStringLiteral("PanelSections"));

  using RenderParameters = colorscreen::render_parameters;
  ParameterState state;
  state.rparams.capture_type = RenderParameters::capture_transparency_with_screen;
  state.rparams.color_model = RenderParameters::color_model_scan;
  state.rparams.contact_copy.simulate = true;
  const ParameterState initialState = state;
  int documentEdits = 0;
  auto getState = [&state]() { return state; };
  auto setState = [&documentEdits](const ParameterState &, const QString &,
                                  const QString &) { ++documentEdits; };
  auto noImage = []() { return std::shared_ptr<colorscreen::image_data>(); };

  auto settingKey = [](const QString &key) {
    return QStringLiteral("inspector/sections/%1/expanded").arg(key);
  };
  auto toggleFor = [](ParameterPanel &panel, const QString &key) {
    QToolButton *result = nullptr;
    for (auto *button : panel.findChildren<QToolButton *>()) {
      if (button->property("sectionKey").toString() == key) {
        if (result)
          return static_cast<QToolButton *>(nullptr);
        result = button;
      }
    }
    return result;
  };
  auto groupFor = [](ParameterPanel &panel, const QString &key) {
    QGroupBox *result = nullptr;
    for (auto *group : panel.findChildren<QGroupBox *>()) {
      if (group->property("sectionKey").toString() == key) {
        if (result)
          return static_cast<QGroupBox *>(nullptr);
        result = group;
      }
    }
    return result;
  };
  auto verify = [&](ParameterPanel &panel, const QStringList &keys,
                    const std::vector<bool> &expanded) {
    int keyedButtons = 0;
    for (auto *button : panel.findChildren<QToolButton *>())
      if (button->property("sectionKey").isValid())
        ++keyedButtons;
    if (keyedButtons != keys.size())
      return fail(QStringLiteral("incomplete or duplicate panel section keys"));
    for (int i = 0; i < keys.size(); ++i) {
      auto *button = toggleFor(panel, keys[i]);
      auto *group = groupFor(panel, keys[i]);
      if (!button || !group || button->isChecked() != expanded[i]
          || button->property("parameterKey").isValid()
          || !group->layout() || group->layout()->count() != 2)
        return fail(QStringLiteral("incorrect section state: %1").arg(keys[i]));
      auto *form = qobject_cast<QFormLayout *>(
          group->layout()->itemAt(1)->layout());
      if (!form || form->rowCount() == 0)
        return fail(QStringLiteral("missing section rows: %1").arg(keys[i]));
      auto rowsRespectPresentation =
          [](auto self, QLayoutItem *item, bool open) -> bool {
        if (QWidget *widget = item->widget()) {
          const QVariant applicable = widget->property("parameterApplicable");
          // A collapsed section must hide every row, and an explicitly
          // inapplicable row must stay hidden. Expanded sections may still
          // contain independently hidden diagnostics (for example a preview
          // with no scan), so do not mistake that presentation state for a
          // folding failure.
          if (!open && !widget->isHidden())
            return false;
          if (applicable.isValid() && !applicable.toBool()
              && !widget->isHidden())
            return false;
          return true;
        }
        if (QLayout *layout = item->layout())
          for (int row = 0; row < layout->count(); ++row)
            if (!self(self, layout->itemAt(row), open))
              return false;
        return true;
      };
      for (int row = 0; row < form->count(); ++row)
        if (!rowsRespectPresentation(rowsRespectPresentation,
                                     form->itemAt(row), expanded[i]))
          return fail(QStringLiteral(
                          "row escaped section folding/applicability: %1")
                          .arg(keys[i]));
    }
    return true;
  };

  /** Describe the two production panels and their complete section identities. */
  struct PanelProbe {
    QStringList keys;
    std::function<std::unique_ptr<ParameterPanel>()> create;
  };
  const std::vector<PanelProbe> probes = {
      {{QStringLiteral("capture.source"), QStringLiteral("capture.optics"),
        QStringLiteral("capture.sensor"),
        QStringLiteral("capture.wavelengths"),
        QStringLiteral("capture.corrections")},
       [&]() {
         return std::make_unique<CapturePanel>(
             getState, setState, noImage, []() {});
       }},
      {{QStringLiteral("sharpness.capture"),
        QStringLiteral("sharpness.measurements"),
        QStringLiteral("sharpness.deconvolution"),
        QStringLiteral("sharpness.wiener"),
        QStringLiteral("sharpness.richardson_lucy"),
        QStringLiteral("sharpness.unsharp"),
        QStringLiteral("sharpness.focus"),
        QStringLiteral("sharpness.adaptive")},
       [&]() {
         return std::make_unique<SharpnessPanel>(
             getState, setState, noImage);
       }},
      {{QStringLiteral("color.process"), QStringLiteral("color.backlight"),
        QStringLiteral("color.dyes"), QStringLiteral("color.viewing"),
        QStringLiteral("color.final")},
       [&]() { return std::make_unique<ColorPanel>(getState, setState, noImage); }},
      {{QStringLiteral("contact_copy.film"),
        QStringLiteral("contact_copy.richards"),
        QStringLiteral("contact_copy.manual_points"),
        QStringLiteral("contact_copy.darkroom")},
       [&]() {
         return std::make_unique<ContactCopyPanel>(getState, setState, noImage);
       }}};

  for (const PanelProbe &probe : probes) {
    auto first = probe.create();
    std::vector<bool> expected(probe.keys.size(), true);
    if (!verify(*first, probe.keys, expected))
      return false;
    for (int i = 0; i < probe.keys.size(); ++i) {
      if (QSettings().contains(settingKey(probe.keys[i])))
        return fail(QStringLiteral("construction persisted a default choice"));
      // Alternate folds to catch accidental reuse of one section identity.
      if (i % 2 == 0) {
        toggleFor(*first, probe.keys[i])->click();
        expected[i] = false;
      }
    }
    first->updateUI();
    if (!verify(*first, probe.keys, expected) || state != initialState)
      return fail(QStringLiteral("folding changed rows or document parameters"));
    {
      auto second = probe.create();
      if (!verify(*second, probe.keys, expected))
        return false;

      // A programmatic fold remains local, including after applicability
      // changes. The second inspector and saved user choice must not follow it.
      toggleFor(*first, probe.keys[0])->setChecked(true);
      state.rparams.capture_type = RenderParameters::capture_plain_image;
      state.rparams.contact_copy.simulate = false;
      first->updateUI();
      second->updateUI();
      for (const QString &key : probe.keys) {
        if (key.startsWith(QStringLiteral("contact_copy."))
            || key == QStringLiteral("color.dyes")
            || key == QStringLiteral("color.viewing")) {
          auto *group = groupFor(*second, key);
          if (!group->isHidden()
              || group->property("parameterApplicable").toBool())
            return fail(QStringLiteral("inapplicable section was shown: %1")
                            .arg(key));
        }
      }
      state = initialState;
      first->updateUI();
      second->updateUI();
      auto firstExpected = expected;
      firstExpected[0] = true;
      if (!verify(*first, probe.keys, firstExpected)
          || !verify(*second, probe.keys, expected)
          || QSettings().value(settingKey(probe.keys[0]), true).toBool())
        return fail(QStringLiteral("refresh lost independent fold preferences"));
      for (const QString &key : probe.keys) {
        if (groupFor(*second, key)->isHidden())
          return fail(QStringLiteral("applicable section did not return: %1")
                          .arg(key));
      }

      if (auto *color = qobject_cast<ColorPanel *>(second.get())) {
        auto *finalGroup = groupFor(*color, QStringLiteral("color.final"));
        auto *curve = color->findChild<ToneCurveWidget *>(
            QStringLiteral("ColorToneCurveWidget"));
        if (!curve || !finalGroup->isAncestorOf(curve)
            || curve->isVisibleTo(finalGroup))
          return fail(QStringLiteral("tone curve escaped Final adjustments"));
        toggleFor(*color, QStringLiteral("color.final"))->click();
        expected[4] = true;
        if (!curve->isVisibleTo(finalGroup))
          return fail(QStringLiteral("expanded Final adjustments lost its curve"));

        auto *spectra = color->findChild<QWidget *>(
            QStringLiteral("ColorSpectralChartRow"));
        if (!spectra || !spectra->isHidden())
          return fail(QStringLiteral("matrix-only model exposed spectral chart"));
        state.rparams.color_model =
            RenderParameters::color_model_dufay_color_cinematography_spectra;
        color->updateUI();
        if (!spectra->property("parameterApplicable").toBool()
            || !spectra->isHidden())
          return fail(QStringLiteral("newly applicable chart escaped saved fold"));
        toggleFor(*color, QStringLiteral("color.dyes"))->click();
        expected[2] = true;
        if (spectra->isHidden())
          return fail(QStringLiteral("expanded dyes lost the applicable chart"));
        state = initialState;
        color->updateUI();
        if (!spectra->isHidden())
          return fail(QStringLiteral("expanded dyes resurrected inapplicable chart"));
      }

      if (auto *sharpness = qobject_cast<SharpnessPanel *>(second.get())) {
        auto *adaptiveToggle =
            toggleFor(*sharpness, QStringLiteral("sharpness.adaptive"));
        auto *adaptiveRow = sharpness->findChild<QWidget *>(
            QStringLiteral("SharpnessAdaptiveChartRow"));
        if (!adaptiveToggle || !adaptiveRow || !adaptiveRow->isHidden()
            || adaptiveRow->property("parameterApplicable").toBool())
          return fail(QStringLiteral(
              "Sharpness exposed adaptive diagnostics without data"));

        // Live analysis makes the row applicable, but never overrides folding.
        adaptiveToggle->click();
        const bool adaptiveExpanded = adaptiveToggle->isChecked();
        sharpness->setAdaptiveAnalysisRunning(true);
        if (!adaptiveRow->property("parameterApplicable").toBool()
            || !adaptiveRow->isHidden())
          return fail(QStringLiteral(
              "Live adaptive analysis escaped a collapsed Sharpness section"));
        adaptiveToggle->click();
        if (adaptiveToggle->isChecked() == adaptiveExpanded
            || adaptiveRow->isHidden())
          return fail(QStringLiteral(
              "Expanded Sharpness section lost its live adaptive chart"));
        sharpness->setAdaptiveAnalysisRunning(false);
        if (!adaptiveRow->isHidden()
            || adaptiveRow->property("parameterApplicable").toBool())
          return fail(QStringLiteral(
              "Finished adaptive analysis retained an empty diagnostic row"));
      }

      // An explicit choice in the new inspector affects future panels only.
      toggleFor(*second, probe.keys[1])->click();
      expected[1] = false;
      first->updateUI();
      second->updateUI();
      if (!toggleFor(*first, probe.keys[1])->isChecked()
          || !verify(*second, probe.keys, expected))
        return fail(QStringLiteral("one inspector changed another's fold state"));
    }
    auto reopened = probe.create();
    if (!verify(*reopened, probe.keys, expected))
      return false;
  }
  if (documentEdits != 0 || state != initialState)
    return fail(QStringLiteral("section preferences entered document/Undo state"));
  return true;
}

/** Exercise persisted Geometry folds and incremental diagnostic visibility. */
bool geometrySectionPreferencesSmoke() {
  auto fail = [](const QString &reason) {
    qCritical() << "Geometry section smoke failed:" << reason;
    return false;
  };

  // Production section keys must never overwrite operator preferences. Run
  // before document creation, with no event pumping under this test identity.
  QTemporaryDir temporary;
  if (!temporary.isValid())
    return fail(QStringLiteral("could not allocate isolated settings identity"));
  struct SettingsIdentityGuard {
    QString organization = QCoreApplication::organizationName();
    QString domain = QCoreApplication::organizationDomain();
    QString application = QCoreApplication::applicationName();
    /** Remove only test settings and restore the original application identity. */
    ~SettingsIdentityGuard() {
      QSettings settings;
      settings.clear();
      settings.sync();
      QCoreApplication::setOrganizationName(organization);
      QCoreApplication::setOrganizationDomain(domain);
      QCoreApplication::setApplicationName(application);
    }
  } settingsGuard;
  const QString identity = QStringLiteral("ColorScreenGeometrySmoke-%1")
      .arg(QFileInfo(temporary.path()).fileName());
  QCoreApplication::setOrganizationName(identity);
  QCoreApplication::setOrganizationDomain(identity + QStringLiteral(".invalid"));
  QCoreApplication::setApplicationName(QStringLiteral("GeometrySections"));

  using Solver = colorscreen::solver_parameters;
  ParameterState state;
  state.scrToImg.type = colorscreen::Dufay;
  state.scrToImg.scanner_type = colorscreen::fixed_lens;
  const ParameterState initialState = state;
  std::shared_ptr<colorscreen::image_data> scan;
  int documentEdits = 0;
  int fitRequests = 0;
  auto getState = [&]() { return state; };
  auto setState = [&](const ParameterState &, const QString &, const QString &) {
    ++documentEdits;
  };
  auto getImage = [&]() { return scan; };
  auto createPanel = [&]() {
    auto panel = std::make_unique<GeometryPanel>(getState, setState, getImage);
    QObject::connect(panel.get(), &GeometryPanel::optimizeRequested,
                     panel.get(), [&](bool) { ++fitRequests; });
    return panel;
  };
  const QStringList keys = {
      QStringLiteral("geometry.registration_points"),
      QStringLiteral("geometry.automatic_registration"),
      QStringLiteral("geometry.fit"),
      QStringLiteral("geometry.final_orientation"),
      QStringLiteral("geometry.visualization")};
  auto settingKey = [](const QString &key) {
    return QStringLiteral("inspector/sections/%1/expanded").arg(key);
  };
  auto toggleFor = [](GeometryPanel &panel, const QString &key) {
    QToolButton *result = nullptr;
    for (auto *button : panel.findChildren<QToolButton *>()) {
      if (button->property("sectionKey").toString() == key) {
        if (result)
          return static_cast<QToolButton *>(nullptr);
        result = button;
      }
    }
    return result;
  };
  auto groupFor = [](GeometryPanel &panel, const QString &key) {
    QGroupBox *result = nullptr;
    for (auto *group : panel.findChildren<QGroupBox *>()) {
      if (group->property("sectionKey").toString() == key) {
        if (result)
          return static_cast<QGroupBox *>(nullptr);
        result = group;
      }
    }
    return result;
  };
  auto verify = [&](GeometryPanel &panel, const std::vector<bool> &expanded) {
    int keyedButtons = 0;
    for (auto *button : panel.findChildren<QToolButton *>())
      if (button->property("sectionKey").isValid())
        ++keyedButtons;
    if (keyedButtons != keys.size())
      return fail(QStringLiteral("incomplete or duplicate section keys"));
    for (int i = 0; i < keys.size(); ++i) {
      auto *button = toggleFor(panel, keys[i]);
      auto *group = groupFor(panel, keys[i]);
      if (!button || !group || button->isChecked() != expanded[i]
          || button->property("parameterKey").isValid()
          || button->accessibleName().isEmpty()
          || !group->layout() || group->layout()->count() != 2)
        return fail(QStringLiteral("incorrect section state: %1").arg(keys[i]));
      auto *form = qobject_cast<QFormLayout *>(
          group->layout()->itemAt(1)->layout());
      if (!form || form->rowCount() == 0)
        return fail(QStringLiteral("missing section rows: %1").arg(keys[i]));
      auto rowsMatch = [](auto self, QLayoutItem *item, bool open) -> bool {
        if (QWidget *widget = item->widget()) {
          const QVariant applicable = widget->property("parameterApplicable");
          return !widget->isHidden()
              == (open && (!applicable.isValid() || applicable.toBool()));
        }
        if (QLayout *layout = item->layout())
          for (int row = 0; row < layout->count(); ++row)
            if (!self(self, layout->itemAt(row), open))
              return false;
        return true;
      };
      for (int row = 0; row < form->count(); ++row)
        if (!rowsMatch(rowsMatch, form->itemAt(row), expanded[i]))
          return fail(QStringLiteral("row escaped section folding: %1")
                          .arg(keys[i]));
    }
    return true;
  };
  const QStringList messages = {
      QStringLiteral("GeometryOptimizationMessage"),
      QStringLiteral("GeometryLensMessage"),
      QStringLiteral("GeometryTiltMessage"),
      QStringLiteral("GeometryNonlinearMessage")};
  auto verifyMessages = [&](GeometryPanel &panel,
                            const std::vector<bool> &applicable) {
    auto *group = groupFor(panel, keys[2]);
    auto *toggle = toggleFor(panel, keys[2]);
    if (!group || !toggle)
      return fail(QStringLiteral("missing fit section"));
    for (int i = 0; i < messages.size(); ++i) {
      auto *label = panel.findChild<QLabel *>(messages[i]);
      if (!label || !group->isAncestorOf(label)
          || label->property("parameterApplicable").toBool() != applicable[i]
          || !label->text().isEmpty() != applicable[i]
          || !label->isHidden() != (applicable[i] && toggle->isChecked()))
        return fail(QStringLiteral("incorrect prerequisite row: %1")
                        .arg(messages[i]));
    }
    return true;
  };
  const QStringList charts = {
      QStringLiteral("GeometryLensChartRow"),
      QStringLiteral("GeometryPerspectiveChartRow"),
      QStringLiteral("GeometryNonlinearChartRow"),
      QStringLiteral("GeometryFinalChartRow")};
  auto verifyCharts = [&](GeometryPanel &panel,
                          const std::vector<bool> &applicable) {
    auto *group = groupFor(panel, keys[4]);
    auto *toggle = toggleFor(panel, keys[4]);
    if (!group || !toggle)
      return fail(QStringLiteral("missing visualization section"));
    for (int i = 0; i < charts.size(); ++i) {
      auto *row = panel.findChild<QWidget *>(charts[i]);
      if (!row || !group->isAncestorOf(row)
          || !row->property("parameterApplicable").isValid()
          || row->property("parameterApplicable").toBool() != applicable[i]
          || !row->isHidden() != (applicable[i] && toggle->isChecked()))
        return fail(QStringLiteral("incorrect chart row: %1").arg(charts[i]));
    }
    return true;
  };

  auto first = createPanel();
  std::vector<bool> expected(keys.size(), true);
  if (!verify(*first, expected)
      || !verifyMessages(*first, {true, true, true, true})
      || !verifyCharts(*first, {false, false, false, false}))
    return false;
  for (int i = 0; i < keys.size(); ++i) {
    if (QSettings().contains(settingKey(keys[i])))
      return fail(QStringLiteral("construction persisted a default choice"));
    if (i % 2 == 0) {
      toggleFor(*first, keys[i])->click();
      expected[i] = false;
    }
  }
  first->updateUI();
  auto second = createPanel();
  if (!verify(*first, expected) || !verify(*second, expected))
    return false;

  // A programmatic fold stays local and never overwrites the saved preference.
  toggleFor(*first, keys[2])->setChecked(true);
  auto firstExpected = expected;
  firstExpected[2] = true;
  first->updateUI();
  second->updateUI();
  if (!verify(*first, firstExpected) || !verify(*second, expected)
      || QSettings().value(settingKey(keys[2]), true).toBool()
      || !verifyMessages(*second, {true, true, true, true}))
    return fail(QStringLiteral("programmatic folding changed another inspector"));

  const int enoughPoints = std::max({Solver::min_points(state.scrToImg.type),
      Solver::min_lens_points(state.scrToImg.type),
      Solver::min_perspective_points(state.scrToImg.type),
      Solver::min_mesh_points(state.scrToImg.type)});
  auto addLocalPoints = [&]() {
    for (int i = 0; i < enoughPoints; ++i)
      state.solver.points.push_back({{1, 1}, {1, 1}, Solver::green});
  };
  addLocalPoints();
  // Exercise the incremental API, not just the complete updateUI() path.
  second->updateRegistrationPointInfo(state);
  if (!verifyMessages(*second, {false, false, false, false}))
    return false;
  toggleFor(*second, keys[2])->click();
  expected[2] = true;
  if (!verifyMessages(*second, {false, false, false, false}))
    return fail(QStringLiteral("expansion resurrected satisfied prerequisites"));
  state.solver.points.clear();
  second->updateRegistrationPointInfo(state);
  if (!verifyMessages(*second, {true, true, true, true}))
    return fail(QStringLiteral("incremental point removal lost prerequisites"));
  toggleFor(*second, keys[2])->click();
  expected[2] = false;

  // Enough clustered points still require a coverage warning, even while folded.
  scan = std::make_shared<colorscreen::image_data>();
  scan->width = 80;
  scan->height = 64;
  state.scrToImg.center = {40, 32};
  state.scrToImg.coordinate1 = {4, 0};
  state.scrToImg.coordinate2 = {0, 4};
  addLocalPoints();
  second->updateRegistrationPointInfo(state);
  if (!verifyMessages(*second, {false, true, false, false}))
    return fail(QStringLiteral("lens coverage warning ignored folding"));
  state.solver.points.push_back({{0, 0}, {0, 0}, Solver::green});
  state.solver.points.push_back({{79, 63}, {20, 16}, Solver::green});
  second->updateRegistrationPointInfo(state);
  if (!verifyMessages(*second, {false, true, false, false}))
    return fail(QStringLiteral("outliers incorrectly cleared the coverage warning"));

  // Coverage uses the central 90% span, not a bounding box. Replace the
  // cluster by a distributed grid instead of relying on isolated outliers.
  state.solver.points.clear();
  const int gridRows = (enoughPoints + 9) / 10;
  for (int i = 0; i < enoughPoints; ++i) {
    const double x = (i % 10) * 79.0 / 9;
    const double y = (i / 10) * 63.0 / (gridRows - 1);
    state.solver.points.push_back({{x, y}, {x / 4, y / 4}, Solver::green});
  }
  second->updateRegistrationPointInfo(state);
  if (!verifyMessages(*second, {false, false, false, false}))
    return fail(QStringLiteral("sufficient lens coverage retained its warning"));
  const ParameterState withGeometry = state;

  second->updateDeformationChart();
  if (!verifyCharts(*second, {false, false, false, true}))
    return fail(QStringLiteral("new geometry escaped a remembered chart fold"));
  state.scrToImg.lens_correction.kr[1] = 0.01;
  state.scrToImg.tilt_x = 1;
  second->updateDeformationChart();
  if (!verifyCharts(*second, {true, true, false, true}))
    return false;
  toggleFor(*second, keys[4])->click();
  expected[4] = true;
  if (!verifyCharts(*second, {true, true, false, true}))
    return fail(QStringLiteral("expansion lost applicable diagnostic charts"));
  second->updateUI();
  if (!verify(*second, expected))
    return false;
  state = withGeometry;
  second->updateDeformationChart();
  if (!verifyCharts(*second, {false, false, false, true}))
    return fail(QStringLiteral("removed corrections left their charts visible"));
  state.scrToImg.coordinate1 = {0, 0};
  state.scrToImg.coordinate2 = {0, 0};
  second->updateDeformationChart();
  toggleFor(*second, keys[4])->click();
  toggleFor(*second, keys[4])->click();
  if (!verifyCharts(*second, {false, false, false, false}))
    return fail(QStringLiteral("expansion resurrected charts without geometry"));
  state = withGeometry;
  scan.reset();
  second->updateDeformationChart();
  if (!verifyCharts(*second, {false, false, false, false}))
    return fail(QStringLiteral("image removal left diagnostic charts visible"));

  state = initialState;
  second->updateUI();
  for (int i = 0; i < keys.size(); ++i) {
    if (toggleFor(*second, keys[i])->isChecked())
      toggleFor(*second, keys[i])->click();
    expected[i] = false;
  }
  QSettings().sync();
  first->updateUI();
  second->updateUI();
  if (!verify(*first, firstExpected) || !verify(*second, expected))
    return fail(QStringLiteral("one inspector changed another's local folds"));
  second.reset();
  auto reopened = createPanel();
  if (!verify(*reopened, expected)
      || !verifyMessages(*reopened, {true, true, true, true})
      || !verifyCharts(*reopened, {false, false, false, false}))
    return fail(QStringLiteral("recreated panel lost remembered folds"));
  for (const QString &key : keys)
    if (QSettings().value(settingKey(key), true).toBool())
      return fail(QStringLiteral("refresh overwrote an explicit fold preference"));
  if (documentEdits != 0 || fitRequests != 0 || state != initialState)
    return fail(QStringLiteral("folding or refresh entered document/fit state"));
  return true;
}

/** Verify capture-aware screen controls in Suggested image setup. */
bool initialSetupGuideScreenDetectionSmoke() {
  auto fail = [](const char *reason) {
    qCritical() << "Initial-setup screen smoke failed:" << reason;
    return false;
  };

  // Include the Bayer checkbox below the dynamic screen controls: this catches
  // the real dialog-layout regression where newly shown rows were clipped into
  // the next checkbox.
  InitialSetupGuideDialog mono(
      nullptr, true, true, true, false, false, false, false, false, nullptr,
      colorscreen::render_parameters::capture_unknown, colorscreen::NoScreen);
  auto *capture = mono.findChild<QComboBox *>(
      QStringLiteral("InitialCaptureTypeCombo"));
  auto *screen = mono.findChild<QComboBox *>(
      QStringLiteral("InitialScreenTypeCombo"));
  auto *screenRow = mono.findChild<QWidget *>(
      QStringLiteral("InitialScreenTypeRow"));
  auto *preferred = mono.findChild<QCheckBox *>(
      QStringLiteral("InitialPreferredColorModelCheck"));
  auto *detect = mono.findChild<QCheckBox *>(
      QStringLiteral("InitialAutoDetectScreenCheck"));
  auto *bayer = mono.findChild<QCheckBox *>(
      QStringLiteral("InitialMonochromeBayerCheck"));
  if (!capture || !screen || !screenRow || !preferred || !detect || !bayer)
    return fail("screen setup controls are missing");

  mono.show();
  QCoreApplication::processEvents();

  int monoCapture = capture->findData(
      (int)colorscreen::render_parameters::capture_transparency);
  if (monoCapture < 0)
    return fail("monochrome screened capture is not offered");
  capture->setCurrentIndex(monoCapture);
  QCoreApplication::processEvents();
  if (screenRow->isHidden() || detect->isHidden() || detect->isEnabled() ||
      !preferred->isHidden() || mono.automaticallyDetectScreen())
    return fail("monochrome detection did not wait for a screen type");

  const int dufayScreen = screen->findData((int)colorscreen::Dufay);
  if (dufayScreen < 0)
    return fail("Dufay is missing from regular screen choices");
  if (screen->itemIcon(dufayScreen).isNull())
    return fail("initial screen selector lost Screen-panel pattern icons");

  screen->setCurrentIndex(dufayScreen);
  QCoreApplication::processEvents();
  QCoreApplication::processEvents();
  if (!detect->isEnabled() || !mono.automaticallyDetectScreen() ||
      mono.selectedScreenType() != colorscreen::Dufay)
    return fail("regular screen selection did not enable autodetection");
  if (preferred->isHidden() || !preferred->isChecked() ||
      !mono.usePreferredColorModel() || preferred->text().isEmpty())
    return fail("Dufay selection did not offer its preferred color model");

  // Every dynamically inserted row must occupy independent dialog geometry.
  if (screenRow->geometry().bottom() >= preferred->geometry().top() ||
      preferred->geometry().bottom() >= detect->geometry().top() ||
      detect->geometry().bottom() >= bayer->geometry().top())
    return fail("dynamic screen setup controls overlap following checkboxes");

  // If screen colours are visible in RGB, common screen types can be
  // identified automatically. Do not make the user choose a screen first.
  InitialSetupGuideDialog visible(
      nullptr, false, false, false, false, false, false, false, false, nullptr,
      colorscreen::render_parameters::capture_transparency_with_screen,
      colorscreen::NoScreen);
  auto *visibleScreenRow = visible.findChild<QWidget *>(
      QStringLiteral("InitialScreenTypeRow"));
  auto *visiblePreferred = visible.findChild<QCheckBox *>(
      QStringLiteral("InitialPreferredColorModelCheck"));
  auto *visibleDetect = visible.findChild<QCheckBox *>(
      QStringLiteral("InitialAutoDetectScreenCheck"));
  if (!visibleScreenRow || !visiblePreferred || !visibleDetect ||
      !visibleScreenRow->isHidden() || !visiblePreferred->isHidden() ||
      visibleDetect->isHidden() || !visibleDetect->isEnabled() ||
      !visible.automaticallyDetectScreen())
    return fail("visible-screen capture still requires manual screen selection");

  // Ordinary images have no historical screen workflow at all.
  InitialSetupGuideDialog plain(
      nullptr, false, false, false, false, false, false, false, false, nullptr,
      colorscreen::render_parameters::capture_plain_image,
      colorscreen::NoScreen);
  auto *plainDetect = plain.findChild<QCheckBox *>(
      QStringLiteral("InitialAutoDetectScreenCheck"));
  if (!plainDetect || !plainDetect->isHidden() ||
      plain.automaticallyDetectScreen())
    return fail("ordinary image exposes screen autodetection");

  return true;
}

/** Exercise real Profile section ownership and persisted folding. */
bool profileSectionPreferencesSmoke() {
  auto fail = [](const QString &reason) {
    qCritical() << "Profile section smoke failed:" << reason;
    return false;
  };

  QTemporaryDir temporary;
  if (!temporary.isValid())
    return fail(QStringLiteral("could not allocate isolated settings identity"));
  struct SettingsIdentityGuard {
    QString organization = QCoreApplication::organizationName();
    QString domain = QCoreApplication::organizationDomain();
    QString application = QCoreApplication::applicationName();
    /** Remove probe settings and restore the application identity. */
    ~SettingsIdentityGuard() {
      QSettings settings;
      settings.clear();
      settings.sync();
      QCoreApplication::setOrganizationName(organization);
      QCoreApplication::setOrganizationDomain(domain);
      QCoreApplication::setApplicationName(application);
    }
  } settingsGuard;
  const QString identity = QStringLiteral("ColorScreenProfileSmoke-%1")
      .arg(QFileInfo(temporary.path()).fileName());
  QCoreApplication::setOrganizationName(identity);
  QCoreApplication::setOrganizationDomain(identity + QStringLiteral(".invalid"));
  QCoreApplication::setApplicationName(QStringLiteral("ProfileSections"));

  ParameterState state;
  state.scrToImg.type = colorscreen::Dufay;
  state.scrToImg.coordinate1 = {8, 0};
  state.scrToImg.coordinate2 = {0, 8};
  const ParameterState initialState = state;
  int documentEdits = 0;
  int optimizeRequests = 0;
  int addSpotRequests = 0;
  auto getState = [&]() { return state; };
  auto setState = [&](const ParameterState &, const QString &, const QString &) {
    ++documentEdits;
  };
  auto noImage = []() { return std::shared_ptr<colorscreen::image_data>(); };
  auto createPanel = [&]() {
    auto panel = std::make_unique<ProfilePanel>(getState, setState, noImage);
    QObject::connect(panel.get(), &ProfilePanel::optimizeColorRequested,
                     panel.get(), [&](bool) { ++optimizeRequests; });
    QObject::connect(panel.get(), &ProfilePanel::addSpotModeRequested,
                     panel.get(), [&](bool) { ++addSpotRequests; });
    return panel;
  };

  const QString spotsKey = QStringLiteral("profile.spots");
  const QString optimizationKey = QStringLiteral("profile.optimization");
  auto settingKey = [](const QString &key) {
    return QStringLiteral("inspector/sections/%1/expanded").arg(key);
  };
  auto toggleFor = [](ProfilePanel &panel, const QString &key) {
    QToolButton *result = nullptr;
    for (auto *button : panel.findChildren<QToolButton *>()) {
      if (button->property("sectionKey").toString() == key) {
        if (result)
          return static_cast<QToolButton *>(nullptr);
        result = button;
      }
    }
    return result;
  };
  auto groupFor = [](ProfilePanel &panel, const QString &key) {
    QGroupBox *result = nullptr;
    for (auto *group : panel.findChildren<QGroupBox *>()) {
      if (group->property("sectionKey").toString() == key) {
        if (result)
          return static_cast<QGroupBox *>(nullptr);
        result = group;
      }
    }
    return result;
  };
  auto verifyOwnership = [&](ProfilePanel &panel) {
    auto *spots = groupFor(panel, spotsKey);
    auto *optimization = groupFor(panel, optimizationKey);
    auto *spotCount =
        panel.findChild<QLabel *>(QStringLiteral("ProfileSpotCount"));
    auto *showSpots =
        panel.findChild<QCheckBox *>(QStringLiteral("ProfileShowSpotsCheck"));
    auto *optimize =
        panel.findChild<QPushButton *>(QStringLiteral("ProfileOptimizeButton"));
    auto *status =
        panel.findChild<QLabel *>(QStringLiteral("ProfileCalibrationStatus"));
    auto *quality =
        panel.findChild<QLabel *>(QStringLiteral("ProfileCalibrationQuality"));
    if (!spots || !optimization || !spotCount || !showSpots || !optimize
        || !status || !quality || !spots->isAncestorOf(spotCount)
        || !spots->isAncestorOf(showSpots)
        || !optimization->isAncestorOf(optimize)
        || !optimization->isAncestorOf(status)
        || !optimization->isAncestorOf(quality))
      return fail(QStringLiteral("Profile controls escaped their sections"));
    return true;
  };

  auto first = createPanel();
  if (!verifyOwnership(*first))
    return false;
  auto *firstSpots = toggleFor(*first, spotsKey);
  auto *firstOptimization = toggleFor(*first, optimizationKey);
  if (!firstSpots || !firstOptimization || !firstSpots->isChecked()
      || !firstOptimization->isChecked()
      || QSettings().contains(settingKey(spotsKey))
      || QSettings().contains(settingKey(optimizationKey)))
    return fail(QStringLiteral("Profile initial section state is incorrect"));

  firstSpots->click();
  first->updateUI();
  auto *firstSpotCount =
      first->findChild<QLabel *>(QStringLiteral("ProfileSpotCount"));
  if (!firstSpotCount || !firstSpotCount->isHidden()
      || QSettings().value(settingKey(spotsKey), true).toBool())
    return fail(QStringLiteral("Profile spots fold did not persist"));

  auto second = createPanel();
  if (!verifyOwnership(*second))
    return false;
  auto *secondSpots = toggleFor(*second, spotsKey);
  auto *secondOptimization = toggleFor(*second, optimizationKey);
  auto *secondSpotCount =
      second->findChild<QLabel *>(QStringLiteral("ProfileSpotCount"));
  auto *secondOptimize =
      second->findChild<QPushButton *>(QStringLiteral("ProfileOptimizeButton"));
  if (!secondSpots || !secondOptimization || !secondSpotCount || !secondOptimize
      || secondSpots->isChecked() || !secondSpotCount->isHidden()
      || !secondOptimization->isChecked() || secondOptimize->isHidden())
    return fail(QStringLiteral("Recreated Profile panel lost saved fold"));

  // A programmatic change is local and does not become an application choice.
  firstSpots->setChecked(true);
  first->updateUI();
  if (!firstSpots->isChecked() || firstSpotCount->isHidden()
      || secondSpots->isChecked()
      || QSettings().value(settingKey(spotsKey), true).toBool())
    return fail(QStringLiteral("Programmatic Profile fold leaked between panels"));

  secondOptimization->click();
  second->updateUI();
  if (secondOptimization->isChecked() || !secondOptimize->isHidden()
      || QSettings().value(settingKey(optimizationKey), true).toBool())
    return fail(QStringLiteral("Profile optimization fold did not persist"));

  second.reset();
  auto reopened = createPanel();
  if (!verifyOwnership(*reopened))
    return false;
  auto *reopenedSpots = toggleFor(*reopened, spotsKey);
  auto *reopenedOptimization = toggleFor(*reopened, optimizationKey);
  if (!reopenedSpots || !reopenedOptimization || reopenedSpots->isChecked()
      || reopenedOptimization->isChecked())
    return fail(QStringLiteral("Profile fold preferences were not restored"));

  if (documentEdits != 0 || optimizeRequests != 0 || addSpotRequests != 0
      || state != initialState)
    return fail(QStringLiteral(
        "Profile construction/folding entered document or operation state"));
  return true;
}

/** Exercise opt-in default/modified/Reset for saved discrete values. */
bool discreteDefaultPresentationSmoke() {
  auto fail = [](const char *reason) {
    qCritical() << "Discrete-default smoke failed:" << reason;
    return false;
  };

  ParameterState state;
  int edits = 0;
  auto getState = [&]() { return state; };
  auto setState = [&](const ParameterState &next, const QString &,
                      const QString &) {
    state = next;
    ++edits;
  };
  auto noImage = []() { return std::shared_ptr<colorscreen::image_data>(); };

  CheckboxSemanticsProbe panel(getState, setState, noImage, nullptr, false);
  const ParameterState defaults;
  QComboBox *mode = panel.addEnumParameter(
      QStringLiteral("Mode"),
      {{0, QStringLiteral("Zero")}, {1, QStringLiteral("One")}},
      [](const ParameterState &s) { return s.rparams.scan_mirror ? 1 : 0; },
      [](ParameterState &s, int value) { s.rparams.scan_mirror = value != 0; },
      nullptr, QString(), QStringLiteral("smoke.discrete.mode"), true);
  QCheckBox *mirror = panel.addCheckboxParameter(
      QStringLiteral("Mirror"),
      [](const ParameterState &s) { return s.scrToImg.final_mirror; },
      [](ParameterState &s, bool value) { s.scrToImg.final_mirror = value; },
      nullptr, QString(), QStringLiteral("smoke.discrete.mirror"), true);
  panel.updateUI();

  auto resetFor = [&panel](const QString &key) {
    for (QToolButton *button : panel.findChildren<QToolButton *>())
      if (button->objectName() == QStringLiteral("ParameterResetButton") &&
          button->property("parameterKey").toString() == key)
        return button;
    return static_cast<QToolButton *>(nullptr);
  };
  QToolButton *modeReset = resetFor(QStringLiteral("smoke.discrete.mode"));
  QToolButton *mirrorReset =
      resetFor(QStringLiteral("smoke.discrete.mirror"));
  QWidget *modeField = mode ? mode->parentWidget() : nullptr;
  QWidget *mirrorField = mirror ? mirror->parentWidget() : nullptr;
  if (!mode || !mirror || !modeReset || !mirrorReset || !modeField ||
      !mirrorField || !modeReset->isHidden() || !mirrorReset->isHidden() ||
      modeField->property("parameterModified").toBool() ||
      mirrorField->property("parameterModified").toBool())
    return fail("default discrete values did not start unmodified");

  const int nonDefaultMode = defaults.rparams.scan_mirror ? 0 : 1;
  mode->setCurrentIndex(mode->findData(nonDefaultMode));
  QMetaObject::invokeMethod(mode, "activated", Qt::DirectConnection,
                            Q_ARG(int, mode->currentIndex()));
  mirror->setChecked(!defaults.scrToImg.final_mirror);
  panel.updateUI();
  if (modeReset->isHidden() || mirrorReset->isHidden() ||
      !modeField->property("parameterModified").toBool() ||
      !mirrorField->property("parameterModified").toBool())
    return fail("modified discrete values did not expose Reset");

  modeReset->click();
  mirrorReset->click();
  panel.updateUI();
  if (state.rparams.scan_mirror != defaults.rparams.scan_mirror ||
      state.scrToImg.final_mirror != defaults.scrToImg.final_mirror ||
      !modeReset->isHidden() || !mirrorReset->isHidden() ||
      modeField->property("parameterModified").toBool() ||
      mirrorField->property("parameterModified").toBool() || edits != 4)
    return fail("discrete Reset did not restore fresh defaults");

  return true;
}

/** Exercise beta-critical non-rendering UI/document invariants. */
bool runBetaInvariantSmoke() {
  auto fail = [](const char *reason) {
    qCritical() << "Beta invariant smoke failed:" << reason;
    return false;
  };

  if (!backgroundThreadRegistryShutdownSmoke()
      || !discreteDefaultPresentationSmoke()
      || !parameterSectionPreferencesSmoke()
      || !colorSectionPreferencesSmoke()
      || !geometrySectionPreferencesSmoke()
      || !profileSectionPreferencesSmoke()
      || !initialSetupGuideScreenDetectionSmoke())
    return false;

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

  // Checkbox enabledCheck must match the rest of ParameterPanel: a missing
  // prerequisite disables a still-visible row. Logical disappearance is an
  // explicit setParameterApplicability() decision, never an overloaded meaning
  // of the helper argument.
  ParameterState checkboxProbeState;
  bool checkboxPrerequisite = false;
  CheckboxSemanticsProbe checkboxProbe(
      [&checkboxProbeState]() { return checkboxProbeState; },
      [&checkboxProbeState](const ParameterState &state, const QString &,
                            const QString &) { checkboxProbeState = state; },
      []() { return std::shared_ptr<colorscreen::image_data>(); }, nullptr,
      false);
  auto checkboxEnabled = [&checkboxPrerequisite](const ParameterState &) {
    return checkboxPrerequisite;
  };
  QCheckBox *plainCheckbox = checkboxProbe.addCheckboxParameter(
      QStringLiteral("Plain checkbox"),
      [](const ParameterState &state) { return state.rparams.scan_mirror; },
      [](ParameterState &state, bool value) { state.rparams.scan_mirror = value; },
      checkboxEnabled);
  QCheckBox *resetCheckbox = checkboxProbe.addCheckboxWithReset(
      QStringLiteral("Reset checkbox"),
      [](const ParameterState &state) { return state.scrToImg.final_mirror; },
      [](ParameterState &state, bool value) { state.scrToImg.final_mirror = value; },
      [](ParameterState &state) { state.scrToImg.final_mirror = false; },
      checkboxEnabled);
  checkboxProbe.updateUI();
  if (plainCheckbox->isHidden() || resetCheckbox->isHidden() ||
      plainCheckbox->isEnabled() || resetCheckbox->isEnabled())
    return fail("checkbox enabledCheck still changed visibility semantics");
  checkboxPrerequisite = true;
  checkboxProbe.updateUI();
  if (plainCheckbox->isHidden() || resetCheckbox->isHidden() ||
      !plainCheckbox->isEnabled() || !resetCheckbox->isEnabled())
    return fail("checkbox enabledCheck did not restore enablement");

  // Whole addSeparator() groups are form rows too. Applicability must hide the
  // group without changing its independent expanded/collapsed state.
  bool sectionApplicable = true;
  QToolButton *sectionToggle =
      checkboxProbe.addSeparator(QStringLiteral("Applicable section"));
  QWidget *sectionGroup = sectionToggle ? sectionToggle->parentWidget() : nullptr;
  if (sectionGroup)
    sectionGroup = sectionGroup->parentWidget();
  QCheckBox *sectionCheckbox = checkboxProbe.addCheckboxParameter(
      QStringLiteral("Section child"),
      [](const ParameterState &) { return false; },
      [](ParameterState &, bool) {});
  QWidget *sectionRow = sectionCheckbox ? sectionCheckbox->parentWidget() : nullptr;
  checkboxProbe.setParameterApplicability(
      sectionGroup, [&sectionApplicable](const ParameterState &) {
        return sectionApplicable;
      });
  checkboxProbe.updateUI();
  if (!sectionGroup || !sectionToggle || !sectionRow ||
      sectionGroup->isHidden() ||
      !sectionGroup->property("parameterApplicable").toBool())
    return fail("section applicability did not expose an applicable group");
  sectionToggle->setChecked(false);
  sectionApplicable = false;
  checkboxProbe.updateUI();
  if (!sectionGroup->isHidden() || sectionToggle->isChecked() ||
      sectionGroup->property("parameterApplicable").toBool())
    return fail("section applicability changed collapsed presentation state");
  sectionApplicable = true;
  checkboxProbe.updateUI();
  if (sectionGroup->isHidden() || sectionToggle->isChecked() ||
      !sectionGroup->property("parameterApplicable").toBool() ||
      !sectionRow->isHidden())
    return fail("restored section applicability resurrected collapsed content");


  // Linked RGB controls are one visual helper but three saved parameters.
  // Different channels need different Undo merge identities, while Link
  // channels itself is presentation-only and must never acquire one.
  ParameterState rgbProbeState;
  QStringList rgbAppliedKeys;
  CheckboxSemanticsProbe rgbProbe(
      [&rgbProbeState]() { return rgbProbeState; },
      [&rgbProbeState, &rgbAppliedKeys](const ParameterState &state,
                                        const QString &, const QString &key) {
        rgbProbeState = state;
        rgbAppliedKeys.append(key);
      },
      []() { return std::shared_ptr<colorscreen::image_data>(); }, nullptr,
      false);
  rgbProbe.addCorrelatedRGBParameter(
      QStringLiteral("Probe RGB"), -10.0, 110.0, 1.0, 0,
      QStringLiteral("%"),
      [](const ParameterState &state) { return state.rparams.age * 100.0; },
      [](ParameterState &state, const colorscreen::rgbdata &value) {
        state.rparams.age = value / 100.0;
      },
      nullptr, QString(), QStringLiteral("probe.rgb"));
  rgbProbe.updateUI();
  auto findRgbSpin = [&rgbProbe](const QString &key) {
    const QList<QDoubleSpinBox *> spins =
        rgbProbe.findChildren<QDoubleSpinBox *>();
    for (QDoubleSpinBox *spin : spins)
      if (spin && spin->property("parameterKey").toString() == key)
        return spin;
    return static_cast<QDoubleSpinBox *>(nullptr);
  };
  QDoubleSpinBox *redRgb = findRgbSpin(QStringLiteral("probe.rgb.red"));
  QDoubleSpinBox *greenRgb = findRgbSpin(QStringLiteral("probe.rgb.green"));
  QDoubleSpinBox *blueRgb = findRgbSpin(QStringLiteral("probe.rgb.blue"));
  const QList<QCheckBox *> rgbChecks = rgbProbe.findChildren<QCheckBox *>();
  if (!redRgb || !greenRgb || !blueRgb || rgbChecks.size() != 1 ||
      !rgbChecks.constFirst()->property("parameterKey").toString().isEmpty())
    return fail("correlated RGB helper lost channel/presentation key ownership");
  rgbChecks.constFirst()->setChecked(false);
  const auto nextRgbValue = [](double value) {
    return value < 109.0 ? value + 1.0 : value - 1.0;
  };
  rgbAppliedKeys.clear();
  redRgb->setValue(nextRgbValue(redRgb->value()));
  greenRgb->setValue(nextRgbValue(greenRgb->value()));
  if (rgbAppliedKeys.size() != 2 ||
      rgbAppliedKeys[0] != QStringLiteral("probe.rgb.red") ||
      rgbAppliedKeys[1] != QStringLiteral("probe.rgb.green"))
    return fail("correlated RGB helper merged identity across saved channels");

  // A tile preview can change its logical tile set without changing the number
  // of tiles (for example RGB/IR presentation changes). Rebuild must compare
  // the full type/caption definition rather than only the widget count.
  {
    ParameterState tileState;
    TileDefinitionProbe tileProbe(
        [&tileState]() { return tileState; },
        [&tileState](const ParameterState &state, const QString &,
                     const QString &) { tileState = state; },
        []() { return std::shared_ptr<colorscreen::image_data>(); });
    tileProbe.initialize();

    auto captions = [&tileProbe]() {
      QStringList result;
      QWidget *tiles = tileProbe.getTilesWidget();
      if (!tiles)
        return result;
      for (QLabel *label : tiles->findChildren<QLabel *>())
        result.append(label->text());
      return result;
    };

    if (captions() !=
        QStringList{QStringLiteral("Before A"), QStringLiteral("Before B")})
      return fail("tile preview did not build its initial definition");

    tileProbe.useAlternateDefinitions();
    if (captions() !=
        QStringList{QStringLiteral("After A"), QStringLiteral("After B")})
      return fail("same-sized tile definition change kept stale captions");
  }

  // Stateful and stateless sliders share one conversion utility. Exercise the
  // typed stateful controls against the stateless wrapper for every supported
  // mapping mode so future changes cannot make their geometry diverge.
  auto sliderMappingsAgree = [](double min, double max, double scale,
                                double gamma, bool logarithmic) {
    ParameterState state;
    state.rparams.gamma = min;
    double statelessValue = min;
    CheckboxSemanticsProbe probe(
        [&state]() { return state; },
        [&state](const ParameterState &next, const QString &,
                 const QString &) { state = next; },
        []() { return std::shared_ptr<colorscreen::image_data>(); }, nullptr,
        false);
    const auto stateful = probe.addSliderParameterControls(
        QStringLiteral("Stateful slider"), min, max, scale, 6, QString(),
        QString(),
        [](const ParameterState &current) { return current.rparams.gamma; },
        [](ParameterState &current, double value) {
          current.rparams.gamma = value;
        },
        gamma, nullptr, logarithmic);
    QWidget *stateless = probe.addSlider(
        QStringLiteral("Stateless slider"), min, max, scale, 6, QString(),
        QString(), min,
        [&statelessValue](double value) { statelessValue = value; }, gamma,
        logarithmic);
    probe.updateUI();

    QSlider *statefulSlider = stateful.slider;
    QSlider *statelessSlider =
        stateless ? stateless->findChild<QSlider *>() : nullptr;
    QDoubleSpinBox *statefulSpin = stateful.spin;
    QDoubleSpinBox *statelessSpin =
        stateless ? stateless->findChild<QDoubleSpinBox *>() : nullptr;
    if (!stateful.container || !statefulSlider || !statelessSlider ||
        !statefulSpin || !statelessSpin ||
        statefulSlider->minimum() != statelessSlider->minimum() ||
        statefulSlider->maximum() != statelessSlider->maximum())
      return false;

    const int sliderMin = statefulSlider->minimum();
    const int sliderMax = statefulSlider->maximum();
    const int sliderMid = sliderMin + (sliderMax - sliderMin) / 2;
    for (int position : {sliderMin, sliderMid, sliderMax}) {
      statefulSlider->setValue(position);
      statelessSlider->setValue(position);
      if (qAbs(statefulSpin->value() - statelessSpin->value()) > 1e-6 ||
          qAbs(state.rparams.gamma - statelessValue) > 1e-6)
        return false;
    }

    for (double value : {min, min + (max - min) * 0.37, max}) {
      statefulSpin->setValue(value);
      statelessSpin->setValue(value);
      if (statefulSlider->value() != statelessSlider->value())
        return false;
    }
    return true;
  };
  if (!sliderMappingsAgree(-2.0, 3.0, 100.0, 1.0, false) ||
      !sliderMappingsAgree(0.0, 100.0, 10.0, 2.5, false) ||
      !sliderMappingsAgree(0.0, 1000.0, 10.0, 1.0, true) ||
      !sliderMappingsAgree(0.1, 1000.0, 100.0, 1.0, true))
    return fail("stateful/stateless slider mappings diverged");

  ParameterState sentinelState;
  sentinelState.rparams.gamma = 0.0;
  CheckboxSemanticsProbe sentinelProbe(
      [&sentinelState]() { return sentinelState; },
      [&sentinelState](const ParameterState &next, const QString &,
                       const QString &) { sentinelState = next; },
      []() { return std::shared_ptr<colorscreen::image_data>(); }, nullptr,
      false);
  QWidget *sentinelField = sentinelProbe.addSliderParameter(
      QStringLiteral("Sentinel slider"), 400.0, 1200.0, 1.0, 2, QString(),
      QStringLiteral("automatic"),
      [](const ParameterState &current) { return current.rparams.gamma; },
      [](ParameterState &current, double value) {
        current.rparams.gamma = value;
      },
      2.0, nullptr, false, QString(), QString(), false, 0.0);
  sentinelProbe.updateUI();
  QSlider *sentinelSlider =
      sentinelField ? sentinelField->findChild<QSlider *>() : nullptr;
  QDoubleSpinBox *sentinelSpin =
      sentinelField ? sentinelField->findChild<QDoubleSpinBox *>() : nullptr;
  if (!sentinelSlider || !sentinelSpin || sentinelSlider->minimum() != 0 ||
      sentinelSlider->maximum() != 65535 || sentinelSlider->value() != 0 ||
      sentinelSpin->value() != 0.0)
    return fail("separated slider sentinel lost its reserved nonlinear position");
  sentinelSlider->setValue(1);
  if (qAbs(sentinelSpin->value() - 400.0) > 1e-6)
    return fail("first regular nonlinear slider position no longer maps to minimum");
  sentinelSpin->setValue(0.0);
  if (sentinelSlider->value() != 0)
    return fail("separated slider sentinel did not round-trip to reserved position");

  // Focus analysis is now a plain background helper. Missing input must fail
  // synchronously rather than depending on QObject/QThread signal delivery.
  colorscreen::finetune_parameters focusParams;
  const FocusAnalysisResult missingFocus = FocusAnalysisWorker::analyze(
      colorscreen::render_parameters(), colorscreen::scr_to_img_parameters(),
      std::shared_ptr<colorscreen::image_data>(), {0, 0}, focusParams, nullptr);
  if (missingFocus.success || missingFocus.cancelled)
    return fail("focus-analysis helper accepted a missing scan");

  const DetectScreenAnalysisResult missingDetection =
      DetectScreenWorker::analyze(
          colorscreen::scr_detect_parameters(), colorscreen::solver_parameters(),
          colorscreen::scr_to_img_parameters(), colorscreen::render_parameters(),
          std::shared_ptr<colorscreen::image_data>(), nullptr);
  if (missingDetection.success || missingDetection.cancelled ||
      missingDetection.screenMap || missingDetection.detected.smap)
    return fail("screen-detection helper accepted a missing scan");

  // Coordinate helpers must fail without a scan and short-circuit an already
  // cancelled request before touching an uninitialized image's pixel data.
  const colorscreen::scr_to_img_parameters coordinateInputs;
  const auto missingCoordinates = CoordinateOptimizationWorker::autodetect(
      coordinateInputs, colorscreen::render_parameters(), {}, nullptr);
  const auto missingRefinement = CoordinateOptimizationWorker::optimize(
      coordinateInputs, colorscreen::render_parameters(), {}, nullptr);
  if (missingCoordinates.success || missingCoordinates.cancelled ||
      missingCoordinates.coordinates != coordinateInputs ||
      missingRefinement.success || missingRefinement.cancelled ||
      missingRefinement.finetune.err.empty())
    return fail("coordinate helper accepted a missing scan");

  auto emptyScan = std::make_shared<colorscreen::image_data>();
  colorscreen::progress_info cancelledCoordinates;
  cancelledCoordinates.cancel();
  const auto cancelledDetection = CoordinateOptimizationWorker::autodetect(
      coordinateInputs, colorscreen::render_parameters(), emptyScan,
      &cancelledCoordinates);
  const auto cancelledRefinement = CoordinateOptimizationWorker::optimize(
      coordinateInputs, colorscreen::render_parameters(), emptyScan,
      &cancelledCoordinates);
  if (cancelledDetection.success || !cancelledDetection.cancelled ||
      cancelledDetection.coordinates != coordinateInputs ||
      cancelledRefinement.success || !cancelledRefinement.cancelled)
    return fail("coordinate helper ignored pre-dispatch cancellation");

  // Multi-area focus helpers share the one-shot lifecycle. Invalid or already
  // cancelled inputs must return before searching/rendering/fitting any pixels.
  const auto missingFocusAreas = FocusAnalysisWorker::findAreas(
      colorscreen::render_parameters(), coordinateInputs, {}, nullptr);
  const auto missingAreaFit = FocusAnalysisWorker::analyzeAreas(
      colorscreen::render_parameters(), coordinateInputs, {}, {},
      colorscreen::finetune_scanner_mtf_sigma, false, nullptr);
  if (missingFocusAreas.success || missingFocusAreas.cancelled ||
      missingFocusAreas.error.empty() || !missingFocusAreas.candidates.empty() ||
      missingAreaFit.success || missingAreaFit.cancelled ||
      missingAreaFit.error.empty() || !missingAreaFit.analysis.selected.empty())
    return fail("multi-area focus helper accepted a missing scan");
  const auto cancelledFocusAreas = FocusAnalysisWorker::findAreas(
      colorscreen::render_parameters(), coordinateInputs, emptyScan,
      &cancelledCoordinates);
  const auto cancelledAreaFit = FocusAnalysisWorker::analyzeAreas(
      colorscreen::render_parameters(), coordinateInputs, emptyScan, {},
      colorscreen::finetune_scanner_mtf_sigma, true, &cancelledCoordinates);
  if (cancelledFocusAreas.success || !cancelledFocusAreas.cancelled ||
      !cancelledFocusAreas.candidates.empty() || cancelledAreaFit.success ||
      !cancelledAreaFit.cancelled || !cancelledAreaFit.candidates.empty())
    return fail("multi-area focus helper ignored pre-dispatch cancellation");

  colorscreen::finetune_area_parameters finetuneAreaParams;
  const FinetuneAreaResult missingFinetune = FinetuneWorker::findPoints(
      colorscreen::solver_parameters(), colorscreen::render_parameters(),
      colorscreen::scr_to_img_parameters(),
      std::shared_ptr<colorscreen::image_data>(), {0, 0, 1, 1},
      finetuneAreaParams, nullptr);
  if (missingFinetune.success || missingFinetune.cancelled ||
      !missingFinetune.points.empty())
    return fail("finetune helper accepted a missing scan");

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

  // Tiles deliberately reuse one pair of editors for the currently selected
  // stitch tile. The parameter key must therefore include the tile coordinates:
  // a fast edit on tile 0 followed by the same visible Exposure control on tile
  // 1 is two independent Undo gestures, not one slider drag.
  auto stitchedScan = std::make_shared<colorscreen::image_data>();
  stitchedScan->stitch = new colorscreen::stitch_project();
  stitchedScan->stitch->params.width = 2;
  stitchedScan->stitch->params.height = 1;

  ParameterState tileBaseline = window.documentStateSnapshot();
  tileBaseline.rparams.set_tile_adjustments_dimensions(2, 1);
  window.applySharedDocumentState(tileBaseline, QStringLiteral("Tile smoke setup"),
                                  QStringLiteral("smoke.tiles.setup"));
  undoStack->clear();

  TilesPanel tiles(
      [&window]() { return window.documentStateSnapshot(); },
      [&window](const ParameterState &state, const QString &description,
                const QString &parameterKey) {
        window.applySharedDocumentState(state, description, parameterKey);
      },
      [stitchedScan]() { return stitchedScan; }, nullptr);
  tiles.updateForNewImage();
  tiles.updateUI();

  auto findTileSpin = [&tiles](const QString &parameterKey) {
    const QList<QDoubleSpinBox *> spins = tiles.findChildren<QDoubleSpinBox *>();
    for (QDoubleSpinBox *spin : spins)
      if (spin && spin->property("parameterKey").toString() == parameterKey)
        return spin;
    return static_cast<QDoubleSpinBox *>(nullptr);
  };
  auto findTileReset = [&tiles](const QString &parameterKey) {
    for (QToolButton *button : tiles.findChildren<QToolButton *>())
      if (button->objectName() == QStringLiteral("ParameterResetButton") &&
          button->property("parameterKey").toString() == parameterKey)
        return button;
    return static_cast<QToolButton *>(nullptr);
  };

  auto *tile0Selector =
      tiles.findChild<QPushButton *>(QStringLiteral("TileSelector_0_0"));
  auto *tile1Selector =
      tiles.findChild<QPushButton *>(QStringLiteral("TileSelector_1_0"));
  auto *tile0Enabled =
      tiles.findChild<QCheckBox *>(QStringLiteral("TileEnabled_0_0"));
  auto *tile1Enabled =
      tiles.findChild<QCheckBox *>(QStringLiteral("TileEnabled_1_0"));
  QDoubleSpinBox *exposure =
      findTileSpin(QStringLiteral("tiles.0.0.exposure"));
  QDoubleSpinBox *darkPoint =
      findTileSpin(QStringLiteral("tiles.0.0.dark_point"));
  QToolButton *tileAdjustmentsToggle = nullptr;
  QGroupBox *tileAdjustmentsGroup = nullptr;
  for (QToolButton *button : tiles.findChildren<QToolButton *>()) {
    if (button->property("sectionKey").toString() ==
        QStringLiteral("tiles.adjustments")) {
      if (tileAdjustmentsToggle)
        return fail("duplicate Tile adjustments section key");
      tileAdjustmentsToggle = button;
    }
  }
  for (QGroupBox *group : tiles.findChildren<QGroupBox *>()) {
    if (group->property("sectionKey").toString() ==
        QStringLiteral("tiles.adjustments")) {
      if (tileAdjustmentsGroup)
        return fail("duplicate Tile adjustments group key");
      tileAdjustmentsGroup = group;
    }
  }
  if (!tile0Selector || !tile1Selector || !tile0Enabled || !tile1Enabled ||
      !exposure || !darkPoint || !tileAdjustmentsToggle ||
      !tileAdjustmentsGroup ||
      tile0Enabled->property("parameterKey").toString() !=
          QStringLiteral("tiles.0.0.enabled") ||
      tile1Enabled->property("parameterKey").toString() !=
          QStringLiteral("tiles.1.0.enabled") ||
      tile0Selector->property("parameterKey").isValid() ||
      tile1Selector->property("parameterKey").isValid())
    return fail("tile keys crossed the document/selection-state boundary");

  // Folding is presentation-only and must own the shared tile editors without
  // touching the independent tile-selector grid or document state.
  const bool originalTileAdjustmentsExpanded =
      tileAdjustmentsToggle->isChecked();
  tileAdjustmentsToggle->setChecked(true);
  tiles.updateUI();
  if (!exposure->isVisibleTo(tileAdjustmentsGroup) ||
      !darkPoint->isVisibleTo(tileAdjustmentsGroup) ||
      tile0Selector->isHidden())
    return fail("expanded Tile adjustments did not expose its editors");
  tileAdjustmentsToggle->setChecked(false);
  tiles.updateUI();
  if (exposure->isVisibleTo(tileAdjustmentsGroup) ||
      darkPoint->isVisibleTo(tileAdjustmentsGroup) ||
      tile0Selector->isHidden())
    return fail("collapsed Tile adjustments did not own only its editors");
  tileAdjustmentsToggle->setChecked(true);
  tiles.updateUI();

  QToolButton *exposureReset =
      findTileReset(QStringLiteral("tiles.0.0.exposure"));
  QToolButton *darkPointReset =
      findTileReset(QStringLiteral("tiles.0.0.dark_point"));
  if (!exposureReset || !darkPointReset || !exposureReset->isHidden() ||
      !darkPointReset->isHidden() ||
      !exposureReset->property("parameterDefaultValue").isValid() ||
      !darkPointReset->property("parameterDefaultValue").isValid())
    return fail("default Tile adjustments did not expose reset metadata");

  const double tile0Before = exposure->value();
  const colorscreen::luminosity_t tile0After =
      static_cast<colorscreen::luminosity_t>(
          tile0Before <= 9.98 ? tile0Before + 0.01 : tile0Before - 0.01);
  exposure->setValue(tile0After);
  tiles.updateUI();
  if (exposureReset->isHidden() ||
      exposureReset->property("parameterKey").toString() !=
          QStringLiteral("tiles.0.0.exposure"))
    return fail("modified tile 0 did not expose its dynamic Reset");

  tile1Selector->setChecked(true);
  if (!tile1Selector->isChecked() ||
      exposure->property("parameterKey").toString() !=
          QStringLiteral("tiles.1.0.exposure") ||
      darkPoint->property("parameterKey").toString() !=
          QStringLiteral("tiles.1.0.dark_point") ||
      exposureReset->property("parameterKey").toString() !=
          QStringLiteral("tiles.1.0.exposure") ||
      darkPointReset->property("parameterKey").toString() !=
          QStringLiteral("tiles.1.0.dark_point") ||
      !exposureReset->isHidden() || !darkPointReset->isHidden())
    return fail("tile selector did not retarget shared Reset metadata");

  const double tile1Before = exposure->value();
  const colorscreen::luminosity_t tile1After =
      static_cast<colorscreen::luminosity_t>(
          tile1Before <= 9.96 ? tile1Before + 0.02 : tile1Before - 0.02);
  exposure->setValue(tile1After);
  tiles.updateUI();
  if (exposureReset->isHidden() ||
      exposureReset->property("parameterKey").toString() !=
          QStringLiteral("tiles.1.0.exposure"))
    return fail("modified tile 1 did not expose its dynamic Reset");

  exposureReset->click();
  tiles.updateUI();
  const ParameterState afterTileReset = window.documentStateSnapshot();
  if (undoStack->count() != 3 || !exposureReset->isHidden() ||
      std::abs(afterTileReset.rparams.get_tile_adjustment(0, 0).exposure -
               tile0After) > 1e-8 ||
      std::abs(afterTileReset.rparams.get_tile_adjustment(1, 0).exposure -
               tile1Before) > 1e-8)
    return fail("dynamic Tile Reset did not target only the selected tile");

  // Reset is deliberately its own Undo gesture, then the two keyed edits remain
  // independently undoable in reverse tile order.
  undoStack->undo();
  const ParameterState afterResetUndo = window.documentStateSnapshot();
  if (std::abs(afterResetUndo.rparams.get_tile_adjustment(0, 0).exposure -
               tile0After) > 1e-8 ||
      std::abs(afterResetUndo.rparams.get_tile_adjustment(1, 0).exposure -
               tile1After) > 1e-8)
    return fail("Undo did not restore the pre-Reset tile 1 value");

  undoStack->undo();
  const ParameterState afterTile1Undo = window.documentStateSnapshot();
  if (std::abs(afterTile1Undo.rparams.get_tile_adjustment(0, 0).exposure -
               tile0After) > 1e-8 ||
      std::abs(afterTile1Undo.rparams.get_tile_adjustment(1, 0).exposure -
               tile1Before) > 1e-8)
    return fail("Undo merged one shared tile editor across different tiles");

  undoStack->undo();
  const ParameterState afterTile0Undo = window.documentStateSnapshot();
  if (std::abs(afterTile0Undo.rparams.get_tile_adjustment(0, 0).exposure -
               tile0Before) > 1e-8 ||
      std::abs(afterTile0Undo.rparams.get_tile_adjustment(1, 0).exposure -
               tile1Before) > 1e-8)
    return fail("second tile Undo did not restore both exposure baselines");

  tileAdjustmentsToggle->setChecked(originalTileAdjustmentsExpanded);
  tiles.updateUI();
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
