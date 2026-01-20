#pragma once

#include <QWidget>
#include <QTimer>
#include "SubtitleOverlay.h"

class JolyAnimation : public QWidget {
  Q_OBJECT
public:
  explicit JolyAnimation(QWidget *parent = nullptr);
  ~JolyAnimation() override;

  void startAnimation();
  void stopAnimation();

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private slots:
  void updateAnimation();

private:
  QTimer *m_animTimer;
  double m_time;
  
  static constexpr int STRIP_COUNT = 30;
  
  struct StripState {
      double currentSpeed;
      double targetSpeed;
      double currentAmpFactor;
      double targetAmpFactor;
      double phase;
  };
  
  struct BoatState {
      bool active;
      double x;
      int stripIndex;
      double speed; // pixels per second
      double tilt;
      int shapeType; // 0, 1, or 2 for different boat shapes
      bool sinking;
      double sinkProgress; // 0.0 to 1.0
  };

  SubtitleOverlay m_subtitles;

  StripState m_strips[STRIP_COUNT];
  std::vector<BoatState> m_boats;
  
  void updateWaveDynamics();
  void spawnBoat();
  void drawBoat(QPainter &p, const BoatState &boat, double yBase, double amplitude, double frequency, double phase);
};
