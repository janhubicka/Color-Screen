#include "AdaptiveSharpeningChart.h"
#include <QPainter>
#include <QPaintEvent>
#include <cmath>
#include <algorithm> // Added for std::clamp
#include <limits> // Added for std::numeric_limits

AdaptiveSharpeningChart::AdaptiveSharpeningChart(QWidget *parent)
    : QWidget(parent)
{
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    resetRanges();
}

// Helper to reset ranges
void AdaptiveSharpeningChart::resetRanges() {
    m_minRed = std::numeric_limits<double>::max();
    m_maxRed = std::numeric_limits<double>::lowest();
    m_minGreen = std::numeric_limits<double>::max();
    m_maxGreen = std::numeric_limits<double>::lowest();
    m_minBlur = std::numeric_limits<double>::max();
    m_maxBlur = std::numeric_limits<double>::lowest();
}

void AdaptiveSharpeningChart::initialize(int width, int height)
{
    m_gridWidth = width;
    m_gridHeight = height;
    m_liveData.assign(width * height, Tile{});
    m_correction.reset();
    m_mode = Mode_StripAnalysis; // Start with strip analysis
    m_dirty = true;
    resetRanges();
    update();
}

void AdaptiveSharpeningChart::updateStrip(int x, int y, double red_width, double green_width)
{
    if (x < 0 || x >= m_gridWidth || y < 0 || y >= m_gridHeight) return;
    
    Tile &t = m_liveData[y * m_gridWidth + x];
    t.red = red_width;
    t.green = green_width;
    t.stripAnalyzed = true;
    t.valid = true;
    
    m_minRed = std::min(m_minRed, red_width);
    m_maxRed = std::max(m_maxRed, red_width);
    m_minGreen = std::min(m_minGreen, green_width);
    m_maxGreen = std::max(m_maxGreen, green_width);
    
    m_mode = Mode_StripAnalysis;
    m_dirty = true;
    update();
}

void AdaptiveSharpeningChart::updateBlur(int x, int y, double correction)
{
    if (x < 0 || x >= m_gridWidth || y < 0 || y >= m_gridHeight) return;

    Tile &t = m_liveData[y * m_gridWidth + x];
    t.blur = correction;
    t.blurAnalyzed = true;
    t.valid = true;

    m_minBlur = std::min(m_minBlur, correction);
    m_maxBlur = std::max(m_maxBlur, correction);

    m_mode = Mode_BlurAnalysis;
    m_dirty = true;
    update();
}

void AdaptiveSharpeningChart::setCorrection(std::shared_ptr<colorscreen::scanner_blur_correction_parameters> correction)
{
    m_correction = correction;
    if (m_correction) {
        m_gridWidth = m_correction->get_width();
        m_gridHeight = m_correction->get_height();
    } else {
        m_gridWidth = 0;
        m_gridHeight = 0;
    }
    m_mode = Mode_FinalCorrection;
    m_dirty = true;
    update();
}

void AdaptiveSharpeningChart::clear()
{
    m_correction.reset();
    m_liveData.clear();
    m_gridWidth = 0;
    m_gridHeight = 0;
    m_preview = QImage();
    m_dirty = false;
    resetRanges();
    update();
}

QSize AdaptiveSharpeningChart::sizeHint() const
{
    return QSize(400, 300);
}

QSize AdaptiveSharpeningChart::minimumSizeHint() const
{
    return QSize(200, 150);
}

void AdaptiveSharpeningChart::paintEvent(QPaintEvent *)
{
    if (m_dirty) {
        updatePreview();
    }

    QPainter painter(this);
    if (m_preview.isNull()) {
        painter.fillRect(rect(), Qt::black);
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, tr("No Data"));
        return;
    }

    // Centered aspect-ratio-respecting preview
    QSize s = m_preview.size();
    if (s.width() > 0 && s.height() > 0) {
        double widgetAspect = (double)width() / height();
        double imgAspect = (double)s.width() / s.height();
        
        QRect target;
        if (widgetAspect > imgAspect) {
             int h = height();
             int w = (int)(h * imgAspect);
             target = QRect((width() - w) / 2, 0, w, h);
        } else {
             int w = width();
             int h = (int)(w / imgAspect);
             target = QRect(0, (height() - h) / 2, w, h);
        }
        
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false); // Keep pixels sharp for grid
        painter.drawImage(target, m_preview);
    }
    
    renderLegend(painter);
}

void AdaptiveSharpeningChart::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_dirty = true; // In case we want to re-render based on size (though we usually scale)
}

void AdaptiveSharpeningChart::updatePreview()
{
    if (m_gridWidth <= 0 || m_gridHeight <= 0) {
        m_preview = QImage();
        m_dirty = false;
        return;
    }
    
    QImage img(m_gridWidth, m_gridHeight, QImage::Format_RGB32);
    img.fill(Qt::black); // Default background if needed, but we overwrite
    
    double rangeRed = (m_maxRed > m_minRed) ? (m_maxRed - m_minRed) : 1.0;
    double rangeGreen = (m_maxGreen > m_minGreen) ? (m_maxGreen - m_minGreen) : 1.0;
    double rangeBlur = (m_maxBlur > m_minBlur) ? (m_maxBlur - m_minBlur) : 1.0;
    
    for (int y = 0; y < m_gridHeight; y++) {
        for (int x = 0; x < m_gridWidth; x++) {
            QColor c = Qt::black; // Uncomputed color
            
            if (m_mode == Mode_FinalCorrection && m_correction) {
                 double val = m_correction->get_correction(x, y);
                 // Heatmap
                 c = QColor::fromHsvF((1.0 - std::min(val, 2.0)/2.0) * 0.66, 1.0, 1.0); 
            } else if (m_mode == Mode_StripAnalysis || m_mode == Mode_BlurAnalysis) {
                const Tile &t = m_liveData[y * m_gridWidth + x];
                if (t.valid) {
                    if (m_mode == Mode_StripAnalysis && t.stripAnalyzed) {
                        // "red strips contributes red color and green strips green"
                        int r = 0;
                        if (m_maxRed > std::numeric_limits<double>::lowest()) {
                             double val = (t.red - m_minRed) / rangeRed;
                             r = std::clamp((int)(val * 255.0), 0, 255);
                        }
                        
                        int g = 0;
                        if (m_maxGreen > std::numeric_limits<double>::lowest()) {
                             double val = (t.green - m_minGreen) / rangeGreen;
                             g = std::clamp((int)(val * 255.0), 0, 255);
                        }
                        c = QColor(r, g, 0);
                    } else if (m_mode == Mode_BlurAnalysis && t.blurAnalyzed) {
                         // Blur analysis: grayscale
                         int b = 0;
                         if (m_maxBlur > std::numeric_limits<double>::lowest()) {
                             double val = (t.blur - m_minBlur) / rangeBlur;
                             b = std::clamp((int)(val * 255.0), 0, 255);
                         }
                         c = QColor(b, b, b);
                    } else {
                        // Uncomputed
                        c = palette().color(QPalette::Window);
                    }
                } else {
                     c = palette().color(QPalette::Window);
                }
            }
            img.setPixelColor(x, y, c);
        }
    }
    
    m_preview = img;
    m_dirty = false;
}

void AdaptiveSharpeningChart::renderLegend(QPainter &painter)
{
    // Simple legend logic
    painter.setPen(Qt::white);
    int h = height();
    int w = width();
    QFontMetrics fm(painter.font());
    int textHeight = fm.height();
    
    if (m_mode == Mode_StripAnalysis) {
        // Layout:
        // Red Text
        // Red Bar
        // Green Text
        // Green Bar
        // (Bottom padding)
        
        int barHeight = 15;
        int spacing = 5;
        
        int bottomY = h - 5;
        
        // Green Section (Bottom)
        int gBarY = bottomY - barHeight; // e.g. h-20
        QRect gLegend(10, gBarY, w - 20, barHeight);
        
        int gTextY = gBarY - spacing; 
        
        // Red Section (Above Green)
        int rBarY = gTextY - textHeight - spacing - barHeight;
        QRect rLegend(10, rBarY, w - 20, barHeight);
        
        int rTextY = rBarY - spacing;

        // Draw Red
        QLinearGradient rGrad(rLegend.topLeft(), rLegend.topRight());
        rGrad.setColorAt(0, Qt::black);
        rGrad.setColorAt(1, Qt::red);
        painter.fillRect(rLegend, rGrad);
        painter.drawRect(rLegend);
        
        // Ensure ranges are valid for display
        double minR = (m_minRed < std::numeric_limits<double>::max()) ? m_minRed : 0.0;
        double maxR = (m_maxRed > std::numeric_limits<double>::lowest()) ? m_maxRed : 0.0;
        
        QString rMinStr = QString("Red Min: %1").arg(minR, 0, 'f', 2);
        QString rMaxStr = QString("Max: %1").arg(maxR, 0, 'f', 2);
        painter.drawText(rLegend.left(), rBarY - 2, rMinStr);
        painter.drawText(rLegend.right() - fm.horizontalAdvance(rMaxStr), rBarY - 2, rMaxStr);

        // Draw Green
        QLinearGradient gGrad(gLegend.topLeft(), gLegend.topRight());
        gGrad.setColorAt(0, Qt::black);
        gGrad.setColorAt(1, Qt::green);
        painter.fillRect(gLegend, gGrad);
        painter.drawRect(gLegend);
        
        double minG = (m_minGreen < std::numeric_limits<double>::max()) ? m_minGreen : 0.0;
        double maxG = (m_maxGreen > std::numeric_limits<double>::lowest()) ? m_maxGreen : 0.0;
        
        QString gMinStr = QString("Green Min: %1").arg(minG, 0, 'f', 2);
        QString gMaxStr = QString("Max: %1").arg(maxG, 0, 'f', 2);
        painter.drawText(gLegend.left(), gBarY - 2, gMinStr);
        painter.drawText(gLegend.right() - fm.horizontalAdvance(gMaxStr), gBarY - 2, gMaxStr);

    } else {
        QRect legendRect(10, h - 30, w - 20, 20);
        
        // Draw gradient
        QLinearGradient grad(legendRect.topLeft(), legendRect.topRight());
        grad.setColorAt(0, Qt::black);
        
        QString label;
        if (m_mode == Mode_BlurAnalysis) {
             grad.setColorAt(1, Qt::white);
             label = "Correction Amount";
        } else {
             grad.setColorAt(1, Qt::white);
             label = "Correction Amount";
        }
        
        painter.drawText(10, h - 35, label);
        
        painter.fillRect(legendRect, grad);
        painter.drawRect(legendRect);
        
        if (m_mode == Mode_BlurAnalysis) {
            double minB = (m_minBlur < std::numeric_limits<double>::max()) ? m_minBlur : 0.0;
            double maxB = (m_maxBlur > std::numeric_limits<double>::lowest()) ? m_maxBlur : 0.0;
            QString minS = QString::number(minB, 'f', 2);
            QString maxS = QString::number(maxB, 'f', 2);
            
            painter.setPen(Qt::white); 
            painter.drawText(legendRect.left(), h - 5, minS);
            painter.drawText(legendRect.right() - fm.horizontalAdvance(maxS), h - 5, maxS);
        } else {
            painter.drawText(legendRect.left(), h - 5, "0");
            painter.drawText(legendRect.right() - 20, h - 5, "255");
        }
    }
}
