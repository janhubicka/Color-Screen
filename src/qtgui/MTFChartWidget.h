#ifndef MTFCHARTWIDGET_H
#define MTFCHARTWIDGET_H

#include <QWidget>
#include <set>
#include "../libcolorscreen/include/render-parameters.h"

class MTFChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MTFChartWidget(QWidget *parent = nullptr);
    
    void setMTFData(const colorscreen::mtf_parameters::computed_mtf &data, bool canSimulateDifraction, double scanDpi, double screenFreq = -1);
    void setMeasuredMTF(const std::vector<colorscreen::mtf_measurement> &measurements, const std::array<double, 4> &channelWavelengths);
    void clear();
    
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    
protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    
private:
    struct LayoutInfo {
        int baseFontSize;
        int smallFontSize;
        int lineHeight;
        int marginLeft;
        int marginRight;
        int marginTop;
        int marginBottom;
        int infoSectionHeight;
        int legendHeight;
        int infoStartY;
        int legendStartY;
        QRect chartRect;
    };
    
    LayoutInfo calculateLayout(int width, int height) const;
    
    struct LegendItem {
        QString name;
        QColor color;
        int width;
        bool visible;
        const std::vector<double> *data = nullptr;
        const colorscreen::mtf_measurement *measurement = nullptr;
    };
    
    std::vector<LegendItem> getLegendItems() const;
    bool isVisible(const QString &name) const { return m_hiddenItems.find(name) == m_hiddenItems.end(); }

    colorscreen::mtf_parameters::computed_mtf m_data;
    bool m_hasData = false;
    bool m_canSimulateDifraction = true;
    double m_scanDpi = 0;
    double m_screenFreq = -1;
    
    std::vector<colorscreen::mtf_measurement> m_measurements;
    std::array<double, 4> m_channelWavelengths;
    bool m_hasMeasuredData = false;

    std::set<QString> m_hiddenItems;
};

#endif // MTFCHARTWIDGET_H
