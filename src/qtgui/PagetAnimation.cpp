#include "PagetAnimation.h"
#include <QPainter>
#include <cmath>

PagetAnimation::PagetAnimation(QWidget *parent)
    : QWidget(parent), m_rng(std::random_device{}()) {
  
  // Animation timer - 60 FPS
  m_animTimer = new QTimer(this);
  connect(m_animTimer, &QTimer::timeout, this, &PagetAnimation::updateAnimation);
  
  // Firework launch timer - every 4 seconds
  m_launchTimer = new QTimer(this);
  connect(m_launchTimer, &QTimer::timeout, this, &PagetAnimation::launchFirework);
}

PagetAnimation::~PagetAnimation() = default;

void PagetAnimation::startAnimation() {
  m_time = 0.0;
  m_fireworks.clear();
  m_animTimer->start(16); // ~60 FPS
  m_launchTimer->start(4000); // 4 seconds
}

void PagetAnimation::stopAnimation() {
  m_animTimer->stop();
  m_launchTimer->stop();
  m_fireworks.clear();
}

void PagetAnimation::launchFirework() {
  if (width() < 100 || height() < 100) return;
  
  std::uniform_real_distribution<> xDist(width() * 0.2, width() * 0.8);
  std::uniform_real_distribution<> yDist(height() * 0.1, height() * 0.85); // Wide height range - low to very high
  std::uniform_real_distribution<> angleDist(-15.0, 15.0); // Random launch angle in degrees
  
  QPointF launchPos(xDist(m_rng), height()); // Bottom of screen
  QPointF targetPos(xDist(m_rng), yDist(m_rng)); // Random height
  double launchAngle = angleDist(m_rng) * M_PI / 180.0; // Convert to radians
  
  Firework fw(launchPos, targetPos);
  fw.launchTime = m_time;
  
  // Bayer pattern repeats: GRBG pattern, rotated 45 degrees
  // With swapped colors: Blue-Red-Green pattern
  QColor colors[2][2] = {
    {QColor(50, 100, 220), QColor(220, 50, 50)},   // Blue, Red (was Green, Red)
    {QColor(50, 200, 50), QColor(50, 100, 220)}    // Green, Blue (was Blue, Green)
  };
  
  double spacing = 8.0; // Tight spacing during launch
  double size = 7.0;    // Square size
  double rotation = 45.0 * M_PI / 180.0; // 45 degree rotation
  
  for (int row = 0; row < 5; ++row) {
    for (int col = 0; col < 5; ++col) {
      double offsetX = (col - 2.0) * spacing;  // Center around 0
      double offsetY = (row - 2.0) * spacing;
      
      // Rotate by 45 degrees
      double rotatedX = offsetX * std::cos(rotation) - offsetY * std::sin(rotation);
      double rotatedY = offsetX * std::sin(rotation) + offsetY * std::cos(rotation);
      
      QPointF pos = launchPos + QPointF(rotatedX, rotatedY);
      
      // Bayer pattern repeats every 2×2
      QColor color = colors[row % 2][col % 2];
      
      // Launch velocity with angle
      double vx = std::sin(launchAngle) * LAUNCH_SPEED;
      double vy = -std::cos(launchAngle) * LAUNCH_SPEED;
      
      Particle p(pos, QPointF(vx, vy), color, size);
      fw.particles.append(p);
    }
  }
  
  m_fireworks.append(fw);
}

void PagetAnimation::explodeFirework(Firework &fw) {
  fw.exploded = true;
  
  // Calculate center from average position of launch particles
  QPointF center(0, 0);
  for (const Particle &p : fw.particles) {
    center += p.pos;
  }
  center /= fw.particles.size();
  
  // Keep the original 25 particles and give them outward velocities
  std::uniform_real_distribution<> fuzzDist(-30.0, 30.0); // Random fuzz
  
  for (Particle &p : fw.particles) {
    // Calculate direction from center to particle
    QPointF dir = p.pos - center;
    double dist = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
    
    if (dist > 0.1) {
      dir /= dist; // Normalize
    } else {
      dir = QPointF(1, 0); // Default direction if particle is at center
    }
    
    // Base outward speed with random fuzz
    double speed = 100.0 + fuzzDist(m_rng);
    
    // Set velocity: outward from center with fuzz
    p.velocity = QPointF(
      dir.x() * speed + fuzzDist(m_rng) * 0.5,
      dir.y() * speed + fuzzDist(m_rng) * 0.5
    );
  }
}

void PagetAnimation::updateAnimation() {
  m_time += 0.016; // 16ms
  
  // Update all fireworks
  for (Firework &fw : m_fireworks) {
    if (!fw.exploded) {
      // Update ALL launch particles together
      if (!fw.particles.isEmpty()) {
        // Update all particles with same physics
        for (Particle &p : fw.particles) {
          p.velocity.setY(p.velocity.y() + GRAVITY * 0.016);
          p.pos += p.velocity * 0.016;
        }
        
        // Check first particle to see if we should explode
        // Wait until rocket has fallen ~50 pixels past its peak before exploding
        Particle &first = fw.particles.first();
        if (first.velocity.y() > 0 && first.pos.y() > fw.targetPos.y() + 50) {
          explodeFirework(fw);
        }
      }
    } else {
      // Update explosion particles
      for (Particle &p : fw.particles) {
        p.velocity.setY(p.velocity.y() + GRAVITY * 0.016);
        p.pos += p.velocity * 0.016;
        p.life -= 0.008; // Fade out
      }
    }
  }
  
  // Remove dead fireworks
  m_fireworks.erase(
    std::remove_if(m_fireworks.begin(), m_fireworks.end(),
      [](const Firework &fw) {
        if (!fw.exploded) return false;
        for (const Particle &p : fw.particles) {
          if (p.life > 0.0) return false;
        }
        return true;
      }),
    m_fireworks.end()
  );
  
  update(); // Trigger repaint
}

void PagetAnimation::paintEvent(QPaintEvent *event) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  
  // Background - pure black
  painter.fillRect(rect(), QColor(0, 0, 0));
  
  // Draw all particles as ROTATED SQUARES (45 degrees)
  for (const Firework &fw : m_fireworks) {
    for (const Particle &p : fw.particles) {
      if (p.life <= 0.0) continue;
      
      // Fade color with life
      QColor color = p.color;
      color.setAlphaF(p.life);
      
      painter.setBrush(color);
      painter.setPen(Qt::NoPen);
      
      double size = p.size * (0.5 + p.life * 0.5);
      
      // Save painter state
      painter.save();
      
      // Translate to particle position, rotate, then draw centered square
      painter.translate(p.pos.x(), p.pos.y());
      painter.rotate(45.0); // Rotate 45 degrees
      
      // Draw square centered at origin
      painter.drawRect(QRectF(-size/2, -size/2, size, size));
      
      // Restore painter state
      painter.restore();
    }
  }
}
