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
    slot.label = new QLabel();
    slot.label->setScaledContents(false);
    slot.label->setAlignment(Qt::AlignCenter);
    slot.label->setMinimumSize(10, 10); // Reduce minimum size
    // pixmap is default constructed (null)
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
    vLayout->addWidget(slot.label, 0, Qt::AlignCenter);
    
    slot.captionLabel = new QLabel(slot.caption);
    slot.captionLabel->setAlignment(Qt::AlignCenter);
    vLayout->addWidget(slot.captionLabel, 0, Qt::AlignCenter);
    
    m_row1Layout->addWidget(slot.container, 1);
  }
  
  mainLayout->addWidget(row1Widget);

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
    vLayout->addWidget(slot.label, 0, Qt::AlignCenter);
    
    slot.captionLabel = new QLabel(slot.caption);
    slot.captionLabel->setAlignment(Qt::AlignCenter);
    vLayout->addWidget(slot.captionLabel, 0, Qt::AlignCenter);
    
    m_row2Layout->addWidget(slot.container, 1);
  }
  
  mainLayout->addWidget(row2Widget);
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
  m_row2Images[5].pixmap = convertSimpleImageToQPixmap(result.sot_spread.get());

  updateImageDisplay();
}

void FinetuneImagesPanel::clear() {
  for (auto& slot : m_row1Images) {
    slot.pixmap = QPixmap();
    slot.label->clear();
    if (slot.container) slot.container->hide();
  }
  for (auto& slot : m_row2Images) {
    slot.pixmap = QPixmap();
    slot.label->clear();
    if (slot.container) slot.container->hide();
  }
}

void FinetuneImagesPanel::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  if (event->size().width() != m_currentWidth) {
    m_currentWidth = event->size().width();
    updateImageDisplay();
  }
}

void FinetuneImagesPanel::updateImageDisplay() {
  if (width() <= 0) return;
  
  auto updateRow = [this](std::vector<ImageSlot>& row) {
    // Count visible images
    int visibleCount = 0;
    for (const auto& slot : row) {
      if (!slot.pixmap.isNull() && slot.pixmap.width() > 0 && slot.pixmap.height() > 0) {
        visibleCount++;
      }
    }
    
    if (visibleCount == 0) return;

    // Calculate target size
    int availableWidth = width() - 20; // margins
    // Subtract spacing between items
    availableWidth -= (visibleCount - 1) * 5;
    
    int targetWidth = availableWidth / visibleCount;
    if (targetWidth < 10) targetWidth = 10;
    
    for (auto& slot : row) {
      if (slot.pixmap.isNull() || slot.pixmap.width() <= 0 || slot.pixmap.height() <= 0) {
        // Hide empty slots entirely
        slot.container->hide();
        continue;
      }
      
      // Show valid slots
      slot.container->show();
      
      int width = slot.pixmap.width();
      int height = slot.pixmap.height();

      // Calculate scaled dimensions maintaining aspect ratio
      int targetHeight = (int)((double)targetWidth * height / width);
      
      // Determine scaling mode: nearest-neighbor if upscaling > 2x
      Qt::TransformationMode mode = Qt::FastTransformation;
      if (targetWidth > width * 2 || targetHeight > height * 2) {
        mode = Qt::FastTransformation; // Nearest-neighbor for pixel visibility
      } else {
        mode = Qt::SmoothTransformation;
      }

      slot.label->setPixmap(slot.pixmap.scaled(targetWidth, targetHeight, 
                                               Qt::KeepAspectRatio, mode));
      slot.label->setFixedSize(targetWidth, targetHeight);
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
