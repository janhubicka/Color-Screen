#include "CapturePanel.h"
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QFormLayout>
#include "../libcolorscreen/include/scr-to-img.h"
#include "../libcolorscreen/include/imagedata.h"

CapturePanel::CapturePanel(StateGetter stateGetter, StateSetter stateSetter, ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent)
{
    setupUi();
}

CapturePanel::~CapturePanel() = default;

void CapturePanel::setupUi()
{
    // Gamma logic
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

    m_detectedGammaValue = new QLabel();
    m_detectedGammaValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->addRow("Detected gamma", m_detectedGammaValue);

    addSliderParameter(
        "Resolution", 0.0, 10000.0, 10.0, 1, "PPI", "unknown",
        [](const ParameterState &s) {
          return s.rparams.sharpen.scanner_mtf.scan_dpi;
        },
        [](ParameterState &s, double v) {
          s.rparams.sharpen.scanner_mtf.scan_dpi = v;
        });

    m_imageResolutionValue = new QLabel();
    m_imageResolutionValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->addRow("Image resolution", m_imageResolutionValue);

    m_screenResolutionValue = new QLabel();
    m_screenResolutionValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->addRow("Resolution from screen", m_screenResolutionValue);

    m_cameraModelValue = new QLabel();
    m_cameraModelValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->addRow("Camera model", m_cameraModelValue);

    m_lensValue = new QLabel();
    m_lensValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->addRow("Lens", m_lensValue);

    m_fStopValue = new QLabel();
    m_fStopValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->addRow("F-stop", m_fStopValue);

    m_focalLengthValue = new QLabel();
    m_focalLengthValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->addRow("Focal length", m_focalLengthValue);

    m_focalLength35mmValue = new QLabel();
    m_focalLength35mmValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->addRow("Focal length (35mm)", m_focalLength35mmValue);

    m_focalPlaneResValue = new QLabel();
    m_focalPlaneResValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->addRow("Focal plane res", m_focalPlaneResValue);

    m_pixelPitchValue = new QLabel();
    m_pixelPitchValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->addRow("Pixel pitch", m_pixelPitchValue);

    m_sensorFillFactorValue = new QLabel();
    m_sensorFillFactorValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->addRow("Sensor fill factor", m_sensorFillFactorValue);

    auto updateInfoLabels = [this](const ParameterState &state) {
        auto img = m_imageGetter();
        
        // Image Resolution
        bool showImageRes = false;
        if (img && img->xdpi > 0 && img->ydpi > 0) {
            showImageRes = true;
            if (std::abs(img->xdpi - img->ydpi) < 1e-6) {
                m_imageResolutionValue->setText(QString("%1 PPI").arg(img->xdpi));
            } else {
                m_imageResolutionValue->setText(QString("%1x%2 PPI").arg(img->xdpi).arg(img->ydpi));
            }
        }
        
        m_imageResolutionValue->setVisible(showImageRes);
        if (auto lab = m_form->labelForField(m_imageResolutionValue))
            lab->setVisible(showImageRes);

        // Screen Resolution
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
                }
            }
        }

        m_screenResolutionValue->setVisible(showScreenRes);
        if (auto lab = m_form->labelForField(m_screenResolutionValue))
            lab->setVisible(showScreenRes);

        // Detected Gamma
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
        }
        m_detectedGammaValue->setVisible(showGamma);
        if (auto lab = m_form->labelForField(m_detectedGammaValue))
            lab->setVisible(showGamma);

        auto updateOptionalLabel = [&](QLabel* label, bool condition, const QString& text) {
            label->setText(text);
            label->setVisible(condition);
            if (auto lab = m_form->labelForField(label))
                lab->setVisible(condition);
        };

        if (img) {
            updateOptionalLabel(m_cameraModelValue, !img->camera_model.empty(), QString::fromStdString(img->camera_model));
            updateOptionalLabel(m_lensValue, !img->lens.empty(), QString::fromStdString(img->lens));
            updateOptionalLabel(m_fStopValue, img->f_stop != -2, QString("f/%1").arg(img->f_stop, 0, 'f', 1));
            updateOptionalLabel(m_focalLengthValue, img->focal_length != -2, QString("%1 mm").arg(img->focal_length, 0, 'f', 1));
            updateOptionalLabel(m_focalLength35mmValue, img->focal_length_in_35mm != -2, QString("%1 mm").arg(img->focal_length_in_35mm, 0, 'f', 1));
            
            bool showFocalPlane = img->focal_plane_x_resolution != -2 && img->focal_plane_y_resolution != -2;
            QString fpText;
            if (showFocalPlane) {
                if (std::abs(img->focal_plane_x_resolution - img->focal_plane_y_resolution) < 1e-6)
                    fpText = QString("%1").arg(img->focal_plane_x_resolution, 0, 'f', 1);
                else
                    fpText = QString("%1x%2").arg(img->focal_plane_x_resolution, 0, 'f', 1).arg(img->focal_plane_y_resolution, 0, 'f', 1);
            }
            updateOptionalLabel(m_focalPlaneResValue, showFocalPlane, fpText);
            
            updateOptionalLabel(m_pixelPitchValue, img->pixel_pitch != -2, QString("%1 µm").arg(img->pixel_pitch, 0, 'f', 2));
            updateOptionalLabel(m_sensorFillFactorValue, img->sensor_fill_factor != -2, QString("%1%").arg(img->sensor_fill_factor * 100.0, 0, 'f', 1));
        } else {
            updateOptionalLabel(m_cameraModelValue, false, "");
            updateOptionalLabel(m_lensValue, false, "");
            updateOptionalLabel(m_fStopValue, false, "");
            updateOptionalLabel(m_focalLengthValue, false, "");
            updateOptionalLabel(m_focalLength35mmValue, false, "");
            updateOptionalLabel(m_focalPlaneResValue, false, "");
            updateOptionalLabel(m_pixelPitchValue, false, "");
            updateOptionalLabel(m_sensorFillFactorValue, false, "");
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
    
    addButtonParameter("", "Crop", [this]() { emit cropRequested(); });

    // Add stretch after the button to keep it at the top
    m_layout->addStretch();
    
    // Initial update
    updateInfoLabels(m_stateGetter());
}

