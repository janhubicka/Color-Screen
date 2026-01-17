#pragma once

#include <QWidget>
#include <QTimer>
#include <QVector>
#include <QPointF>
#include <QColor>
#include <random>

struct Ball {
  QPointF pos;
  QPointF velocity;
  QColor color;
  double radius = 10.0;
  
  Ball(QPointF p, QColor c, double r = 10.0)
      : pos(p), velocity(0, 0), color(c), radius(r) {}
};

class ThamesAnimation : public QWidget {
  Q_OBJECT
public:
  explicit ThamesAnimation(QWidget *parent = nullptr);
  ~ThamesAnimation() override;

  void startAnimation();
  void stopAnimation();

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private slots:
  void updateAnimation();
  void triggerRandomMovement();

private:
  void initializeGrid();
  void updatePhysics(double dt);
  void checkCollisions();
  void checkWallCollisions(Ball &ball);
  void checkBallCollisions(Ball &ball1, Ball &ball2);

  QTimer *m_animTimer;
  QTimer *m_triggerTimer;
  QVector<Ball> m_balls;
  
  std::mt19937 m_rng;
  
  static constexpr double BALL_SPACING = 30.0;
  static constexpr double FRICTION = 0.995; // Slight energy loss
  static constexpr double MIN_VELOCITY = 0.1;
};
