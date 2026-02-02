#include "SharpnessPanel.h"
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img.h"
#include "MTFChartWidget.h"
#include "FinetuneImagesPanel.h"
#include <QDebug>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QString>
#include <QFileDialog>
#include <QFormLayout>
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
#include <QVBoxLayout> // Added
#include <QtConcurrent>
#include <QMimeData>
#include <QDrag>
#include <QMouseEvent>

using namespace colorscreen;
using sharpen_mode = colorscreen::sharpen_parameters::sharpen_mode;

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
                    if (w && event->pos().y() < w->geometry().bottom()) {
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
    : TilePreviewPanel(stateGetter, stateSetter, imageGetter, parent) {
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
      });

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
      }, 3);

  // Wavelength
  // Range 0.0 - 1200.0 (0.0 = unknown)
  addSliderParameter(
      "Wavelength", 0.0, 1200.0, 10.0, 1, "nm", "unknown",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.wavelength;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf.wavelength = v;
      });

  // Defocus
  // Range 0.0 - 10.0 mm
  // Non-linear Gamma 2.0 (slow start)
  // Enabled only if simulate_difraction_p()
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
        return s.rparams.sharpen.scanner_mtf.simulate_difraction_p();
      }, 3);

  // Blur diameter
  // Range 0.0 - 20.0 pixels
  // Non-linear Gamma 2.0 (slow start)
  // Enabled only if !simulate_difraction_p()
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
        return !s.rparams.sharpen.scanner_mtf.simulate_difraction_p();
      });


  // Add "Match measured data" button (visible only if measured data exists)
  addButtonParameter("", "Match measured data", 
    [this]() {
      applyChange([](ParameterState &s) {
        const char *error = nullptr;
        s.rparams.sharpen.scanner_mtf.estimate_parameters(
            s.rparams.sharpen.scanner_mtf, nullptr, nullptr, &error);
      });
    },
    [this, separatorToggle](const ParameterState &s) {
       bool visible = s.rparams.sharpen.scanner_mtf.measurements.size() > 0;
       if (separatorToggle && !separatorToggle->isChecked())
         visible = false;
       return visible;
    });

  // MTF Scale
  // Range 0.0 - 2.0 (0.0 = no MTF)
  addSliderParameter(
      "MTF scale", 0.0, 2.0, 100.0, 2, "", "no MTF",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf_scale;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf_scale = v;
      });

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
      });

  addSeparator("Wiener filter");

  // Signal to noise ratio
  // Range 0 - 65535, slow at start (gamma 2.0)
  addSliderParameter(
      "Signal to noise ratio", 0.0, 65535.0, 1.0, 0, "", "",
      [](const ParameterState &s) { return s.rparams.sharpen.scanner_snr; },
      [](ParameterState &s, double v) { s.rparams.sharpen.scanner_snr = v; },
      2.0 // Gamma (slow start)
  );

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
      2.0 // Gamma (slow start)
  );

  // Richardson-Lucy sigma
  // Range 0.0 - 2.0, floating point
  addSliderParameter(
      "Sigma", 0.0, 2.0, 1000.0, 3, "", "",
      [](const ParameterState &s) {
        return s.rparams.sharpen.richardson_lucy_sigma;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.richardson_lucy_sigma = v;
      });

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
      }, 3);

  addSliderParameter(
      "Amount", 0.0, 100.0, 100.0, 1, "", "",
      [](const ParameterState &s) { return s.rparams.sharpen.usm_amount; },
      [](ParameterState &s, double v) { s.rparams.sharpen.usm_amount = v; },
      2.0, // Gamma (slow start)
      [](const ParameterState &s) {
        return s.rparams.sharpen.mode == sharpen_mode::unsharp_mask;
      });

  addSeparator("Focus analyzer");
  
  addCheckboxParameter(
      "Optimize Sigma",
      [this](const ParameterState &) {
        return (m_finetuneFlags & colorscreen::finetune_scanner_mtf_sigma) != 0;
      },
      [this](ParameterState &, bool v) {
        if (v) m_finetuneFlags |= colorscreen::finetune_scanner_mtf_sigma;
        else m_finetuneFlags &= ~colorscreen::finetune_scanner_mtf_sigma;
      });

  addCheckboxParameter(
      "Optimize Defocus",
      [this](const ParameterState &) {
        return (m_finetuneFlags & colorscreen::finetune_scanner_mtf_defocus) != 0;
      },
      [this](ParameterState &, bool v) {
        if (v) m_finetuneFlags |= colorscreen::finetune_scanner_mtf_defocus;
        else m_finetuneFlags &= ~colorscreen::finetune_scanner_mtf_defocus;
      });

  m_analyzeAreaBtn = addToggleButtonParameter("", tr("Analyze area"), [this](bool checked) {
    emit focusAnalysisRequested(checked, m_finetuneFlags);
  });

  // Finetune diagnostic images section (initially hidden)
  m_finetuneImagesPanel = new FinetuneImagesPanel();
  m_finetuneImagesPanel->hide();

  QWidget *fiWrapper = new QWidget();
  m_finetuneImagesContainer = new QVBoxLayout(fiWrapper);
  m_finetuneImagesContainer->setContentsMargins(0, 0, 0, 0);

  QWidget *detachableFI =
      createDetachableSection("Finetune Diagnostic Images", m_finetuneImagesPanel, [this]() {
        emit detachFinetuneImagesRequested(m_finetuneImagesPanel);
      });
  m_finetuneImagesContainer->addWidget(detachableFI);

  if (m_currentGroupForm)
    m_currentGroupForm->addRow(fiWrapper);
  else
    m_form->addRow(fiWrapper);
}

void SharpnessPanel::updateMTFChart() {
  if (!m_mtfChart)
    return;

  ParameterState state = m_stateGetter();

  // Compute MTF curves with 100 steps
  mtf_parameters::computed_mtf curves =
      state.rparams.sharpen.scanner_mtf.compute_curves(100);

  // Pass simulation flag to chart
  bool canSimulateDifraction =
      state.rparams.sharpen.scanner_mtf.simulate_difraction_p();
  // Calculate screen frequency if applicable
  double screenFreq = -1;
  auto img = m_imageGetter();
  if (img && state.scrToImg.type != colorscreen::Random) {
      colorscreen::scr_to_img scrToImgObj;
      scrToImgObj.set_parameters(state.scrToImg, *img);
      double pixel_size = scrToImgObj.pixel_size(img->width, img->height);
      screenFreq = colorscreen::scr_names[(int)state.scrToImg.type].frequency * pixel_size;
  }

  m_mtfChart->setMTFData(curves, canSimulateDifraction,
                         state.rparams.sharpen.scanner_mtf.scan_dpi,
                         screenFreq);

  // Pass all measured MTF data if available
  const auto &scanner_mtf = state.rparams.sharpen.scanner_mtf;
  if (!scanner_mtf.measurements.empty()) {
    m_mtfChart->setMeasuredMTF(scanner_mtf.measurements, {scanner_mtf.get_channel_wavelength (0), scanner_mtf.get_channel_wavelength (1), scanner_mtf.get_channel_wavelength (2), scanner_mtf.get_channel_wavelength (3)});
  } else {
    // No measured data, clear it
    m_mtfChart->setMeasuredMTF({}, {});
  }

  // Update diffraction notice
  bool canSimulate = state.rparams.sharpen.scanner_mtf.can_simulate_difraction_p();
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
}

// Methods removed as they are now in TilePreviewPanel or handled by it
// scheduleTileUpdate, startNextRender, performTileRender, resizeEvent

std::vector<std::pair<render_screen_tile_type, QString>>
SharpnessPanel::getTileTypes() const {
  return {{original_screen, "Original"},
          {blured_screen, "Blured"},
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

// reattachTiles removed (in base)
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
        waveSpin->setDecimals(1);
        waveSpin->setSpecialValueText(tr("unknown"));
        waveSpin->setEnabled(m.channel == -1);
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
        sameCheck->setToolTip(tr("If enabled solver will assume that measurement come from the same capture as prevoius one and will use the same focus displacement"));
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
        // Force layout update of the section container
        if (m_finetuneImagesPanel->parentWidget()) {
            m_finetuneImagesPanel->parentWidget()->show();
        }
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

void SharpnessPanel::setFocusAnalysisChecked(bool checked) {
    if (m_analyzeAreaBtn) {
        m_analyzeAreaBtn->blockSignals(true);
        m_analyzeAreaBtn->setChecked(checked);
        m_analyzeAreaBtn->blockSignals(false);
    }
}
