#ifndef ADAPTIVESHARPENINGCHART_H
#define ADAPTIVESHARPENINGCHART_H

#include <QWidget>
#include <QImage>
#include <QAction>
#include <QPointer>
#include <memory>
#include <vector>
#include "../libcolorscreen/include/scanner-blur-correction-parameters.h"

class QPainter;

class AdaptiveSharpeningChart : public QWidget
{
    Q_OBJECT
public:
    explicit AdaptiveSharpeningChart(QWidget *parent = nullptr);

    enum Mode {
        Mode_StripAnalysis,
        Mode_BlurAnalysis,
        Mode_FinalCorrection
    };

    /** Prepare a WIDTH by HEIGHT live-analysis grid. */
    void initialize(int width, int height);

    /** Record one coarse strip-width result at X,Y. */
    void updateStrip(int x, int y, double red_width, double green_width);
    /** Record one dense correction result at X,Y. */
    void updateBlur(int x, int y, double correction);

    /** Display a completed correction table and any attached diagnostics. */
    void setCorrection(std::shared_ptr<colorscreen::scanner_blur_correction_parameters> correction);

    /** Clear both live and completed chart data. */
    void clear();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    enum FinalDisplayMode {
        FinalDisplay_Correction,
        FinalDisplay_Spread,
        FinalDisplay_Support,
        FinalDisplay_Contrast
    };

    void updatePreview();
    void renderLegend(QPainter &painter);
    void resetRanges();
    void setFinalDisplayMode(FinalDisplayMode mode);
    void updateFinalRange();
    double finalDisplayValue(int x, int y) const;
    QRect previewTargetRect() const;

    std::shared_ptr<colorscreen::scanner_blur_correction_parameters> m_correction;
    
    // Internal buffer for live analysis
    struct Tile {
        double red = 0.0;
        double green = 0.0;
        double blur = 0.0;
        bool valid = false;
        bool stripAnalyzed = false;
        bool blurAnalyzed = false;
    };
    std::vector<Tile> m_liveData;
    int m_gridWidth = 0;
    int m_gridHeight = 0;
    
    Mode m_mode = Mode_FinalCorrection;
    FinalDisplayMode m_finalDisplayMode = FinalDisplay_Correction;
    QPointer<QAction> m_showCorrectionAction;
    QPointer<QAction> m_showSpreadAction;
    QPointer<QAction> m_showSupportAction;
    QPointer<QAction> m_showContrastAction;
    
    QImage m_preview;
    bool m_dirty = false;
    
    // Dynamic scaling ranges
    double m_minRed = 0.0, m_maxRed = 1.0;
    double m_minGreen = 0.0, m_maxGreen = 1.0;
    double m_minBlur = 0.0, m_maxBlur = 1.0;
    double m_minFinal = 0.0, m_maxFinal = 1.0;
};

#endif // ADAPTIVESHARPENINGCHART_H
