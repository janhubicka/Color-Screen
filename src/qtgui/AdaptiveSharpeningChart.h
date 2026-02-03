#ifndef ADAPTIVESHARPENINGCHART_H
#define ADAPTIVESHARPENINGCHART_H

#include <QWidget>
#include <QImage>
#include <memory>
#include "../libcolorscreen/include/scanner-blur-correction-parameters.h"

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

    // Initialize for a new run
    void initialize(int width, int height);

    // Update methods for live analysis
    void updateStrip(int x, int y, double red_width, double green_width);
    void updateBlur(int x, int y, double correction);

    // Set final or existing correction
    void setCorrection(std::shared_ptr<colorscreen::scanner_blur_correction_parameters> correction);
    
    void clear();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updatePreview();
    void renderLegend(QPainter &painter);
    void resetRanges();

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
    
    QImage m_preview;
    bool m_dirty = false;
    
    // Dynamic scaling ranges
    double m_minRed = 0.0, m_maxRed = 1.0;
    double m_minGreen = 0.0, m_maxGreen = 1.0;
    double m_minBlur = 0.0, m_maxBlur = 1.0;
};

#endif // ADAPTIVESHARPENINGCHART_H
