#ifndef FINETUNE_IMAGES_PANEL_H
#define FINETUNE_IMAGES_PANEL_H

#include <QWidget>
#include <QLabel>
#include <vector>
#include "../libcolorscreen/include/finetune.h"

class QVBoxLayout;
class QHBoxLayout;

class FinetuneImagesPanel : public QWidget {
  Q_OBJECT
public:
  explicit FinetuneImagesPanel(QWidget *parent = nullptr);
  ~FinetuneImagesPanel() override = default;

  void setFinetuneResult(const colorscreen::finetune_result& result);
  void clear();

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  void setupUi();
  void updateImageDisplay();
  QPixmap convertSimpleImageToQPixmap(const colorscreen::simple_image* img);

  // Two rows of images
  QHBoxLayout *m_row1Layout = nullptr;
  QHBoxLayout *m_row2Layout = nullptr;
  
  struct ImageSlot {
    QWidget *container = nullptr;
    QLabel *label = nullptr;
    QLabel *captionLabel = nullptr;
    QString caption;
    QPixmap pixmap;
  };
  
  std::vector<ImageSlot> m_row1Images; // original, sharpened, simulated, diff
  std::vector<ImageSlot> m_row2Images; // screen, blured_screen, emulsion_screen, merged_screen, collected_screen, dot_spread
  
  int m_currentWidth = 0;
};

#endif // FINETUNE_IMAGES_PANEL_H
