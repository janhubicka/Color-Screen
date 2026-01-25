#include "CapturePanel.h"
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFormLayout>
#include <QSlider>
#include <QDoubleSpinBox>
#include "../libcolorscreen/include/scr-to-img.h"
#include "../libcolorscreen/include/imagedata.h"
#include "BacklightChartWidget.h"

CapturePanel::CapturePanel(StateGetter stateGetter, StateSetter stateSetter, ImageGetter imageGetter, ReloadCallback reloadCallback, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent), m_reloadCallback(reloadCallback)
{
    setupUi();
}

CapturePanel::~CapturePanel() = default;

void CapturePanel::setupUi()
{
    auto onUseGamma = [this]() {
        auto img = m_imageGetter();
        if (img && img->gamma >= 0) {
            applyChange([img](ParameterState &s) { s.rparams.gamma = img->gamma; }, "Use detected gamma");
        } else if (img && img->gamma == -1) {
            applyChange([](ParameterState &s) { s.rparams.gamma = -1.0; }, "Use sRGB gamma");
        }
    };

    auto onUseRes = [this](double res) {
        if (res > 0) {
            applyChange([res](ParameterState &s) { s.rparams.sharpen.scanner_mtf.scan_dpi = res; }, "Use image resolution");
        }
    };

    auto onUseFStop = [this]() {
        auto img = m_imageGetter();
        if (img && img->f_stop > 0) {
            applyChange([img](ParameterState &s) { s.rparams.sharpen.scanner_mtf.f_stop = img->f_stop; }, "Use EXIF f-stop");
        }
    };

    auto onUsePixelPitch = [this]() {
        auto img = m_imageGetter();
        if (img && img->pixel_pitch > 0) {
            applyChange([img](ParameterState &s) { s.rparams.sharpen.scanner_mtf.pixel_pitch = img->pixel_pitch; }, "Use EXIF pixel pitch");
        }
    };

    auto onUseMirror = [this]() {
        auto img = m_imageGetter();
        if (img && img->mirror != -1) {
            applyChange([img](ParameterState &s) { s.rparams.scan_mirror = (img->mirror != 0); }, "Use EXIF mirroring");
        }
    };

    auto addValueWithUseButton = [&](const QString &label, QLabel **valueLabel, QPushButton **useBtn, std::function<void()> onUse) {
        QWidget *container = new QWidget();
        QHBoxLayout *hLayout = new QHBoxLayout(container);
        hLayout->setContentsMargins(0, 0, 0, 0);
        hLayout->setSpacing(5);

        *valueLabel = new QLabel();
        (*valueLabel)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hLayout->addWidget(*valueLabel, 1);

        *useBtn = new QPushButton("Use");
        (*useBtn)->setFixedWidth(40);
        (*useBtn)->setVisible(false);
        hLayout->addWidget(*useBtn, 0);

        if (onUse) {
            connect(*useBtn, &QPushButton::clicked, this, onUse);
        }

        m_form->addRow(label, container);
    };

    // 0. Demosaic (Enum) + Reload
    QWidget *demosaicContainer = new QWidget();
    QHBoxLayout *demosaicHLayout = new QHBoxLayout(demosaicContainer);
    demosaicHLayout->setContentsMargins(0, 0, 0, 0);
    demosaicHLayout->setSpacing(5);

    m_demosaicCombo = new QComboBox();
    m_demosaicCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_demosaicCombo->setMinimumContentsLength(10);
    m_demosaicCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    for (int i = 0; i < (int)colorscreen::image_data::demosaic_max; ++i) {
        m_demosaicCombo->addItem(QString::fromUtf8(colorscreen::image_data::demosaic_names[i].pretty_name), i);
        if (colorscreen::image_data::demosaic_names[i].help) {
            m_demosaicCombo->setItemData(i, QString::fromUtf8(colorscreen::image_data::demosaic_names[i].help), Qt::ToolTipRole);
        }
    }
    demosaicHLayout->addWidget(m_demosaicCombo, 1);

    m_reloadDemosaicBtn = new QPushButton("Reload and demosaic");
    m_reloadDemosaicBtn->setVisible(false);
    demosaicHLayout->addWidget(m_reloadDemosaicBtn, 0);

    m_form->addRow("Demosaic", demosaicContainer);

    connect(m_demosaicCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        int val = m_demosaicCombo->itemData(index).toInt();
        applyChange([val](ParameterState &s) { s.rparams.demosaic = (colorscreen::image_data::demosaicing_t)val; }, "Demosaic");
    });

    connect(m_reloadDemosaicBtn, &QPushButton::clicked, this, [this]() {
        if (m_reloadCallback) m_reloadCallback();
    });

    // 1. Image gamma (Slider)
    std::map<double, QString> gammas;
    gammas[-1.0] = "sRGB gamma";
    gammas[0.0] = "Use ICC profile";
    
    std::map<double, QString> quickSelects = gammas;
    quickSelects[1.0] = "1.0";
    quickSelects[1.9] = "1.9";
    quickSelects[2.2] = "2.2";
    
    addDoubleParameter("Image gamma", -1.0, 5.0, 
        [](const ParameterState &s) { return s.rparams.gamma; },
        [](ParameterState &s, double v) { s.rparams.gamma = v; },
        gammas,
        quickSelects,
        nullptr
    );

    // 2. Detected gamma (Label) + Use
    addValueWithUseButton("Detected gamma", &m_detectedGammaValue, &m_useDetectedGammaBtn, [this, onUseGamma]() { onUseGamma(); });

    // 3. Resolution (Slider)
    addSliderParameter(
        "Resolution", 0.0, 10000.0, 10.0, 1, "PPI", "unknown",
        [](const ParameterState &s) {
          return s.rparams.sharpen.scanner_mtf.scan_dpi;
        },
        [](ParameterState &s, double v) {
          s.rparams.sharpen.scanner_mtf.scan_dpi = v;
        });

    // 4. Image resolution (Label) + Use
    addValueWithUseButton("Image resolution", &m_imageResolutionValue, &m_useImageResBtn, [this, onUseRes]() {
        auto img = m_imageGetter();
        if (img) onUseRes(img->xdpi);
    });

    // 5. Resolution from screen (Label) + Use
    addValueWithUseButton("Resolution from screen", &m_screenResolutionValue, &m_useScreenResBtn, [this, onUseRes]() {
        ParameterState state = m_stateGetter();
        auto img = m_imageGetter();
        if (img && img->width > 0 && img->height > 0) {
            colorscreen::scr_to_img map;
            map.set_parameters(state.scrToImg, *img);
            double pixel_size = map.pixel_size(img->width, img->height);
            double estimated_dpi = state.scrToImg.estimate_dpi(pixel_size);
            onUseRes(estimated_dpi);
        }
    });

    // 6. Focal plane resolution (Label) + Use
    addValueWithUseButton("Focal plane res", &m_focalPlaneResValue, &m_useFocalPlaneResBtn, [this, onUseRes]() {
        auto img = m_imageGetter();
        if (img) onUseRes(img->focal_plane_x_resolution);
    });

    // 7. Camera model (Label)
    m_cameraModelValue = new QLabel();
    m_cameraModelValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->addRow("Camera model", m_cameraModelValue);

    // 8. Lens (Label)
    m_lensValue = new QLabel();
    m_lensValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->addRow("Lens", m_lensValue);

    // 8b. Software (Label)
    m_softwareValue = new QLabel();
    m_softwareValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->addRow("Software", m_softwareValue);

    // 9. Nominal f-stop (Slider)
    addSliderParameter(
        "Nominal f-stop", 0.0, 64.0, 1000.0, 2, "", "unknown",
        [](const ParameterState &s) {
          return s.rparams.sharpen.scanner_mtf.f_stop;
        },
        [](ParameterState &s, double v) {
          s.rparams.sharpen.scanner_mtf.f_stop = v;
        });

    // 10. F-stop (Label) + Use
    addValueWithUseButton("F-stop", &m_fStopValue, &m_useFStopBtn, [this, onUseFStop]() { onUseFStop(); });

    // 11. Focal length (Label)
    m_focalLengthValue = new QLabel();
    m_focalLengthValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->addRow("Focal length", m_focalLengthValue);

    // 12. Focal length (35mm) (Label)
    m_focalLength35mmValue = new QLabel();
    m_focalLength35mmValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->addRow("Focal length (35mm)", m_focalLength35mmValue);

    // 12b. Mirroring (Label) + Use
    addValueWithUseButton("Mirroring", &m_mirrorValue, &m_useMirrorBtn, [this, onUseMirror]() { onUseMirror(); });

    // 12c. Sensor presets (Dropdown)
    QComboBox *presets = new QComboBox();
    presets->addItem("Presets...", 0.0);
    presets->addItem("PhaseOne 53.4mm", 53.4);
    presets->addItem("PhaseOne 53.7mm", 53.7);
    presets->addItem("Medium Format 43.8mm", 43.8);
    presets->addItem("Full Frame (36mm)", 36.0);
    presets->addItem("APS-H (28.3mm)", 28.3);
    presets->addItem("APS-C (23.0mm)", 23.0);
    presets->addItem("Micro Four Thirds (17.3mm)", 17.3);
    presets->addItem("1-inch (13.2mm)", 13.2);
    presets->addItem("1/1.7-inch (7.6mm)", 7.6);
    presets->addItem("1/2.5-inch (5.76mm)", 5.76);
    
    connect(presets, &QComboBox::activated, this, [this, presets](int index) {
        double val = presets->itemData(index).toDouble();
        if (val > 0) {
            auto img = m_imageGetter();
            if (img && img->width > 0) {
                double pitch = (val * 1000.0) / img->width;
                applyChange([pitch](ParameterState &s) {
                    s.rparams.sharpen.scanner_mtf.pixel_pitch = pitch;
                }, "Sensor width preset");
            }
        }
        presets->setCurrentIndex(0);
    });
    m_form->addRow("Sensor presets", presets);

    // 13. Sensor width (Slider)
    // We need to store this widget to update it
    QWidget *sensorWidthSlider = addSlider(
        "Sensor width", 0.0, 1000.0, 10.0, 2, "mm", "unknown",
        0.0,
        [this](double v) {
            auto img = m_imageGetter();
            if (img && img->width > 0 && v > 0) {
                double pitch = (v * 1000.0) / img->width;
                applyChange([pitch](ParameterState &s) {
                    s.rparams.sharpen.scanner_mtf.pixel_pitch = pitch;
                }, "Sensor width");
            } else if (v == 0) {
                applyChange([](ParameterState &s) {
                    s.rparams.sharpen.scanner_mtf.pixel_pitch = 0;
                }, "Sensor width");
            }
        }
    );

    // 14. Sensor pixel pitch (Slider)
    addSliderParameter(
        "Sensor pixel pitch", 0.0, 100.0, 1000.0, 3, "μm", "unknown",
        [](const ParameterState &s) {
          return s.rparams.sharpen.scanner_mtf.pixel_pitch;
        },
        [](ParameterState &s, double v) {
          s.rparams.sharpen.scanner_mtf.pixel_pitch = v;
        });

    // 15. Pixel pitch (Label) + Use
    addValueWithUseButton("Pixel pitch", &m_pixelPitchValue, &m_usePixelPitchBtn, [this, onUsePixelPitch]() { onUsePixelPitch(); });

    // 16. Sensor fill factor (Slider)
    addSliderParameter(
        "Sensor fill factor", 0.0, 8.0, 1000.0, 3, "", "unknown",
        [](const ParameterState &s) {
          return s.rparams.sharpen.scanner_mtf.sensor_fill_factor;
        },
        [](ParameterState &s, double v) {
          s.rparams.sharpen.scanner_mtf.sensor_fill_factor = v;
        });

    auto updateInfoLabels = [this, sensorWidthSlider](const ParameterState &state) {
        auto img = m_imageGetter();
        
        auto setVisibleRow = [&](QWidget *field, bool visible) {
            field->setVisible(visible);
            if (auto lab = m_form->labelForField(field))
                lab->setVisible(visible);
        };

        // 1 & 2. Gamma
        bool showGamma = false;
        if (img && img->gamma != -2) {
            showGamma = true;
            if (img->gamma == -1.0) {
                m_detectedGammaValue->setText("sRGB gamma");
            } else if (img->gamma == 0.0) {
                m_detectedGammaValue->setText("Use ICC profile");
            } else {
                m_detectedGammaValue->setText(QString::number(img->gamma));
            }
            m_useDetectedGammaBtn->setVisible(std::abs(img->gamma - state.rparams.gamma) > 0.001);
        }
        setVisibleRow(m_detectedGammaValue->parentWidget(), showGamma);

        // 4. Image Resolution
        bool showImageRes = false;
        double imgRes = 0;
        if (img && img->xdpi > 0 && img->ydpi > 0) {
            showImageRes = true;
            imgRes = img->xdpi;
            if (std::abs(img->xdpi - img->ydpi) < 1e-6) {
                m_imageResolutionValue->setText(QString("%1 PPI").arg(img->xdpi));
            } else {
                m_imageResolutionValue->setText(QString("%1x%2 PPI").arg(img->xdpi).arg(img->ydpi));
            }
            m_useImageResBtn->setVisible(std::abs(img->xdpi - state.rparams.sharpen.scanner_mtf.scan_dpi) > 0.1);
        }
        setVisibleRow(m_imageResolutionValue->parentWidget(), showImageRes);

        // 5. Screen Resolution
        bool showScreenRes = false;
        if (state.scrToImg.type != colorscreen::Random) {
            if (img && img->width > 0 && img->height > 0) {
                colorscreen::scr_to_img map;
                map.set_parameters(state.scrToImg, *img);
                double pixel_size = map.pixel_size(img->width, img->height);
                double estimated_dpi = state.scrToImg.estimate_dpi(pixel_size);
                if (estimated_dpi > 0) {
                    showScreenRes = true;
                    m_screenResolutionValue->setText(QString("%1 PPI").arg(estimated_dpi, 0, 'f', 1));
                    m_useScreenResBtn->setVisible(std::abs(estimated_dpi - state.rparams.sharpen.scanner_mtf.scan_dpi) > 0.1);
                }
            }
        }
        setVisibleRow(m_screenResolutionValue->parentWidget(), showScreenRes);

        // 0. Demosaic
        bool canDemosaic = img && img->demosaiced_by != colorscreen::image_data::demosaic_max;
        if (canDemosaic) {
            m_demosaicCombo->blockSignals(true);
            int idx = m_demosaicCombo->findData((int)state.rparams.demosaic);
            if (idx != -1) m_demosaicCombo->setCurrentIndex(idx);
            m_demosaicCombo->blockSignals(false);
            
            bool needsReload = (state.rparams.demosaic != img->demosaiced_by);
            m_reloadDemosaicBtn->setVisible(needsReload);
        }
        setVisibleRow(m_demosaicCombo->parentWidget(), canDemosaic);

        // 6. Focal plane resolution
        bool showFocalPlane = img && img->focal_plane_x_resolution > 0 && img->focal_plane_y_resolution > 0;
        if (showFocalPlane) {
            QString fpText;
            if (std::abs(img->focal_plane_x_resolution - img->focal_plane_y_resolution) < 1e-6)
                fpText = QString("%1 PPI").arg(img->focal_plane_x_resolution, 0, 'f', 1);
            else
                fpText = QString("%1x%2 PPI").arg(img->focal_plane_x_resolution, 0, 'f', 1).arg(img->focal_plane_y_resolution, 0, 'f', 1);
            m_focalPlaneResValue->setText(fpText);
            m_useFocalPlaneResBtn->setVisible(std::abs(img->focal_plane_x_resolution - state.rparams.sharpen.scanner_mtf.scan_dpi) > 0.1);
        }
        setVisibleRow(m_focalPlaneResValue->parentWidget(), showFocalPlane);

        // 7 & 8. Camera/Lens
        if (img) {
            m_cameraModelValue->setText(QString::fromStdString(img->camera_model));
            m_lensValue->setText(QString::fromStdString(img->lens));
        }
        setVisibleRow(m_cameraModelValue, img && !img->camera_model.empty());
        setVisibleRow(m_lensValue, img && !img->lens.empty());

        // 8b. Software
        if (img) {
            m_softwareValue->setText(QString::fromStdString(img->software));
        }
        setVisibleRow(m_softwareValue, img && !img->software.empty());

        // 10. F-stop
        bool showFStopExif = img && img->f_stop > 0;
        if (showFStopExif) {
            m_fStopValue->setText(QString("f/%1").arg(img->f_stop, 0, 'f', 1));
            m_useFStopBtn->setVisible(std::abs(img->f_stop - state.rparams.sharpen.scanner_mtf.f_stop) > 0.01);
        }
        setVisibleRow(m_fStopValue->parentWidget(), showFStopExif);

        // 11 & 12. Focal length
        if (img) {
            m_focalLengthValue->setText(QString("%1 mm").arg(img->focal_length, 0, 'f', 1));
            m_focalLength35mmValue->setText(QString("%1 mm").arg(img->focal_length_in_35mm, 0, 'f', 1));
        }
        setVisibleRow(m_focalLengthValue, img && img->focal_length > 0);
        setVisibleRow(m_focalLength35mmValue, img && img->focal_length_in_35mm > 0);

        // 15. Pixel pitch
        bool showPixelPitchExif = img && img->pixel_pitch > 0;
        if (showPixelPitchExif) {
            m_pixelPitchValue->setText(QString("%1 µm").arg(img->pixel_pitch, 0, 'f', 2));
            m_usePixelPitchBtn->setVisible(std::abs(img->pixel_pitch - state.rparams.sharpen.scanner_mtf.pixel_pitch) > 0.001);
        }
        setVisibleRow(m_pixelPitchValue->parentWidget(), showPixelPitchExif);

        // 12b. Mirroring
        bool showMirrorExif = img && img->mirror != -1;
        if (showMirrorExif) {
            m_mirrorValue->setText(img->mirror ? "Mirrored" : "Not mirrored");
            m_useMirrorBtn->setVisible((img->mirror != 0) != state.rparams.scan_mirror);
        }
        setVisibleRow(m_mirrorValue->parentWidget(), showMirrorExif);

        // Sensor width slider update (linked)
        if (img && img->width > 0) {
            double width_mm = (state.rparams.sharpen.scanner_mtf.pixel_pitch * img->width) / 1000.0;
            // Need to update the slider without triggering onChanged to avoid feedback loops?
            // ParameterPanel::addSlider returns the container. I need the slider inside.
            QSlider *slider = sensorWidthSlider->findChild<QSlider*>();
            QDoubleSpinBox *spin = sensorWidthSlider->findChild<QDoubleSpinBox*>();
            if (slider && spin) {
                slider->blockSignals(true);
                spin->blockSignals(true);
                // Value to slider mapping is internal to addSlider... 
                // Re-calculating slider value:
                // The scale was 10.0 in addSlider call for sensor width
                spin->setValue(width_mm);
                slider->setValue(qRound(width_mm * 10.0)); 
                slider->blockSignals(false);
                spin->blockSignals(false);
            }
        }
    };

    m_paramUpdaters.push_back(updateInfoLabels);

  /* Only makes effect with backlight correction on; handle it later.  */
#if 0
  addSliderParameter(
      "Scanner/camera black point", 0, 1, 1, 2, "", "",
      [](const ParameterState &s) { return s.rparams.backlight_correction_black; },
      [](ParameterState &s, double v) { s.rparams.backlight_correction_black = v; }, 3.0,
      nullptr, true);
#endif
    
    addButtonParameter("Field leveling", "Set reference", [this]() { emit fieldLevelingRequested(); });
    
    m_backlightWidget = new BacklightChartWidget();
    QWidget *backlightSection = createDetachableSection("Backlight", m_backlightWidget, [this]() {
      emit detachBacklightRequested(m_backlightWidget);
    });
    m_form->addRow(backlightSection);
    
    addButtonParameter("Crop image", "Change crop", [this]() { emit cropRequested(); });
    
    // Initial update
    updateInfoLabels(m_stateGetter());

    m_widgetStateUpdaters.push_back([this, backlightSection]() {
        auto scan = m_imageGetter();
        ParameterState s = m_stateGetter();
        bool visible = scan != nullptr && s.rparams.backlight_correction != nullptr;
        backlightSection->setVisible(visible);
        if (visible && m_backlightWidget) {
            m_backlightWidget->setBacklightData(s.rparams.backlight_correction,
                                              scan->width, scan->height,
                                              s.rparams.get_scan_crop(scan->width, scan->height),
                                              s.rparams.backlight_correction_black,
                                              s.rparams.scan_mirror,
                                              s.rparams.scan_rotation);
        }
    });

    updateUI();
}

void CapturePanel::reattachBacklight(QWidget *w) {
    if (w != m_backlightWidget)
        return;

    for (int i = 0; i < m_form->rowCount(); ++i) {
        QLayoutItem *item = m_form->itemAt(i, QFormLayout::SpanningRole);
        if (item && item->widget()) {
            QWidget *section = item->widget();
            if (section->layout()) {
                QVBoxLayout *vl = qobject_cast<QVBoxLayout*>(section->layout());
                if (vl && vl->count() > 0) {
                    QWidget *header = vl->itemAt(0)->widget();
                    if (header) {
                        QLabel *titleLabel = header->findChild<QLabel*>();
                        if (titleLabel && titleLabel->text() == "Backlight") {
                           vl->addWidget(w);
                           w->show();
                           header->show();
                           updateUI();
                           return;
                        }
                    }
                }
            }
        }
    }
}


