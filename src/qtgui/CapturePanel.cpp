#include "CapturePanel.h"
#include <QSignalBlocker>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFormLayout>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QStringList>
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
    // Keep manually constructed rows in the current foldable section, matching
    // ParameterPanel's stateful helpers.
    auto addFieldRow = [this](const QString &label, QWidget *field) {
        if (m_currentGroupForm)
            m_currentGroupForm->addRow(label, field);
        else
            m_form->addRow(label, field);
    };
    auto addWidgetRow = [this](QWidget *field) {
        if (m_currentGroupForm)
            m_currentGroupForm->addRow(field);
        else
            m_form->addRow(field);
    };
    auto labelForField = [this](QWidget *field) -> QLabel * {
        auto findInForm = [this, field](QFormLayout *form) -> QLabel * {
            if (!form || !field)
                return nullptr;
            for (QWidget *candidate = field; candidate && candidate != this;
                 candidate = candidate->parentWidget()) {
                int row = -1;
                QFormLayout::ItemRole role = QFormLayout::FieldRole;
                form->getWidgetPosition(candidate, &row, &role);
                if (row >= 0 && role != QFormLayout::LabelRole)
                    return qobject_cast<QLabel *>(form->labelForField(candidate));
            }
            return nullptr;
        };
        if (QLabel *label = findInForm(m_form))
            return label;
        for (QFormLayout *groupForm : m_groupForms)
            if (QLabel *label = findInForm(groupForm))
                return label;
        return nullptr;
    };

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

    auto onUseDetectedWavelengths = [this]() {
        auto img = m_imageGetter();
        if (!img)
            return;
        applyChange([img](ParameterState &s) {
            const bool hasRgb = img->has_rgb();
            const bool hasScalar = img->has_grayscale_or_ir();
            for (int c = 0; c < 4; ++c) {
                const bool present = c < 3 ? hasRgb : hasScalar;
                const double wavelength = img->wavelengths[c];
                if (present && colorscreen::my_isfinite(wavelength)
                    && wavelength > 0)
                    s.rparams.sharpen.scanner_mtf.wavelengths[c] = wavelength;
            }
        }, "Use detected wavelengths");
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

        addFieldRow(label, container);
    };

    addSeparator(tr("Source"), QStringLiteral("capture.source"));

    // Capture type is the top-level workflow choice: it determines whether
    // historical color-screen restoration is meaningful for this document.
    m_captureTypeCombo = new QComboBox();
    m_captureTypeCombo->setObjectName(QStringLiteral("CaptureTypeCombo"));
    m_captureTypeCombo->setProperty("parameterKey",
                                    QStringLiteral("capture.type"));
    m_captureTypeCombo->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_captureTypeCombo->setMinimumContentsLength(18);
    for (int i = 0; i < (int)colorscreen::render_parameters::capture_max; ++i) {
      const auto &property =
          colorscreen::render_parameters::capture_properties[i];
      m_captureTypeCombo->addItem(QString::fromUtf8(property.pretty_name), i);
      if (property.help && property.help[0])
        m_captureTypeCombo->setItemData(
            m_captureTypeCombo->count() - 1, QString::fromUtf8(property.help),
            Qt::ToolTipRole);
    }
    m_captureTypeCombo->setToolTip(
        tr("Physical capture/material type. This selects the applicable "
           "restoration path; ordinary images use capture correction and "
           "sharpening but not historical color-screen reconstruction."));
    addFieldRow(tr("Capture type"), m_captureTypeCombo);
    connect(m_captureTypeCombo, QOverload<int>::of(&QComboBox::activated),
            this, [this](int index) {
      const auto capture =
          static_cast<decltype(colorscreen::render_parameters::capture_unknown)>(
              m_captureTypeCombo->itemData(index).toInt());
      applyChange([capture](ParameterState &state) {
        state.rparams.capture_type = capture;
        if (capture != colorscreen::render_parameters::capture_unknown &&
            !colorscreen::render_parameters::capture_has_screen_p(capture))
          state.scrToImg.type = colorscreen::NoScreen;
      }, "Capture type", QStringLiteral("capture.type"));
    });

    // Demosaic (Enum) + Reload
    QWidget *demosaicContainer = new QWidget();
    QHBoxLayout *demosaicHLayout = new QHBoxLayout(demosaicContainer);
    demosaicHLayout->setContentsMargins(0, 0, 0, 0);
    demosaicHLayout->setSpacing(5);

    m_demosaicCombo = new QComboBox();
    m_demosaicCombo->setObjectName(QStringLiteral("CaptureDemosaicCombo"));
    m_demosaicCombo->setProperty("parameterKey",
                                 QStringLiteral("capture.demosaic"));
    m_demosaicCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_demosaicCombo->setMinimumContentsLength(10);
    m_demosaicCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    for (int i = 0; i < (int)colorscreen::image_data::demosaic_max; ++i) {
        m_demosaicCombo->addItem(QString::fromUtf8(colorscreen::image_data::demosaic_names[i].pretty_name), i);
        if (colorscreen::image_data::demosaic_names[i].help) {
            m_demosaicCombo->setItemData(i, QString::fromUtf8(colorscreen::image_data::demosaic_names[i].help), Qt::ToolTipRole);
        }
    }
    m_demosaicCombo->setToolTip("Algorithm to interpolate missing color information from CFA (Bayer) sensor.");
    demosaicHLayout->addWidget(m_demosaicCombo, 1);

    m_reloadDemosaicBtn = new QPushButton("Reload and demosaic");
    m_reloadDemosaicBtn->setVisible(false);
    demosaicHLayout->addWidget(m_reloadDemosaicBtn, 0);

    addFieldRow(tr("Demosaic"), demosaicContainer);

    connect(m_demosaicCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        int val = m_demosaicCombo->itemData(index).toInt();
        applyChange(
            [val](ParameterState &s) {
              s.rparams.demosaic =
                  (colorscreen::image_data::demosaicing_t)val;
            },
            "Demosaic", QStringLiteral("capture.demosaic"));
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
        nullptr,
        "Gamma correction applied to the input scan.",
        QStringLiteral("capture.gamma"), true
    );

    // 2. Detected gamma (Label) + Use
    addValueWithUseButton("Detected gamma", &m_detectedGammaValue, &m_useDetectedGammaBtn, [this, onUseGamma]() { onUseGamma(); });

    addSeparator(tr("Resolution and optics"),
                 QStringLiteral("capture.optics"));

    // 3. Resolution (Slider)
    addSliderParameter(
        "Resolution", 0.0, 10000.0, 10.0, 1, "PPI", "unknown",
        [](const ParameterState &s) {
          return s.rparams.sharpen.scanner_mtf.scan_dpi;
        },
        [](ParameterState &s, double v) {
          s.rparams.sharpen.scanner_mtf.scan_dpi = v;
        }, 1.0, nullptr, false,
        "Scanner or camera resolution in Pixels Per Inch (PPI). Crucial for MTF-based sharpening.",
        QStringLiteral("capture.mtf.scan_dpi"), true);
    
    addButtonParameter("Resolution", "Measure", [this]() { emit measureRequested(); });

    // 4. Image resolution (Label) + Use
    addValueWithUseButton("Image resolution", &m_imageResolutionValue, &m_useImageResBtn, [this, onUseRes]() {
        auto img = m_imageGetter();
        if (img) onUseRes(img->xdpi);
    });

    // 5. Resolution from screen (Label) + Use
    addValueWithUseButton("Resolution from screen", &m_screenResolutionValue, &m_useScreenResBtn, [this, onUseRes]() {
        ParameterState state = m_stateGetter();
        auto img = m_imageGetter();
        if (img && img->width > 0 && img->height > 0
            && colorscreen::screen_geometry_configured_p(state.scrToImg)) {
            colorscreen::scr_to_img map;
            if (!map.set_parameters(state.scrToImg, *img))
                return;
            double pixel_size = map.pixel_size({0, 0, img->width, img->height});
            double estimated_dpi = state.scrToImg.estimate_dpi(pixel_size);
            onUseRes(estimated_dpi);
        }
    });

    // 6. Focal plane resolution (Label) + Use
    addValueWithUseButton("Focal plane res", &m_focalPlaneResValue, &m_useFocalPlaneResBtn, [this, onUseRes]() {
        auto img = m_imageGetter();
        if (img) onUseRes(img->focal_plane_x_resolution);
    });

    // 6b. Image resolution by EXIF (Label) + Use
    addValueWithUseButton("Image resolution by exif", &m_exifResolutionValue, &m_useExifResBtn, [this, onUseRes]() {
        auto img = m_imageGetter();
        if (img) onUseRes(img->exif_xdpi);
    });

    // 7. Camera model (Label)
    m_cameraModelValue = new QLabel();
    m_cameraModelValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    addFieldRow(tr("Camera model"), m_cameraModelValue);

    // 8. Lens (Label)
    m_lensValue = new QLabel();
    m_lensValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    addFieldRow(tr("Lens"), m_lensValue);

    // 8b. Software (Label)
    m_softwareValue = new QLabel();
    m_softwareValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    addFieldRow(tr("Software"), m_softwareValue);

    // 9. Nominal f-stop (Slider)
    addSliderParameter(
        "Nominal f-stop", 0.0, 64.0, 1000.0, 2, "", "unknown",
        [](const ParameterState &s) {
          return s.rparams.sharpen.scanner_mtf.f_stop;
        },
        [](ParameterState &s, double v) {
          s.rparams.sharpen.scanner_mtf.f_stop = v;
        }, 1.0, nullptr, false,
        "Lens aperture used during capture. Affects diffraction part of the MTF model used for sharpening.",
        QStringLiteral("capture.mtf.f_stop"), true);

    // 10. F-stop (Label) + Use
    addValueWithUseButton("Exif F-stop", &m_fStopValue, &m_useFStopBtn, [this, onUseFStop]() { onUseFStop(); });

    // 11. Focal length (Label)
    m_focalLengthValue = new QLabel();
    m_focalLengthValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    addFieldRow(tr("Focal length"), m_focalLengthValue);

    // 12. Focal length (35mm) (Label)
    m_focalLength35mmValue = new QLabel();
    m_focalLength35mmValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    addFieldRow(tr("Focal length (35mm)"), m_focalLength35mmValue);

    addSeparator(tr("Sensor"), QStringLiteral("capture.sensor"));

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
    addFieldRow(tr("Sensor presets"), presets);

    // 13. Sensor width (Slider)
    // Sensor width is a derived presentation of the saved pixel-pitch
    // parameter, so it deliberately shares the same stable parameter key.
    const QString pixelPitchKey = QStringLiteral("capture.mtf.pixel_pitch");
    const SliderWidgets sensorWidthSlider = addSliderControls(
        "Sensor width", 0.0, 1000.0, 10.0, 2, "mm", "unknown",
        0.0,
        [this, pixelPitchKey](double v) {
            auto img = m_imageGetter();
            int divisor = (img && m_assumeRotationBox->isChecked() && img->height > 0) ? img->height : (img ? img->width : 0);
            if (img && divisor > 0 && v > 0) {
                double pitch = (v * 1000.0) / divisor;
                applyChange([pitch](ParameterState &s) {
                    s.rparams.sharpen.scanner_mtf.pixel_pitch = pitch;
                }, "Sensor width", pixelPitchKey);
            } else if (v == 0) {
                applyChange([](ParameterState &s) {
                    s.rparams.sharpen.scanner_mtf.pixel_pitch = 0;
                }, "Sensor width", pixelPitchKey);
            }
        }
    );
    sensorWidthSlider.container->setObjectName(
        QStringLiteral("CaptureSensorWidthField"));
    sensorWidthSlider.container->setProperty("parameterKey", pixelPitchKey);
    sensorWidthSlider.slider->setProperty("parameterKey", pixelPitchKey);
    sensorWidthSlider.spin->setProperty("parameterKey", pixelPitchKey);

    // 13b. Rotation assumption
    m_assumeRotationBox = new QCheckBox("Assume 90 degrees rotation");
    m_assumeRotationBox->setObjectName(
        QStringLiteral("CaptureAssumeRotationCheck"));
    addFieldRow(QString(), m_assumeRotationBox);
    connect(m_assumeRotationBox, &QCheckBox::toggled, this, [this]() {
        updateUI();
    });

    // 13c. Notice
    m_sensorWidthNotice = new QLabel("Sensor width computation is only correct if image was not cropped or rotated before loading to Color-Screen.");
    m_sensorWidthNotice->setWordWrap(true);
    QFont noticeFont = m_sensorWidthNotice->font();
    noticeFont.setItalic(true);
    noticeFont.setPointSize(noticeFont.pointSize() - 1);
    m_sensorWidthNotice->setFont(noticeFont);
    addFieldRow(QString(), m_sensorWidthNotice);

    // 14. Sensor pixel pitch (Slider)
    addSliderParameter(
        "Sensor pixel pitch", 0.0, 100.0, 1000.0, 3, "μm", "unknown",
        [](const ParameterState &s) {
          return s.rparams.sharpen.scanner_mtf.pixel_pitch;
        },
        [](ParameterState &s, double v) {
          s.rparams.sharpen.scanner_mtf.pixel_pitch = v;
        }, 1.0, nullptr, false,
        "Physical distance between centers of adjacent pixels on the sensor.",
        QStringLiteral("capture.mtf.pixel_pitch"), true);

    // 15. Pixel pitch (Label) + Use
    addValueWithUseButton("Pixel pitch", &m_pixelPitchValue, &m_usePixelPitchBtn, [this, onUsePixelPitch]() { onUsePixelPitch(); });

    // 16. Sensor fill factor (Slider)
    addSliderParameter(
        "Sensor fill factor", 0.0, 32.0, 1000.0, 3, "", "unknown",
        [](const ParameterState &s) {
          return s.rparams.sharpen.scanner_mtf.sensor_fill_factor;
        },
        [](ParameterState &s, double v) {
          s.rparams.sharpen.scanner_mtf.sensor_fill_factor = v;
        }, 1.0, nullptr, false,
        "The fraction of the pixel area that is sensitive to light. Affects sensor MTF used for sharpening.",
        QStringLiteral("capture.mtf.sensor_fill_factor"), true);

    // 16b. Detected sensor fill (Label) + Use
    addValueWithUseButton("Detected sensor fill factor", &m_detectedSensorFillValue, &m_useDetectedSensorFillBtn, [this]() {
        auto img = m_imageGetter();
        if (img && img->sensor_fill_factor > 0) {
            applyChange([img](ParameterState &s) {
                s.rparams.sharpen.scanner_mtf.sensor_fill_factor = img->sensor_fill_factor;
            }, "Use detected sensor fill");
        }
    });

    addSeparator(tr("Spectral wavelengths"),
                 QStringLiteral("capture.wavelengths"));

    // 17. Scanner MTF Wavelengths
    m_redWavelengthWidget = addSliderParameter(
        "Red wavelength", 380.0, 780.0, 1.0, 0, "nm", "default (600 nm)",
        [](const ParameterState &s) {
          return s.rparams.sharpen.scanner_mtf.wavelengths[0];
        },
        [](ParameterState &s, double v) {
          s.rparams.sharpen.scanner_mtf.wavelengths[0] = v;
        }, 1.0, nullptr, false, "Wavelength in nanometers used for MTF modeling of diffraction for the red channel.",
        QStringLiteral("capture.mtf.wavelength.red"), true, 0.0);

    m_greenWavelengthWidget = addSliderParameter(
        "Green wavelength", 380.0, 780.0, 1.0, 0, "nm", "default (530 nm)",
        [](const ParameterState &s) {
          return s.rparams.sharpen.scanner_mtf.wavelengths[1];
        },
        [](ParameterState &s, double v) {
          s.rparams.sharpen.scanner_mtf.wavelengths[1] = v;
        }, 1.0, nullptr, false, "Wavelength in nanometers used for MTF modeling of diffraction for the green channel.",
        QStringLiteral("capture.mtf.wavelength.green"), true, 0.0);

    m_blueWavelengthWidget = addSliderParameter(
        "Blue wavelength", 380.0, 780.0, 1.0, 0, "nm", "default (450 nm)",
        [](const ParameterState &s) {
          return s.rparams.sharpen.scanner_mtf.wavelengths[2];
        },
        [](ParameterState &s, double v) {
          s.rparams.sharpen.scanner_mtf.wavelengths[2] = v;
        }, 1.0, nullptr, false, "Wavelength in nanometers used for MTF modeling of diffraction for the blue channel.",
        QStringLiteral("capture.mtf.wavelength.blue"), true, 0.0);

    const SliderWidgets irWavelength = addSliderParameterControls(
        "IR wavelength", 380.0, 1100.0, 1.0, 0, "nm", "default (750 nm)",
        [](const ParameterState &s) {
          return s.rparams.sharpen.scanner_mtf.wavelengths[3];
        },
        [](ParameterState &s, double v) {
          s.rparams.sharpen.scanner_mtf.wavelengths[3] = v;
        }, 1.0, nullptr, false, "Wavelength in nanometers used for MTF modeling of diffraction for the scalar or infrared channel.",
        QStringLiteral("capture.mtf.wavelength.scalar"), true, 0.0);
    m_irWavelengthWidget = irWavelength.container;
    m_irWavelengthSpin = irWavelength.spin;

    // Detected scanner/camera wavelengths use the same explicit Use workflow
    // as f-stop, pixel pitch, fill factor and resolution.
    addValueWithUseButton(
        "Detected wavelengths", &m_detectedWavelengthsValue,
        &m_useDetectedWavelengthsBtn,
        [onUseDetectedWavelengths]() { onUseDetectedWavelengths(); });

    auto updateInfoLabels =
        [this, sensorWidthSlider, labelForField](const ParameterState &state) {
        auto img = m_imageGetter();

        // The stored parameter can come from another image. Rebuild the
        // selector from this scan's channel capabilities and display Unknown
        // when the stored choice is incompatible instead of inventing a
        // different physical capture type.
        QSignalBlocker signalBlocker1(m_captureTypeCombo);
        m_captureTypeCombo->clear();
        for (int i = 0;
             i < (int)colorscreen::render_parameters::capture_max; ++i) {
          const auto capture = static_cast<decltype(
              colorscreen::render_parameters::capture_unknown)>(i);
          if (!img || capture == colorscreen::render_parameters::capture_unknown ||
              colorscreen::render_parameters::capture_type_compatible_p(
                  capture, img.get())) {
            const auto &property =
                colorscreen::render_parameters::capture_properties[i];
            m_captureTypeCombo->addItem(QString::fromUtf8(property.pretty_name),
                                        i);
            if (property.help && property.help[0])
              m_captureTypeCombo->setItemData(
                  m_captureTypeCombo->count() - 1,
                  QString::fromUtf8(property.help), Qt::ToolTipRole);
          }
        }
        const auto effectiveCapture =
            img ? state.rparams.get_capture_type(img.get())
                : state.rparams.capture_type;
        int captureIndex =
            m_captureTypeCombo->findData((int)effectiveCapture);
        if (captureIndex < 0)
          captureIndex = m_captureTypeCombo->findData(
              (int)colorscreen::render_parameters::capture_unknown);
        if (captureIndex >= 0)
          m_captureTypeCombo->setCurrentIndex(captureIndex);
        signalBlocker1.unblock();
        
        auto setVisibleRow = [this](QWidget *field, bool visible) {
            setParameterRowApplicable(field, visible);
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

        
        // Wavelength visibility and labels
        if (m_redWavelengthWidget) {
            bool has_rgb = img && img->has_rgb();
            bool has_ir = img && img->has_grayscale_or_ir();
            
            setVisibleRow(m_redWavelengthWidget, has_rgb);
            setVisibleRow(m_greenWavelengthWidget, has_rgb);
            setVisibleRow(m_blueWavelengthWidget, has_rgb);
            
            // If only IR/Grayscale, show the 4th slider but label it "Wavelength"
            setVisibleRow(m_irWavelengthWidget, has_ir);
            
            if (has_ir && !has_rgb) {
                if (auto lab = labelForField(m_irWavelengthWidget)) {
                    lab->setText("Wavelength");
                }
            } else if (has_ir && has_rgb) {
                if (auto lab = labelForField(m_irWavelengthWidget)) {
                    lab->setText("IR wavelength");
                }
            }
            if (m_irWavelengthSpin)
                m_irWavelengthSpin->setSpecialValueText(
                    has_rgb ? "default (750 nm)" : "default (550 nm)");

            QStringList detectedWavelengths;
            bool wavelengthsDiffer = false;
            static const char *channelNames[] = {"R", "G", "B", "IR"};
            if (img) {
                for (int c = 0; c < 4; ++c) {
                    const bool present = c < 3 ? has_rgb : has_ir;
                    const double wavelength = img->wavelengths[c];
                    if (!present || !colorscreen::my_isfinite(wavelength)
                        || wavelength <= 0)
                        continue;
                    if (has_rgb)
                        detectedWavelengths
                            << QString("%1 %2 nm")
                                   .arg(channelNames[c])
                                   .arg(wavelength, 0, 'f', 0);
                    else
                        detectedWavelengths
                            << QString("%1 nm").arg(wavelength, 0, 'f', 0);
                    wavelengthsDiffer
                        |= std::abs(wavelength
                                    - state.rparams.sharpen.scanner_mtf
                                          .wavelengths[c])
                           > 0.5;
                }
            }
            const bool showDetectedWavelengths = !detectedWavelengths.isEmpty();
            if (showDetectedWavelengths) {
                m_detectedWavelengthsValue->setText(
                    detectedWavelengths.join(", "));
                m_useDetectedWavelengthsBtn->setVisible(wavelengthsDiffer);
            }
            setVisibleRow(m_detectedWavelengthsValue->parentWidget(),
                          showDetectedWavelengths);
        }

        // 4. Image Resolution
        bool showImageRes = false;
        if (img && img->xdpi > 0 && img->ydpi > 0) {
            showImageRes = true;
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
        if (colorscreen::screen_geometry_configured_p(state.scrToImg)) {
            if (img && img->width > 0 && img->height > 0) {
                colorscreen::scr_to_img map;
                if (map.set_parameters(state.scrToImg, *img)) {
                    double pixel_size = map.pixel_size({0, 0, img->width, img->height});
                    double estimated_dpi = state.scrToImg.estimate_dpi(pixel_size);
                    if (estimated_dpi > 0) {
                        showScreenRes = true;
                        m_screenResolutionValue->setText(QString("%1 PPI").arg(estimated_dpi, 0, 'f', 1));
                        m_useScreenResBtn->setVisible(std::abs(estimated_dpi - state.rparams.sharpen.scanner_mtf.scan_dpi) > 0.1);
                    }
                }
            }
        }
        setVisibleRow(m_screenResolutionValue->parentWidget(), showScreenRes);

        // 0. Demosaic
        bool canDemosaic = img && img->demosaiced_by != colorscreen::image_data::demosaic_max;
        if (canDemosaic) {
            QSignalBlocker signalBlocker2(m_demosaicCombo);
            int idx = m_demosaicCombo->findData((int)state.rparams.demosaic);
            if (idx != -1) m_demosaicCombo->setCurrentIndex(idx);
            signalBlocker2.unblock();
            
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
            double divisor = (m_assumeRotationBox->isChecked() && img->height > 0) ? img->height : img->width;
            double width_mm = (state.rparams.sharpen.scanner_mtf.pixel_pitch * divisor) / 1000.0;
            QSignalBlocker sliderBlocker(sensorWidthSlider.slider);
            QSignalBlocker spinBlocker(sensorWidthSlider.spin);
            sensorWidthSlider.spin->setValue(width_mm);
            // Sensor width uses a 10x linear slider scale.
            sensorWidthSlider.slider->setValue(qRound(width_mm * 10.0));
        }

        // 6b. EXIF Resolution
        bool showExifRes = img && img->exif_xdpi > 0 && img->exif_ydpi > 0;
        if (showExifRes) {
            if (std::abs(img->exif_xdpi - img->exif_ydpi) < 1e-6) {
                m_exifResolutionValue->setText(QString("%1 PPI").arg(img->exif_xdpi));
            } else {
                m_exifResolutionValue->setText(QString("%1x%2 PPI").arg(img->exif_xdpi).arg(img->exif_ydpi));
            }
            m_useExifResBtn->setVisible(std::abs(img->exif_xdpi - state.rparams.sharpen.scanner_mtf.scan_dpi) > 0.1);
        }
        setVisibleRow(m_exifResolutionValue->parentWidget(), showExifRes);

        // 16b. Detected sensor fill
        bool showDetectedFill = img && img->sensor_fill_factor > 0;
        if (showDetectedFill) {
            m_detectedSensorFillValue->setText(QString::number(img->sensor_fill_factor, 'f', 3));
            m_useDetectedSensorFillBtn->setVisible(std::abs(img->sensor_fill_factor - state.rparams.sharpen.scanner_mtf.sensor_fill_factor) > 0.001);
        }
        setVisibleRow(m_detectedSensorFillValue->parentWidget(), showDetectedFill);

        // Rotation box / notice visibility
        bool showRotationTools = img && (img->width > 0 || img->height > 0);
        setVisibleRow(m_assumeRotationBox, showRotationTools);
        setVisibleRow(m_sensorWidthNotice, showRotationTools);
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
    
    addSeparator(tr("Capture corrections"),
                 QStringLiteral("capture.corrections"));

    addButtonParameter("Flat field", "Set reference", [this]() { emit flatFieldRequested(); });
    
    m_backlightWidget = new BacklightChartWidget();
    QWidget *backlightSection =
        createDetachableSection("Backlight", m_backlightWidget);
    addWidgetRow(backlightSection);
    
    m_cropBtn = addToggleButtonParameter(
        "Crop image", "Change crop",
        [this](bool) { emit cropRequested(); });
    
    // Initial update
    updateInfoLabels(m_stateGetter());

    m_widgetStateUpdaters.push_back([this, backlightSection]() {
        auto scan = m_imageGetter();
        ParameterState s = m_stateGetter();
        bool visible = scan != nullptr && s.rparams.backlight_correction != nullptr;
        setParameterRowApplicable(backlightSection, visible);
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

void CapturePanel::setCropChecked(bool checked) {
    if (m_cropBtn) {
        QSignalBlocker signalBlocker5(m_cropBtn);
        m_cropBtn->setChecked(checked);
        signalBlocker5.unblock();
    }
}
