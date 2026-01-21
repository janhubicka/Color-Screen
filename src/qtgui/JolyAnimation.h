#pragma once

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
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
  QElapsedTimer m_elapsedTimer;
  double m_physicsAccumulator;
  double m_time;
  double m_bounciness; // 0.0 to 1.0, increases over time for dramatic physics
  
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
  
  struct Dolphin {
      bool active;
      double x, y; // Absolute coordinates
      double startX, startY; // Jump start point
      double vx, vy; // Velocity
      int stripIndex; // Associated wave strip
      double size;
      double angle; // Rotation angle
  };

  struct Whale {
      bool active;
      double x, y;
      int stripIndex;
      double timer; // Animation timer
      bool isBreaching; // True = breach, False = spout
      double angle;     // Tilt angle for wave interaction
      double size;
  };

  SubtitleOverlay m_subtitles;

  StripState m_strips[STRIP_COUNT];
  std::vector<BoatState> m_boats;
  std::vector<Cannonball> m_cannonballs;
  std::vector<OrangeState> m_oranges;
  std::vector<BottleState> m_bottles;
  std::vector<Dolphin> m_dolphins;
  std::vector<Whale> m_whales;
  ParrotState m_parrot;
  
  void stepAnimation(double dt);
  
  void updateWaveDynamics();
  void spawnBoat();
  void spawnParrot();
  void spawnBottle();
  void spawnDolphin();
  void spawnWhale();
  
  void drawBoat(QPainter &p, const BoatState &boat, double yBase, double amplitude, double frequency, double phase);
  void drawParrot(QPainter &p);
  void drawCannonball(QPainter &p, const Cannonball &cb);
  void drawOrange(QPainter &p, const OrangeState &orange, double yBase, double amplitude, double frequency, double phase);
  void drawBottle(QPainter &p, const BottleState &bottle, double yBase, double amplitude, double frequency, double phase);
  void drawDolphin(QPainter &p, const Dolphin &dolphin);
  void drawWhale(QPainter &p, const Whale &whale, double yBase, double phase);
};
