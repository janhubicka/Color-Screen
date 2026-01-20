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
  
  m_subtitles.start("Joly Color Screen", "1896", "Prof. John Joly, Dublin");
  
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
    
    if (activeCount >= 10) return; // Increased max boats to 10
    
    // Increased spawn rate (1 in 100 chance per frame vs 1 in 300)
    // Only spawn if animation has started properly (time > 30.0)
    if (m_time > 30.0 && QRandomGenerator::global()->bounded(100) < 2) { 
        BoatState newBoat;
        newBoat.active = true;
        newBoat.sinking = false;
        newBoat.sinkProgress = 0.0;
        
        newBoat.stripIndex = 5 + QRandomGenerator::global()->bounded(STRIP_COUNT - 5);
        newBoat.shapeType = QRandomGenerator::global()->bounded(3);
        newBoat.tilt = 0;
        
        bool leftToRight = QRandomGenerator::global()->bounded(2) == 0;
        if (leftToRight) {
            newBoat.x = -150;
            newBoat.speed = 80 + QRandomGenerator::global()->bounded(60);
        } else {
            newBoat.x = width() + 150;
            newBoat.speed = -(80 + QRandomGenerator::global()->bounded(60));
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

void JolyAnimation::updateAnimation() {
  double dt = 0.016;
  m_time += dt;
  m_subtitles.update(dt);
  
  updateWaveDynamics();
  spawnBoat();
  
  for (auto &b : m_boats) {
      if (b.active) {
          if (b.sinking) {
              b.sinkProgress += dt * 0.15; // Slow sinking (~6-7 seconds)
              b.x += b.speed * dt * 0.3; // Forward motion continues but slower
              
              // Only deactivate when properly submerged
              if (b.sinkProgress > 1.2) { 
                  b.active = false;
              }
          } else {
              b.x += b.speed * dt;
              if (b.x < -200 || b.x > width() + 200) {
                  b.active = false;
              }
              
              // Risk of capsizing depends on wave "drama" (amplitude and speed)
              const auto &strip = m_strips[b.stripIndex];
              
              // Base risk depends on how "high" the waves are relative to calm state (0.8)
              // Range of factor is approx 0.8 to 1.2
              double riskFactor = 0.0;
              if (strip.currentAmpFactor > 0.9) {
                  // Exponential risk increase above threshold
                  // At 1.2 (max), diff is 0.3. 0.3^2 = 0.09.
                  double diff = strip.currentAmpFactor - 0.9;
                  riskFactor = diff * diff * 0.2; // 0.018 probability per frame at max
              }
              
              // Speed mult multiplier: fast waves are more dangerous
              // Speed varies approx 2.0 to 4.0. Base 3.0.
              double speedMult = strip.currentSpeed / 3.0;
              
              // Final probability per frame (at 60fps)
              // reduced to 1/3 as per user request
              double prob = (riskFactor * speedMult) / 3.0;
              
              // Use random check against probability
              // generateDouble() returns 0..1
              if (QRandomGenerator::global()->generateDouble() < prob) {
                  b.sinking = true;
              }
          }
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
        // Capsize: tilt towards 180 degrees
        double targetTilt = (boat.speed > 0) ? 180 : -180;
        
        // Tilt happens relatively fast in the beginning
        double tiltProgress = qMin(1.0, boat.sinkProgress * 2.0);
        tiltAngle = tiltAngle * (1.0 - tiltProgress) + targetTilt * tiltProgress;
        
        // Sink downwards
        // Need to sink deep enough to be covered by next strip
        // Strip height is roughly h/30 (e.g. 20-30px). Wave amp is ~20px.
        // Sinking 80px should be plenty.
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
    
    if (boat.sinking) {
       // Darken slightly as it sinks to simulate going underwater?
       // Or simpler: just let geometry clip it.
       // Let's use a slightly darker shade if sinking deep
       int gray = 255 - (int)(qMin(1.0, boat.sinkProgress) * 100);
       p.setBrush(QColor(gray, gray, gray));
    } else {
       p.setBrush(Qt::white);
    }
    
    p.setPen(QPen(Qt::gray, 1));
    p.drawPath(hull);
    p.drawPath(sail);
    
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
  
  // Render subtitles
  m_subtitles.paint(&p, rect());
}

void JolyAnimation::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
}
