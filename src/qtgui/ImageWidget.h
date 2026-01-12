#pragma once

#include <QWidget>
#include <QImage>
#include <QThread>
#include <memory>
#include "../libcolorscreen/include/progress-info.h"

// Forward declarations
namespace colorscreen {
    class image_data;
    struct render_parameters;
}
class Renderer;

class ImageWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ImageWidget(QWidget *parent = nullptr);
    ~ImageWidget() override;

    // Use shared_ptr and pointer for rparams (we will copy rparams when requesting render)
    void setImage(std::shared_ptr<colorscreen::image_data> scan, colorscreen::render_parameters *rparams);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

signals:
    void progressStarted(std::shared_ptr<colorscreen::progress_info> info);
    void progressFinished(std::shared_ptr<colorscreen::progress_info> info);

private slots:
    void handleImageReady(int reqId, QImage image, double x, double y, double scale);

private:
    void requestRender();

    std::shared_ptr<colorscreen::image_data> m_scan;
    colorscreen::render_parameters *m_rparams = nullptr;

    Renderer *m_renderer = nullptr;
    QThread *m_renderThread = nullptr;

    QImage m_pixmap; // The currently displayed rendered tile
    
    // Current View State
    double m_scale = 1.0;
    double m_viewX = 0.0; // Top-left of the view in Image Coordinates
    double m_viewY = 0.0;

    int m_currentReqId = 0;
    std::shared_ptr<colorscreen::progress_info> m_currentProgress; // Track current progress info

    // Interaction
    QPoint m_lastMousePos;
    bool m_isDragging = false;
};
