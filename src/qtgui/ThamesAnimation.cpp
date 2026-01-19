#include "ThamesAnimation.h"
#include <QPainter>
#include <QResizeEvent>
#include <cmath>
#include <random>

ThamesAnimation::ThamesAnimation(QWidget *parent)
    : QWidget(parent), m_rng(std::random_device{}()) {
  
  // Animation timer - 60 FPS
  m_animTimer = new QTimer(this);
  connect(m_animTimer, &QTimer::timeout, this, &ThamesAnimation::updateAnimation);
  
  // Random movement trigger - ~10 seconds
  m_triggerTimer = new QTimer(this);
  connect(m_triggerTimer, &QTimer::timeout, this, &ThamesAnimation::triggerRandomMovement);
  
  initializeGrid();
  m_subtitles.start("Thames color screen", "1908–1910", "Invented by Clare Livingston Finlay");
}


ThamesAnimation::~ThamesAnimation() = default;

void ThamesAnimation::startAnimation() {
  m_animTimer->start(16); // ~60 FPS
  m_triggerTimer->start(10000); // 10 seconds
}

void ThamesAnimation::stopAnimation() {
  m_animTimer->stop();
  m_triggerTimer->stop();
}

void ThamesAnimation::initializeGrid() {
  m_balls.clear();
  
  int w = width();
  int h = height();
  
  if (w < 50 || h < 50) return; // Too small
  
  // Calculate grid dimensions
  int cols = static_cast<int>(w / BALL_SPACING);
  int rows = static_cast<int>(h / BALL_SPACING);
  
  // Center the grid
  double offsetX = (w - (cols - 1) * BALL_SPACING) / 2.0;
  double offsetY = (h - (rows - 1) * BALL_SPACING) / 2.0;
  
  double radius = BALL_SPACING * 0.45;
  
  // Create checkerboard pattern
  for (int row = 0; row < rows; ++row) {
    for (int col = 0; col < cols; ++col) {
      QPointF pos(offsetX + col * BALL_SPACING, offsetY + row * BALL_SPACING);
      
      // Checkerboard: red and green
      QColor color = ((row + col) % 2 == 0) ? QColor(220, 50, 50) : QColor(50, 180, 50);
      
      m_balls.append(Ball(pos, color, radius));
    }
  }
}

void ThamesAnimation::updateAnimation() {
  updatePhysics(0.016); // ~16ms per frame
  checkCollisions();
  m_subtitles.update(0.016);
  update(); // Trigger repaint
}

void ThamesAnimation::updatePhysics(double dt) {
  for (Ball &ball : m_balls) {
    // Update position
    ball.pos += ball.velocity * dt * 60.0; // Scale for visual speed
    
    // Apply friction
    ball.velocity *= FRICTION;
    
    // Stop very slow balls
    if (ball.velocity.x() * ball.velocity.x() + 
        ball.velocity.y() * ball.velocity.y() < MIN_VELOCITY * MIN_VELOCITY) {
      ball.velocity = QPointF(0, 0);
    }
    
    // Check wall collisions
    checkWallCollisions(ball);
  }
}

void ThamesAnimation::checkCollisions() {
  // Simple O(n²) collision detection
  for (int i = 0; i < m_balls.size(); ++i) {
    for (int j = i + 1; j < m_balls.size(); ++j) {
      checkBallCollisions(m_balls[i], m_balls[j]);
    }
  }
}

void ThamesAnimation::checkWallCollisions(Ball &ball) {
  double margin = ball.radius * 0.5; // Small margin for corner escape
  
  // Left/Right walls
  if (ball.pos.x() - ball.radius < 0) {
    ball.pos.setX(ball.radius);
    ball.velocity.setX(-ball.velocity.x() * 0.9); // Some energy loss
  } else if (ball.pos.x() + ball.radius > width()) {
    if (ball.pos.y() < margin || ball.pos.y() > height() - margin) {
      // Corner escape - remove ball
      ball.velocity = QPointF(0, 0);
      ball.pos.setX(width() + ball.radius * 2); // Move off screen
      return;
    }
    ball.pos.setX(width() - ball.radius);
    ball.velocity.setX(-ball.velocity.x() * 0.9);
  }
  
  // Top/Bottom walls
  if (ball.pos.y() - ball.radius < 0) {
    ball.pos.setY(ball.radius);
    ball.velocity.setY(-ball.velocity.y() * 0.9);
  } else if (ball.pos.y() + ball.radius > height()) {
    if (ball.pos.x() < margin || ball.pos.x() > width() - margin) {
      // Corner escape
      ball.velocity = QPointF(0, 0);
      ball.pos.setY(height() + ball.radius * 2);
      return;
    }
    ball.pos.setY(height() - ball.radius);
    ball.velocity.setY(-ball.velocity.y() * 0.9);
  }
}

void ThamesAnimation::checkBallCollisions(Ball &ball1, Ball &ball2) {
  QPointF delta = ball2.pos - ball1.pos;
  double dist = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
  double minDist = ball1.radius + ball2.radius;
  
  if (dist < minDist && dist > 0.001) {
    // Collision detected - elastic collision
    QPointF normal = delta / dist;
    
    // Separate balls
    double overlap = minDist - dist;
    ball1.pos -= normal * (overlap / 2.0);
    ball2.pos += normal * (overlap / 2.0);
    
    // Calculate relative velocity
    QPointF relVel = ball2.velocity - ball1.velocity;
    double velAlongNormal = relVel.x() * normal.x() + relVel.y() * normal.y();
    
    // Only resolve if balls are moving toward each other
    if (velAlongNormal < 0) {
      return;
    }
    
    // Apply impulse (assuming equal mass)
    QPointF impulse = normal * velAlongNormal;
    ball1.velocity += impulse * 0.9; // Some energy loss
    ball2.velocity -= impulse * 0.9;
  }
}

void ThamesAnimation::triggerRandomMovement() {
  if (m_balls.isEmpty()) return;
  
  std::uniform_int_distribution<> countDist(1, 3);
  std::uniform_int_distribution<> indexDist(0, m_balls.size() - 1);
  std::uniform_real_distribution<> velDist(-100.0, 100.0);
  
  int numBalls = countDist(m_rng);
  
  for (int i = 0; i < numBalls; ++i) {
    int index = indexDist(m_rng);
    Ball &ball = m_balls[index];
    
    // Apply random velocity
    ball.velocity.setX(ball.velocity.x() + velDist(m_rng));
    ball.velocity.setY(ball.velocity.y() + velDist(m_rng));
  }
}

void ThamesAnimation::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  
  // Background - darker blue
  p.fillRect(rect(), QColor(50, 70, 110));

  for (const auto &ball : m_balls) {
    p.setBrush(ball.color);
    p.setPen(Qt::NoPen);
    p.drawEllipse(ball.pos, ball.radius, ball.radius);
  }
  
  // Render subtitles
  m_subtitles.paint(&p, rect());
}

void ThamesAnimation::resizeEvent(QResizeEvent *event) {
  initializeGrid();
  QWidget::resizeEvent(event);
}
