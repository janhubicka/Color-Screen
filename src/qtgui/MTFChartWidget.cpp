#include "MTFChartWidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <QFontMetrics>

MTFChartWidget::MTFChartWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(200);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
}

void MTFChartWidget::setMTFData(const colorscreen::mtf_parameters::computed_mtf &data, bool canSimulateDifraction)
{
    m_data = data;
    m_hasData = !data.system_mtf.empty();
    m_canSimulateDifraction = canSimulateDifraction;
    update();
}

void MTFChartWidget::setMeasuredMTF(const std::vector<double> &freq, const std::vector<double> &contrast)
{
    m_measuredFreq = freq;
    m_measuredContrast = contrast;
    m_hasMeasuredData = !freq.empty() && !contrast.empty();
    update();
}

void MTFChartWidget::clear()
{
    m_hasData = false;
    m_hasMeasuredData = false;
    m_measuredFreq.clear();
    m_measuredContrast.clear();
    update();
}

QSize MTFChartWidget::sizeHint() const
{
    // Golden ratio: width:height = 1.61:1
    // For 500px width -> height = 500/1.61 + margins = 310 + 125 = 435
    return QSize(500, 435);
}

QSize MTFChartWidget::minimumSizeHint() const
{
    // Minimum for golden ratio
    return QSize(300, 310);
}

bool MTFChartWidget::hasHeightForWidth() const
{
    return true;
}

int MTFChartWidget::heightForWidth(int width) const
{
    // Maintain golden ratio: width:height = 1.61:1
    // Chart area height = width / 1.61
    // Total height = chart height + margins (top 20 + bottom 105)
    int chartHeight = static_cast<int>(width / 1.61);
    return chartHeight + 125;  // 20 (top) + 105 (bottom)
}

void MTFChartWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Background
    painter.fillRect(rect(), palette().base());
    
    if (!m_hasData || m_data.system_mtf.empty())
    {
        painter.setPen(palette().text().color());
        painter.drawText(rect(), Qt::AlignCenter, "No MTF data");
        return;
    }
    
    // Define margins and chart area
    const int marginLeft = 60;
    const int marginRight = 20;
    const int marginTop = 20;
    const int marginBottom = 105;  // More space for label + legend
    
    QRect chartRect(marginLeft, marginTop, 
                    width() - marginLeft - marginRight,
                    height() - marginTop - marginBottom);
    
    if (chartRect.width() < 10 || chartRect.height() < 10)
        return;
    
    // Draw axes
    painter.setPen(QPen(palette().text().color(), 1));
    painter.drawRect(chartRect);
    
    // Draw grid and labels
    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);
    
    // Y-axis labels (0-100%)
    for (int i = 0; i <= 10; i++)
    {
        int y = chartRect.bottom() - (chartRect.height() * i / 10);
        painter.drawLine(chartRect.left() - 5, y, chartRect.left(), y);
        
        QString label = QString::number(i * 10);
        QRect textRect(0, y - 10, marginLeft - 10, 20);
        painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, label);
        
        // Grid line
        if (i > 0 && i < 10)
        {
            painter.setPen(QPen(palette().mid().color(), 1, Qt::DotLine));
            painter.drawLine(chartRect.left(), y, chartRect.right(), y);
            painter.setPen(QPen(palette().text().color(), 1));
        }
    }
    
    // X-axis labels (0.0-1.0 frequency)
    for (int i = 0; i <= 10; i++)
    {
        int x = chartRect.left() + (chartRect.width() * i / 10);
        painter.drawLine(x, chartRect.bottom(), x, chartRect.bottom() + 5);
        
        QString label = QString::number(i / 10.0, 'f', 1);
        QRect textRect(x - 20, chartRect.bottom() + 5, 40, 20);
        painter.drawText(textRect, Qt::AlignCenter, label);
        
        // Grid line
        if (i > 0 && i < 10)
        {
            painter.setPen(QPen(palette().mid().color(), 1, Qt::DotLine));
            painter.drawLine(x, chartRect.top(), x, chartRect.bottom());
            painter.setPen(QPen(palette().text().color(), 1));
        }
    }
    
    // Draw Nyquist line at 0.5
    int nyquistX = chartRect.left() + chartRect.width() / 2;
    painter.setPen(QPen(QColor(150, 150, 150), 2, Qt::DashLine));
    painter.drawLine(nyquistX, chartRect.top(), nyquistX, chartRect.bottom());
    
    QFont smallFont = font;
    smallFont.setPointSize(8);
    painter.setFont(smallFont);
    painter.setPen(palette().text().color());
    painter.drawText(nyquistX - 30, chartRect.top() - 5, 60, 15, 
                     Qt::AlignCenter, "Nyquist");
    painter.setFont(font);
    
    // Axis labels
    painter.setPen(palette().text().color());
    painter.drawText(QRect(0, chartRect.bottom() + 25, width(), 15),
                     Qt::AlignCenter, "Pixel frequency");
    
    painter.save();
    painter.translate(15, height() / 2);
    painter.rotate(-90);
    painter.drawText(-50, 0, 100, 20, Qt::AlignCenter, "MTF (%)");
    painter.restore();
    
    // Helper function to draw a curve
    auto drawCurve = [&](const std::vector<double> &data, const QColor &color, int lineWidth = 2) {
        if (data.empty()) return;
        
        painter.setPen(QPen(color, lineWidth));
        QPointF prevPoint;
        
        for (size_t i = 0; i < data.size(); ++i)
        {
            double freq = i / (double)(data.size() - 1);
            double value = data[i] * 100.0; // Scale to percentage
            
            int x = chartRect.left() + (int)(freq * chartRect.width());
            int y = chartRect.bottom() - (int)((value / 100.0) * chartRect.height());
            
            QPointF point(x, y);
            
            if (i > 0)
                painter.drawLine(prevPoint, point);
            
            prevPoint = point;
        }
    };
    
    // Draw all curves
    // Only show difraction-specific curves if difraction can be simulated
    if (m_canSimulateDifraction)
    {
        drawCurve(m_data.lens_difraction_mtf, QColor(255, 100, 100));  // Red - Difraction
        drawCurve(m_data.stokseth_defocus_mtf, QColor(255, 165, 0));   // Orange - Defocus
    }
    else
    {
        // Show Hopkins blur when difraction cannot be simulated
        drawCurve(m_data.hopkins_blur_mtf, QColor(139, 69, 19));       // Brown - Hopkins blur
    }
    
    drawCurve(m_data.gaussian_blur_mtf, QColor(100, 200, 100));    // Green - Gaussian blur
    drawCurve(m_data.lens_mtf, QColor(100, 100, 255));             // Blue - Lens
    drawCurve(m_data.sensor_mtf, QColor(200, 100, 200));           // Purple - Sensor
    drawCurve(m_data.system_mtf, QColor(0, 180, 180), 3);          // Cyan (bold) - System
    
    // Draw measured MTF data if available
    if (m_hasMeasuredData && m_measuredFreq.size() == m_measuredContrast.size())
    {
        painter.setPen(QPen(QColor(255, 0, 255), 2));  // Magenta
        QPointF prevPoint;
        
        for (size_t i = 0; i < m_measuredFreq.size(); ++i)
        {
            double freq = m_measuredFreq[i];
            double value = m_measuredContrast[i] * 0.01; 
            
            // Clamp to visible range
            if (freq < 0.0 || freq > 1.0) continue;
            
            int x = chartRect.left() + (int)(freq * chartRect.width());
            int y = chartRect.bottom() - (int)(value * chartRect.height());
            
            QPointF point(x, y);
            
            if (i > 0)
                painter.drawLine(prevPoint, point);
            
            prevPoint = point;
        }
    }
    
    // Draw legend below "Pixel frequency" label
    int legendY = chartRect.bottom() + 45;  // Below the axis label
    int legendX = chartRect.left();
    int itemWidth = 120;
    int lineHeight = 20;
    
    struct LegendItem {
        QString name;
        QColor color;
        int width;
        bool visible;
    };
    
    LegendItem items[] = {
        {"Difraction", QColor(255, 100, 100), 2, m_canSimulateDifraction},
        {"Defocus", QColor(255, 165, 0), 2, m_canSimulateDifraction},
        {"Hopkins blur", QColor(139, 69, 19), 2, !m_canSimulateDifraction},
        {"Gaussian blur", QColor(100, 200, 100), 2, true},
        {"Lens", QColor(100, 100, 255), 2, true},
        {"Sensor", QColor(200, 100, 200), 2, true},
        {"System", QColor(0, 180, 180), 3, true},
        {"Measured MTF", QColor(255, 0, 255), 2, m_hasMeasuredData}
    };
    
    int col = 0;
    for (const auto &item : items)
    {
        if (!item.visible)
            continue;
            
        int x = legendX + col * itemWidth;
        
        // Line sample
        painter.setPen(QPen(item.color, item.width));
        painter.drawLine(x, legendY + lineHeight / 2, 
                        x + 20, legendY + lineHeight / 2);
        
        // Text
        painter.setPen(palette().text().color());
        painter.drawText(x + 25, legendY, 95, lineHeight,
                        Qt::AlignLeft | Qt::AlignVCenter, item.name);
        
        col++;
        if (col >= 3) {  // 3 items per row
            col = 0;
            legendY += lineHeight;
        }
    }
}
