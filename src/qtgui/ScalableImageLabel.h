#ifndef SCALABLE_IMAGE_LABEL_H
#define SCALABLE_IMAGE_LABEL_H

#include <QWidget>
#include <QPixmap>

class ScalableImageLabel : public QWidget {
  Q_OBJECT
public:
  explicit ScalableImageLabel(QWidget *parent = nullptr);
  ~ScalableImageLabel() override = default;

  void setPixmap(const QPixmap &pixmap);
  const QPixmap &pixmap() const { return m_pixmap; }

  void setAlignment(Qt::Alignment alignment) { m_alignment = alignment; update(); }
  Qt::Alignment alignment() const { return m_alignment; }

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;
  int heightForWidth(int width) const override;
  bool hasHeightForWidth() const override { return true; }

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  QPixmap m_pixmap;
  Qt::Alignment m_alignment = Qt::AlignCenter;
};

#endif // SCALABLE_IMAGE_LABEL_H
