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
#include <string>
#include <utility>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QString>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QMimeData>
#include <QDrag>
#include <QMouseEvent>
#include <QSpinBox>
#include <QApplication>
#include "AdaptiveSharpeningChart.h"
#include "AdaptiveSharpeningDialog.h"

using namespace colorscreen;
using sharpen_mode = colorscreen::sharpen_parameters::sharpen_mode;
using resampling_kernel
    = colorscreen::sharpen_parameters::resampling_kernel;

namespace {



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
    std::shared_ptr<colorscreen::image_data> scan = m_imageGetter();
    if (scan && scan->has_rgb() && scan->has_grayscale_or_ir()) {
      return {{dot_spread, "RGB Dot Spread"}, {dot_spread_ir, "IR Dot Spread"}};
    } else if (scan && scan->has_rgb()) {
      return {{dot_spread, "RGB Dot Spread"}};
    }
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
      /* Blurred and sharpened screen tiles convert capture MTF dimensions to
         screen units, so an arbitrary fallback scale would be misleading. */
      return colorscreen::screen_geometry_configured_p(state.scrToImg);
  }

private:
  colorscreen::sharpen_parameters m_lastSharpen;
};

} // namespace

SharpnessPanel::SharpnessPanel(StateGetter stateGetter, StateSetter stateSetter,
                               ImageGetter imageGetter,
                               MtfCalibrationCallbacks mtfCalibration,
                               QWidget *parent)
    : TilePreviewPanel(stateGetter, stateSetter, imageGetter, parent),
      m_mtfCalibration(std::move(mtfCalibration)) {
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
      }, nullptr, "Select the sharpening algorithm. \"None\" disables sharpening, \"Wiener\" and \"Richardson-Lucy\" use the MTF model, \"Unsharp mask\" is a classic edge enhancement.",
      QStringLiteral("sharpness.mode"), true);

  m_scannerCameraSeparatorToggle = addSeparator(
      "Scanner/Camera properties", QStringLiteral("sharpness.capture"));
  m_scannerCameraSeparatorToggle->setObjectName(
      QStringLiteral("ScannerCameraPropertiesToggle"));

  // MTF Chart
  m_mtfChart = new MTFChartWidget();
  m_mtfChart->setMinimumHeight(250);
  connect(m_mtfChart, &MTFChartWidget::measurementSelected, this,
          [this](int index) { selectMtfMeasurement(index); });

  // Create container for MTF
  QWidget *mtfWrapper = new QWidget();
  auto *mtfContainer = new QVBoxLayout(mtfWrapper);
  mtfContainer->setContentsMargins(0, 0, 0, 0);

  QWidget *detachableMTF = createDetachableSection("MTF Chart", m_mtfChart);
  mtfContainer->addWidget(detachableMTF);

  m_showSignedOtfCheck = new QCheckBox(tr("Show signed physical OTF"), mtfWrapper);
  m_showSignedOtfCheck->setObjectName(
      QStringLiteral("SharpnessSignedOtfCheck"));
  m_showSignedOtfCheck->setToolTip(
      tr("Show the signed analytical system transfer predicted by the physical "
         "lens model. Measured slanted-edge curves remain MTF magnitudes; "
         "negative lobes are inferred from the fitted optical model."));
  connect(m_showSignedOtfCheck, &QCheckBox::toggled, m_mtfChart,
          &MTFChartWidget::setShowSignedOTF);
  mtfContainer->addWidget(m_showSignedOtfCheck);

  if (m_currentGroupForm)
    m_currentGroupForm->addRow(mtfWrapper);
  else
    m_form->addRow(mtfWrapper);
  updateMTFChart();

  DotSpreadPreviewPanel *dotSpread =
      new DotSpreadPreviewPanel(m_stateGetter, m_stateSetter, m_imageGetter);
  dotSpread->init("Dot Spread");
  connect(dotSpread, &TilePreviewPanel::progressStarted, this, &SharpnessPanel::progressStarted);
  connect(dotSpread, &TilePreviewPanel::progressFinished, this, &SharpnessPanel::progressFinished);
  
  m_widgetStateUpdaters.push_back([dotSpread, this]() {
      dotSpread->updateUI();
  });

  if (m_currentGroupForm) m_currentGroupForm->addRow(dotSpread);
  else m_form->addRow(dotSpread);

  // Measured-MTF controls do not exist conceptually until a measurement has
  // been loaded or made. Keep that applicability separate from enablement.
  QCheckBox *useMeasuredMtf = addCheckboxParameter(
      "Use measured MTF",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.measured_mtf_idx >= 0;
      },
      [](ParameterState &s, bool v) {
        s.rparams.sharpen.scanner_mtf.measured_mtf_idx = v ? 0 : -1;
      },
      nullptr,
      "Use a measured MTF curve directly instead of the fitted analytical physical or fallback model.",
      QStringLiteral("sharpness.capture.use_measured_mtf"), true);
  useMeasuredMtf->setObjectName(QStringLiteral("MtfUseMeasuredCheck"));
  setParameterApplicability(useMeasuredMtf, [](const ParameterState &state) {
    return !state.rparams.sharpen.scanner_mtf.measurements.empty();
  });


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
      }, 1.0, nullptr, false, "Residual compact Gaussian blur. In the physical model it is applied after diffraction and defocus; in the empirical fallback it is the compact core blur.",
      QStringLiteral("sharpness.capture.sigma"), true);



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
      }, false, "Image-plane focus displacement in millimeters. The physical model evaluates the signed incoherent OTF of a defocused circular pupil.",
      QStringLiteral("sharpness.capture.defocus"), true);

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
      "Fraction of optical energy redistributed into the broad symmetric halo. Zero disables the halo without discarding its radius.",
      QStringLiteral("sharpness.capture.halo_fraction"), true);

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
      "Gaussian standard deviation of the broad symmetric halo in output pixels. It has no effect while the halo fraction is zero.",
      QStringLiteral("sharpness.capture.halo_sigma"), true);

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
      }, false, "Simulates a uniform \"box\" blur of a specific diameter in pixels. Used when diffraction simulation is disabled.",
      QStringLiteral("sharpness.capture.blur_diameter"), true);

  addButtonParameter(
      "", "Open slanted edge reference",
      [this]() { emit openSlantedEdgeReferenceRequested(); }, nullptr,
      "Open another scan as a slanted-edge reference. The reference uses the "
      "current document's shared sharpening parameters and measurements.");

  // Measure MTF button
  m_measureMtfBtn = addToggleButtonParameter(
      "", tr("Measure MTF from edge"), [this](bool checked) {
        emit measureMtfRequested(checked);
      }, nullptr,
      [this](const ParameterState &) { return m_imageGetter() != nullptr; },
      tr("Select an area containing a slanted edge in the loaded image to "
         "compute its MTF."));
  m_measureMtfBtn->setObjectName(QStringLiteral("MtfMeasureButton"));

  // Add the explicit model-fitting dialog when measured data is available.
  m_fitMtfBtn = addButtonParameter(
      "", "Fit measured MTF model", [this]() { fitMeasuredMtf(); },
      [this](const ParameterState &) {
        return !m_mtfCalibration.fitAvailable || m_mtfCalibration.fitAvailable();
      },
      "Choose the physical diffraction model, edit capture metadata, and "
      "explicitly select which values should be optimized. Numeric zero is "
      "never interpreted implicitly by this dialog.");
  m_fitMtfBtn->setObjectName(QStringLiteral("MtfFitButton"));
  setParameterApplicability(m_fitMtfBtn, [](const ParameterState &state) {
    return !state.rparams.sharpen.scanner_mtf.measurements.empty();
  });

  m_mtfFitStatusLabel = new QLabel(this);
  m_mtfFitStatusLabel->setWordWrap(true);
  m_mtfFitStatusLabel->setObjectName(QStringLiteral("MtfCalibrationStatus"));
  if (m_currentGroupForm)
    m_currentGroupForm->addRow(tr("Model status:"), m_mtfFitStatusLabel);
  else
    m_form->addRow(tr("Model status:"), m_mtfFitStatusLabel);
  updateMtfCalibrationStatus();

  // MTF Scale
  // Range 0.0 - 2.0 (0.0 = no MTF)
  addSliderParameter(
      "MTF scale", 0.0, 2.0, 100.0, 2, "", "no MTF",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf_scale;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf_scale = v;
      }, 1.0, nullptr, false, "Global intensity of the deconvolution-based sharpening. 0.0 disables it, 1.0 is standard.",
      QStringLiteral("sharpness.capture.mtf_scale"), true);

  addSeparator("Measurements", QStringLiteral("sharpness.measurements"));

  auto *inspectWidget = new QWidget(this);
  auto *inspectLayout = new QHBoxLayout(inspectWidget);
  inspectLayout->setContentsMargins(0, 0, 0, 0);
  inspectLayout->setSpacing(4);
  m_measurementSelector = new QComboBox(inspectWidget);
  m_measurementSelector->setObjectName(QStringLiteral("MtfMeasurementSelector"));
  m_measurementSelector->setSizeAdjustPolicy(
      QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_measurementSelector->setMinimumContentsLength(18);
  inspectLayout->addWidget(m_measurementSelector, 1);
  m_locateMeasurementBtn = new QPushButton(tr("Show ROI"), inspectWidget);
  m_locateMeasurementBtn->setObjectName(QStringLiteral("MtfMeasurementLocate"));
  m_locateMeasurementBtn->setToolTip(
      tr("Center the image on the selected measurement's recorded region of interest."));
  inspectLayout->addWidget(m_locateMeasurementBtn);
  connect(m_measurementSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int row) {
            const int measurement = row >= 0
                ? m_measurementSelector->itemData(row).toInt() : -1;
            selectMtfMeasurement(measurement);
          });
  connect(m_locateMeasurementBtn, &QPushButton::clicked, this, [this]() {
    if (m_selectedMtfMeasurement >= 0)
      emit mtfMeasurementLocateRequested(m_selectedMtfMeasurement);
  });
  if (m_currentGroupForm)
    m_currentGroupForm->addRow(tr("Inspect:"), inspectWidget);
  else
    m_form->addRow(tr("Inspect:"), inspectWidget);

  m_measurementDetailLabel = new QLabel(this);
  m_measurementDetailLabel->setWordWrap(true);
  m_measurementDetailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_measurementDetailLabel->setObjectName(
      QStringLiteral("MtfMeasurementProvenance"));
  if (m_currentGroupForm)
    m_currentGroupForm->addRow(m_measurementDetailLabel);
  else
    m_form->addRow(m_measurementDetailLabel);

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

  addSeparator("Deconvolution", QStringLiteral("sharpness.deconvolution"));

  // Supersample
  // Range 1 - 16, integer
  addSliderParameter(
      "Supersample", 1.0, 16.0, 1.0, 0, "", "",
      [](const ParameterState &s) {
        return (double)s.rparams.sharpen.supersample;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.supersample = (int)v;
      }, 1.0, nullptr, false, "Process the sharpening at a higher resolution than the original scan to reduce aliasing artifacts. Increases computation time significantly.",
      QStringLiteral("sharpness.deconvolution.supersample"), true);

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
      "better preserves frequencies very close to two-dimensional Nyquist.",
      QStringLiteral("sharpness.deconvolution.kernel"));

  addSeparator("Wiener filter", QStringLiteral("sharpness.wiener"));

  // Signal to noise ratio
  // Range 0 - 65535, slow at start (gamma 2.0)
  addSliderParameter(
      "Signal to noise ratio", 0.0, 65535.0, 1.0, 0, "", "",
      [](const ParameterState &s) { return s.rparams.sharpen.scanner_snr; },
      [](ParameterState &s, double v) { s.rparams.sharpen.scanner_snr = v; },
      2.0, // Gamma (slow start)
      nullptr, false, "Used by the Wiener filter to balance between sharpening detail and amplifying image noise. Higher values result in stronger sharpening.",
      QStringLiteral("sharpness.wiener.snr"), true);

  addSeparator("Richardson–Lucy deconvolution",
               QStringLiteral("sharpness.richardson_lucy"));

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
      nullptr, false, "Number of Richardson-Lucy iterations. Zero disables Richardson-Lucy sharpening; more iterations generally produce sharper results but may introduce \"ringing\" or \"halos\".",
      QStringLiteral("sharpness.richardson_lucy.iterations"), true);

  // Richardson-Lucy sigma
  // Range 0.0 - 2.0, floating point
  addSliderParameter(
      "Sigma", 0.0, 2.0, 1000.0, 3, "", "",
      [](const ParameterState &s) {
        return s.rparams.sharpen.richardson_lucy_sigma;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.richardson_lucy_sigma = v;
      }, 1.0, nullptr, false, "Damping factor for the Richardson-Lucy algorithm to suppress noise amplification in dark areas.",
      QStringLiteral("sharpness.richardson_lucy.sigma"), true);

  addSeparator("Unsharp mask", QStringLiteral("sharpness.unsharp"));

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
      }, false, "The radius of the unsharp mask (edge enhancement) in pixels.",
      QStringLiteral("sharpness.unsharp.radius"), true);

  addSliderParameter(
      "Amount", 0.0, 100.0, 100.0, 1, "", "",
      [](const ParameterState &s) { return s.rparams.sharpen.usm_amount; },
      [](ParameterState &s, double v) { s.rparams.sharpen.usm_amount = v; },
      2.0, // Gamma (slow start)
      [](const ParameterState &s) {
        return s.rparams.sharpen.mode == sharpen_mode::unsharp_mask;
      }, false, "The strength of the unsharp mask enhancement.",
      QStringLiteral("sharpness.unsharp.amount"), true);

  addSeparator("Focus analyzer", QStringLiteral("sharpness.focus"));
  
  QCheckBox *optimizeSigmaCheck = addCheckboxParameter(
      "Optimize Sigma",
      [this](const ParameterState &) {
        return (m_finetuneFlags & colorscreen::finetune_scanner_mtf_sigma) != 0;
      },
      [this](ParameterState &, bool v) {
        if (v) m_finetuneFlags |= colorscreen::finetune_scanner_mtf_sigma;
        else m_finetuneFlags &= ~colorscreen::finetune_scanner_mtf_sigma;
      }, nullptr, "Included in the focus analyzer optimization loop. Multi-area analysis can fit Sigma and Defocus together using scalar-prefit basin seeds; the process-screen MTF is the primary diagnostic.");
  optimizeSigmaCheck->setObjectName(
      QStringLiteral("SharpnessOptimizeSigmaCheck"));

  QCheckBox *optimizeDefocusCheck = addCheckboxParameter(
      "Optimize Defocus",
      [this](const ParameterState &) {
        return (m_finetuneFlags & colorscreen::finetune_scanner_mtf_defocus) != 0;
      },
      [this](ParameterState &, bool v) {
        if (v) m_finetuneFlags |= colorscreen::finetune_scanner_mtf_defocus;
        else m_finetuneFlags &= ~colorscreen::finetune_scanner_mtf_defocus;
      }, nullptr, "Included in the focus analyzer optimization loop. Multi-area analysis preserves a loaded MTF calibration as one start while also testing scalar Sigma/Defocus basins.");
  optimizeDefocusCheck->setObjectName(
      QStringLiteral("SharpnessOptimizeDefocusCheck"));

  auto focusAnalysisReady = [this](const ParameterState &state) {
    return m_imageGetter() != nullptr &&
           colorscreen::screen_geometry_configured_p(state.scrToImg);
  };

  m_analyzeAreaBtn = addToggleButtonParameter(
      "", tr("Analyze area"),
      [this](bool checked) {
        emit focusAnalysisRequested(checked, m_finetuneFlags);
      },
      nullptr, focusAnalysisReady,
      tr("Experimental tool that attempts to find the best Focus/Sigma by "
         "analyzing the local contrast and sharpness of the selected area."));
  m_analyzeAreaBtn->setObjectName(
      QStringLiteral("SharpnessAnalyzeFocusAreaButton"));

  QLabel *focusRequirement = new QLabel(this);
  focusRequirement->setObjectName(
      QStringLiteral("SharpnessFocusRequirement"));
  focusRequirement->setWordWrap(true);
  if (m_currentGroupForm)
    m_currentGroupForm->addRow(tr("Requirement:"), focusRequirement);
  else
    m_form->addRow(tr("Requirement:"), focusRequirement);
  m_paramUpdaters.push_back(
      [this, focusRequirement](const ParameterState &state) {
        if (!m_imageGetter()) {
          focusRequirement->setText(
              tr("Load an image before using the Focus analyzer."));
        } else if (!colorscreen::screen_geometry_configured_p(state.scrToImg)) {
          focusRequirement->setText(
              tr("Fit screen geometry before using the Focus analyzer."));
        } else {
          focusRequirement->clear();
        }
      });
  setParameterApplicability(
      focusRequirement, [this](const ParameterState &state) {
        return !m_imageGetter() ||
               !colorscreen::screen_geometry_configured_p(state.scrToImg);
      });

  m_findFocusAreasBtn = addButtonParameter(
      "", tr("Find focus areas"),
      [this]() { emit findFocusAreasRequested(); }, focusAnalysisReady,
      tr("Search a linear interpolated reconstruction for locally uniform "
         "colour regions suitable for robust multi-area focus analysis."));
  m_findFocusAreasBtn->setObjectName(
      QStringLiteral("SharpnessFindFocusAreasButton"));

  m_analyzeFocusAreasBtn =
      new QPushButton(tr("Analyze focus areas"), this);
  m_analyzeFocusAreasBtn->setObjectName(
      QStringLiteral("SharpnessAnalyzeFocusAreasButton"));
  m_analyzeFocusAreasBtn->setEnabled(false);
  m_analyzeFocusAreasBtn->setToolTip(
      tr("Verify the discovered regions independently, choose a "
         "colour-diverse subset, fit one shared focus model, then run "
         "leave-one-out and held-out validation. Coupled physical Sigma/"
         "Defocus fits use robust scalar-prefit starts and report the "
         "process-screen MTF relevant to colour recovery."));
  connect(m_analyzeFocusAreasBtn, &QPushButton::clicked, this,
          [this]() { emit analyzeFocusAreasRequested(m_finetuneFlags); });
  if (m_currentGroupForm)
    m_currentGroupForm->addRow(m_analyzeFocusAreasBtn);
  else
    m_form->addRow(m_analyzeFocusAreasBtn);

  m_focusAreaStatusLabel = new QLabel(tr("No focus analysis areas found."), this);
  m_focusAreaStatusLabel->setWordWrap(true);
  if (m_currentGroupForm)
    m_currentGroupForm->addRow(m_focusAreaStatusLabel);
  else
    m_form->addRow(m_focusAreaStatusLabel);

  // Finetune diagnostic images section (initially hidden)
  m_finetuneImagesPanel = new FinetuneImagesPanel();

  m_finetuneImagesWrapper = new QWidget();
  auto *finetuneImagesContainer = new QVBoxLayout(m_finetuneImagesWrapper);
  finetuneImagesContainer->setContentsMargins(0, 0, 0, 0);

  QWidget *detachableFI =
      createDetachableSection("Finetune Diagnostic Images",
                              m_finetuneImagesPanel);
  finetuneImagesContainer->addWidget(detachableFI);
  
  m_finetuneImagesWrapper->setObjectName(
      QStringLiteral("SharpnessFinetuneImagesRow"));

  if (m_currentGroupForm)
    m_currentGroupForm->addRow(m_finetuneImagesWrapper);
  else
    m_form->addRow(m_finetuneImagesWrapper);
  setParameterApplicability(
      m_finetuneImagesWrapper, [this](const ParameterState &) {
        return m_finetuneImagesAvailable;
      });

  addSeparator("Adaptive sharpening",
               QStringLiteral("sharpness.adaptive"));
  
  QPushButton *analyzeDisplacements = addButtonParameter(
      "", tr("Analyze adaptive sharpening"),
      [this]() { onAnalyzeDisplacements(); },
      [](const ParameterState &s) {
        return colorscreen::screen_geometry_configured_p(s.scrToImg);
      },
      tr("Run adaptive sharpening analysis after screen geometry has been established."));
  analyzeDisplacements->setObjectName(
      QStringLiteral("SharpnessAnalyzeDisplacementsButton"));

  QLabel *adaptiveRequirement = new QLabel(
      tr("Fit screen geometry before adaptive sharpening analysis."), this);
  adaptiveRequirement->setObjectName(
      QStringLiteral("SharpnessAdaptiveRequirement"));
  adaptiveRequirement->setWordWrap(true);
  if (m_currentGroupForm)
    m_currentGroupForm->addRow(tr("Requirement:"), adaptiveRequirement);
  else
    m_form->addRow(tr("Requirement:"), adaptiveRequirement);
  setParameterApplicability(
      adaptiveRequirement, [](const ParameterState &state) {
        return !colorscreen::screen_geometry_configured_p(state.scrToImg);
      });

  QPushButton *clearAdaptiveCorrection = addButtonParameter(
      "", tr("Clear adaptive correction"),
      [this]() {
        applyChange(
            [](ParameterState &state) {
              state.rparams.scanner_blur_correction.reset();
            },
            tr("Clear adaptive sharpening correction"));
      },
      nullptr,
      tr("Remove the accepted spatially varying sharpening correction. "
         "The action is undoable and leaves the analysis settings unchanged."));
  clearAdaptiveCorrection->setObjectName(
      QStringLiteral("SharpnessClearAdaptiveCorrectionButton"));
  setParameterApplicability(
      clearAdaptiveCorrection, [](const ParameterState &state) {
        return state.rparams.scanner_blur_correction != nullptr;
      });

  m_adaptiveChart = new AdaptiveSharpeningChart(this);
  m_adaptiveChart->initialize(10, 10); // Default size until real data comes
  
  m_adaptiveChartWrapper = new QWidget();
  auto *adaptiveChartContainer = new QVBoxLayout(m_adaptiveChartWrapper);
  adaptiveChartContainer->setContentsMargins(0, 0, 0, 0);

  QWidget *detachableChart =
      createDetachableSection("Adaptive Sharpening Chart", m_adaptiveChart);
  adaptiveChartContainer->addWidget(detachableChart);
  m_adaptiveChartWrapper->setObjectName(
      QStringLiteral("SharpnessAdaptiveChartRow"));
  
  if (m_currentGroupForm)
      m_currentGroupForm->addRow(m_adaptiveChartWrapper);
  else
      m_form->addRow(m_adaptiveChartWrapper);
  setParameterApplicability(
      m_adaptiveChartWrapper, [this](const ParameterState &state) {
        return m_adaptiveAnalysisRunning
            || state.rparams.scanner_blur_correction != nullptr;
      });

  // Rows are populated after their section headers. Replay parameter values,
  // applicability and restored folding once construction is complete so a
  // saved collapsed state is correct before the inspector is first shown.
  updateUI();
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

  auto img = m_imageGetter();
  const bool hasRgb = img ? img->has_rgb() : true;

  // Compute model curves with 100 steps for all channels.
  std::array<mtf_parameters::computed_mtf, 4> curves;
  for (int c = 0; c < 4; c++) {
      mtf_parameters p = chartParameters;
      p.wavelength = p.get_channel_wavelength(c, hasRgb);
      curves[c] = p.compute_curves(100);
  }

  // Pass simulation flag to chart
  bool canSimulateDifraction
      = chartParameters.model != colorscreen::mtf_model::empirical_fallback
        && chartParameters.can_simulate_diffraction_p();
  // Calculate screen frequency if applicable
  double screenFreq = -1;
  if (img && colorscreen::screen_geometry_configured_p(state.scrToImg)) {
      colorscreen::scr_to_img scrToImgObj;
      scrToImgObj.set_parameters(state.scrToImg, *img);
      double pixel_size = scrToImgObj.pixel_size({0, 0, img->width, img->height});
      screenFreq = colorscreen::scr_names[(int)state.scrToImg.type].frequency * pixel_size;
  }

  m_mtfChart->setChannelsPresence(img ? img->has_rgb() : true, img ? img->has_grayscale_or_ir() : true);
  m_mtfChart->setMTFData(curves, canSimulateDifraction,
                         state.rparams.sharpen.scanner_mtf.scan_dpi,
                         screenFreq);
  if (m_showSignedOtfCheck)
    m_showSignedOtfCheck->setEnabled(canSimulateDifraction);

  // Pass all measured MTF data if available
  const auto &scanner_mtf = state.rparams.sharpen.scanner_mtf;
  if (!scanner_mtf.measurements.empty()) {
    m_mtfChart->setMeasuredMTF(
        scanner_mtf.measurements,
        {scanner_mtf.get_channel_wavelength (0, hasRgb),
         scanner_mtf.get_channel_wavelength (1, hasRgb),
         scanner_mtf.get_channel_wavelength (2, hasRgb),
         scanner_mtf.get_channel_wavelength (3, hasRgb)});
  } else {
    // No measured data, clear it
    m_mtfChart->setMeasuredMTF({}, {});
  }

}

void SharpnessPanel::updateScreenTiles() {
  // Schedule debounced update
  scheduleTileUpdate();
}

void SharpnessPanel::applyChange(
    std::function<void(ParameterState &)> modifier, const QString &description,
    const QString &parameterKey) {
  ParameterPanel::applyChange(modifier, description, parameterKey);
  updateMTFChart();
  updateScreenTiles();
}

void SharpnessPanel::onParametersRefreshed(const ParameterState &state) {
  updateMeasurementList();
  updateMTFChart();
  updateMtfCalibrationStatus();
  updateScreenTiles();

  // The row's logical applicability is independent of its section fold.
  if (m_adaptiveChartWrapper)
    setParameterRowApplicable(
        m_adaptiveChartWrapper,
        m_adaptiveAnalysisRunning
            || state.rparams.scanner_blur_correction != nullptr);
}

std::vector<std::pair<render_screen_tile_type, QString>>
SharpnessPanel::getTileTypes() const {
  return {{original_screen, "Original"},
          {blurred_screen, "Digitized"},
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

AdaptiveSharpeningChart *SharpnessPanel::getAdaptiveChart() const
{
  return m_adaptiveChart.data();
}

/** Open the adaptive-analysis settings dialog and launch one run.
    The settings are remembered between invocations but are not part of the
    persistent image/render parameter state.  */
void SharpnessPanel::onAnalyzeDisplacements() {
  const ParameterState state = m_stateGetter();
  const bool physicalFocusAvailable
      = state.rparams.sharpen.scanner_mtf.simulate_diffraction_p();
  const bool focusInterpolationAvailable = physicalFocusAvailable;
  const bool varyingStripWidths
      = colorscreen::screen_with_varying_strips_p(state.scrToImg.type);
  const auto image = m_imageGetter();
  const bool hasRgb = image && image->has_rgb();

  if (!m_adaptiveSharpeningParametersInitialized)
    m_adaptiveSharpeningParameters.interpolateFocus
        = focusInterpolationAvailable;

  auto *dialog = new AdaptiveSharpeningDialog(
      m_adaptiveSharpeningParameters, physicalFocusAvailable,
      focusInterpolationAvailable, varyingStripWidths, hasRgb, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &QDialog::accepted, this, [this, dialog]() {
    m_adaptiveSharpeningParameters = dialog->parameters();
    m_adaptiveSharpeningParametersInitialized = true;
    emit adaptiveSharpeningRequested(m_adaptiveSharpeningParameters);
  });
  dialog->open();
}

/** Open the explicit fit dialog. Numerical optimization and publication are
    document-owned so primary and reference panels share one cancellation and
    stale-result policy. */
void SharpnessPanel::fitMeasuredMtf() {
  const ParameterState baseline = m_stateGetter();
  const colorscreen::mtf_parameters current =
      baseline.rparams.sharpen.scanner_mtf;
  auto *dialog = new MTFFitDialog(current, this);
  dialog->setObjectName(QStringLiteral("MtfFitDialog"));
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &QDialog::accepted, this, [this, dialog, baseline]() {
    // The dialog is an editable snapshot. Never start expensive work from it
    // after the source document changed, even if the user later undoes back to
    // the same values: acceptance belongs to this exact dialog lifetime.
    if (m_stateGetter() != baseline) {
      auto *box = new QMessageBox(
          QMessageBox::Information, tr("MTF model fit"),
          tr("The document changed while the fit dialog was open. Reopen the "
             "dialog so its measurements and model values match the current "
             "document."),
          QMessageBox::Ok, this);
      box->setObjectName(QStringLiteral("MtfFitStaleDialog"));
      box->setAttribute(Qt::WA_DeleteOnClose);
      box->open();
      return;
    }

    const colorscreen::mtf_parameters input = dialog->parameters();
    const colorscreen::mtf_estimation_options options = dialog->options();
    const int flags = dialog->estimationFlags();
    const bool accepted =
        m_mtfCalibration.fitRequested &&
        m_mtfCalibration.fitRequested(baseline, input, options, flags, this);
    if (!accepted) {
      auto *box = new QMessageBox(
          QMessageBox::Information, tr("MTF model fit"),
          tr("The MTF fit could not be started because its document is closing, "
             "its inputs changed, or another model fit is already active."),
          QMessageBox::Ok, this);
      box->setObjectName(QStringLiteral("MtfFitUnavailableDialog"));
      box->setAttribute(Qt::WA_DeleteOnClose);
      box->open();
    }
  });
  dialog->open();
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

QString SharpnessPanel::mtfCalibrationSummary() const {
  if (m_mtfCalibration.summary)
    return m_mtfCalibration.summary();
  const qsizetype count = static_cast<qsizetype>(
      m_stateGetter().rparams.sharpen.scanner_mtf.measurements.size());
  return count == 0
      ? tr("Capture MTF: not measured")
      : tr("Capture MTF: %1 saved measurement%2 • ready to fit/validate model")
            .arg(count)
            .arg(count == 1 ? QString() : QStringLiteral("s"));
}

void SharpnessPanel::updateMtfCalibrationStatus() {
  if (!m_mtfFitStatusLabel)
    return;
  QString status = mtfCalibrationSummary();
  const QString prefix = tr("Capture MTF: ");
  if (status.startsWith(prefix))
    status.remove(0, prefix.size());
  m_mtfFitStatusLabel->setText(status);
}

void SharpnessPanel::refreshMtfCalibrationStatus() {
  updateMtfCalibrationStatus();
  if (m_fitMtfBtn) {
    const bool hasMeasurements =
        !m_stateGetter().rparams.sharpen.scanner_mtf.measurements.empty();
    const bool sectionOpen = !m_scannerCameraSeparatorToggle ||
                             m_scannerCameraSeparatorToggle->isChecked();
    const bool documentAvailable =
        !m_mtfCalibration.fitAvailable || m_mtfCalibration.fitAvailable();
    m_fitMtfBtn->setEnabled(hasMeasurements && sectionOpen && documentAvailable);
  }
}

void SharpnessPanel::selectMtfMeasurement(int index) {
  const auto &measurements =
      m_stateGetter().rparams.sharpen.scanner_mtf.measurements;
  if (index < 0 || index >= static_cast<int>(measurements.size()))
    index = -1;
  const bool changed = m_selectedMtfMeasurement != index;
  m_selectedMtfMeasurement = index;

  if (m_measurementSelector) {
    const QSignalBlocker blocker(m_measurementSelector);
    const int row = m_measurementSelector->findData(index);
    m_measurementSelector->setCurrentIndex(row >= 0 ? row : 0);
  }
  if (m_mtfChart)
    m_mtfChart->setSelectedMeasurement(index);
  updateSelectedMeasurementDetails();
  if (changed)
    emit mtfMeasurementSelected(index);
}

void SharpnessPanel::updateSelectedMeasurementDetails() {
  if (!m_measurementDetailLabel || !m_locateMeasurementBtn)
    return;
  const auto &measurements =
      m_stateGetter().rparams.sharpen.scanner_mtf.measurements;
  if (m_selectedMtfMeasurement < 0 ||
      m_selectedMtfMeasurement >= static_cast<int>(measurements.size())) {
    m_measurementDetailLabel->setText(
        measurements.empty()
            ? tr("No measured MTF curves are stored.")
            : tr("Select a measured curve or legend entry to inspect its source and edge."));
    m_measurementDetailLabel->setToolTip(QString());
    m_locateMeasurementBtn->setEnabled(false);
    return;
  }

  const colorscreen::mtf_measurement &m =
      measurements[m_selectedMtfMeasurement];
  QString source;
  if (!m.source_filename.empty()) {
    const QString full = QString::fromUtf8(m.source_filename.c_str());
    source = QFileInfo(full).fileName();
    if (source.isEmpty())
      source = full;
    m_measurementDetailLabel->setToolTip(full);
  } else {
    source = tr("unknown source");
    m_measurementDetailLabel->setToolTip(QString());
  }
  if (m.source_width > 0 && m.source_height > 0)
    source += tr(" (%1×%2)").arg(m.source_width).arg(m.source_height);

  QString detail = tr("Source: %1").arg(source);
  if (m.has_spatial_metadata()) {
    detail += tr("\nROI: x=%1, y=%2, %3×%4 px")
                  .arg(m.roi.x).arg(m.roi.y).arg(m.roi.width).arg(m.roi.height);
    detail += tr(" • edge (%1, %2) → (%3, %4)")
                  .arg(m.edge_p1.x, 0, 'f', 2)
                  .arg(m.edge_p1.y, 0, 'f', 2)
                  .arg(m.edge_p2.x, 0, 'f', 2)
                  .arg(m.edge_p2.y, 0, 'f', 2);
    detail += tr("\nAngle %1° • fit RMS %2 px • contrast %3 • SNR %4 • phase %5%")
                  .arg(m.edge_angle, 0, 'f', 3)
                  .arg(m.edge_fit_rms, 0, 'f', 3)
                  .arg(m.edge_contrast, 0, 'g', 4)
                  .arg(m.edge_snr, 0, 'f', 1)
                  .arg(m.phase_coverage * 100.0, 0, 'f', 1);
    m_locateMeasurementBtn->setEnabled(true);
  } else {
    detail += tr("\nLocation unavailable for this legacy/imported measurement.");
    m_locateMeasurementBtn->setEnabled(false);
  }
  m_measurementDetailLabel->setText(detail);
}

void SharpnessPanel::updateMeasurementList() {
    if (!m_measurementsLayout) return;

    ParameterState state = m_stateGetter();
    const auto &measurements = state.rparams.sharpen.scanner_mtf.measurements;

    // Memoization to avoid flickering and unnecessary rebuilds
    if (m_measurementUiInitialized && measurements == m_lastMeasurements) {
        updateSelectedMeasurementDetails();
        return;
    }
    m_measurementUiInitialized = true;
    m_lastMeasurements = measurements;

    /* Structural/metadata edits can reorder or replace records. Clear the
       inspection index rather than accidentally associating an old overlay
       with a different measurement occupying the same vector position. */
    m_selectedMtfMeasurement = -1;
    if (m_mtfChart)
        m_mtfChart->setSelectedMeasurement(-1);
    if (m_measurementSelector) {
        const QSignalBlocker blocker(m_measurementSelector);
        m_measurementSelector->clear();
        m_measurementSelector->addItem(tr("None"), -1);
        for (int i = 0; i < static_cast<int>(measurements.size()); ++i) {
            QString label = tr("%1 — %2")
                                .arg(i + 1)
                                .arg(QString::fromStdString(measurements[i].name));
            m_measurementSelector->addItem(label, i);
        }
        m_measurementSelector->setCurrentIndex(0);
        m_measurementSelector->setEnabled(!measurements.empty());
    }
    updateSelectedMeasurementDetails();
    emit mtfMeasurementSelected(-1);

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
        const QString measurementKeyBase =
            QStringLiteral("sharpness.measurements.%1").arg(i);
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
        const QString nameKey = measurementKeyBase + QStringLiteral(".name");
        nameEdit->setProperty("parameterKey", nameKey);
        connect(nameEdit, &QLineEdit::editingFinished, this,
                [this, i, nameEdit, nameKey]() {
            applyChange([i, nameEdit](ParameterState &s) {
                if (i < (int)s.rparams.sharpen.scanner_mtf.measurements.size()) {
                    s.rparams.sharpen.scanner_mtf.measurements[i].name = nameEdit->text().toStdString();
                }
            }, tr("Change MTF measurement name"), nameKey);
        });
        hLayout->addWidget(nameEdit, 1);

        // Channel
        QComboBox *chanCombo = new QComboBox();
        const QString channelKey =
            measurementKeyBase + QStringLiteral(".channel");
        chanCombo->setProperty("parameterKey", channelKey);
        chanCombo->addItem(tr("Unknown"), -1);
        chanCombo->addItem(tr("Red"), 0);
        chanCombo->addItem(tr("Green"), 1);
        chanCombo->addItem(tr("Blue"), 2);
        chanCombo->addItem(tr("IR"), 3);

        int idx = chanCombo->findData(m.channel);
        if (idx != -1) chanCombo->setCurrentIndex(idx);
        chanCombo->setFixedWidth(100);

        connect(chanCombo, QOverload<int>::of(&QComboBox::activated), this,
                [this, i, chanCombo, channelKey](int index) {
            int val = chanCombo->itemData(index).toInt();
            applyChange([i, val](ParameterState &s) {
                if (i < (int)s.rparams.sharpen.scanner_mtf.measurements.size()) {
                    s.rparams.sharpen.scanner_mtf.measurements[i].channel = val;
                }
            }, tr("Change MTF measurement channel"), channelKey);
        });
        hLayout->addWidget(chanCombo);

        // Wavelength
        QDoubleSpinBox *waveSpin = new QDoubleSpinBox();
        const QString wavelengthKey =
            measurementKeyBase + QStringLiteral(".wavelength");
        waveSpin->setProperty("parameterKey", wavelengthKey);
        waveSpin->setRange(0, 2000);
        waveSpin->setValue(m.wavelength);
        waveSpin->setSuffix(" nm");
        waveSpin->setDecimals(3);
        waveSpin->setSpecialValueText(tr("unknown"));
        waveSpin->setToolTip(
            tr("Authoritative wavelength of this measured edge. Channel is "
               "only a label and does not disable this field."));
        waveSpin->setFixedWidth(120);
        connect(waveSpin, &QDoubleSpinBox::editingFinished, this,
                [this, i, waveSpin, wavelengthKey]() {
            double val = waveSpin->value();
            applyChange([i, val](ParameterState &s) {
                if (i < (int)s.rparams.sharpen.scanner_mtf.measurements.size()) {
                    s.rparams.sharpen.scanner_mtf.measurements[i].wavelength = val;
                }
            }, tr("Change MTF measurement wavelength"), wavelengthKey);
        });
        hLayout->addWidget(waveSpin);

        // Same capture
        QCheckBox *sameCheck = new QCheckBox();
        const QString sameCaptureKey =
            measurementKeyBase + QStringLiteral(".same_capture");
        sameCheck->setProperty("parameterKey", sameCaptureKey);
        sameCheck->setFixedWidth(50);
        sameCheck->setToolTip(
            tr("Share the fitted focus displacement with the preceding "
               "measurement because both curves came from the same capture."));
        sameCheck->setChecked(m.same_capture);
        if (i == 0) {
            sameCheck->setChecked(false);
            sameCheck->setEnabled(false);
        }

        connect(sameCheck, &QCheckBox::toggled, this,
                [this, i, sameCaptureKey](bool v) {
            applyChange([i, v](ParameterState &s) {
                if (i < (int)s.rparams.sharpen.scanner_mtf.measurements.size()) {
                    s.rparams.sharpen.scanner_mtf.measurements[i].same_capture = v;
                }
            }, tr("Change MTF measurement same capture"), sameCaptureKey);
        });
        hLayout->addWidget(sameCheck);

        m_measurementsLayout->addWidget(row);
    }
}
void SharpnessPanel::updateFinetuneImages(
    const colorscreen::finetune_result &result) {
  if (m_finetuneImagesPanel)
    m_finetuneImagesPanel->setFinetuneResult(result);
  m_finetuneImagesAvailable = true;
  updateWidgetStates();
}

void SharpnessPanel::showAdaptiveChart() {
  setAdaptiveAnalysisRunning(true);
}

/** Keep live adaptive diagnostics visible without bypassing section folding. */
void SharpnessPanel::setAdaptiveAnalysisRunning(bool running) {
  m_adaptiveAnalysisRunning = running;
  updateWidgetStates();
}

void SharpnessPanel::setFocusAnalysisChecked(bool checked) {
    if (m_analyzeAreaBtn) {
        QSignalBlocker signalBlocker1(m_analyzeAreaBtn);
        m_analyzeAreaBtn->setChecked(checked);
        signalBlocker1.unblock();
    }
}

/** Update controls for the document-local automatic focus-area workflow. */
void SharpnessPanel::setFocusAreaAnalysisState(int candidateCount, bool running,
                                               const QString &summary) {
    const bool geometryReady = colorscreen::screen_geometry_configured_p(
        m_stateGetter().scrToImg);
    if (m_findFocusAreasBtn)
        m_findFocusAreasBtn->setEnabled(!running && geometryReady);
    if (m_analyzeFocusAreasBtn)
        m_analyzeFocusAreasBtn->setEnabled(!running && geometryReady && candidateCount >= 3);
    if (m_focusAreaStatusLabel) {
        if (!geometryReady)
            m_focusAreaStatusLabel->setText(
                tr("Fit screen geometry before focus analysis."));
        else if (!summary.isEmpty())
            m_focusAreaStatusLabel->setText(summary);
        else if (running)
            m_focusAreaStatusLabel->setText(tr("Focus-area analysis running…"));
        else
            m_focusAreaStatusLabel->setText(
                tr("%1 candidate focus area(s) available.").arg(candidateCount));
    }
}

void SharpnessPanel::setMeasureMtfChecked(bool checked) {
    if (m_measureMtfBtn) {
        QSignalBlocker signalBlocker2(m_measureMtfBtn);
        m_measureMtfBtn->setChecked(checked);
        signalBlocker2.unblock();
    }
}

void SharpnessPanel::setMeasureMtfEnabled(bool enabled) {
    if (m_measureMtfBtn) {
        m_measureMtfBtn->setEnabled(enabled);
    }
}

