#ifndef BACKLIGHTCHARTWIDGET_H
#define BACKLIGHTCHARTWIDGET_H

#include <QWidget>
#include <QImage>
#include <memory>
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/backlight-correction-parameters.h"

class BacklightChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BacklightChartWidget(QWidget *parent = nullptr);
    
    void setBacklightData(std::shared_ptr<colorscreen::backlight_correction_parameters> cor,
                         int scanWidth, int scanHeight,
                         const colorscreen::int_image_area &scan_area,
                         colorscreen::luminosity_t black,
                         bool mirror, int rotation);
    void clear();
    
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    
protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    
private:
    void updatePreview();
    
    std::shared_ptr<colorscreen::backlight_correction_parameters> m_cor;
    int m_scanWidth = 0;
    int m_scanHeight = 0;
    colorscreen::int_image_area m_scanArea;
    colorscreen::luminosity_t m_black = 0;
    bool m_mirror = false;
    int m_rotation = 0;
    
    QImage m_preview;
    bool m_dirty = false;
};

#endif // BACKLIGHTCHARTWIDGET_H
