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
  m_cannonballs.reserve(20);
  
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
    
    if (activeCount >= 15) return; 
    
    // Increased spawn rate
    if (m_time > 30.0 && QRandomGenerator::global()->bounded(100) < 2) { 
        BoatState newBoat;
        newBoat.active = true;
        newBoat.sinking = false;
        newBoat.sinkProgress = 0.0;
        newBoat.isPirate = false;
        newBoat.fireCooldown = 0.0;
        newBoat.tilt = 0;
        newBoat.vy = 0; // Initial vertical velocity
        newBoat.y = 0;  // Will be set in first update
        
        // 1 in 30 chance to be a pirate ship
        if (QRandomGenerator::global()->bounded(30) == 0) {
            newBoat.isPirate = true;
            newBoat.shapeType = 2; // Pirate ships usually look like Tall Ships
            
            // Pirate Logic: Hunt an existing boat!
            int targetIdx = -1;
            for (int i = 0; i < (int)m_boats.size(); ++i) {
                if (m_boats[i].active && !m_boats[i].sinking && !m_boats[i].isPirate) {
                    targetIdx = i;
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
                newBoat.speed = (leftToRight ? 1 : -1) * (120 + QRandomGenerator::global()->bounded(60));
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

        // Initialize Y to surface immediately to avoid drop-in
        double stripHeight = static_cast<double>(height()) / STRIP_COUNT;
        newBoat.y = newBoat.stripIndex * stripHeight;
        
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
    if (m_time > 15.0 && QRandomGenerator::global()->bounded(8000) < 1) {
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
  
  double stripHeight = static_cast<double>(height()) / STRIP_COUNT;
  
  double frequency = 0.02; 
  double delay = 20.0;
  
  double globalAmp = stripHeight * 0.8; 
  double rampFactor = 1.0;
  if (m_time < delay) {
     rampFactor = 0.0;
  } else if (m_time < delay + 2.0) {
     rampFactor = (m_time - delay) * 0.5;
  }
  
  const double GRAVITY = 800.0; // pixels / s^2

  // Update Boats
  for (int i = 0; i < (int)m_boats.size(); ++i) {
      BoatState &b = m_boats[i];
      if (!b.active) continue;

      if (b.sinking) {
          b.sinkProgress += dt * 0.15; 
          b.x += b.speed * dt * 0.3; 
          b.y += 100 * dt; // Sink down absolute
          if (b.sinkProgress > 1.2) b.active = false;
      } else {
          b.x += b.speed * dt;
          if (b.x < -200 || b.x > width() + 200) b.active = false;

          // Boat Physics 
          // 1. Calculate water surface height and velocity at boat x
          double perspectiveFactor = (double)(b.stripIndex + 1) / STRIP_COUNT;
          double stripAmp = globalAmp * rampFactor * perspectiveFactor * m_strips[b.stripIndex].currentAmpFactor;
          double phase = m_strips[b.stripIndex].phase;
          double stripFreq = frequency * (0.9 + 0.2 * qCos(b.stripIndex * 0.5));
          double stripSpeed = m_strips[b.stripIndex].currentSpeed;
          
          double angle = b.x * stripFreq + m_time * stripSpeed + phase;
          double baseY = b.stripIndex * stripHeight;
          double yWater = baseY + stripAmp * qSin(angle);
          
          // vyWater = derivative of yWater wrt time
          // d(angle)/dt = stripSpeed
          // vyWater = stripAmp * cos(angle) * stripSpeed
          double vyWater = stripAmp * qCos(angle) * stripSpeed;
          
          // 2. Apply Gravity
          b.vy += GRAVITY * dt;
          b.y += b.vy * dt;
          
          // 3. Collision with water
          if (b.y > yWater) {
              b.y = yWater;
              // If we slam into water, take its velocity, but ensure we don't just stick if it accelerates down fast?
              // Simple kinematic: Stick to surface if moving down into it.
              b.vy = vyWater;
              
              // Calculate tilt based on wave slope
              double slope = stripAmp * stripFreq * qCos(angle);
              b.tilt = qRadiansToDegrees(qAtan(slope));
              b.tilt += 10.0 * qSin(m_time * 3.0 + b.x * 0.01);
          } else {
              // Airborne - keep tilt constant or damp it? Keep last tilt.
          }

          // Pirate Logic
          if (b.isPirate) {
              b.fireCooldown -= dt;
              if (b.fireCooldown <= 0) {
                  // Find target
                  int targetIdx = -1;
                  double minDist = 500.0; // Increased range
                  
                  for (int j = 0; j < (int)m_boats.size(); ++j) {
                      if (i == j) continue;
                      if (!m_boats[j].active || m_boats[j].sinking || m_boats[j].isPirate) continue;
                      
                      const auto &target = m_boats[j];
                      
                      // Allow cross-strip shooting
                      double dx = b.x - target.x;
                      double dy = b.y - target.y;
                      double dist = qSqrt(dx*dx + dy*dy);
                      
                      if (dist < minDist) {
                          minDist = dist;
                          targetIdx = j;
                      }
                  }
                  
                  if (targetIdx != -1) {
                      // Fire Canonball!
                      b.fireCooldown = 1.5 + QRandomGenerator::global()->generateDouble(); 
                      
                      Cannonball ball;
                      ball.active = true;
                      
                      ball.x = b.x;
                      ball.y = b.y - 10; // Slightly up (deck level)
                      
                      const auto &target = m_boats[targetIdx];
                      
                      // Ballistic Aiming
                      // Try to hit target in T seconds
                      // T proportional to distance?
                      double dist = minDist;
                      double T = dist / 300.0; // Flight time guess, 300px/s avg horizontal speed
                      if (T < 0.5) T = 0.5;
                      
                      // Predict target pos? Assume static for now or linear predict
                      double predX = target.x + target.speed * T * 0.8;
                      double predY = target.y; // Assume similar Y
                      
                      double dx = predX - ball.x;
                      double dy = predY - ball.y;
                      
                      // x = vx * T  => vx = dx / T
                      ball.vx = dx / T;
                      
                      // y = vy*T + 0.5*g*T^2 ==> vy = (y - 0.5*g*T^2)/T
                      // dy = y_dest - y_start
                      // dy = vy * T + 0.5 * g * T^2
                      // vy = (dy - 0.5 * g * T^2) / T
                      ball.vy = (dy - 0.5 * GRAVITY * T * T) / T;
                      
                      m_cannonballs.push_back(ball);
                  }
              }
          } else {
              // Sinking logic
              const auto &strip = m_strips[b.stripIndex];
              if (strip.currentAmpFactor > 0.95 && QRandomGenerator::global()->bounded(1000) < 2) {
                  b.sinking = true;
              }
          }
      }
  }
  
  // Update Cannonballs
  for (auto it = m_cannonballs.begin(); it != m_cannonballs.end();) {
      if (!it->active) {
          it = m_cannonballs.erase(it);
          continue;
      }
      
      it->vy += GRAVITY * dt; // Gravity
      it->x += it->vx * dt;
      it->y += it->vy * dt;
      
      if (it->x < -200 || it->x > width() + 200 || it->y > height() + 200) {
          it->active = false;
          ++it;
          continue;
      }
      
      // Collision Check
      bool hit = false;
      for (auto &b : m_boats) {
          if (b.active && !b.sinking && !b.isPirate) {
              double dx = it->x - b.x;
              double dy = it->y - (b.y - 10); // Hit centerish
              
              // Hit radius approx 25px
              if (dx*dx + dy*dy < 25*25) {
                  b.sinking = true;
                  hit = true;
                  break; // Only sink one boat
              }
          }
      }
      
      if (hit) {
          it->active = false; // Destroy ball on impact
      }
      ++it;
  }
  
  // Update parrot
  if (m_parrot.active) {
      m_parrot.x += m_parrot.speedX * dt;
      m_parrot.y += m_parrot.speedY * dt;
      m_parrot.wingPhase += 15.0 * dt; 
      
      if (m_parrot.x < -200 || m_parrot.x > width() + 200) {
          m_parrot.active = false;
      }
  }
  
  update(); 
}

void JolyAnimation::drawBoat(QPainter &p, const BoatState &boat, double yBase, double amplitude, double frequency, double phase) {
    // Only use boat.y for position, ignore wave calc here (done in update)
    double y = boat.y;
    double tiltAngle = boat.tilt;

    double sinkY = 0;
    

    if (boat.sinking) {
        double targetTilt = (boat.speed > 0) ? 180 : -180;
        double tiltProgress = qMin(1.0, boat.sinkProgress * 2.0);
        tiltAngle = tiltAngle * (1.0 - tiltProgress) + targetTilt * tiltProgress;
        sinkY = boat.sinkProgress * 80.0;
    }
    
    p.save();
    p.translate(boat.x, y + sinkY);
    
    double scale = 0.4 + 1.2 * ((double)boat.stripIndex / STRIP_COUNT);
    p.scale(scale, scale);
    
    p.rotate(tiltAngle);
    
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
        p.setPen(QPen(Qt::black, 1)); 
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
    
    p.scale(0.8, 0.8);
    
    QColor bodyColor(0, 255, 0); 
    QColor wingColor(50, 205, 50); 
    
    QPen outlinePen(Qt::black, 0);
    p.setPen(outlinePen);
    
    QPainterPath body;
    // Head and body - Streamlined Horizontal Flight
    body.moveTo(0, 0); 
    // Head area (Forward)
    body.cubicTo(10, -5, 25, 0, 30, 5); 
    // Beak position ref: (30, 5) roughly
    
    // Belly (Bottom curve)
    body.cubicTo(15, 15, -10, 15, -20, 10);
    
    // Tail (Extending back)
    body.lineTo(-50, 5);
    body.lineTo(-45, -5);
    
    // Back (Top curve)
    body.cubicTo(-20, -10, -10, -10, 0, 0); 
    
    p.setBrush(bodyColor);
    p.drawPath(body);
    
    QPainterPath beak;
    beak.moveTo(22, 2);
    beak.cubicTo(28, 2, 30, 8, 28, 12);
    beak.lineTo(22, 8);
    beak.closeSubpath();
    p.setBrush(Qt::red);
    p.drawPath(beak);
    
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawEllipse(20, -2, 4, 4);
    p.setBrush(Qt::black);
    p.drawEllipse(21, -1, 2, 2);
    p.setPen(outlinePen);
    
    double flap = qSin(m_parrot.wingPhase);
    QPainterPath wing;
    
    // Attach wing closer to center of body shoulder
    wing.moveTo(-5, 0);
    
    // Wing tip moves up/down
    double tipY = 10 + flap * 35; 
    
    // Wider wing span
    wing.quadTo(-15, tipY - 15, -45, tipY); 
    wing.quadTo(-10, tipY + 5, 10, 10);
    
    p.setBrush(wingColor);
    p.drawPath(wing);
    
    p.restore();
}

void JolyAnimation::drawCannonball(QPainter &p, const Cannonball &cb) {
    p.save();
    p.translate(cb.x, cb.y);
    p.setBrush(Qt::black);
    p.setPen(Qt::NoPen);
    p.drawEllipse(-3, -3, 6, 6); // 6px ball
    p.restore();
}

void JolyAnimation::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  
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
      QColor color;
      switch (i % 3) {
          case 0: color = QColor(220, 50, 50); break; 
          case 1: color = QColor(50, 200, 50); break; 
          case 2: color = QColor(50, 50, 220); break; 
      }

      double perspectiveFactor = (double)(i + 1) / STRIP_COUNT;
      double stripAmp = globalAmp * rampFactor * perspectiveFactor * m_strips[i].currentAmpFactor;
      
      double phase = m_strips[i].phase;
      double stripFreq = frequency * (0.9 + 0.2 * qCos(i * 0.5));
      double stripSpeed = m_strips[i].currentSpeed;

      QPolygonF stripPoly;
      
      double baseX = 0;
      double baseY = i * stripHeight;
      
      for (int x = 0; x <= w; x += 5) {
          double angle = x * stripFreq + m_time * stripSpeed + phase;
          double waveY = stripAmp * qSin(angle);
          stripPoly << QPointF(x, baseY + waveY);
      }
      
      double bottomY = (i + 1.5) * stripHeight + stripAmp; 
      
      stripPoly << QPointF(w, bottomY);
      stripPoly << QPointF(0, bottomY);
      
      p.setBrush(color);
      p.setPen(Qt::NoPen);
      p.drawPolygon(stripPoly);
      
      QLinearGradient gradient(0, baseY, 0, bottomY);
      gradient.setColorAt(0, Qt::transparent);
      gradient.setColorAt(0.5, QColor(0, 0, 0, 50)); 
      gradient.setColorAt(1, QColor(0, 0, 0, 150)); 
      
      QPainterPath path;
      path.addPolygon(stripPoly);
      p.fillPath(path, gradient);
      
      for (const auto &b : m_boats) {
          if (b.active && b.stripIndex == i) {
              drawBoat(p, b, baseY, stripAmp, stripFreq, phase);
          }
      }
  }
  
  // Draw Cannonballs (on top of all waves for visibility, or interleaved? 
  // User asked for "visible cannon balls". On top is safest.)
  for (const auto &cb : m_cannonballs) {
      if (cb.active) {
          drawCannonball(p, cb);
      }
  }

  drawParrot(p);
  
  m_subtitles.paint(&p, rect());
}

void JolyAnimation::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
}
