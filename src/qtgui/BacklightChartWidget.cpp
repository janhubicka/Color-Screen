#include "BacklightChartWidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <QTransform>
#include "../libcolorscreen/include/colorscreen.h"

BacklightChartWidget::BacklightChartWidget(QWidget *parent)
    : QWidget(parent)
{
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
}

void BacklightChartWidget::setBacklightData(std::shared_ptr<colorscreen::backlight_correction_parameters> cor,
                                         int scanWidth, int scanHeight,
                                         const colorscreen::int_image_area &scan_area,
                                         colorscreen::luminosity_t black,
                                         bool mirror, int rotation)
{
    m_cor = cor;
    m_scanWidth = scanWidth;
    m_scanHeight = scanHeight;
    m_scanArea = scan_area;
    m_black = black;
    m_mirror = mirror;
    m_rotation = rotation;
    m_dirty = true;
    update();
}

void BacklightChartWidget::clear()
{
    m_cor.reset();
    m_preview = QImage();
    m_dirty = false;
    update();
}

QSize BacklightChartWidget::sizeHint() const
{
    return QSize(256, 256);
}

QSize BacklightChartWidget::minimumSizeHint() const
{
    return QSize(128, 128);
}

void BacklightChartWidget::paintEvent(QPaintEvent *event)
{
    if (m_dirty) {
        updatePreview();
    }

    QPainter painter(this);
    if (m_preview.isNull()) {
        painter.fillRect(rect(), Qt::black);
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "No Backlight Data");
        return;
    }

    // Centered aspect-ratio-respecting preview
    QSize s = m_preview.size();
    if (s.width() > 0 && s.height() > 0) {
        QSize targetSize = s.scaled(size(), Qt::KeepAspectRatio);
        QRect target((width() - targetSize.width()) / 2,
                     (height() - targetSize.height()) / 2,
                     targetSize.width(), targetSize.height());
        painter.drawImage(target, m_preview);
    }
}

void BacklightChartWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_dirty = true;
    update();
}

void BacklightChartWidget::updatePreview()
{
    if (!m_cor || m_scanWidth <= 0 || m_scanHeight <= 0) {
        m_preview = QImage();
        m_dirty = false;
        return;
    }

    int maxDim = qMax(width(), height());
    if (maxDim <= 0) maxDim = 256;

    int w = m_scanArea.width;
    int h = m_scanArea.height;
    if (w <= 0 || h <= 0) {
        w = m_scanWidth;
        h = m_scanHeight;
    }

    int previewWidth, previewHeight;
    if (w >= h) {
        previewWidth = maxDim;
        previewHeight = qMax(1, (int)(maxDim * (double)h / w));
    } else {
        previewHeight = maxDim;
        previewWidth = qMax(1, (int)(maxDim * (double)w / h));
    }

    QImage img(previewWidth, previewHeight, QImage::Format_RGB888);
    img.fill(Qt::black);

    colorscreen::tile_parameters tile;
    tile.width = previewWidth;
    tile.height = previewHeight;
    tile.pixelbytes = 3;
    tile.rowstride = previewWidth * 3;
    tile.pixels = img.bits();

    colorscreen::int_image_area scanArea = m_scanArea;

    m_cor->render_preview(tile, m_scanWidth, m_scanHeight, scanArea, m_black);
    
    // Apply rotation and mirroring
    if (m_mirror || m_rotation != 0) {
        QTransform trans;
        if (m_mirror) {
            trans.scale(-1, 1);
        }
        if (m_rotation != 0) {
            trans.rotate(m_rotation * 90);
        }
        img = img.transformed(trans);
    }
    
    m_preview = img;
    m_dirty = false;
}
