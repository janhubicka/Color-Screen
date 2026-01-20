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
      double y;   // Absolute Y position
      double vy;  // Vertical velocity
      int stripIndex;
      double speed; // pixels per second
      double tilt;
      int shapeType; // 0, 1, or 2 for different boat shapes
      bool sinking;
      double sinkProgress; // 0.0 to 1.0
      bool isPirate;
      double fireCooldown;
  };
  
  struct ParrotState {
      bool active;
      double x, y;
      double speedX, speedY;
      double wingPhase;
      bool movingRight;
      int stripIndex;    // Depth plane
      bool hasOrange;    // Carrying payload
      double dropTimer;  // When to drop
  };
  
  struct OrangeState {
      bool active;
      double x, y;
      double vx, vy;
      int stripIndex;
      bool onWater;
      double bobPhase;
      double floatTime; // Time spent on water
  };
  
  struct BottleState {
      bool active;
      double x, y; 
      double vy; // Vertical velocity
      double speed;
      int stripIndex;
      double tilt;
      double phase;
  };

  struct Cannonball {
      bool active;
      double x, y;
      double vx, vy;
  };

  SubtitleOverlay m_subtitles;

  StripState m_strips[STRIP_COUNT];
  std::vector<BoatState> m_boats;
  std::vector<Cannonball> m_cannonballs;
  std::vector<OrangeState> m_oranges;
  std::vector<BottleState> m_bottles;
  ParrotState m_parrot;
  
  void updateWaveDynamics();
  void spawnBoat();
  void spawnParrot();
  void spawnBottle();
  void drawBoat(QPainter &p, const BoatState &boat, double yBase, double amplitude, double frequency, double phase);
  void drawParrot(QPainter &p);
  void drawCannonball(QPainter &p, const Cannonball &cb);
  void drawOrange(QPainter &p, const OrangeState &orange, double yBase, double amplitude, double frequency, double phase);
  void drawBottle(QPainter &p, const BottleState &bottle, double yBase, double amplitude, double frequency, double phase);
};
