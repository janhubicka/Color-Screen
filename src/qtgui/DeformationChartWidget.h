#ifndef DEFORMATIONCHARTWIDGET_H
#define DEFORMATIONCHARTWIDGET_H

#include <QWidget>
#include "../libcolorscreen/include/color.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"

class QSlider;
class QLabel;

class DeformationChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DeformationChartWidget(QWidget *parent = nullptr);
    
    void setDeformationData(const colorscreen::scr_to_img_parameters &deformed,
                           const colorscreen::scr_to_img_parameters &undeformed,
                           int scanWidth, int scanHeight);
    void setHeatmapTolerance(double tolerance);
    void clear();
    
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    
protected:
    void paintEvent(QPaintEvent *event) override;
    
private:
    colorscreen::scr_to_img_parameters m_deformedParams;
    colorscreen::scr_to_img_parameters m_undeformedParams;
    int m_scanWidth = 0;
    int m_scanHeight = 0;
    bool m_hasData = false;
    double m_heatmapTolerance = 0.1; // Default tolerance
    
    QSlider *m_exaggerateSlider;
    QLabel *m_sliderLabel;
    
    // Helper to calculate aspect ratio
    double getAspectRatio() const;
    
    // Helper to get exaggeration factor from slider (logarithmic scale 1.0 to 100.0)
    float getExaggerationFactor() const;
};

#endif // DEFORMATIONCHARTWIDGET_H
