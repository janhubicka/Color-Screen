#include "SharpnessPanel.h"
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/imagedata.h"
#include "../libcolorscreen/include/scr-to-img.h"
#include "MTFChartWidget.h"
#include "MTFFitDialog.h"
#include "FinetuneImagesPanel.h"
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <cmath>
#include <exception>
#include <string>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QString>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QIcon> // Added
#include <QLineEdit>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTimer>
#include <QTabWidget>
#include <QVBoxLayout> // Added
#include <QtConcurrent>
#include <QMimeData>
#include <QDrag>
#include <QMouseEvent>
#include <QSpinBox>
#include <QApplication>
#include "AdaptiveSharpeningChart.h"

using namespace colorscreen;
using sharpen_mode = colorscreen::sharpen_parameters::sharpen_mode;
using resampling_kernel
    = colorscreen::sharpen_parameters::resampling_kernel;

namespace {

/** Result transferred from the MTF fitting worker to the GUI thread.  */
struct MtfFitResult {
  colorscreen::mtf_parameters baseline;
  colorscreen::mtf_parameters input;
  colorscreen::mtf_parameters fitted;
  double objective = -1;
  size_t observations = 0;
  std::string error;
  bool cancelled = false;
};

/** Dialog exposing the adaptive-analysis controls also available to the
    command-line finetune/analyze-scanner-blur paths.  Operational settings are
    intentionally kept outside ParameterState because they describe one
    analysis run rather than persistent rendering state.  */
class AdaptiveSharpeningDialog : public QDialog {
public:
  AdaptiveSharpeningDialog(const AdaptiveSharpeningParameters &initial,
                           bool physicalFocusAvailable,
                           bool varyingStripWidths, bool hasRgb,
                           QWidget *parent = nullptr)
      : QDialog(parent), m_physicalFocusAvailable(physicalFocusAvailable),
        m_varyingStripWidths(varyingStripWidths), m_hasRgb(hasRgb) {
    setWindowTitle(tr("Adaptive sharpening analysis"));
    setModal(true);
    setSizeGripEnabled(true);

    auto *mainLayout = new QVBoxLayout(this);
    auto *description = new QLabel(
        tr("Choose the parameters fitted at every sample and the sampling "
           "grids. Zero in an automatic dimension lets the library derive it "
           "from the image aspect ratio or the paired value."), this);
    description->setWordWrap(true);
    mainLayout->addWidget(description);

    auto *tabs = new QTabWidget(this);
    mainLayout->addWidget(tabs, 1);

    auto *fitGroup = new QGroupBox(tr("Parameters to optimize"), this);
    auto *fitForm = new QFormLayout(fitGroup);
    m_correctionCombo = new QComboBox(fitGroup);
    m_correctionCombo->addItem(
        tr("Scanner/camera defocus (or fallback blur diameter)"),
        QVariant::fromValue(static_cast<qulonglong>(colorscreen::finetune_scanner_mtf_defocus)));
    m_correctionCombo->addItem(
        tr("Scanner/camera defocus per channel"),
        QVariant::fromValue(static_cast<qulonglong>(colorscreen::finetune_scanner_mtf_channel_defocus)));
    m_correctionCombo->addItem(
        tr("Legacy screen blur"),
        QVariant::fromValue(static_cast<qulonglong>(colorscreen::finetune_screen_blur)));
    m_correctionCombo->addItem(
        tr("Legacy screen blur per channel"),
        QVariant::fromValue(static_cast<qulonglong>(colorscreen::finetune_screen_channel_blurs)));
    const uint64_t correctionMask = colorscreen::finetune_screen_blur
        | colorscreen::finetune_screen_channel_blurs
        | colorscreen::finetune_scanner_mtf_defocus
        | colorscreen::finetune_scanner_mtf_channel_defocus;
    const uint64_t initialCorrection = initial.flags & correctionMask;
    for (int i = 0; i < m_correctionCombo->count(); ++i)
      if (m_correctionCombo->itemData(i).toULongLong() == initialCorrection) {
        m_correctionCombo->setCurrentIndex(i);
        break;
      }
    m_correctionCombo->setToolTip(
        tr("The correction table stores one scalar blur/focus family. "
           "Per-channel fits are currently reduced to their mean when stored."));
    fitForm->addRow(tr("Correction:"), m_correctionCombo);

    m_positionCheck = addCheckBox(
        fitForm, tr("Refine screen position"),
        initial.flags & colorscreen::finetune_position,
        tr("Refine the exact local screen phase/translation for every fit."));
    m_sigmaCheck = addCheckBox(
        fitForm, tr("Optimize residual MTF sigma"),
        initial.flags & colorscreen::finetune_scanner_mtf_sigma,
        tr("Fit the residual Gaussian component together with scanner/camera focus."));
    m_fogCheck = addCheckBox(
        fitForm, tr("Optimize fog / dark point"),
        initial.flags & colorscreen::finetune_fog,
        tr("Fit the local fog/dark offset as an auxiliary parameter."));
    m_monochromeCheck = addCheckBox(
        fitForm, tr("Use monochrome / IR channel"),
        initial.flags & colorscreen::finetune_bw,
        tr("Use the measured monochrome/infrared channel when present; otherwise "
           "finetune derives grayscale from RGB."));
    if (!m_hasRgb) {
      m_monochromeCheck->setChecked(true);
      m_monochromeCheck->setEnabled(false);
      m_monochromeCheck->setToolTip(
          tr("The loaded scan has no RGB channels, so monochrome fitting is "
             "mandatory."));
    }
    m_simulatedInfraredCheck = addCheckBox(
        fitForm, tr("Simulate infrared layer"),
        initial.flags & colorscreen::finetune_simulate_infrared,
        tr("Experimental RGB objective that estimates a neutral simulated-IR layer."));
    m_normalizeCheck = addCheckBox(
        fitForm, tr("Normalize RGB colors"),
        !(initial.flags & colorscreen::finetune_no_normalize),
        tr("Remove the approximately neutral image layer before registration."));
    m_dataCollectionCheck = addCheckBox(
        fitForm, tr("Use data collection for colors"),
        !(initial.flags & colorscreen::finetune_no_data_collection),
        tr("Use the fast patch-color estimate when it is applicable."));
    m_leastSquaresCheck = addCheckBox(
        fitForm, tr("Use least-squares color fit"),
        !(initial.flags & colorscreen::finetune_no_least_squares),
        tr("Use variable projection for linear screen colors when applicable."));
    m_optimizeStripWidthsCheck = addCheckBox(
        fitForm, tr("Optimize strip widths in coarse prepass"),
        initial.optimizeStripWidthsInPrepass,
        tr("For Dufay and other variable-strip screens, fit strip widths in "
           "the exact coarse prepass. Disable this to keep the strip widths "
           "already present in the rendering parameters (or process defaults)."));
    m_reoptimizeStripWidthsCheck = addCheckBox(
        fitForm, tr("Reoptimize strip widths in dense pass"),
        initial.reoptimizeStripWidths,
        tr("For Dufay and other variable-strip screens, fit strip widths again "
           "inside every dense sample instead of fixing the robust prepass values."));
    if (!m_varyingStripWidths) {
      m_optimizeStripWidthsCheck->setChecked(false);
      m_optimizeStripWidthsCheck->setEnabled(false);
      m_optimizeStripWidthsCheck->setToolTip(
          tr("The selected screen process has no variable strip widths."));
      m_reoptimizeStripWidthsCheck->setChecked(false);
      m_reoptimizeStripWidthsCheck->setEnabled(false);
      m_reoptimizeStripWidthsCheck->setToolTip(
          tr("The selected screen process has no variable strip widths."));
    }
    auto *fitTab = new QWidget(tabs);
    auto *fitTabLayout = new QVBoxLayout(fitTab);
    fitTabLayout->addWidget(fitGroup);
    fitTabLayout->addStretch();
    tabs->addTab(fitTab, tr("Fitting"));

    auto *gridGroup = new QGroupBox(tr("Sampling grids"), this);
    auto *gridForm = new QFormLayout(gridGroup);
    m_xStepsSpin = addAutomaticSpin(gridForm, tr("Correction columns:"),
                                    initial.xSteps, 1000,
                                    tr("Width of the final correction table."));
    m_yStepsSpin = addAutomaticSpin(gridForm, tr("Correction rows:"),
                                    initial.ySteps, 1000,
                                    tr("Height of the final correction table; automatic preserves image aspect ratio."));
    m_xSubstepsSpin = addAutomaticSpin(gridForm, tr("Samples per cell, X:"),
                                       initial.xSubsteps, 100,
                                       tr("Independent horizontal finetune samples reduced into each correction cell; automatic defaults to 5."));
    m_ySubstepsSpin = addAutomaticSpin(gridForm, tr("Samples per cell, Y:"),
                                       initial.ySubsteps, 100,
                                       tr("Independent vertical finetune samples reduced into each correction cell; automatic follows X or defaults to 5."));
    m_stripXStepsSpin = addAutomaticSpin(gridForm, tr("Coarse prepass columns:"),
                                         initial.stripXSteps, 1000,
                                         tr("Horizontal samples used by the exact coarse blur/focus and strip-width prepass."));
    m_stripYStepsSpin = addAutomaticSpin(gridForm, tr("Coarse prepass rows:"),
                                         initial.stripYSteps, 1000,
                                         tr("Vertical samples used by the coarse prepass; automatic preserves image aspect ratio."));
    auto *reductionGroup = new QGroupBox(tr("Robust reduction"), this);
    auto *reductionForm = new QFormLayout(reductionGroup);
    m_skipMinSpin = new QDoubleSpinBox(reductionGroup);
    m_skipMinSpin->setRange(0.0, 50.0);
    m_skipMinSpin->setDecimals(2);
    m_skipMinSpin->setSuffix(tr(" %"));
    m_skipMinSpin->setValue(initial.skipMin);
    m_skipMinSpin->setToolTip(tr("Discard this fraction from the low end before robust averaging."));
    reductionForm->addRow(tr("Skip low:"), m_skipMinSpin);
    m_skipMaxSpin = new QDoubleSpinBox(reductionGroup);
    m_skipMaxSpin->setRange(0.0, 50.0);
    m_skipMaxSpin->setDecimals(2);
    m_skipMaxSpin->setSuffix(tr(" %"));
    m_skipMaxSpin->setValue(initial.skipMax);
    m_skipMaxSpin->setToolTip(tr("Discard this fraction from the high end / worst-fit tail."));
    reductionForm->addRow(tr("Skip high:"), m_skipMaxSpin);
    m_toleranceSpin = new QDoubleSpinBox(reductionGroup);
    m_toleranceSpin->setRange(-1.0, 10.0);
    m_toleranceSpin->setDecimals(4);
    m_toleranceSpin->setSpecialValueText(tr("disabled"));
    m_toleranceSpin->setValue(initial.tolerance);
    m_toleranceSpin->setToolTip(tr("Maximum accepted robust correction range within a cell. -1 disables the check."));
    reductionForm->addRow(tr("Correction tolerance:"), m_toleranceSpin);
    auto *samplingTab = new QWidget(tabs);
    auto *samplingTabLayout = new QVBoxLayout(samplingTab);
    samplingTabLayout->addWidget(gridGroup);
    samplingTabLayout->addWidget(reductionGroup);
    samplingTabLayout->addStretch();
    tabs->addTab(samplingTab, tr("Sampling"));

    auto *focusGroup = new QGroupBox(tr("Physical-focus acceleration"), this);
    auto *focusForm = new QFormLayout(focusGroup);
    m_interpolateFocusCheck = addCheckBox(
        focusForm, tr("Interpolate cached focus nodes"), initial.interpolateFocus,
        tr("Use nonlinear exact focus nodes and interpolate intermediate dense-pass "
           "screens. The coarse prepass and final selected solution remain exact."));
    m_focusMtfSpin = new QDoubleSpinBox(focusGroup);
    m_focusMtfSpin->setRange(0.001, 99.999);
    m_focusMtfSpin->setDecimals(3);
    m_focusMtfSpin->setSuffix(tr(" %"));
    m_focusMtfSpin->setValue(initial.focusMtfThreshold * 100.0);
    m_focusMtfSpin->setToolTip(tr("Stop the useful focus range at the first defocus where the screen-frequency system MTF reaches this magnitude."));
    focusForm->addRow(tr("Minimum screen-frequency MTF:"), m_focusMtfSpin);
    m_focusNodesSpin = new QSpinBox(focusGroup);
    m_focusNodesSpin->setRange(2, 64);
    m_focusNodesSpin->setValue(initial.focusInterpolationNodes);
    m_focusNodesSpin->setToolTip(tr("Number of quadratically spaced exact focus nodes, including both endpoints."));
    focusForm->addRow(tr("Exact focus nodes:"), m_focusNodesSpin);
    m_profileCheck = addCheckBox(
        focusForm, tr("Collect finetune profile"), initial.reportProfile,
        tr("Collect and print cache, FFT, objective and timing counters for this analysis."));
    auto *focusTab = new QWidget(tabs);
    auto *focusTabLayout = new QVBoxLayout(focusTab);
    focusTabLayout->addWidget(focusGroup);
    focusTabLayout->addStretch();
    tabs->addTab(focusTab, tr("Focus cache"));

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                     this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(m_buttons);

    connect(m_correctionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateAvailability(); });
    connect(m_sigmaCheck, &QCheckBox::toggled, this,
            [this](bool) { updateAvailability(); });
    connect(m_optimizeStripWidthsCheck, &QCheckBox::toggled, this,
            [this](bool) { updateAvailability(); });
    connect(m_reoptimizeStripWidthsCheck, &QCheckBox::toggled, this,
            [this](bool) { updateAvailability(); });
    connect(m_monochromeCheck, &QCheckBox::toggled, this,
            [this](bool) { updateAvailability(); });
    connect(m_simulatedInfraredCheck, &QCheckBox::toggled, this,
            [this](bool) { updateAvailability(); });
    connect(m_interpolateFocusCheck, &QCheckBox::toggled, this,
            [this](bool) { updateAvailability(); });
    connect(m_skipMinSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { updateAvailability(); });
    connect(m_skipMaxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { updateAvailability(); });

    updateAvailability();
    resize(620, 620);
  }

  /** Return the settings currently selected by the user.  */
  AdaptiveSharpeningParameters parameters() const {
    AdaptiveSharpeningParameters result;
    result.stripXSteps = m_stripXStepsSpin->value();
    result.stripYSteps = m_stripYStepsSpin->value();
    result.xSteps = m_xStepsSpin->value();
    result.ySteps = m_yStepsSpin->value();
    result.xSubsteps = m_xSubstepsSpin->value();
    result.ySubsteps = m_ySubstepsSpin->value();
    result.flags = m_correctionCombo->currentData().toULongLong();
    if (m_positionCheck->isChecked())
      result.flags |= colorscreen::finetune_position;
    if (m_sigmaCheck->isEnabled() && m_sigmaCheck->isChecked())
      result.flags |= colorscreen::finetune_scanner_mtf_sigma;
    if (m_fogCheck->isEnabled() && m_fogCheck->isChecked())
      result.flags |= colorscreen::finetune_fog;
    if (m_monochromeCheck->isChecked())
      result.flags |= colorscreen::finetune_bw;
    if (m_simulatedInfraredCheck->isChecked())
      result.flags |= colorscreen::finetune_simulate_infrared;
    if (!m_normalizeCheck->isChecked())
      result.flags |= colorscreen::finetune_no_normalize;
    if (!m_dataCollectionCheck->isChecked())
      result.flags |= colorscreen::finetune_no_data_collection;
    if (!m_leastSquaresCheck->isChecked())
      result.flags |= colorscreen::finetune_no_least_squares;
    result.optimizeStripWidthsInPrepass
        = m_optimizeStripWidthsCheck->isEnabled()
          && m_optimizeStripWidthsCheck->isChecked();
    result.reoptimizeStripWidths = m_reoptimizeStripWidthsCheck->isChecked();
    result.skipMin = m_skipMinSpin->value();
    result.skipMax = m_skipMaxSpin->value();
    result.tolerance = m_toleranceSpin->value();
    result.reportProfile = m_profileCheck->isChecked();
    result.interpolateFocus = m_interpolateFocusCheck->isEnabled()
                              && m_interpolateFocusCheck->isChecked();
    result.focusMtfThreshold = m_focusMtfSpin->value() / 100.0;
    result.focusInterpolationNodes = m_focusNodesSpin->value();
    return result;
  }

private:
  /** Add one check box ROW with VALUE and TOOLTIP.  */
  QCheckBox *addCheckBox(QFormLayout *form, const QString &label, bool value,
                         const QString &tooltip) {
    auto *check = new QCheckBox(label, form->parentWidget());
    check->setChecked(value);
    check->setToolTip(tooltip);
    form->addRow(QString(), check);
    return check;
  }

  /** Add an integer row where zero means that the library derives the value.  */
  QSpinBox *addAutomaticSpin(QFormLayout *form, const QString &label,
                             int value, int maximum,
                             const QString &tooltip) {
    auto *spin = new QSpinBox(form->parentWidget());
    spin->setRange(0, maximum);
    spin->setSpecialValueText(tr("automatic"));
    spin->setValue(value);
    spin->setToolTip(tooltip);
    form->addRow(label, spin);
    return spin;
  }

  /** Update controls whose validity depends on the selected fit parameters.  */
  void updateAvailability() {
    const uint64_t correction = m_correctionCombo->currentData().toULongLong();
    const bool mtfCorrection = correction == colorscreen::finetune_scanner_mtf_defocus
        || correction == colorscreen::finetune_scanner_mtf_channel_defocus;
    if (!mtfCorrection && m_sigmaCheck->isChecked())
      m_sigmaCheck->setChecked(false);
    m_sigmaCheck->setEnabled(mtfCorrection);

    /* Simulated IR is an RGB objective and is distinct from measured IR/BW.
       It also disables normalization and data collection internally.  Reflect
       those implications explicitly so the dialog does not advertise ignored
       settings.  */
    const bool canSimulateInfrared = m_hasRgb && !m_monochromeCheck->isChecked();
    if (!canSimulateInfrared && m_simulatedInfraredCheck->isChecked())
      m_simulatedInfraredCheck->setChecked(false);
    m_simulatedInfraredCheck->setEnabled(canSimulateInfrared);
    if (m_simulatedInfraredCheck->isChecked()) {
      m_normalizeCheck->setChecked(false);
      m_dataCollectionCheck->setChecked(false);
    }

    /* The BW/IR path has no RGB color vector, so fog and RGB normalization
       are ignored by finetune_solver.  Keep their checked state while they
       are disabled so switching back to RGB restores the user's choices.  */
    const bool monochrome = m_monochromeCheck->isChecked();
    m_fogCheck->setEnabled(!monochrome);
    m_normalizeCheck->setEnabled(!monochrome
                                 && !m_simulatedInfraredCheck->isChecked());
    m_dataCollectionCheck->setEnabled(!m_simulatedInfraredCheck->isChecked());

    const bool scalarPhysicalFocus
        = correction == colorscreen::finetune_scanner_mtf_defocus
          && m_physicalFocusAvailable;
    const bool interpolationCompatible
        = scalarPhysicalFocus && !m_sigmaCheck->isChecked()
          && !m_reoptimizeStripWidthsCheck->isChecked();
    if (!interpolationCompatible && m_interpolateFocusCheck->isChecked())
      m_interpolateFocusCheck->setChecked(false);
    m_interpolateFocusCheck->setEnabled(interpolationCompatible);
    const bool interpolationActive = interpolationCompatible
                                     && m_interpolateFocusCheck->isChecked();
    m_focusMtfSpin->setEnabled(interpolationActive);
    m_focusNodesSpin->setEnabled(interpolationActive);

    QString status;
    if (m_skipMinSpin->value() + m_skipMaxSpin->value() >= 100.0)
      status = tr("Skip-low and skip-high must add to less than 100%.");
    else if (!m_physicalFocusAvailable
             && correction == colorscreen::finetune_scanner_mtf_defocus)
      status = tr("Physical capture metadata are incomplete: this correction "
                  "mode will use the compact fallback blur diameter and focus "
                  "interpolation is unavailable.");
    else if (!m_varyingStripWidths)
      status = tr("The coarse prepass still estimates the global blur/focus; "
                  "the selected process has no variable strip widths.");
    else if (m_optimizeStripWidthsCheck->isChecked())
      status = tr("The coarse prepass determines robust strip widths and the "
                  "global blur/focus before the dense correction pass.");
    else if (m_reoptimizeStripWidthsCheck->isChecked())
      status = tr("The coarse prepass keeps the current strip widths; strip "
                  "widths are fitted independently in every dense sample.");
    else
      status = tr("Strip widths are kept fixed at the current rendering values "
                  "through both the coarse and dense passes.");
    m_statusLabel->setText(status);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(
        m_skipMinSpin->value() + m_skipMaxSpin->value() < 100.0);
  }

  bool m_physicalFocusAvailable = false;
  bool m_varyingStripWidths = false;
  bool m_hasRgb = false;
  QComboBox *m_correctionCombo = nullptr;
  QCheckBox *m_positionCheck = nullptr;
  QCheckBox *m_sigmaCheck = nullptr;
  QCheckBox *m_fogCheck = nullptr;
  QCheckBox *m_monochromeCheck = nullptr;
  QCheckBox *m_simulatedInfraredCheck = nullptr;
  QCheckBox *m_normalizeCheck = nullptr;
  QCheckBox *m_dataCollectionCheck = nullptr;
  QCheckBox *m_leastSquaresCheck = nullptr;
  QSpinBox *m_xStepsSpin = nullptr;
  QSpinBox *m_yStepsSpin = nullptr;
  QSpinBox *m_xSubstepsSpin = nullptr;
  QSpinBox *m_ySubstepsSpin = nullptr;
  QSpinBox *m_stripXStepsSpin = nullptr;
  QSpinBox *m_stripYStepsSpin = nullptr;
  QCheckBox *m_optimizeStripWidthsCheck = nullptr;
  QCheckBox *m_reoptimizeStripWidthsCheck = nullptr;
  QDoubleSpinBox *m_skipMinSpin = nullptr;
  QDoubleSpinBox *m_skipMaxSpin = nullptr;
  QDoubleSpinBox *m_toleranceSpin = nullptr;
  QCheckBox *m_interpolateFocusCheck = nullptr;
  QDoubleSpinBox *m_focusMtfSpin = nullptr;
  QSpinBox *m_focusNodesSpin = nullptr;
  QCheckBox *m_profileCheck = nullptr;
  QLabel *m_statusLabel = nullptr;
  QDialogButtonBox *m_buttons = nullptr;
};

// Helper for drag and drop reordering
class DragHandle : public QLabel {
public:
    DragHandle(int index, QWidget *parent = nullptr) : QLabel(parent), m_index(index) {
        setPixmap(QPixmap(":icons/hand.svg").scaled(16, 16, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        setFixedSize(24, 24);
        setAlignment(Qt::AlignCenter);
        setCursor(Qt::OpenHandCursor);
        setToolTip(tr("Drag to reorder"));
    }

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            QDrag *drag = new QDrag(this);
            QMimeData *mimeData = new QMimeData();
            mimeData->setData("application/x-mtf-measurement-index", QByteArray::number(m_index));
            drag->setMimeData(mimeData);
            
            // Create a preview pixmap of the row? For now just use the hand
            drag->setPixmap(pixmap());
            drag->setHotSpot(event->pos());
            
            setCursor(Qt::ClosedHandCursor);
            drag->exec(Qt::MoveAction);
            setCursor(Qt::OpenHandCursor);
        }
    }

private:
    int m_index;
};

class MeasurementContainer : public QWidget {
public:
    using ReorderCallback = std::function<void(int, int)>;
    MeasurementContainer(ReorderCallback onReorder, QWidget *parent = nullptr) 
        : QWidget(parent), m_onReorder(onReorder) {
        setAcceptDrops(true);
    }

protected:
    void dragEnterEvent(QDragEnterEvent *event) override {
        if (event->mimeData()->hasFormat("application/x-mtf-measurement-index"))
            event->acceptProposedAction();
    }

    void dragMoveEvent(QDragMoveEvent *event) override {
        event->acceptProposedAction();
    }

    void dropEvent(QDropEvent *event) override {
        bool ok;
        int fromIndex = event->mimeData()->data("application/x-mtf-measurement-index").toInt(&ok);
        if (ok) {
            // Find which row we dropped on. We can iterate through the layout.
            QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(this->layout());
            if (layout) {
                int toIndex = -1;
                for (int i = 0; i < layout->count(); ++i) {
                    QWidget *w = layout->itemAt(i)->widget();
                    if (w && event->position().toPoint().y() < w->geometry().bottom()) {
                        toIndex = i;
                        break;
                    }
                }
                
                // Adjustment for header row? The current implementation adds a header at index 0.
                if (toIndex != -1) {
                    // Header is at 0, measurements start at 1
                    int actualFrom = fromIndex;
                    int actualTo = std::max(0, toIndex - 1); // -1 because of header
                    
                    // Cap at measurement count
                    // We don't have the count here easily, but let the callback handle it.
                    if (actualFrom != actualTo) {
                        m_onReorder(actualFrom, actualTo);
                    }
                }
            }
        }
        event->acceptProposedAction();
    }

    ReorderCallback m_onReorder;
};


class DotSpreadPreviewPanel : public TilePreviewPanel {
public:
  DotSpreadPreviewPanel(StateGetter stateGetter, StateSetter stateSetter,
                     ImageGetter imageGetter, QWidget *parent = nullptr)
      : TilePreviewPanel(stateGetter, stateSetter, imageGetter, parent, false) {
    setDebounceInterval(5);
  }

  void init(const QString &title) { setupTiles(title); }

protected:
  std::vector<std::pair<render_screen_tile_type, QString>>
  getTileTypes() const override {
    return {{dot_spread, "Dot Spread"}};
  }

  bool shouldUpdateTiles(const ParameterState &state) override {
      // Dot spread might change if sharpening params change
      if (!state.rparams.sharpen.equal_p(m_lastSharpen))
         return true;
      return false;
  }

  void onTileUpdateScheduled() override {
    ParameterState state = m_stateGetter();
    m_lastSharpen = state.rparams.sharpen;
  }

  bool requiresScan() const override { return false; }
  
  bool isTileRenderingEnabled(const ParameterState &state) const override {
      // Always enabled regardless of scrToImg setting
      return true;
  }

private:
  colorscreen::sharpen_parameters m_lastSharpen;
};

} // namespace

SharpnessPanel::SharpnessPanel(StateGetter stateGetter, StateSetter stateSetter,
                               ImageGetter imageGetter, QWidget *parent)
    : TilePreviewPanel(stateGetter, stateSetter, imageGetter, parent),
      m_mtfFitQueue() {
  connect(&m_mtfFitQueue, &TaskQueue::progressStarted, this,
          &SharpnessPanel::progressStarted);
  connect(&m_mtfFitQueue, &TaskQueue::progressFinished, this,
          &SharpnessPanel::progressFinished);
  m_finetuneFlags = colorscreen::finetune_scanner_mtf_sigma |
                    colorscreen::finetune_scanner_mtf_defocus;
  setDebounceInterval(5);
  setupUi();
}

SharpnessPanel::~SharpnessPanel() = default;
void SharpnessPanel::setupUi() {
  // Screen tile previews
  setupTiles("Sharpness Preview");

  // Sharpen mode dropdown
  addEnumParameter<sharpen_mode, sharpen_parameters::sharpen_mode_names,
                   (int)sharpen_mode::sharpen_mode_max>(
      "Sharpen",
      [](const ParameterState &s) { return (int)s.rparams.sharpen.mode; },
      [](ParameterState &s, int v) {
        s.rparams.sharpen.mode = (sharpen_mode)v;
      }, nullptr, "Select the sharpening algorithm. \"None\" disables sharpening, \"Wiener\" and \"Richardson-Lucy\" use the MTF model, \"Unsharp mask\" is a classic edge enhancement.");

  QToolButton *separatorToggle = addSeparator("Scanner/Camera properties");

  m_diffractionNotice = new QLabel();
  m_diffractionNotice->setWordWrap(true);
  m_diffractionNotice->setTextFormat(Qt::RichText);
  QFont noticeFont = m_diffractionNotice->font();
  noticeFont.setItalic(true);
  noticeFont.setPointSize(noticeFont.pointSize() - 1);
  m_diffractionNotice->setFont(noticeFont);
  
  if (m_currentGroupForm)
      m_currentGroupForm->addRow(m_diffractionNotice);
  else
      m_form->addRow(m_diffractionNotice);

  // MTF Chart
  m_mtfChart = new MTFChartWidget();
  m_mtfChart->setMinimumHeight(250);

  // Create container for MTF
  QWidget *mtfWrapper = new QWidget();
  m_mtfContainer = new QVBoxLayout(mtfWrapper);
  m_mtfContainer->setContentsMargins(0, 0, 0, 0);

  QWidget *detachableMTF =
      createDetachableSection("MTF Chart", m_mtfChart, [this]() {
        emit detachMTFChartRequested(m_mtfChart);
      });
  m_mtfContainer->addWidget(detachableMTF);

  m_showSignedOtfCheck = new QCheckBox(tr("Show signed physical OTF"), mtfWrapper);
  m_showSignedOtfCheck->setToolTip(
      tr("Show the signed analytical system transfer predicted by the physical "
         "lens model. Measured slanted-edge curves remain MTF magnitudes; "
         "negative lobes are inferred from the fitted optical model."));
  connect(m_showSignedOtfCheck, &QCheckBox::toggled, m_mtfChart,
          &MTFChartWidget::setShowSignedOTF);
  m_mtfContainer->addWidget(m_showSignedOtfCheck);

  if (m_currentGroupForm)
    m_currentGroupForm->addRow(mtfWrapper);
  else
    m_form->addRow(mtfWrapper);
  updateMTFChart();

  DotSpreadPreviewPanel *dotSpread =
      new DotSpreadPreviewPanel(m_stateGetter, m_stateSetter, m_imageGetter);
  m_dotSpreadPanel = dotSpread;
  dotSpread->init("Dot Spread");
  connect(dotSpread, &TilePreviewPanel::detachTilesRequested, this,
          &SharpnessPanel::detachDotSpreadRequested);
  connect(dotSpread, &TilePreviewPanel::progressStarted, this, &SharpnessPanel::progressStarted);
  connect(dotSpread, &TilePreviewPanel::progressFinished, this, &SharpnessPanel::progressFinished);
  
  m_widgetStateUpdaters.push_back([dotSpread, this]() {
      dotSpread->updateUI();
  });

  if (m_currentGroupForm) m_currentGroupForm->addRow(dotSpread);
  else m_form->addRow(dotSpread);

  // Add "Use measured MTF" checkbox (visible only if measured data exists and
  // separator is open)
  addCheckboxParameter(
      "Use measured MTF",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.measured_mtf_idx >= 0;
      },
      [](ParameterState &s, bool v) {
        s.rparams.sharpen.scanner_mtf.measured_mtf_idx = v ? 0 : -1;
      },
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.measurements.size();
      }, "Use a measured MTF curve directly instead of the fitted analytical physical or fallback model.");


  // Gaussian blur (Sigma)
  // Range 0.0 - 20.0, Pixels.
  // Slider step? 3 decimal precision for entry, but slider might be coarser or
  // same? If we use scale 1000, slider has 20000 steps. That works efficiently
  // on modern computers.

  addSliderParameter(
      "Gaussian blur sigma", 0.0, 20.0, 1000.0, 3, "pixels", "",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.sigma;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf.sigma = v;
      }, 1.0, nullptr, false, "Residual compact Gaussian blur. In the physical model it is applied after diffraction and defocus; in the empirical fallback it is the compact core blur.");

  // Wavelength
  // Range 0.0 - 1200.0 (0.0 = unknown)
  addSliderParameter(
      "Wavelength", 0.0, 1200.0, 10.0, 1, "nm", "unknown",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.wavelength;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf.wavelength = v;
      }, 1.0, nullptr, false, "Peak wavelength in nanometers used for calculating diffraction-limited MTF. 0 means use the average wavelength from the Capture tab.");

  // Defocus
  // Range 0.0 - 10.0 mm
  // Non-linear Gamma 2.0 (slow start)
  // Enabled only if simulate_diffraction_p()
  addSliderParameter(
      "Defocus", 0.0, 10.0, 1000.0, 3, "mm", "",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.defocus;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf.defocus = v;
      },
      2.0, // Gamma
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.simulate_diffraction_p();
      }, false, "Image-plane focus displacement in millimeters. The physical model evaluates the signed incoherent OTF of a defocused circular pupil.");

  /* Broad halo parameters fitted by the physical model.  They are placed next
     to the other optical parameters so the result of the fitting dialog is
     immediately visible and can be adjusted without reopening the optimizer.
     A zero fraction disables the halo; the stored radius is then inactive but
     remains available as a starting value for a later fit.  */
  addSliderParameter(
      "Halo fraction", 0.0, 0.95, 10000.0, 4, "", "none",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.halo_fraction;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf.halo_fraction = v;
      },
      1.0,
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.simulate_diffraction_p();
      },
      false,
      "Fraction of optical energy redistributed into the broad symmetric halo. Zero disables the halo without discarding its radius.");

  addSliderParameter(
      "Halo radius", 0.0, 256.0, 1000.0, 3, "pixels", "not set",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.halo_sigma;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf.halo_sigma = v;
      },
      2.0,
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.simulate_diffraction_p();
      },
      false,
      "Gaussian standard deviation of the broad symmetric halo in output pixels. It has no effect while the halo fraction is zero.");

  // Blur diameter
  // Range 0.0 - 20.0 pixels
  // Non-linear Gamma 2.0 (slow start)
  // Enabled only if !simulate_diffraction_p()
  addSliderParameter(
      "Blur diameter", 0.0, 20.0, 1000.0, 2, "pixels", "",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.blur_diameter;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf.blur_diameter = v;
      },
      2.0, // Gamma
      [](const ParameterState &s) {
        return !s.rparams.sharpen.scanner_mtf.simulate_diffraction_p();
      }, false, "Simulates a uniform \"box\" blur of a specific diameter in pixels. Used when diffraction simulation is disabled.");


  // Measure MTF button
  m_measureMtfBtn = addToggleButtonParameter("", "Measure mtf of an edge", [this](bool checked) {
    emit measureMtfRequested(checked);
  }, nullptr, nullptr, "Select an area containing a slanted edge to compute its MTF.");

  // Add the explicit model-fitting dialog when measured data is available.
  m_fitMtfBtn = addButtonParameter(
      "", "Fit measured MTF model", [this]() { fitMeasuredMtf(); },
      [this, separatorToggle](const ParameterState &s) {
        bool visible = !s.rparams.sharpen.scanner_mtf.measurements.empty();
        if (separatorToggle && !separatorToggle->isChecked())
          visible = false;
        return visible && !m_mtfFitRunning;
      },
      "Choose the physical diffraction model, edit capture metadata, and "
      "explicitly select which values should be optimized. Numeric zero is "
      "never interpreted implicitly by this dialog.");

  // MTF Scale
  // Range 0.0 - 2.0 (0.0 = no MTF)
  addSliderParameter(
      "MTF scale", 0.0, 2.0, 100.0, 2, "", "no MTF",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf_scale;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf_scale = v;
      }, 1.0, nullptr, false, "Global intensity of the deconvolution-based sharpening. 0.0 disables it, 1.0 is standard.");

  addSeparator("Measurements");
  addButtonParameter("", "Load QuickMTF measurement", [this]() { loadMTF(); });

  QWidget *measContainer = new MeasurementContainer([this](int from, int to) {
    applyChange([from, to](ParameterState &s) {
        auto &ms = s.rparams.sharpen.scanner_mtf.measurements;
        if (from >= 0 && from < (int)ms.size() && to >= 0 && to < (int)ms.size()) {
            auto item = ms[from];
            ms.erase(ms.begin() + from);
            ms.insert(ms.begin() + to, item);
        }
    }, tr("Reorder MTF measurements"));
  });
  m_measurementsLayout = new QVBoxLayout(measContainer);
  m_measurementsLayout->setContentsMargins(0, 0, 0, 0);
  m_measurementsLayout->setSpacing(4);
  if (m_currentGroupForm)
      m_currentGroupForm->addRow(measContainer);
  else
      m_form->addRow(measContainer);

  addSeparator("Deconvolution");

  // Supersample
  // Range 1 - 16, integer
  addSliderParameter(
      "Supersample", 1.0, 16.0, 1.0, 0, "", "",
      [](const ParameterState &s) {
        return (double)s.rparams.sharpen.supersample;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.supersample = (int)v;
      }, 1.0, nullptr, false, "Process the sharpening at a higher resolution than the original scan to reduce aliasing artifacts. Increases computation time significantly.");

  // Reconstruction kernel used only when supersampling is active. Lanczos 3
  // is the practical default for lens-limited scans; Lanczos 8 is retained for
  // data whose useful spectrum extends close to two-dimensional Nyquist.
  addEnumParameter<resampling_kernel,
                   sharpen_parameters::resampling_kernel_names,
                   (int)resampling_kernel::resampling_kernel_max>(
      "Supersampling kernel",
      [](const ParameterState &s) {
        return (int)s.rparams.sharpen.resampling;
      },
      [](ParameterState &s, int v) {
        s.rparams.sharpen.resampling = (resampling_kernel)v;
      },
      [](const ParameterState &s) {
        return s.rparams.sharpen.supersample > 1;
      },
      "Select the reconstruction filter used before deconvolution. Lanczos 3 "
      "is faster and normally sufficient for lens-limited scans; Lanczos 8 "
      "better preserves frequencies very close to two-dimensional Nyquist.");

  addSeparator("Wiener filter");

  // Signal to noise ratio
  // Range 0 - 65535, slow at start (gamma 2.0)
  addSliderParameter(
      "Signal to noise ratio", 0.0, 65535.0, 1.0, 0, "", "",
      [](const ParameterState &s) { return s.rparams.sharpen.scanner_snr; },
      [](ParameterState &s, double v) { s.rparams.sharpen.scanner_snr = v; },
      2.0, // Gamma (slow start)
      nullptr, false, "Used by the Wiener filter to balance between sharpening detail and amplifying image noise. Higher values result in stronger sharpening.");

  addSeparator("Richardson–Lucy deconvolution");

  // Richardson-Lucy iterations
  // Range 0 - 50000, integer, slow at beginning (gamma 2.0)
  addSliderParameter(
      "Iterations", 0.0, 50000.0, 1.0, 0, "", "",
      [](const ParameterState &s) {
        return (double)s.rparams.sharpen.richardson_lucy_iterations;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.richardson_lucy_iterations = (int)v;
      },
      2.0, // Gamma (slow start)
      nullptr, false, "Number of iterations for the Richardson-Lucy deconvolution. More iterations produce sharper results but may introduce \"ringing\" or \"halos\".");

  // Richardson-Lucy sigma
  // Range 0.0 - 2.0, floating point
  addSliderParameter(
      "Sigma", 0.0, 2.0, 1000.0, 3, "", "",
      [](const ParameterState &s) {
        return s.rparams.sharpen.richardson_lucy_sigma;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.richardson_lucy_sigma = v;
      }, 1.0, nullptr, false, "Damping factor for the Richardson-Lucy algorithm to suppress noise amplification in dark areas.");

  addSeparator("Unsharp mask");

  // Unsharp mask radius
  // Range 0.0 - 20.0, Pixels
  // Same properties as Gaussian blur sigma
  // Enabled only when mode is unsharp_mask
  addSliderParameter(
      "Radius", 0.0, 20.0, 1000.0, 3, "pixels", "",
      [](const ParameterState &s) { return s.rparams.sharpen.usm_radius; },
      [](ParameterState &s, double v) { s.rparams.sharpen.usm_radius = v; },
      1.0, // No gamma
      [](const ParameterState &s) {
        return s.rparams.sharpen.mode == sharpen_mode::unsharp_mask;
      }, false, "The radius of the unsharp mask (edge enhancement) in pixels.");

  addSliderParameter(
      "Amount", 0.0, 100.0, 100.0, 1, "", "",
      [](const ParameterState &s) { return s.rparams.sharpen.usm_amount; },
      [](ParameterState &s, double v) { s.rparams.sharpen.usm_amount = v; },
      2.0, // Gamma (slow start)
      [](const ParameterState &s) {
        return s.rparams.sharpen.mode == sharpen_mode::unsharp_mask;
      }, "The strength of the unsharp mask enhancement.");

  addSeparator("Focus analyzer");
  
  addCheckboxParameter(
      "Optimize Sigma",
      [this](const ParameterState &) {
        return (m_finetuneFlags & colorscreen::finetune_scanner_mtf_sigma) != 0;
      },
      [this](ParameterState &, bool v) {
        if (v) m_finetuneFlags |= colorscreen::finetune_scanner_mtf_sigma;
        else m_finetuneFlags &= ~colorscreen::finetune_scanner_mtf_sigma;
      }, nullptr, "Included in the focus analyzer optimization loop.");

  addCheckboxParameter(
      "Optimize Defocus",
      [this](const ParameterState &) {
        return (m_finetuneFlags & colorscreen::finetune_scanner_mtf_defocus) != 0;
      },
      [this](ParameterState &, bool v) {
        if (v) m_finetuneFlags |= colorscreen::finetune_scanner_mtf_defocus;
        else m_finetuneFlags &= ~colorscreen::finetune_scanner_mtf_defocus;
      }, nullptr, "Included in the focus analyzer optimization loop.");

  m_analyzeAreaBtn = addToggleButtonParameter("", tr("Analyze area"), [this](bool checked) {
    emit focusAnalysisRequested(checked, m_finetuneFlags);
  }, nullptr, nullptr, "Experimental tool that attempts to find the best Focus/Sigma by analyzing the local contrast and sharpness of the selected area.");

  // Finetune diagnostic images section (initially hidden)
  m_finetuneImagesPanel = new FinetuneImagesPanel();

  m_finetuneImagesWrapper = new QWidget();
  m_finetuneImagesContainer = new QVBoxLayout(m_finetuneImagesWrapper);
  m_finetuneImagesContainer->setContentsMargins(0, 0, 0, 0);

  QWidget *detachableFI =
      createDetachableSection("Finetune Diagnostic Images", m_finetuneImagesPanel, [this]() {
        emit detachFinetuneImagesRequested(m_finetuneImagesPanel);
      });
  m_finetuneImagesContainer->addWidget(detachableFI);
  
  m_finetuneImagesWrapper->hide();

  if (m_currentGroupForm)
    m_currentGroupForm->addRow(m_finetuneImagesWrapper);
  else
    m_form->addRow(m_finetuneImagesWrapper);

  addSeparator("Adaptive sharpening");
  
  QPushButton *analyzeDisplacementsBtn = new QPushButton(tr("Analyze displacements"));
  analyzeDisplacementsBtn->setToolTip(tr("Run adaptive sharpening analysis"));
  connect(analyzeDisplacementsBtn, &QPushButton::clicked, this, &SharpnessPanel::onAnalyzeDisplacements);
  
  if (m_currentGroupForm)
    m_currentGroupForm->addRow(analyzeDisplacementsBtn);
  else
    m_form->addRow(analyzeDisplacementsBtn);

  m_adaptiveChart = new AdaptiveSharpeningChart(this);
  m_adaptiveChart->initialize(10, 10); // Default size until real data comes
  
  m_adaptiveChartWrapper = new QWidget();
  m_adaptiveChartContainer = new QVBoxLayout(m_adaptiveChartWrapper);
  m_adaptiveChartContainer->setContentsMargins(0, 0, 0, 0);
  
  QWidget *detachableChart = createDetachableSection("Adaptive Sharpening Chart", m_adaptiveChart, [this]() {
      emit detachAdaptiveChartRequested(m_adaptiveChart);
  });
  
  m_adaptiveChartContainer->addWidget(detachableChart);
  m_adaptiveChartWrapper->hide();
  
  if (m_currentGroupForm)
      m_currentGroupForm->addRow(m_adaptiveChartWrapper);
  else
      m_form->addRow(m_adaptiveChartWrapper);
}

void SharpnessPanel::updateMTFChart() {
  if (!m_mtfChart)
    return;

  ParameterState state = m_stateGetter();

  /* Always plot the fitted/modelled transfer independently of whether the
     user currently selected a measured MTF for sharpening.  A measured curve
     contains magnitude only and must not suppress the physical model that was
     fitted from it.  */
  mtf_parameters chartParameters = state.rparams.sharpen.scanner_mtf;
  chartParameters.measured_mtf_idx = -1;

  // Compute model curves with 100 steps.
  mtf_parameters::computed_mtf curves = chartParameters.compute_curves(100);

  // Pass simulation flag to chart
  bool canSimulateDifraction
      = chartParameters.model != colorscreen::mtf_model::empirical_fallback
        && chartParameters.can_simulate_diffraction_p();
  // Calculate screen frequency if applicable
  double screenFreq = -1;
  auto img = m_imageGetter();
  if (img && state.scrToImg.type != colorscreen::Random) {
      colorscreen::scr_to_img scrToImgObj;
      scrToImgObj.set_parameters(state.scrToImg, *img);
      double pixel_size = scrToImgObj.pixel_size({0, 0, img->width, img->height});
      screenFreq = colorscreen::scr_names[(int)state.scrToImg.type].frequency * pixel_size;
  }

  m_mtfChart->setMTFData(curves, canSimulateDifraction,
                         state.rparams.sharpen.scanner_mtf.scan_dpi,
                         screenFreq);
  if (m_showSignedOtfCheck)
    m_showSignedOtfCheck->setEnabled(canSimulateDifraction);

  // Pass all measured MTF data if available
  const auto &scanner_mtf = state.rparams.sharpen.scanner_mtf;
  if (!scanner_mtf.measurements.empty()) {
    m_mtfChart->setMeasuredMTF(scanner_mtf.measurements, {scanner_mtf.get_channel_wavelength (0), scanner_mtf.get_channel_wavelength (1), scanner_mtf.get_channel_wavelength (2), scanner_mtf.get_channel_wavelength (3)});
  } else {
    // No measured data, clear it
    m_mtfChart->setMeasuredMTF({}, {});
  }

  // Update diffraction notice
  bool canSimulate = state.rparams.sharpen.scanner_mtf.can_simulate_diffraction_p();
  if (canSimulate) {
      m_diffractionNotice->hide();
  } else {
      m_diffractionNotice->show();
      QStringList missing;
      const auto &mtf = state.rparams.sharpen.scanner_mtf;
      if (mtf.pixel_pitch <= 0) missing << "<b>Sensor pixel pitch</b>";
      if (mtf.f_stop <= 0) missing << "<b>Nominal f-stop</b>";
      if (mtf.wavelength <= 0) missing << "<b>Wavelength</b>";
      if (mtf.scan_dpi <= 0) missing << "<b>Resolution</b>";
      
      m_diffractionNotice->setText(QString("To enable diffraction simulation, please set missing data: %1.")
                                  .arg(missing.join(", ")));
  }
}

void SharpnessPanel::updateScreenTiles() {
  // Schedule debounced update
  scheduleTileUpdate();
}

void SharpnessPanel::applyChange(
    std::function<void(ParameterState &)> modifier, const QString &description) {
  ParameterPanel::applyChange(modifier, description);
  updateMTFChart();
  updateScreenTiles();
}

void SharpnessPanel::onParametersRefreshed(const ParameterState &state) {
  updateMeasurementList();
  updateMTFChart();
  updateScreenTiles();

  // Update Adaptive Sharpening Chart visibility
  bool hasAdaptiveData = state.rparams.scanner_blur_correction != nullptr;
  if (m_adaptiveChartWrapper) {
    // Only hide if not already visible (to avoid hiding it while analysis is running)
    // or if we explicitly want to follow the state.
    // If we have data, we show it. If not, we only hide if we're not expecting data soon.
    m_adaptiveChartWrapper->setVisible(hasAdaptiveData);
  }
}

// Methods removed as they are now in TilePreviewPanel or handled by it
// scheduleTileUpdate, startNextRender, performTileRender, resizeEvent

std::vector<std::pair<render_screen_tile_type, QString>>
SharpnessPanel::getTileTypes() const {
  return {{original_screen, "Original"},
          {blurred_screen, "Blurred"},
          {sharpened_screen, "Sharpened"}};
}

bool SharpnessPanel::shouldUpdateTiles(const ParameterState &state) {
  if (m_lastTileSize == 0 || // First run
                             // tileSize check is done in base
      (int)state.scrToImg.type != m_lastScrType ||
      !state.rparams.sharpen.equal_p(m_lastSharpen) ||
      state.rparams.red_strip_width != m_lastRedStripWidth ||
      state.rparams.green_strip_width != m_lastGreenStripWidth) 
    return true;
  return false;
}

void SharpnessPanel::onTileUpdateScheduled() {
  ParameterState state = m_stateGetter();
  // Cache current parameters
  m_lastScrType = (int)state.scrToImg.type;
  m_lastSharpen = state.rparams.sharpen;
  m_lastRedStripWidth = state.rparams.red_strip_width;
  m_lastGreenStripWidth = state.rparams.green_strip_width;
}

QWidget *SharpnessPanel::getMTFChartWidget() const { return m_mtfChart; }

// getTilesWidget removed here (in base)

// createDetachableSection removed (moved to ParameterPanel)

void SharpnessPanel::reattachMTFChart(QWidget *widget) {
  if (widget != m_mtfChart)
    return;

  if (m_mtfContainer && m_mtfContainer->count() > 0) {
    QWidget *section = m_mtfContainer->itemAt(0)->widget();
    if (section && section->layout()) {
      // Remove placeholder (last item)
      QLayoutItem *item =
          section->layout()->takeAt(section->layout()->count() - 1);
      if (item) {
        if (item->widget())
          delete item->widget();
        delete item;
      }

      // Add widget back
      section->layout()->addWidget(widget);
      widget->show();

      // Show header again
      if (section->layout()->count() > 0) {
        QLayoutItem *headerItem = section->layout()->itemAt(0);
        if (headerItem && headerItem->widget()) {
          headerItem->widget()->show();
        }
      }
    }
  }
}

void SharpnessPanel::reattachDotSpread(QWidget *widget) {
    if (m_dotSpreadPanel)
        m_dotSpreadPanel->reattachTiles(widget);
}

/** Open the adaptive-analysis settings dialog and launch one run.
    The settings are remembered between invocations but are not part of the
    persistent image/render parameter state.  */
void SharpnessPanel::onAnalyzeDisplacements() {
  const ParameterState state = m_stateGetter();
  const bool physicalFocusAvailable
      = state.rparams.sharpen.scanner_mtf.simulate_diffraction_p();
  const bool varyingStripWidths
      = colorscreen::screen_with_varying_strips_p(state.scrToImg.type);
  const auto image = m_imageGetter();
  const bool hasRgb = image && image->has_rgb();

  if (!m_adaptiveSharpeningParametersInitialized)
    m_adaptiveSharpeningParameters.interpolateFocus = physicalFocusAvailable;

  AdaptiveSharpeningDialog dialog(m_adaptiveSharpeningParameters,
                                   physicalFocusAvailable,
                                   varyingStripWidths, hasRgb, this);
  if (dialog.exec() != QDialog::Accepted)
    return;

  m_adaptiveSharpeningParameters = dialog.parameters();
  m_adaptiveSharpeningParametersInitialized = true;
  emit adaptiveSharpeningRequested(m_adaptiveSharpeningParameters);
}

// reattachTiles removed (in base)
/** Open the explicit fit dialog and run its numerical optimization in a
    background task.  The worker operates on a snapshot and the completed
    result is committed as one undoable parameter-state change.  */
void SharpnessPanel::fitMeasuredMtf() {
  const ParameterState state = m_stateGetter();
  const colorscreen::mtf_parameters &current =
      state.rparams.sharpen.scanner_mtf;
  MTFFitDialog dialog(current, this);
  if (dialog.exec() != QDialog::Accepted)
    return;

  const colorscreen::mtf_parameters input = dialog.parameters();
  const colorscreen::mtf_estimation_options options = dialog.options();
  const int flags = dialog.estimationFlags();
  auto result = std::make_shared<MtfFitResult>();
  result->baseline = current;
  result->input = input;
  result->fitted = input;
  m_mtfFitRunning = true;
  if (m_fitMtfBtn)
    m_fitMtfBtn->setEnabled(false);

  m_mtfFitQueue.runAsync(
      [result, options, flags](colorscreen::progress_info *progress) {
        for (size_t measurement = 0;
             measurement < result->input.measurements.size(); measurement++) {
          if (!options.include_measurement_p(measurement))
            continue;
          const colorscreen::mtf_measurement &curve =
              result->input.measurements[measurement];
          for (size_t sample = 0; sample < curve.size(); sample++)
            if (curve.get_freq(static_cast<int>(sample)) <= 0.5)
              result->observations++;
        }

        try {
          const char *error = nullptr;
          result->objective = result->fitted.estimate_parameters(
              result->input, options, nullptr, progress, &error, flags);
          result->cancelled = progress
                              && (progress->cancelled()
                                  || progress->pool_cancel());
          if (error)
            result->error = error;
        } catch (const std::exception &exception) {
          result->error = exception.what();
        } catch (...) {
          result->error = "unexpected exception during MTF fitting";
        }
      },
      [this, result]() {
        m_mtfFitRunning = false;
        /* Re-run all panel availability predicates instead of unconditionally
           enabling the button.  Measurements or the containing section may
           have changed while the background fit was running.  */
        updateUI();
        if (result->cancelled)
          return;
        if (result->objective < 0 || !result->error.empty()) {
          QMessageBox::warning(
              this, tr("MTF model fit"),
              tr("The MTF model could not be fitted: %1")
                  .arg(QString::fromStdString(
                      result->error.empty() ? "unknown fitting error"
                                            : result->error)));
          return;
        }

        /* Do not silently overwrite measurements or model metadata edited
           while a long-running fit was in progress.  The dialog values are
           applied only when the underlying scanner-MTF state is still the
           snapshot from which the fit was started.  */
        const ParameterState currentState = m_stateGetter();
        const colorscreen::mtf_parameters &currentMtf =
            currentState.rparams.sharpen.scanner_mtf;
        if (!currentMtf.equal_p(result->baseline)) {
          QMessageBox::warning(
              this, tr("MTF model fit"),
              tr("The MTF measurements or model parameters changed while the "
                 "fit was running. The completed result was not applied; "
                 "start the fit again from the current values."));
          return;
        }

        const colorscreen::mtf_parameters fitted = result->fitted;
        applyChange(
            [fitted](ParameterState &updated) {
              updated.rparams.sharpen.scanner_mtf = fitted;
            },
            tr("Fit measured MTF model"));

        const double rms = result->observations
                               ? std::sqrt(result->objective
                                           / result->observations)
                               : 0.0;
        QString details =
            tr("The selected model was fitted successfully.\n\n"
               "RMS residual: %1 percentage points\n"
               "Gaussian sigma: %2 px")
                .arg(rms, 0, 'g', 6)
                .arg(fitted.sigma, 0, 'g', 8);
        if (fitted.model == colorscreen::mtf_model::physical_diffraction)
          {
            details += tr("\nDefocus: %1 mm\nMarked f-number: %2"
                          "\nSensor fill factor: %3\nHalo fraction: %4")
                         .arg(fitted.defocus, 0, 'g', 8)
                         .arg(fitted.f_stop, 0, 'g', 8)
                         .arg(fitted.sensor_fill_factor, 0, 'g', 8)
                         .arg(fitted.halo_fraction, 0, 'g', 8);
            if (fitted.halo_fraction > 0)
              details += tr("\nHalo radius: %1 px")
                             .arg(fitted.halo_sigma, 0, 'g', 8);
            else
              details += tr("\nHalo radius: inactive");
          }
        else
          details += tr("\nFallback blur diameter: %1 px")
                         .arg(fitted.blur_diameter, 0, 'g', 8);
        QMessageBox::information(this, tr("MTF model fit"), details);
      });
}

void SharpnessPanel::loadMTF() {
  QStringList fileNames = QFileDialog::getOpenFileNames(
      this, tr("Load QuickMTF measurements"), "",
      tr("QuickMTF files (*.csv *.txt);;All Files (*)"));

  if (fileNames.isEmpty())
    return;

  ParameterState state = m_stateGetter();
  bool anySuccess = false;

  for (const QString &fileName : fileNames) {
    FILE *f = fopen(fileName.toLocal8Bit().constData(), "r");
    if (!f) {
      QMessageBox::warning(this, tr("Warning"),
                            tr("Could not open file %1").arg(fileName));
      continue;
    }

    const char *error = nullptr;
    std::string baseName = QFileInfo(fileName).completeBaseName().toStdString();
    if (state.rparams.sharpen.scanner_mtf.load_csv(
            f, baseName, &error) < 0) {
      QMessageBox::warning(
          this, tr("Warning"),
          tr("Error loading MTF measurement from %1: %2")
              .arg(fileName)
              .arg(error ? QString::fromUtf8(error) : tr("Unknown error")));
      fclose(f);
      continue;
    }
    fclose(f);
    anySuccess = true;
  }

  if (anySuccess) {
    // Now apply the change
    applyChange([state](ParameterState &s) {
      s = state;
    }, tr("Load MTF measurements"));
    updateMeasurementList();
  }
}

void SharpnessPanel::updateMeasurementList() {
    if (!m_measurementsLayout) return;

    ParameterState state = m_stateGetter();
    const auto &measurements = state.rparams.sharpen.scanner_mtf.measurements;

    // Memoization to avoid flickering and unnecessary rebuilds
    if (measurements == m_lastMeasurements) {
        return;
    }
    m_lastMeasurements = measurements;

    // Clear layout
    QLayoutItem *item;
    while ((item = m_measurementsLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    if (measurements.empty()) return;

    // Header row
    QWidget *header = new QWidget();
    QHBoxLayout *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 2, 0, 2);
    headerLayout->setSpacing(4);

    // Spacer for drag handle + delete button
    headerLayout->addSpacing(34 + 24); 

    QFont boldFont = this->font();
    boldFont.setBold(true);

    QLabel *nameLabel = new QLabel(tr("Name"));
    nameLabel->setFont(boldFont);
    headerLayout->addWidget(nameLabel, 1);

    QLabel *chanLabel = new QLabel(tr("Channel"));
    chanLabel->setFont(boldFont);
    chanLabel->setFixedWidth(100); 
    headerLayout->addWidget(chanLabel);

    QLabel *waveLabel = new QLabel(tr("Wavelength"));
    waveLabel->setFont(boldFont);
    waveLabel->setFixedWidth(120);
    headerLayout->addWidget(waveLabel);

    QLabel *sameLabel = new QLabel(tr("Same"));
    sameLabel->setFixedWidth(50);
    sameLabel->setFont(boldFont);
    headerLayout->addWidget(sameLabel);

    m_measurementsLayout->addWidget(header);

    for (int i = 0; i < (int)measurements.size(); ++i) {
        const auto &m = measurements[i];
        QWidget *row = new QWidget();
        QHBoxLayout *hLayout = new QHBoxLayout(row);
        hLayout->setContentsMargins(0, 0, 0, 0);
        hLayout->setSpacing(4);

        // Drag handle
        DragHandle *handle = new DragHandle(i, row);
        hLayout->addWidget(handle);

        // Delete button
        QPushButton *delBtn = new QPushButton();
        delBtn->setIcon(QIcon::fromTheme("edit-delete"));
        delBtn->setFlat(true);
        delBtn->setToolTip(tr("Delete measurement"));
        delBtn->setMaximumWidth(30);
        connect(delBtn, &QPushButton::clicked, this, [this, i]() {
            applyChange([i](ParameterState &s) {
                if (i < (int)s.rparams.sharpen.scanner_mtf.measurements.size()) {
                    s.rparams.sharpen.scanner_mtf.measurements.erase(
                        s.rparams.sharpen.scanner_mtf.measurements.begin() + i);
                    if (s.rparams.sharpen.scanner_mtf.measured_mtf_idx >= (int)s.rparams.sharpen.scanner_mtf.measurements.size())
                        s.rparams.sharpen.scanner_mtf.measured_mtf_idx = (int)s.rparams.sharpen.scanner_mtf.measurements.size() - 1;
                }
            }, tr("Delete MTF measurement"));
        });
        hLayout->addWidget(delBtn);

        // Name
        QLineEdit *nameEdit = new QLineEdit(QString::fromStdString(m.name));
        connect(nameEdit, &QLineEdit::editingFinished, this, [this, i, nameEdit]() {
            applyChange([i, nameEdit](ParameterState &s) {
                if (i < (int)s.rparams.sharpen.scanner_mtf.measurements.size()) {
                    s.rparams.sharpen.scanner_mtf.measurements[i].name = nameEdit->text().toStdString();
                }
            }, tr("Change MTF measurement name"));
        });
        hLayout->addWidget(nameEdit, 1);

        // Channel
        QComboBox *chanCombo = new QComboBox();
        chanCombo->addItem(tr("Unknown"), -1);
        chanCombo->addItem(tr("Red"), 0);
        chanCombo->addItem(tr("Green"), 1);
        chanCombo->addItem(tr("Blue"), 2);
        chanCombo->addItem(tr("IR"), 3);

        int idx = chanCombo->findData(m.channel);
        if (idx != -1) chanCombo->setCurrentIndex(idx);
        chanCombo->setFixedWidth(100);

        connect(chanCombo, QOverload<int>::of(&QComboBox::activated), this, [this, i, chanCombo](int index) {
            int val = chanCombo->itemData(index).toInt();
            applyChange([i, val](ParameterState &s) {
                if (i < (int)s.rparams.sharpen.scanner_mtf.measurements.size()) {
                    s.rparams.sharpen.scanner_mtf.measurements[i].channel = val;
                }
            }, tr("Change MTF measurement channel"));
        });
        hLayout->addWidget(chanCombo);

        // Wavelength
        QDoubleSpinBox *waveSpin = new QDoubleSpinBox();
        waveSpin->setRange(0, 2000);
        waveSpin->setValue(m.wavelength);
        waveSpin->setSuffix(" nm");
        waveSpin->setDecimals(3);
        waveSpin->setSpecialValueText(tr("unknown"));
        waveSpin->setToolTip(
            tr("Authoritative wavelength of this measured edge. Channel is "
               "only a label and does not disable this field."));
        waveSpin->setFixedWidth(120);
        connect(waveSpin, &QDoubleSpinBox::editingFinished, this, [this, i, waveSpin]() {
            double val = waveSpin->value();
            applyChange([i, val](ParameterState &s) {
                if (i < (int)s.rparams.sharpen.scanner_mtf.measurements.size()) {
                    s.rparams.sharpen.scanner_mtf.measurements[i].wavelength = val;
                }
            }, tr("Change MTF measurement wavelength"));
        });
        hLayout->addWidget(waveSpin);

        // Same capture
        QCheckBox *sameCheck = new QCheckBox();
        sameCheck->setFixedWidth(50);
        sameCheck->setToolTip(
            tr("Share the fitted focus displacement with the preceding "
               "measurement because both curves came from the same capture."));
        sameCheck->setChecked(m.same_capture);
        if (i == 0) {
            sameCheck->setChecked(false);
            sameCheck->setEnabled(false);
        }

        connect(sameCheck, &QCheckBox::toggled, this, [this, i](bool v) {
            applyChange([i, v](ParameterState &s) {
                if (i < (int)s.rparams.sharpen.scanner_mtf.measurements.size()) {
                    s.rparams.sharpen.scanner_mtf.measurements[i].same_capture = v;
                }
            }, tr("Change MTF measurement same capture"));
        });
        hLayout->addWidget(sameCheck);

        m_measurementsLayout->addWidget(row);
    }
}
void SharpnessPanel::updateFinetuneImages(const colorscreen::finetune_result& result) {
    if (m_finetuneImagesPanel) {
        m_finetuneImagesPanel->setFinetuneResult(result);
        m_finetuneImagesPanel->show();
    }
    if (m_finetuneImagesWrapper) {
        m_finetuneImagesWrapper->show();
    }
}

void SharpnessPanel::reattachFinetuneImages(QWidget *widget) {
  if (widget != m_finetuneImagesPanel)
    return;

  if (m_finetuneImagesContainer && m_finetuneImagesContainer->count() > 0) {
    QWidget *section = m_finetuneImagesContainer->itemAt(0)->widget();
    if (section && section->layout()) {
      // Remove placeholder (last item)
      QLayoutItem *item =
          section->layout()->takeAt(section->layout()->count() - 1);
      if (item) {
        if (item->widget())
          delete item->widget();
        delete item;
      }

      // Add widget back
      section->layout()->addWidget(widget);
      widget->show();

      // Show header again
      if (section->layout()->count() > 0) {
        QLayoutItem *headerItem = section->layout()->itemAt(0);
        if (headerItem && headerItem->widget()) {
          headerItem->widget()->show();
        }
      }
    }
  }
}

void SharpnessPanel::showAdaptiveChart() {
    if (m_adaptiveChartWrapper) {
        m_adaptiveChartWrapper->show();
    }
}

void SharpnessPanel::setFocusAnalysisChecked(bool checked) {
    if (m_analyzeAreaBtn) {
        m_analyzeAreaBtn->blockSignals(true);
        m_analyzeAreaBtn->setChecked(checked);
        m_analyzeAreaBtn->blockSignals(false);
    }
}

void SharpnessPanel::setMeasureMtfChecked(bool checked) {
    if (m_measureMtfBtn) {
        m_measureMtfBtn->blockSignals(true);
        m_measureMtfBtn->setChecked(checked);
        m_measureMtfBtn->blockSignals(false);
    }
}

void SharpnessPanel::setMeasureMtfEnabled(bool enabled) {
    if (m_measureMtfBtn) {
        m_measureMtfBtn->setEnabled(enabled);
    }
}

void SharpnessPanel::reattachAdaptiveChart(QWidget *widget) {
    if (widget && m_adaptiveChart == widget) {
         QFormLayout *layout = qobject_cast<QFormLayout*>(this->layout());
         if (layout) layout->addRow(widget);
    }
}
