#include "AdaptiveSharpeningChart.h"
#include <QAction>
#include <QActionGroup>
#include <QHelpEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QToolTip>
#include <cmath>
#include <algorithm>
#include <limits>

/** Construct an adaptive-analysis chart and its final-map context menu. */
AdaptiveSharpeningChart::AdaptiveSharpeningChart(QWidget *parent)
    : QWidget(parent)
{
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setContextMenuPolicy(Qt::ActionsContextMenu);

    auto *displayGroup = new QActionGroup(this);
    displayGroup->setExclusive(true);
    m_showCorrectionAction = new QAction(tr("Show correction"), this);
    m_showSpreadAction = new QAction(tr("Show robust measurement spread"), this);
    m_showSupportAction = new QAction(tr("Show accepted sample fraction"), this);
    m_showContrastAction = new QAction(tr("Show mean fitted contrast"), this);
    for (QAction *action : {m_showCorrectionAction, m_showSpreadAction,
                            m_showSupportAction, m_showContrastAction}) {
        action->setCheckable(true);
        displayGroup->addAction(action);
        addAction(action);
    }
    m_showCorrectionAction->setChecked(true);
    m_showSpreadAction->setEnabled(false);
    m_showSupportAction->setEnabled(false);
    m_showContrastAction->setEnabled(false);
    connect(m_showCorrectionAction, &QAction::triggered, this,
            [this]() { setFinalDisplayMode(FinalDisplay_Correction); });
    connect(m_showSpreadAction, &QAction::triggered, this,
            [this]() { setFinalDisplayMode(FinalDisplay_Spread); });
    connect(m_showSupportAction, &QAction::triggered, this,
            [this]() { setFinalDisplayMode(FinalDisplay_Support); });
    connect(m_showContrastAction, &QAction::triggered, this,
            [this]() { setFinalDisplayMode(FinalDisplay_Contrast); });
    setToolTip(tr("Right-click the final map to select correction or confidence diagnostics."));
    resetRanges();
}

/** Reset dynamic ranges before starting or clearing an analysis. */
void AdaptiveSharpeningChart::resetRanges()
{
    m_minRed = std::numeric_limits<double>::max();
    m_maxRed = std::numeric_limits<double>::lowest();
    m_minGreen = std::numeric_limits<double>::max();
    m_maxGreen = std::numeric_limits<double>::lowest();
    m_minBlur = std::numeric_limits<double>::max();
    m_maxBlur = std::numeric_limits<double>::lowest();
    m_minFinal = std::numeric_limits<double>::max();
    m_maxFinal = std::numeric_limits<double>::lowest();
}

/** Prepare a WIDTH by HEIGHT live-analysis grid. */
void AdaptiveSharpeningChart::initialize(int width, int height)
{
    m_gridWidth = width;
    m_gridHeight = height;
    m_liveData.assign(width * height, Tile{});
    m_correction.reset();
    if (m_showSpreadAction)
        m_showSpreadAction->setEnabled(false);
    if (m_showSupportAction)
        m_showSupportAction->setEnabled(false);
    if (m_showContrastAction)
        m_showContrastAction->setEnabled(false);
    setFinalDisplayMode(FinalDisplay_Correction);
    m_mode = Mode_StripAnalysis; // Start with strip analysis
    m_dirty = true;
    resetRanges();
    update();
}

/** Record strip widths RED_WIDTH and GREEN_WIDTH for coarse cell X,Y. */
void AdaptiveSharpeningChart::updateStrip(int x, int y, double red_width,
                                          double green_width)
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

/** Record dense CORRECTION for cell X,Y. */
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

/** Display CORRECTION and enable diagnostic maps when they are available. */
void AdaptiveSharpeningChart::setCorrection(
    std::shared_ptr<colorscreen::scanner_blur_correction_parameters> correction)
{
    /* Parameter-state updates call this for every unrelated GUI change.  Do
       not churn the chart (or its detachable context-menu actions) unless the
       correction object itself changed.  Adaptive analysis installs a fresh
       correction object when new data are available.  */
    if (m_correction == correction)
        return;
    m_correction = correction;
    if (m_correction) {
        m_gridWidth = m_correction->get_width();
        m_gridHeight = m_correction->get_height();
    } else {
        m_gridWidth = 0;
        m_gridHeight = 0;
    }
    const bool hasDiagnostics = m_correction && m_correction->has_diagnostics();
    /* These actions are presentation-only.  They may disappear when the chart
       has been detached/reparented while a parameter-state update is in
       flight, so never make correction-state updates depend on their
       lifetime.  */
    if (m_showSpreadAction)
        m_showSpreadAction->setEnabled(hasDiagnostics);
    if (m_showSupportAction)
        m_showSupportAction->setEnabled(hasDiagnostics);
    if (m_showContrastAction)
        m_showContrastAction->setEnabled(hasDiagnostics);
    if (!hasDiagnostics && m_finalDisplayMode != FinalDisplay_Correction)
        setFinalDisplayMode(FinalDisplay_Correction);
    m_mode = Mode_FinalCorrection;
    updateFinalRange();
    m_dirty = true;
    update();
}

/** Clear all live and final chart state. */
void AdaptiveSharpeningChart::clear()
{
    m_correction.reset();
    m_liveData.clear();
    m_gridWidth = 0;
    m_gridHeight = 0;
    m_preview = QImage();
    m_dirty = false;
    if (m_showSpreadAction)
        m_showSpreadAction->setEnabled(false);
    if (m_showSupportAction)
        m_showSupportAction->setEnabled(false);
    if (m_showContrastAction)
        m_showContrastAction->setEnabled(false);
    setFinalDisplayMode(FinalDisplay_Correction);
    resetRanges();
    update();
}

/** Select MODE for the completed correction map. */
void AdaptiveSharpeningChart::setFinalDisplayMode(FinalDisplayMode mode)
{
    if (mode != FinalDisplay_Correction
        && (!m_correction || !m_correction->has_diagnostics()))
        mode = FinalDisplay_Correction;
    m_finalDisplayMode = mode;
    if (m_showCorrectionAction)
        m_showCorrectionAction->setChecked(mode == FinalDisplay_Correction);
    if (m_showSpreadAction)
        m_showSpreadAction->setChecked(mode == FinalDisplay_Spread);
    if (m_showSupportAction)
        m_showSupportAction->setChecked(mode == FinalDisplay_Support);
    if (m_showContrastAction)
        m_showContrastAction->setChecked(mode == FinalDisplay_Contrast);
    updateFinalRange();
    m_dirty = true;
    update();
}

/** Return the scalar represented by the selected final map at X,Y. */
double AdaptiveSharpeningChart::finalDisplayValue(int x, int y) const
{
    if (!m_correction)
        return 0.0;
    if (m_finalDisplayMode == FinalDisplay_Correction)
        return m_correction->get_correction(x, y);
    const auto *diagnostics = m_correction->get_diagnostics(x, y);
    if (!diagnostics)
        return 0.0;
    switch (m_finalDisplayMode) {
    case FinalDisplay_Correction:
        return m_correction->get_correction(x, y);
    case FinalDisplay_Spread:
        return diagnostics->robust_spread;
    case FinalDisplay_Support:
        return diagnostics->total_samples > 0
                   ? static_cast<double>(diagnostics->accepted_samples)
                         / diagnostics->total_samples
                   : 0.0;
    case FinalDisplay_Contrast:
        return diagnostics->mean_contrast;
    }
    return 0.0;
}

/** Recompute the display range for the selected final-map quantity. */
void AdaptiveSharpeningChart::updateFinalRange()
{
    if (m_finalDisplayMode == FinalDisplay_Support) {
        m_minFinal = 0.0;
        m_maxFinal = 1.0;
        return;
    }

    m_minFinal = m_finalDisplayMode == FinalDisplay_Correction
                     ? std::numeric_limits<double>::max()
                     : 0.0;
    m_maxFinal = std::numeric_limits<double>::lowest();
    if (!m_correction)
        return;
    for (int y = 0; y < m_correction->get_height(); y++)
        for (int x = 0; x < m_correction->get_width(); x++) {
            const double value = finalDisplayValue(x, y);
            if (!colorscreen::my_isfinite(value))
                continue;
            m_minFinal = std::min(m_minFinal, value);
            m_maxFinal = std::max(m_maxFinal, value);
        }
    if (m_maxFinal == std::numeric_limits<double>::lowest()
        || m_minFinal == std::numeric_limits<double>::max()) {
        m_minFinal = 0.0;
        m_maxFinal = 1.0;
    }
}

/** Return the widget rectangle occupied by the un-smoothed map preview. */
QRect AdaptiveSharpeningChart::previewTargetRect() const
{
    if (m_preview.isNull() || m_preview.width() <= 0 || m_preview.height() <= 0
        || width() <= 0 || height() <= 0)
        return QRect();
    const double widgetAspect = static_cast<double>(width()) / height();
    const double imageAspect
        = static_cast<double>(m_preview.width()) / m_preview.height();
    if (widgetAspect > imageAspect) {
        const int h = height();
        const int w = static_cast<int>(h * imageAspect);
        return QRect((width() - w) / 2, 0, w, h);
    }
    const int w = width();
    const int h = static_cast<int>(w / imageAspect);
    return QRect(0, (height() - h) / 2, w, h);
}

/** Show exact per-cell final values for tooltip EVENTs. */
bool AdaptiveSharpeningChart::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip && m_mode == Mode_FinalCorrection
        && m_correction) {
        auto *helpEvent = static_cast<QHelpEvent *>(event);
        const QRect target = previewTargetRect();
        if (target.contains(helpEvent->pos()) && target.width() > 0
            && target.height() > 0) {
            const int x = std::clamp(
                (helpEvent->pos().x() - target.x()) * m_gridWidth
                    / target.width(),
                0, m_gridWidth - 1);
            const int y = std::clamp(
                (helpEvent->pos().y() - target.y()) * m_gridHeight
                    / target.height(),
                0, m_gridHeight - 1);
            QString text = tr("Cell %1,%2\nCorrection: %3")
                               .arg(x)
                               .arg(y)
                               .arg(m_correction->get_correction(x, y), 0,
                                    'g', 8);
            if (const auto *diagnostics
                = m_correction->get_diagnostics(x, y)) {
                const double support
                    = diagnostics->total_samples > 0
                          ? 100.0 * diagnostics->accepted_samples
                                / diagnostics->total_samples
                          : 0.0;
                text += tr("\nRobust spread: %1\nAccepted samples: %2/%3 "
                           "(%4%)\nMean fitted contrast: %5%")
                            .arg(diagnostics->robust_spread, 0, 'g', 8)
                            .arg(diagnostics->accepted_samples)
                            .arg(diagnostics->total_samples)
                            .arg(support, 0, 'f', 1)
                            .arg(diagnostics->mean_contrast * 100.0, 0, 'g',
                                 6);
            }
            QToolTip::showText(helpEvent->globalPos(), text, this, target);
            return true;
        }
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    return QWidget::event(event);
}

/** Return the preferred chart size. */
QSize AdaptiveSharpeningChart::sizeHint() const
{
    return QSize(400, 300);
}

/** Return the smallest useful chart size. */
QSize AdaptiveSharpeningChart::minimumSizeHint() const
{
    return QSize(200, 150);
}

/** Paint the map and legend. */
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
    const QRect target = previewTargetRect();
    if (!target.isEmpty()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false); // Keep pixels sharp for grid
        painter.drawImage(target, m_preview);
    }
    
    renderLegend(painter);
}

/** Mark the preview dirty after resize EVENT. */
void AdaptiveSharpeningChart::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_dirty = true; // In case we want to re-render based on size (though we usually scale)
}

/** Rebuild the small nearest-neighbour map image from current data. */
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
    if (m_mode == Mode_FinalCorrection)
        updateFinalRange();
    const double rangeFinal
        = (m_maxFinal > m_minFinal) ? (m_maxFinal - m_minFinal) : 1.0;
    
    for (int y = 0; y < m_gridHeight; y++) {
        for (int x = 0; x < m_gridWidth; x++) {
            QColor c = Qt::black; // Uncomputed color
            
            if (m_mode == Mode_FinalCorrection && m_correction) {
                 const double value = finalDisplayValue(x, y);
                 const double normalized
                     = std::clamp((value - m_minFinal) / rangeFinal, 0.0, 1.0);
                 switch (m_finalDisplayMode) {
                 case FinalDisplay_Correction:
                     c = QColor::fromHsvF((1.0 - normalized) * 0.66, 1.0, 1.0);
                     break;
                 case FinalDisplay_Spread:
                     // Low spread is high confidence (green), high spread red.
                     c = QColor::fromHsvF((1.0 - normalized) * 0.33, 1.0, 1.0);
                     break;
                 case FinalDisplay_Support:
                     // More accepted samples are better.
                     c = QColor::fromHsvF(normalized * 0.33, 1.0, 1.0);
                     break;
                 case FinalDisplay_Contrast: {
                     const int gray = std::clamp(
                         static_cast<int>(normalized * 255.0), 0, 255);
                     c = QColor(gray, gray, gray);
                     break;
                 }
                 }
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

/** Draw the legend using PAINTER. */
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
        QLinearGradient grad(legendRect.topLeft(), legendRect.topRight());
        QString label;
        QString minText;
        QString maxText;

        if (m_mode == Mode_BlurAnalysis) {
            grad.setColorAt(0, Qt::black);
            grad.setColorAt(1, Qt::white);
            label = tr("Correction amount");
            const double minValue
                = m_minBlur < std::numeric_limits<double>::max() ? m_minBlur
                                                                 : 0.0;
            const double maxValue
                = m_maxBlur > std::numeric_limits<double>::lowest() ? m_maxBlur
                                                                    : 0.0;
            minText = QString::number(minValue, 'g', 6);
            maxText = QString::number(maxValue, 'g', 6);
        } else {
            switch (m_finalDisplayMode) {
            case FinalDisplay_Correction:
                grad.setColorAt(0, QColor::fromHsvF(0.66, 1.0, 1.0));
                grad.setColorAt(1, QColor::fromHsvF(0.0, 1.0, 1.0));
                label = tr("Correction (right-click for diagnostics)");
                minText = QString::number(m_minFinal, 'g', 6);
                maxText = QString::number(m_maxFinal, 'g', 6);
                break;
            case FinalDisplay_Spread:
                grad.setColorAt(0, QColor::fromHsvF(0.33, 1.0, 1.0));
                grad.setColorAt(1, QColor::fromHsvF(0.0, 1.0, 1.0));
                label = tr("Robust measurement spread (lower is better)");
                minText = QString::number(m_minFinal, 'g', 6);
                maxText = QString::number(m_maxFinal, 'g', 6);
                break;
            case FinalDisplay_Support:
                grad.setColorAt(0, QColor::fromHsvF(0.0, 1.0, 1.0));
                grad.setColorAt(1, QColor::fromHsvF(0.33, 1.0, 1.0));
                label = tr("Accepted sample fraction");
                minText = QString::number(m_minFinal * 100.0, 'g', 5) + "%";
                maxText = QString::number(m_maxFinal * 100.0, 'g', 5) + "%";
                break;
            case FinalDisplay_Contrast:
                grad.setColorAt(0, Qt::black);
                grad.setColorAt(1, Qt::white);
                label = tr("Mean fitted contrast");
                minText = QString::number(m_minFinal * 100.0, 'g', 5) + "%";
                maxText = QString::number(m_maxFinal * 100.0, 'g', 5) + "%";
                break;
            }
        }

        painter.drawText(10, h - 35, label);
        painter.fillRect(legendRect, grad);
        painter.drawRect(legendRect);
        painter.setPen(Qt::white);
        painter.drawText(legendRect.left(), h - 5, minText);
        painter.drawText(legendRect.right() - fm.horizontalAdvance(maxText),
                         h - 5, maxText);
    }
}
