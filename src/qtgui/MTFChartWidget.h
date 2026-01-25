#ifndef MTFCHARTWIDGET_H
#define MTFCHARTWIDGET_H

#include <QWidget>
#include "../libcolorscreen/include/render-parameters.h"

class MTFChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MTFChartWidget(QWidget *parent = nullptr);
    
    void setMTFData(const colorscreen::mtf_parameters::computed_mtf &data, bool canSimulateDifraction, double scanDpi);
    void setMeasuredMTF(const std::vector<double> &freq, const std::vector<double> &contrast);
    void clear();
    
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    
protected:
    void paintEvent(QPaintEvent *event) override;
    
private:
    colorscreen::mtf_parameters::computed_mtf m_data;
    bool m_hasData = false;
    bool m_canSimulateDifraction = true;
    double m_scanDpi = 0;
    
    std::vector<double> m_measuredFreq;
    std::vector<double> m_measuredContrast;
    bool m_hasMeasuredData = false;
};

#endif // MTFCHARTWIDGET_H
