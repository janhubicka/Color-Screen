#ifndef CAPTURE_PANEL_H
#define CAPTURE_PANEL_H

#include <QWidget>
#include <vector>
#include <functional>
#include <map>
#include <memory>
#include "ParameterState.h"
#include "../libcolorscreen/include/base.h" // For image_data definition if needed, or forward decl

namespace colorscreen {
    class image_data;
}

#include "ParameterPanel.h"

class CapturePanel : public ParameterPanel
{
    Q_OBJECT
public:
    explicit CapturePanel(StateGetter stateGetter, StateSetter stateSetter, ImageGetter imageGetter, QWidget *parent = nullptr);
    ~CapturePanel() override;

signals:
    void cropRequested();

private:
    void setupUi();
    class QLabel *m_imageResolutionValue = nullptr;
    class QPushButton *m_useImageResBtn = nullptr;
    class QLabel *m_screenResolutionValue = nullptr;
    class QPushButton *m_useScreenResBtn = nullptr;
    class QLabel *m_detectedGammaValue = nullptr;
    class QPushButton *m_useDetectedGammaBtn = nullptr;
    class QLabel *m_cameraModelValue = nullptr;
    class QLabel *m_lensValue = nullptr;
    class QLabel *m_fStopValue = nullptr;
    class QPushButton *m_useFStopBtn = nullptr;
    class QLabel *m_focalLengthValue = nullptr;
    class QLabel *m_focalLength35mmValue = nullptr;
    class QLabel *m_focalPlaneResValue = nullptr;
    class QPushButton *m_useFocalPlaneResBtn = nullptr;
    class QLabel *m_pixelPitchValue = nullptr;
    class QPushButton *m_usePixelPitchBtn = nullptr;
    class QLabel *m_softwareValue = nullptr;
    class QLabel *m_sensorFillFactorValue = nullptr;
};

#endif // CAPTURE_PANEL_H
