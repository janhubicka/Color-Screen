#pragma once
#include <QWidget>
#include <QImage>
#include <memory>
// Include parameters definitions
#include "../libcolorscreen/include/imagedata.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/scr-detect-parameters.h"
#include "../libcolorscreen/include/render-type-parameters.h"
#include "../libcolorscreen/include/progress-info.h"
#include "RenderQueue.h"

class QSlider;
class QThread;
class Renderer;

class NavigationView : public QWidget
{
    Q_OBJECT
public:
    explicit NavigationView(QWidget *parent = nullptr);
    ~NavigationView() override;

    void setImage(std::shared_ptr<colorscreen::image_data> scan, 
                  colorscreen::render_parameters *rparams,
                  colorscreen::scr_to_img_parameters *scrToImg,
                  colorscreen::scr_detect_parameters *scrDetect);
    
    // Update parameters without recreating renderer (non-blocking)
    void updateParameters(colorscreen::render_parameters *rparams,
                         colorscreen::scr_to_img_parameters *scrToImg,
                         colorscreen::scr_detect_parameters *scrDetect);

public slots:
    void onViewStateChanged(QRectF visibleRect, double scale); // Connected to ImageWidget
    void setMinScale(double scale);

signals:
    void zoomChanged(double scale);
    void panChanged(double x, double y);
    void progressStarted(std::shared_ptr<colorscreen::progress_info> progress);
    void progressFinished(std::shared_ptr<colorscreen::progress_info> progress);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void onSliderValueChanged(int value);
    void onImageReady(int reqId, QImage image, double x, double y, double scale, bool success);

private:
    void updateSliderRange();

    QSlider *m_zoomSlider;
    
    RenderQueue m_renderQueue;
    
private slots:
    void onTriggerRender(int reqId, std::shared_ptr<colorscreen::progress_info> progress);
    
private:
    // Core Data (same as ImageWidget, but we own our Renderer)
    std::shared_ptr<colorscreen::image_data> m_scan;
    colorscreen::render_parameters *m_rparams = nullptr;
    colorscreen::scr_to_img_parameters *m_scrToImg = nullptr;
    colorscreen::scr_detect_parameters *m_scrDetect = nullptr;
    colorscreen::render_type_parameters m_renderType; // Internal copy

    Renderer *m_renderer = nullptr;
    QThread *m_renderThread = nullptr;
    
    std::shared_ptr<colorscreen::progress_info> m_currentProgress;
    
    // Single-thread rendering queue
    bool m_renderInProgress = false;
    bool m_hasPendingRender = false;
    
    QImage m_previewImage;
    double m_previewScale = 1.0; // Scale of preview relative to original scan
    
    // Viewport info (from ImageWidget)
    QRectF m_visibleRect;
    double m_mainScale = 1.0;
    
    double m_minScale = 0.1; // From ImageWidget
    
    bool m_isDragging = false;
    QPointF m_dragOffset;
    
    // UI Layout area for image (might differ from widget size if Aspect Ratio preserved)
    QRect m_imageRect;
};
