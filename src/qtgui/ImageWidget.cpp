#include "ImageWidget.h"
#include "Renderer.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>
#include <QDebug>
#include "../libcolorscreen/include/imagedata.h" 
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/progress-info.h"

// Ensure shared ptr can be passed via signals
Q_DECLARE_METATYPE(std::shared_ptr<colorscreen::progress_info>)
Q_DECLARE_METATYPE(colorscreen::render_parameters)

ImageWidget::ImageWidget(QWidget *parent)
    : QWidget(parent)
{
    qRegisterMetaType<std::shared_ptr<colorscreen::progress_info>>();
    qRegisterMetaType<colorscreen::render_parameters>();
    
    // Background color
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::darkGray);
    setAutoFillBackground(true);
    setPalette(pal);
    
    setMouseTracking(false); // Only track when dragging
}

double ImageWidget::getMinScale() const { return m_minScale; }

void ImageWidget::setZoom(double scale)
{
    if (qAbs(scale - m_scale) > 0.000001) {
        // Keep center? For now just zoom.
        // Ideally zoom around center of view.
        double centerX = m_viewX + (width() / m_scale) / 2.0;
        double centerY = m_viewY + (height() / m_scale) / 2.0;
        
        m_scale = scale;
        
        // New view top-left
        m_viewX = centerX - (width() / m_scale) / 2.0;
        m_viewY = centerY - (height() / m_scale) / 2.0;
        
        requestRender();
        emit viewStateChanged(QRectF(m_viewX, m_viewY, width() / m_scale, height() / m_scale), m_scale);
    }
}

void ImageWidget::setPan(double x, double y)
{
    if (qAbs(x - m_viewX) > 0.1 || qAbs(y - m_viewY) > 0.1) {
        m_viewX = x;
        m_viewY = y;
        requestRender();
        emit viewStateChanged(QRectF(m_viewX, m_viewY, width() / m_scale, height() / m_scale), m_scale);
    }
}

ImageWidget::~ImageWidget()
{
    if (m_renderThread) {
        m_renderThread->quit();
        m_renderThread->wait();
    }
}

void ImageWidget::setImage(std::shared_ptr<colorscreen::image_data> scan, 
                          colorscreen::render_parameters *rparams,
                          colorscreen::scr_to_img_parameters *scrToImg,
                          colorscreen::scr_detect_parameters *scrDetect,
                          colorscreen::render_type_parameters *renderType)
{
    m_scan = scan;
    m_rparams = rparams;
    m_scrToImg = scrToImg;
    m_scrDetect = scrDetect;
    m_renderType = renderType;
    m_pixmap = QImage(); // Clear current image

    // Reset View
    if (m_scan && m_scan->width > 0) {
        // Fit to view
        double w = width();
        double h = height();
        
        // Handle rotation for scale calculation
        double rot = m_scrToImg ? m_scrToImg->final_rotation : 0.0;
        int angle = (int)rot; 
        angle = angle % 360;
        if (angle < 0) angle += 360;
        
        double imgW = m_scan->width;
        double imgH = m_scan->height;
        
        if (angle == 90 || angle == 270) {
            std::swap(imgW, imgH);
        }

        if (w > 0 && h > 0) {
           double scaleX = w / imgW;
           double scaleY = h / imgH;
           m_scale = qMin(scaleX, scaleY);
           if (m_scale == 0) m_scale = 1.0;
           m_minScale = m_scale;
        } else {
            m_scale = 0.1; // Fallback
            m_minScale = 0.1;
        }
        m_viewX = 0;
        m_viewY = 0;
    }
    
    emit viewStateChanged(QRectF(m_viewX, m_viewY, width() / m_scale, height() / m_scale), m_scale);
    update();

    // Initialize Renderer if needed
    if (m_renderer) {
        m_renderer->deleteLater();
        m_renderer = nullptr;
    }
    if (m_renderThread) {
        m_renderThread->quit();
        m_renderThread->wait();
        m_renderThread->deleteLater();
        m_renderThread = nullptr;
    }
    
    if (m_scan && m_rparams) {
        m_renderThread = new QThread(this);
        // Pass shared_ptr and value copy of params
        // Check for null pointers on new params, though they should be valid if called from MainWindow
        static colorscreen::scr_to_img_parameters defaultScrToImg;
        static colorscreen::scr_detect_parameters defaultScrDetect;
        static colorscreen::render_type_parameters defaultRenderType;
        
        m_renderer = new Renderer(m_scan, *m_rparams, 
            m_scrToImg ? *m_scrToImg : defaultScrToImg, 
            m_scrDetect ? *m_scrDetect : defaultScrDetect,
            m_renderType ? *m_renderType : defaultRenderType);
        m_renderer->moveToThread(m_renderThread);

        connect(m_renderThread, &QThread::finished, m_renderer, &QObject::deleteLater);
        connect(m_renderer, &Renderer::imageReady, this, &ImageWidget::handleImageReady);
        
        m_renderThread->start();

        requestRender();
    }
    update();
}

void ImageWidget::requestRender()
{
    if (!m_renderer || !m_scan) return;
    
    // Cancel previous
    if (m_currentProgress) {
        m_currentProgress->cancel();
        emit progressFinished(m_currentProgress);
        m_currentProgress.reset();
    }

    m_currentReqId++;
    double xOff = m_viewX * m_scale;
    double yOff = m_viewY * m_scale;
    
    m_currentProgress = std::make_shared<colorscreen::progress_info>();
    m_currentProgress->set_task("Rendering", 0);
    emit progressStarted(m_currentProgress);
    
    // Invoke render on worker thread
    QMetaObject::invokeMethod(m_renderer, "render", Qt::QueuedConnection,
        Q_ARG(int, m_currentReqId),
        Q_ARG(double, xOff),
        Q_ARG(double, yOff),
        Q_ARG(double, m_scale),
        Q_ARG(int, width()),
        Q_ARG(int, height()),
        Q_ARG(colorscreen::render_parameters, *m_rparams),
        Q_ARG(std::shared_ptr<colorscreen::progress_info>, m_currentProgress)
    );
}

void ImageWidget::handleImageReady(int reqId, QImage image, double x, double y, double scale)
{
    if (reqId == m_currentReqId) {
        m_pixmap = image;
        if (m_currentProgress) {
             emit progressFinished(m_currentProgress);
             m_currentProgress.reset();
        }
        update();
    }
}

void ImageWidget::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    if (!m_pixmap.isNull()) {
        p.drawImage(0, 0, m_pixmap);
    } else {
        p.drawText(rect(), Qt::AlignCenter, "No Image");
    }
}

void ImageWidget::resizeEvent(QResizeEvent *event)
{
    requestRender();
    emit viewStateChanged(QRectF(m_viewX, m_viewY, width() / m_scale, height() / m_scale), m_scale);
}

void ImageWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        m_lastMousePos = event->pos();
    }
}

void ImageWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();

        // Move view opposite to drag
        m_viewX -= delta.x() / m_scale;
        m_viewY -= delta.y() / m_scale;

        requestRender();
        emit viewStateChanged(QRectF(m_viewX, m_viewY, width() / m_scale, height() / m_scale), m_scale);
    }
}

void ImageWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
    }
}

void ImageWidget::wheelEvent(QWheelEvent *event)
{
    double numDegrees = event->angleDelta().y() / 8.0;
    double numSteps = numDegrees / 15.0;
    double factor = qPow(1.1, numSteps);

    double mouseX = event->position().x();
    double mouseY = event->position().y();

    double mouseImageX = m_viewX + mouseX / m_scale;
    double mouseImageY = m_viewY + mouseY / m_scale;

    m_scale *= factor;
    
    // Clamp scale? (Optional)

    // Adjust view so mouseImageX remains under mouseX
    // new_viewX + mouseX / new_scale = mouseImageX
    m_viewX = mouseImageX - mouseX / m_scale;
    m_viewY = mouseImageY - mouseY / m_scale;

    requestRender();
    emit viewStateChanged(QRectF(m_viewX, m_viewY, width() / m_scale, height() / m_scale), m_scale);
}
