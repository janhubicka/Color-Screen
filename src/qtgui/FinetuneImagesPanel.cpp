#include "FinetuneImagesPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QImage>
#include <QPixmap>

namespace {
// Helper widget that correctly propagates heightForWidth
class SlotWidget : public QWidget {
public:
    explicit SlotWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override {
        if (layout()) {
            return layout()->heightForWidth(w);
        }
        return QWidget::heightForWidth(w);
    }
};

// Helper layout that ensures heightForWidth propagation
class VerticalSlotLayout : public QVBoxLayout {
public:
    using QVBoxLayout::QVBoxLayout;
    int heightForWidth(int w) const override {
        // QVBoxLayout implementation of heightForWidth usually sums up its children
        // but it needs to be explicitly enabled/overridden to work with our SlotWidget
        if (count() == 0) return 0;
        int h = spacing() * (count() - 1) + contentsMargins().top() + contentsMargins().bottom();
        for (int i = 0; i < count(); ++i) {
            QLayoutItem *item = itemAt(i);
            if (item->widget()) {
                if (item->widget()->hasHeightForWidth())
                    h += item->widget()->heightForWidth(w);
                else
                    h += item->widget()->sizeHint().height();
            } else if (item->layout()) {
                if (item->layout()->hasHeightForWidth())
                    h += item->layout()->heightForWidth(w);
                else
                    h += item->layout()->sizeHint().height();
            } else {
                h += item->sizeHint().height();
            }
        }
        return h;
    }
};
}

FinetuneImagesPanel::FinetuneImagesPanel(QWidget *parent)
    : QWidget(parent) {
  setupUi();
}

void FinetuneImagesPanel::setupUi() {
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(5);

  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

  m_row1Layout = new QHBoxLayout();
  m_row1Layout->setContentsMargins(0, 0, 0, 0);
  m_row1Layout->setSpacing(5);
  
  auto createSlot = [this](const QString& caption, std::vector<ImageSlot>& row, QHBoxLayout* rowLayout) {
    ImageSlot slot;
    slot.caption = caption;
    slot.label = new ScalableImageLabel();
    slot.label->setAlignment(Qt::AlignCenter);
    
    // Create height-for-width aware container
    QWidget *container = new SlotWidget();
    VerticalSlotLayout *vLayout = new VerticalSlotLayout(container);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->setSpacing(2);
    
    // Image label (the height-for-width provider)
    vLayout->addWidget(slot.label, 0);
    
    slot.captionLabel = new QLabel(slot.caption);
    slot.captionLabel->setAlignment(Qt::AlignCenter);
    vLayout->addWidget(slot.captionLabel, 0, Qt::AlignCenter);
    
    rowLayout->addWidget(container, 1);
    
    // We can't keep a pointer to container since we removed it from ImageSlot struct
    // Wait, I should add it back or just use the label's parent.
    // I removed it from 'FinetuneImagesPanel.h' but I can just use slot.label->parentWidget()!
    row.push_back(slot);
  };

  createSlot("Original", m_row1Images, m_row1Layout);
  createSlot("Sharpened", m_row1Images, m_row1Layout);
  createSlot("Simulated", m_row1Images, m_row1Layout);
  createSlot("Diff", m_row1Images, m_row1Layout);
  
  mainLayout->addLayout(m_row1Layout, 0);

  m_row2Layout = new QHBoxLayout();
  m_row2Layout->setContentsMargins(0, 0, 0, 0);
  m_row2Layout->setSpacing(5);

  createSlot("Screen", m_row2Images, m_row2Layout);
  createSlot("Blured Screen", m_row2Images, m_row2Layout);
  createSlot("Emulsion", m_row2Images, m_row2Layout);
  createSlot("Merged", m_row2Images, m_row2Layout);
  createSlot("Collected", m_row2Images, m_row2Layout);
  createSlot("Dot Spread", m_row2Images, m_row2Layout);
  
  mainLayout->addLayout(m_row2Layout, 0);
  
  // Stretch at the end absorbs EXTRA space if the parent is very tall
  mainLayout->addStretch(1);

  // Tell the layout to respect size hints and height-for-width correctly
  mainLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
}

bool FinetuneImagesPanel::hasHeightForWidth() const {
  return true;
}

int FinetuneImagesPanel::heightForWidth(int w) const {
  if (layout()) {
    return layout()->heightForWidth(w);
  }
  return QWidget::heightForWidth(w);
}

void FinetuneImagesPanel::setFinetuneResult(const colorscreen::finetune_result& result) {
  if (m_row1Images.size() < 4 || m_row2Images.size() < 6) return;

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
    if (slot.label->parentWidget()) slot.label->parentWidget()->hide();
  }
  for (auto& slot : m_row2Images) {
    slot.pixmap = QPixmap();
    slot.label->setPixmap(QPixmap());
    if (slot.label->parentWidget()) slot.label->parentWidget()->hide();
  }
}

void FinetuneImagesPanel::updateImageDisplay() {
  auto updateRow = [&](std::vector<ImageSlot>& row) {
    for (auto& slot : row) {
      QWidget* container = slot.label->parentWidget();
      if (slot.pixmap.isNull() || slot.pixmap.width() <= 0 || slot.pixmap.height() <= 0) {
        if (container) container->hide();
        slot.label->setPixmap(QPixmap());
      } else {
        if (container) container->show();
        slot.label->setPixmap(slot.pixmap);
      }
    }
  };

  updateRow(m_row1Images);
  updateRow(m_row2Images);
}

QPixmap FinetuneImagesPanel::convertSimpleImageToQPixmap(const colorscreen::simple_image* img) {
  if (!img || img->width <= 0 || img->height <= 0 || img->stride <= 0) {
    return QPixmap();
  }
  if (img->width > 100000 || img->height > 100000) {
      return QPixmap();
  }
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
