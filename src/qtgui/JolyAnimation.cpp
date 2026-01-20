#include "JolyAnimation.h"
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QtMath>
#include <QRandomGenerator>

JolyAnimation::JolyAnimation(QWidget *parent)
    : QWidget(parent), m_time(0.0) {
  
  // Animation timer - 60 FPS
  m_animTimer = new QTimer(this);
  connect(m_animTimer, &QTimer::timeout, this, &JolyAnimation::updateAnimation);
  
  m_subtitles.start("Joly Colour Screen", "1896", "Patented Prof. John Joly, Dublin in 1894 used regular red, green and blue-violet lines");
  
  // Initialize strips
  for (int i = 0; i < STRIP_COUNT; ++i) {
      m_strips[i].phase = i * 0.5 + (i * i * 0.05);
      m_strips[i].currentSpeed = 3.0 * (0.8 + 0.4 * qSin(i * 0.32));
      m_strips[i].targetSpeed = m_strips[i].currentSpeed;
      m_strips[i].currentAmpFactor = 1.0;
      m_strips[i].targetAmpFactor = 1.0;
  }
  
  // Initialize boat pool
  m_boats.reserve(10);
  
  m_parrot.active = false;
}

JolyAnimation::~JolyAnimation() = default;

void JolyAnimation::startAnimation() {
  m_animTimer->start(16); // ~60 FPS
}

void JolyAnimation::stopAnimation() {
  m_animTimer->stop();
}

void JolyAnimation::updateWaveDynamics() {
    // Randomly update targets
    if (QRandomGenerator::global()->bounded(100) < 2) { 
        int idx = QRandomGenerator::global()->bounded(STRIP_COUNT);
        m_strips[idx].targetSpeed = 3.0 * (0.5 + QRandomGenerator::global()->generateDouble());
        m_strips[idx].targetAmpFactor = 0.8 + 0.4 * QRandomGenerator::global()->generateDouble();
    }
    
    // VERY Smooth interpolation 
    for (int i = 0; i < STRIP_COUNT; ++i) {
        m_strips[i].currentSpeed += (m_strips[i].targetSpeed - m_strips[i].currentSpeed) * 0.002;
        m_strips[i].currentAmpFactor += (m_strips[i].targetAmpFactor - m_strips[i].currentAmpFactor) * 0.002;
    }
}

void JolyAnimation::spawnBoat() {
    int activeCount = 0;
    for (const auto &b : m_boats) {
        if (b.active) activeCount++;
    }
    
    if (activeCount >= 10) return; 
    
    // Increased spawn rate
    if (m_time > 30.0 && QRandomGenerator::global()->bounded(100) < 2) { 
        BoatState newBoat;
        newBoat.active = true;
        newBoat.sinking = false;
        newBoat.sinkProgress = 0.0;
        newBoat.isPirate = false;
        newBoat.fireCooldown = 0.0;
        newBoat.tilt = 0;
        
        // 1 in 10 chance to be a pirate ship
        if (QRandomGenerator::global()->bounded(10) == 0) {
            newBoat.isPirate = true;
            newBoat.shapeType = 2; // Pirate ships usually look like Tall Ships
            
            // Pirate Logic: Hunt an existing boat!
            // Find a target to chase
            int targetIdx = -1;
            for (int i = 0; i < (int)m_boats.size(); ++i) {
                if (m_boats[i].active && !m_boats[i].sinking && !m_boats[i].isPirate) {
                    targetIdx = i;
                    // Prefer finding one that is not too far along? 
                    // Or just random valid target. Random is fine.
                    if (QRandomGenerator::global()->bounded(2) == 0) break;
                }
            }
            
            if (targetIdx != -1) {
                // Spawn on SAME strip to chase
                const auto &target = m_boats[targetIdx];
                newBoat.stripIndex = target.stripIndex;
                
                // Chase speed: faster than target
                bool movingRight = target.speed > 0;
                double speedMag = qAbs(target.speed) * 1.5; // 50% faster
                
                if (movingRight) {
                    newBoat.speed = speedMag;
                    newBoat.x = -150; // Spawn behind (left)
                } else {
                    newBoat.speed = -speedMag;
                    newBoat.x = width() + 150; // Spawn behind (right)
                }
            } else {
                // No prey? Random patrol
                newBoat.stripIndex = 5 + QRandomGenerator::global()->bounded(STRIP_COUNT - 5);
                bool leftToRight = QRandomGenerator::global()->bounded(2) == 0;
                newBoat.speed = (leftToRight ? 1 : -1) * (120 + QRandomGenerator::global()->bounded(60)); // Fast patrol
                newBoat.x = leftToRight ? -150 : width() + 150;
            }
            
        } else {
            // Normal Boat Spawn
            newBoat.stripIndex = 5 + QRandomGenerator::global()->bounded(STRIP_COUNT - 5);
            newBoat.shapeType = QRandomGenerator::global()->bounded(3);
            
            bool leftToRight = QRandomGenerator::global()->bounded(2) == 0;
            if (leftToRight) {
                newBoat.x = -150;
                newBoat.speed = 80 + QRandomGenerator::global()->bounded(60);
            } else {
                newBoat.x = width() + 150;
                newBoat.speed = -(80 + QRandomGenerator::global()->bounded(60));
            }
        }
        
        bool placed = false;
        for (auto &b : m_boats) {
            if (!b.active) {
                b = newBoat;
                placed = true;
                break;
            }
        }
        if (!placed) {
            m_boats.push_back(newBoat);
        }
    }
}

void JolyAnimation::spawnParrot() {
    if (m_parrot.active) return;
    
    // Random chance to spawn parrot
    if (m_time > 15.0 && QRandomGenerator::global()->bounded(800) < 1) {
        m_parrot.active = true;
        m_parrot.wingPhase = 0;
        m_parrot.y = 50 + QRandomGenerator::global()->bounded(height() / 2); 
        
        m_parrot.movingRight = QRandomGenerator::global()->bounded(2) == 0;
        if (m_parrot.movingRight) {
            m_parrot.x = -100;
            m_parrot.speedX = 150 + QRandomGenerator::global()->bounded(100);
        } else {
            m_parrot.x = width() + 100;
            m_parrot.speedX = -(150 + QRandomGenerator::global()->bounded(100));
        }
        m_parrot.speedY = (QRandomGenerator::global()->generateDouble() - 0.5) * 50; 
    }
}

void JolyAnimation::updateAnimation() {
  double dt = 0.016;
  m_time += dt;
  m_subtitles.update(dt);
  
  updateWaveDynamics();
  spawnBoat();
  spawnParrot();
  
  // First pass: Move boats and pirate logic
  for (int i = 0; i < (int)m_boats.size(); ++i) {
      BoatState &b = m_boats[i];
      if (!b.active) continue;

      if (b.sinking) {
          b.sinkProgress += dt * 0.15; 
          b.x += b.speed * dt * 0.3; 
          if (b.sinkProgress > 1.2) b.active = false;
      } else {
          b.x += b.speed * dt;
          if (b.x < -200 || b.x > width() + 200) b.active = false;

          // Pirate Logic
          if (b.isPirate) {
              b.fireCooldown -= dt;
              if (b.fireCooldown <= 0) {
                  // Find target
                  int targetIdx = -1;
                  double minDist = 300.0; // Range
                  
                  for (int j = 0; j < (int)m_boats.size(); ++j) {
                      if (i == j) continue;
                      if (!m_boats[j].active || m_boats[j].sinking || m_boats[j].isPirate) continue;
                      
                      double dist = qAbs(b.x - m_boats[j].x) + qAbs(b.stripIndex - m_boats[j].stripIndex) * 20;
                      if (dist < minDist) {
                          minDist = dist;
                          targetIdx = j;
                      }
                  }
                  
                  if (targetIdx != -1) {
                      // Fire!
                      m_boats[targetIdx].sinking = true;
                      b.fireCooldown = 2.0; // Reload time
                      
                      // Optional: We could store a "muzzle flash" timer to draw it
                  }
              }
          } else {
              // Normal boat physics capsize chance
              const auto &strip = m_strips[b.stripIndex];
              double riskFactor = 0.0;
              if (strip.currentAmpFactor > 0.9) {
                  double diff = strip.currentAmpFactor - 0.9;
                  riskFactor = diff * diff * 0.2;
              }
              double speedMult = strip.currentSpeed / 3.0;
              double prob = (riskFactor * speedMult) / 3.0;
              
              if (QRandomGenerator::global()->generateDouble() < prob) {
                  b.sinking = true;
              }
          }
      }
  }
  
  // Update parrot
  if (m_parrot.active) {
      m_parrot.x += m_parrot.speedX * dt;
      m_parrot.y += m_parrot.speedY * dt;
      m_parrot.wingPhase += 15.0 * dt; // Flapping speed
      
      if (m_parrot.x < -200 || m_parrot.x > width() + 200) {
          m_parrot.active = false;
      }
  }
  
  update(); // Trigger repaint
}

void JolyAnimation::drawBoat(QPainter &p, const BoatState &boat, double yBase, double amplitude, double frequency, double phase) {
    // Calculate y position on the wave
    double angle = boat.x * frequency + m_time * m_strips[boat.stripIndex].currentSpeed + phase;
    double waveY = amplitude * qSin(angle);
    double y = yBase + waveY;
    
    // Calculate slope for tilt
    double slope = amplitude * frequency * qCos(angle);
    double tiltAngle = qRadiansToDegrees(qAtan(slope));
    
    // Add chaos
    tiltAngle += 10.0 * qSin(m_time * 3.0 + boat.x * 0.01);
    
    // Sinking effects
    double sinkY = 0;
    if (boat.sinking) {
        double targetTilt = (boat.speed > 0) ? 180 : -180;
        double tiltProgress = qMin(1.0, boat.sinkProgress * 2.0);
        tiltAngle = tiltAngle * (1.0 - tiltProgress) + targetTilt * tiltProgress;
        sinkY = boat.sinkProgress * 80.0;
    }
    
    p.save();
    p.translate(boat.x, y + sinkY);
    
    // Perspective scaling
    double scale = 0.4 + 1.2 * ((double)boat.stripIndex / STRIP_COUNT);
    p.scale(scale, scale);
    
    p.rotate(tiltAngle);
    
    // If moving left, flip horizontally
    if (boat.speed < 0) {
        p.scale(-1, 1);
    }
    
    QPainterPath hull;
    QPainterPath sail;
    
    switch (boat.shapeType) {
        case 0: // Classic Paper Boat
            hull.moveTo(-15, 0);
            hull.lineTo(15, 0);
            hull.lineTo(10, 8);
            hull.lineTo(-10, 8);
            hull.closeSubpath();
            
            sail.moveTo(0, 0);
            sail.lineTo(0, -20);
            sail.lineTo(12, -5);
            sail.closeSubpath();
            break;
            
        case 1: // Flat Skiff / Canoe
            hull.moveTo(-18, 0);
            hull.cubicTo(-10, 6, 10, 6, 18, 0);
            hull.lineTo(15, 5);
            hull.lineTo(-15, 5);
            hull.closeSubpath();
            
            // Small mast
            sail.moveTo(-5, 0);
            sail.lineTo(-5, -15);
            sail.lineTo(5, -15);
            sail.lineTo(5, 0);
            sail.closeSubpath();
            break;
            
        case 2: // Tall Ship Profile (simplified)
            hull.moveTo(-12, 0);
            hull.lineTo(14, 0);
            hull.lineTo(10, 10);
            hull.lineTo(-10, 10);
            hull.closeSubpath();
            
            // Multiple sails
            sail.moveTo(0, 0);
            sail.lineTo(0, -25); 
            sail.moveTo(0, -5);
            sail.lineTo(10, -5);
            sail.lineTo(0, -20);
            
            sail.moveTo(-8, 0); 
            sail.lineTo(-8, -18);
            sail.lineTo(-2, -10);
            sail.lineTo(-8, -5);
            break;
    }
    
    if (boat.isPirate) {
        p.setBrush(Qt::black);
        p.setPen(QPen(Qt::white, 1)); // White outlines for visibility
    } else if (boat.sinking) {
       int gray = 255 - (int)(qMin(1.0, boat.sinkProgress) * 100);
       p.setBrush(QColor(gray, gray, gray));
       p.setPen(QPen(Qt::gray, 1));
    } else {
       p.setBrush(Qt::white);
       p.setPen(QPen(Qt::gray, 1));
    }
    
    p.drawPath(hull);
    p.drawPath(sail);
    
    p.restore();
}

void JolyAnimation::drawParrot(QPainter &p) {
    if (!m_parrot.active) return;
    
    p.save();
    p.translate(m_parrot.x, m_parrot.y);
    
    if (!m_parrot.movingRight) {
        p.scale(-1, 1);
    }
    
    // Scale down a bit
    p.scale(0.8, 0.8);
    
    // Bright green body
    QColor bodyColor(0, 255, 0); // Bright green
    QColor wingColor(50, 205, 50); // Lime green
    
    // Black outline
    QPen outlinePen(Qt::black, 2);
    p.setPen(outlinePen);
    
    QPainterPath body;
    // Head and body
    body.moveTo(0, 0);
    body.cubicTo(10, -10, 20, -5, 25, 0); 
    body.cubicTo(30, 10, 10, 30, -10, 40); 
    body.lineTo(-20, 50); 
    body.lineTo(-15, 40);
    body.cubicTo(-25, 20, -10, 10, 0, 0); 
    
    p.setBrush(bodyColor);
    p.drawPath(body);
    
    // Red Beak
    QPainterPath beak;
    beak.moveTo(22, 2);
    beak.cubicTo(28, 2, 30, 8, 28, 12);
    beak.lineTo(22, 8);
    beak.closeSubpath();
    p.setBrush(Qt::red);
    p.drawPath(beak);
    
    // Eye
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawEllipse(20, -2, 4, 4);
    p.setBrush(Qt::black);
    p.drawEllipse(21, -1, 2, 2);
    p.setPen(outlinePen);
    
    // Wings
    double flap = qSin(m_parrot.wingPhase);
    QPainterPath wing;
    wing.moveTo(5, 10);
    double tipY = 20 + flap * 20;
    wing.quadTo(-10, tipY - 10, -20, tipY);
    wing.quadTo(0, tipY - 5, 15, 15);
    
    p.setBrush(wingColor);
    p.drawPath(wing);
    
    p.restore();
}

void JolyAnimation::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  
  // Background - dark
  p.fillRect(rect(), Qt::black);

  int w = width();
  int h = height();
  
  double stripHeight = static_cast<double>(h) / STRIP_COUNT;
  
  double frequency = 0.02; 
  double delay = 20.0;
  
  double globalAmp = stripHeight * 0.8; 
  double rampFactor = 1.0;
  if (m_time < delay) {
     rampFactor = 0.0;
  } else if (m_time < delay + 2.0) {
     rampFactor = (m_time - delay) * 0.5;
  }

  for (int i = 0; i < STRIP_COUNT; ++i) {
      // Determine color
      QColor color;
      switch (i % 3) {
          case 0: color = QColor(220, 50, 50); break; // Red
          case 1: color = QColor(50, 200, 50); break; // Green
          case 2: color = QColor(50, 50, 220); break; // Blue
      }

      double perspectiveFactor = (double)(i + 1) / STRIP_COUNT;
      double stripAmp = globalAmp * rampFactor * perspectiveFactor * m_strips[i].currentAmpFactor;
      
      double phase = m_strips[i].phase;
      double stripFreq = frequency * (0.9 + 0.2 * qCos(i * 0.5));
      double stripSpeed = m_strips[i].currentSpeed;

      QPolygonF stripPoly;
      
      double baseX = 0;
      double baseY = i * stripHeight;
      
      // Top Edge
      for (int x = 0; x <= w; x += 5) {
          double angle = x * stripFreq + m_time * stripSpeed + phase;
          double waveY = stripAmp * qSin(angle);
          stripPoly << QPointF(x, baseY + waveY);
      }
      
      // Bottom Edge
      double bottomY = (i + 1.5) * stripHeight + stripAmp; 
      
      stripPoly << QPointF(w, bottomY);
      stripPoly << QPointF(0, bottomY);
      
      p.setBrush(color);
      p.setPen(Qt::NoPen);
      p.drawPolygon(stripPoly);
      
      // Add shading
      QLinearGradient gradient(0, baseY, 0, bottomY);
      gradient.setColorAt(0, Qt::transparent);
      gradient.setColorAt(0.5, QColor(0, 0, 0, 50)); 
      gradient.setColorAt(1, QColor(0, 0, 0, 150)); 
      
      QPainterPath path;
      path.addPolygon(stripPoly);
      p.fillPath(path, gradient);
      
      // Draw ANY boats that belong to this strip
      for (const auto &b : m_boats) {
          if (b.active && b.stripIndex == i) {
              drawBoat(p, b, baseY, stripAmp, stripFreq, phase);
          }
      }
  }
  
  // Draw Parrot on top of waves
  drawParrot(p);
  
  // Render subtitles
  m_subtitles.paint(&p, rect());
}

void JolyAnimation::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
}
