#include "FinetuneImagesPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QImage>
#include <QPixmap>

FinetuneImagesPanel::FinetuneImagesPanel(QWidget *parent)
    : QWidget(parent) {
  setupUi();
}

void FinetuneImagesPanel::setupUi() {
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(5);

  // Row 1: original, sharpened, simulated, diff
  QWidget *row1Widget = new QWidget();
  m_row1Layout = new QHBoxLayout(row1Widget);
  m_row1Layout->setContentsMargins(0, 0, 0, 0);
  m_row1Layout->setSpacing(5);
  
  auto createImageSlot = [](const QString& caption) -> ImageSlot {
    ImageSlot slot;
    slot.caption = caption;
    slot.label = new ScalableImageLabel();
    // Align image to bottom so it sits on top of the caption even if widget is tall
    slot.label->setAlignment(Qt::AlignBottom | Qt::AlignHCenter);
    return slot;
  };

  m_row1Images.push_back(createImageSlot("Original"));
  m_row1Images.push_back(createImageSlot("Sharpened"));
  m_row1Images.push_back(createImageSlot("Simulated"));
  m_row1Images.push_back(createImageSlot("Diff"));

  for (auto& slot : m_row1Images) {
    slot.container = new QWidget();
    QVBoxLayout *vLayout = new QVBoxLayout(slot.container);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->setSpacing(2);
    // Add stretchable label
    vLayout->addWidget(slot.label, 1);
    
    slot.captionLabel = new QLabel(slot.caption);
    slot.captionLabel->setAlignment(Qt::AlignCenter);
    // Fixed size caption
    vLayout->addWidget(slot.captionLabel, 0, Qt::AlignCenter);
    
    m_row1Layout->addWidget(slot.container, 1);
  }
  
  mainLayout->addWidget(row1Widget, 1);

  // Row 2: screen, blured_screen, emulsion_screen, merged_screen, collected_screen, dot_spread
  QWidget *row2Widget = new QWidget();
  m_row2Layout = new QHBoxLayout(row2Widget);
  m_row2Layout->setContentsMargins(0, 0, 0, 0);
  m_row2Layout->setSpacing(5);

  m_row2Images.push_back(createImageSlot("Screen"));
  m_row2Images.push_back(createImageSlot("Blured Screen"));
  m_row2Images.push_back(createImageSlot("Emulsion"));
  m_row2Images.push_back(createImageSlot("Merged"));
  m_row2Images.push_back(createImageSlot("Collected"));
  m_row2Images.push_back(createImageSlot("Dot Spread"));

  for (auto& slot : m_row2Images) {
    slot.container = new QWidget();
    QVBoxLayout *vLayout = new QVBoxLayout(slot.container);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->setSpacing(2);
    vLayout->addWidget(slot.label, 1);
    
    slot.captionLabel = new QLabel(slot.caption);
    slot.captionLabel->setAlignment(Qt::AlignCenter);
    vLayout->addWidget(slot.captionLabel, 0, Qt::AlignCenter);
    
    m_row2Layout->addWidget(slot.container, 1);
  }
  
  mainLayout->addWidget(row2Widget, 1);
}

void FinetuneImagesPanel::setFinetuneResult(const colorscreen::finetune_result& result) {
  // Map result images to slots and convert to QPixmap immediately
  m_row1Images[0].pixmap = convertSimpleImageToQPixmap(result.orig.get());
  m_row1Images[1].pixmap = convertSimpleImageToQPixmap(result.sharpened.get());
  m_row1Images[2].pixmap = convertSimpleImageToQPixmap(result.simulated.get());
  m_row1Images[3].pixmap = convertSimpleImageToQPixmap(result.diff.get());

  m_row2Images[0].pixmap = convertSimpleImageToQPixmap(result.screen.get());
  m_row2Images[1].pixmap = convertSimpleImageToQPixmap(result.blured_screen.get());
  m_row2Images[2].pixmap = convertSimpleImageToQPixmap(result.emulsion_screen.get());
  m_row2Images[3].pixmap = convertSimpleImageToQPixmap(result.merged_screen.get());
  m_row2Images[4].pixmap = convertSimpleImageToQPixmap(result.collected_screen.get());
  m_row2Images[5].pixmap = convertSimpleImageToQPixmap(result.dot_spread.get());

  updateImageDisplay();
}

void FinetuneImagesPanel::clear() {
  for (auto& slot : m_row1Images) {
    slot.pixmap = QPixmap();
    slot.label->setPixmap(QPixmap());
    if (slot.container) slot.container->hide();
  }
  for (auto& slot : m_row2Images) {
    slot.pixmap = QPixmap();
    slot.label->setPixmap(QPixmap());
    if (slot.container) slot.container->hide();
  }
}

void FinetuneImagesPanel::updateImageDisplay() {
  auto updateRow = [&](std::vector<ImageSlot>& row) {
    // Check if we have any valid images to determine row visibility (optional)
    // But mainly we just update individual slots
    for (auto& slot : row) {
      if (slot.pixmap.isNull() || slot.pixmap.width() <= 0 || slot.pixmap.height() <= 0) {
        slot.container->hide();
        slot.label->setPixmap(QPixmap());
      } else {
        slot.container->show();
        slot.label->setPixmap(slot.pixmap);
        // No manual sizing or scaling here; ScalableImageLabel and Layout handle it
      }
    }
  };

  updateRow(m_row1Images);
  updateRow(m_row2Images);
}

QPixmap FinetuneImagesPanel::convertSimpleImageToQPixmap(const colorscreen::simple_image* img) {
  // Validate input
  if (!img || img->width <= 0 || img->height <= 0 || img->stride <= 0) {
    return QPixmap();
  }
  
  // Safety checks for corrupted data
  if (img->width > 100000 || img->height > 100000) {
      return QPixmap();
  }
  
  // Stride must be at least width * 3 (RGB)
  if (img->stride < img->width * 3) {
      return QPixmap();
  }

  QImage qImg(img->width, img->height, QImage::Format_RGB888);
  
  for (int y = 0; y < img->height; ++y) {
    for (int x = 0; x < img->width; ++x) {
      colorscreen::simple_image::rgb pixel = img->get_pixel(x, y);
      int r = qBound(0, pixel.red, 255);
      int g = qBound(0, pixel.green, 255);
      int b = qBound(0, pixel.blue, 255);
      qImg.setPixel(x, y, qRgb(r, g, b));
    }
  }

  return QPixmap::fromImage(qImg);
}
