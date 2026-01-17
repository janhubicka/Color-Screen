#pragma once

#include <QWidget>
#include <QTimer>
#include <QVector>
#include <QPointF>
#include <QColor>
#include <random>

struct Particle {
  QPointF pos;
  QPointF velocity;
  QColor color;
  double life = 1.0; // 0.0 to 1.0
  double size = 3.0;
  
  Particle(QPointF p, QPointF v, QColor c, double s = 3.0)
      : pos(p), velocity(v), color(c), size(s) {}
};

struct Firework {
  QVector<Particle> particles;
  bool exploded = false;
  double launchTime = 0.0;
  QPointF launchPos;
  QPointF targetPos;
  
  Firework(QPointF launch, QPointF target)
      : launchPos(launch), targetPos(target) {}
};

class PagetAnimation : public QWidget {
  Q_OBJECT
public:
  explicit PagetAnimation(QWidget *parent = nullptr);
  ~PagetAnimation() override;

  void startAnimation();
  void stopAnimation();

protected:
  void paintEvent(QPaintEvent *event) override;

private slots:
  void updateAnimation();
  void launchFirework();

private:
  void explodeFirework(Firework &fw);
  void updateParticles();
  
  QTimer *m_animTimer;
  QTimer *m_launchTimer;
  QVector<Firework> m_fireworks;
  
  std::mt19937 m_rng;
  double m_time = 0.0;
  
  static constexpr double GRAVITY = 200.0;
  static constexpr double LAUNCH_SPEED = 600.0;  // Increased for higher flights
  static constexpr int PARTICLES_PER_CLUSTER = 4; // 2x2 Bayer pattern
};
