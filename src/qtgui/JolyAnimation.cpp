#include "JolyAnimation.h"
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QtMath>
#include <QRandomGenerator>

// ============================================================================
// ANIMATION CONTROL CONSTANTS
// ============================================================================
// Adjust these values to tune the animation behavior

// --- Timing Parameters ---
constexpr double ANIMATION_FPS = 60.0;                    // Animation frame rate
constexpr double WAVE_STARTUP_DELAY = 20.0;               // Seconds before waves start moving
constexpr double WAVE_RAMP_DURATION = 2.0;                // Seconds for waves to reach full amplitude
constexpr double BOAT_SPAWN_START = 20.0;                 // Seconds before boats start appearing
constexpr double DOLPHIN_SPAWN_START = 25.0;              // Seconds before dolphins appear
constexpr double WHALE_SPAWN_START = 90.0;                // Seconds before whales appear
constexpr double PIRATE_SPAWN_START = 30.0;               // Seconds before pirates appear
constexpr double PARROT_SPAWN_START = 60.0;               // Seconds before parrots appear
constexpr double BOTTLE_SPAWN_START = 120.0;               // Seconds before beer bottles appear
constexpr double BOUNCINESS_RAMP_DURATION = 300.0;        // Seconds to reach maximum bounce (2 minutes)

// --- Physics Parameters ---
constexpr double GRAVITY = 400.0;                         // Gravity in pixels/s² (affects falling objects)
constexpr double BOUNCE_THRESHOLD = 200.0;                // Min impact velocity (px/s) to trigger bounce
constexpr double BOUNCE_FORCE = 0.5;                      // Bounce force multiplier (0.0 = no bounce, 1.0 = full reflection)
constexpr double HARD_IMPACT_THRESHOLD = 450.0;           // Impact velocity that sinks boats (px/s)
constexpr double WAVE_FREQUENCY = 0.02;                   // Base wave frequency (affects wavelength)
constexpr double WAVE_AMPLITUDE_FACTOR = 0.8;             // Wave height as fraction of strip height

// --- Spawn Rates & Limits ---
constexpr int MAX_BOATS = 10;                             // Maximum simultaneous boats
constexpr int BOAT_SPAWN_CHANCE = 30;                     // 1 in N chance per frame to spawn boat
constexpr int PARROT_SPAWN_CHANCE = 300;                  // 1 in N chance per frame to spawn parrot
constexpr int BOTTLE_SPAWN_CHANCE = 200;                  // 1 in N chance per frame to spawn bottle
constexpr int MAX_BOTTLES = 2;                            // Maximum simultaneous bottles
constexpr int MAX_CANNONBALLS = 20;                       // Maximum simultaneous cannonballs

// --- Object Behavior ---
constexpr double ORANGE_FLOAT_TIME = 5.0;                 // Seconds oranges float before sinking
constexpr double PIRATE_FIRE_COOLDOWN_MIN = 1.5;          // Min seconds between pirate shots
constexpr double PIRATE_FIRE_COOLDOWN_MAX = 2.5;          // Max seconds between pirate shots
constexpr double PIRATE_FIRE_RANGE = 500.0;               // Max distance pirates can shoot (pixels)
constexpr double PARROT_DROP_DELAY_MIN = 2.0;             // Min seconds before parrot drops orange
constexpr double PARROT_DROP_DELAY_MAX = 5.0;             // Max seconds before parrot drops orange

// --- Visual Scaling ---
constexpr double BOAT_SIZE_MULTIPLIER = 1.5;              // Boat size (1.0 = original, 1.5 = 50% larger)
constexpr double BOTTLE_SIZE_MULTIPLIER = 0.80;           // Bottle size 
constexpr double DOLPHIN_SIZE = 20.0; // Small 
constexpr double WHALE_SIZE = 110.0;

constexpr int DOLPHIN_SPAWN_CHANCE = 200; // Frequent but not swarming
constexpr int WHALE_SPAWN_CHANCE = 300; // Much more frequent (was 300)

// --- Advanced Animation Params ---
constexpr double WHALE_SURFACE_OFFSET = 15.0;     // Target height (smaller = higher out of water)
constexpr double WHALE_SUBMERGED_OFFSET = 50.0;   // Start depth
constexpr double DOLPHIN_JUMP_VELOCITY = -90.0;  // Initial Dy (stronger jump)

// ============================================================================


JolyAnimation::JolyAnimation(QWidget *parent)
    : QWidget(parent), m_physicsAccumulator(0.0), m_time(0.0), m_bounciness(0.0) {
  
  // Animation timer - 60 FPS
  m_animTimer = new QTimer(this);
  connect(m_animTimer, &QTimer::timeout, this, &JolyAnimation::updateAnimation);
  
  m_subtitles.start("Joly Colour Screen", "1896", "Patented by Prof. John Joly of Dublin in 1894 using regular red, green, and blue-violet lines");
  
  // Initialize strips
  for (int i = 0; i < STRIP_COUNT; ++i) {
      m_strips[i].phase = i * 0.5 + (i * i * 0.05);
      m_strips[i].currentSpeed = 1.5 * (0.8 + 0.4 * qSin(i * 0.32));
      m_strips[i].targetSpeed = m_strips[i].currentSpeed;
      m_strips[i].currentAmpFactor = 1.0;
      m_strips[i].targetAmpFactor = 1.0;
  }
  
  // Initialize boat pool
  m_boats.reserve(10);
  m_cannonballs.reserve(20);
  m_oranges.reserve(5);
  m_bottles.reserve(5);
  
  m_parrot.active = false;
}

JolyAnimation::~JolyAnimation() = default;

void JolyAnimation::startAnimation() {
  m_elapsedTimer.start();
  m_animTimer->start(16); // ~60 FPS
}

void JolyAnimation::stopAnimation() {
  m_animTimer->stop();
}

void JolyAnimation::updateWaveDynamics() {
    // Randomly update targets
    if (QRandomGenerator::global()->bounded(100) < 2) { 
        int idx = QRandomGenerator::global()->bounded(STRIP_COUNT);
        m_strips[idx].targetSpeed = 1.5 * (0.5 + QRandomGenerator::global()->generateDouble());
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
    if (m_time > BOAT_SPAWN_START && QRandomGenerator::global()->bounded(BOAT_SPAWN_CHANCE) == 0) { 
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
        // Pirate ships start later (30s)
        if (m_time > PIRATE_SPAWN_START && QRandomGenerator::global()->bounded(BOAT_SPAWN_CHANCE) == 0) {
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
                newBoat.speed = (leftToRight ? 1 : -1) * (60 + QRandomGenerator::global()->bounded(30));
                newBoat.x = leftToRight ? -150 : width() + 150;
            }
            
        } else {
            // Normal Boat Spawn
            newBoat.stripIndex = 5 + QRandomGenerator::global()->bounded(STRIP_COUNT - 5);
            newBoat.shapeType = QRandomGenerator::global()->bounded(3);
            
            bool leftToRight = QRandomGenerator::global()->bounded(2) == 0;
            if (leftToRight) {
                newBoat.x = -150;
                newBoat.speed = 40 + QRandomGenerator::global()->bounded(30);
            } else {
                newBoat.x = width() + 150;
                newBoat.speed = -(40 + QRandomGenerator::global()->bounded(30));
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
    if (m_time > PARROT_SPAWN_START && QRandomGenerator::global()->bounded(PARROT_SPAWN_CHANCE) < 1) { // More frequent for checking
        m_parrot.active = true;
        m_parrot.wingPhase = 0;
        
        // Random strip depth
        m_parrot.stripIndex = QRandomGenerator::global()->bounded(STRIP_COUNT);
        
        // Set Y higher up in sky generally, based on perspective
        double stripHeight = static_cast<double>(height()) / STRIP_COUNT;
        double horizonY = m_parrot.stripIndex * stripHeight;
        m_parrot.y = horizonY - 50 - QRandomGenerator::global()->bounded(150); 
        
        m_parrot.hasOrange = QRandomGenerator::global()->bounded(2) == 0; // 50% chance
        m_parrot.dropTimer = PARROT_DROP_DELAY_MIN + (PARROT_DROP_DELAY_MAX - PARROT_DROP_DELAY_MIN) * QRandomGenerator::global()->generateDouble();
        
        m_parrot.movingRight = QRandomGenerator::global()->bounded(2) == 0;
        if (m_parrot.movingRight) {
            m_parrot.x = -100;
            m_parrot.speedX = 75 + QRandomGenerator::global()->bounded(50);
        } else {
            m_parrot.x = width() + 100;
            m_parrot.speedX = -(75 + QRandomGenerator::global()->bounded(50));
        }
        m_parrot.speedY = (QRandomGenerator::global()->generateDouble() - 0.5) * 15; 
    }
}

void JolyAnimation::spawnBottle() {
    int activeCount = 0;
    for (const auto &b : m_bottles) if (b.active) activeCount++;

    for (const auto &b : m_bottles) if (b.active) activeCount++;
    if (activeCount >= 2) return;
    
    // Bottles start appearing after 1m 30s (90s)
    if (m_time > BOTTLE_SPAWN_START && QRandomGenerator::global()->bounded(BOTTLE_SPAWN_CHANCE) == 0) { // Rare
        BottleState b;
        b.active = true;
        
        // Lowest two strips (foreground)
        b.stripIndex = STRIP_COUNT - 1 - QRandomGenerator::global()->bounded(2);
        
        bool leftToRight = QRandomGenerator::global()->bounded(2) == 0;
        b.speed = (leftToRight ? 1.0 : -1.0) * (15 + QRandomGenerator::global()->bounded(20));
        b.x = leftToRight ? -50 : width() + 50;
        
        b.phase = QRandomGenerator::global()->generateDouble() * 6.28;
        b.tilt = 0;
        b.vy = 0;
        
        // Initialize Y to surface
        double stripHeight = static_cast<double>(height()) / STRIP_COUNT;
        b.y = b.stripIndex * stripHeight;
        
        // Try to place in empty slot
        bool placed = false;
        for (auto &slot : m_bottles) {
            if (!slot.active) {
                slot = b;
                placed = true;
                break;
            }
        }
        if (!placed) m_bottles.push_back(b);
    }
}

void JolyAnimation::spawnDolphin() {
    if (m_time < DOLPHIN_SPAWN_START) return;
    
    // Spawn randomness
    if (QRandomGenerator::global()->bounded(DOLPHIN_SPAWN_CHANCE) == 0) {
        Dolphin d;
        d.active = true;
        // Mid to foreground strips (index 5 to 25)
        d.stripIndex = 5 + QRandomGenerator::global()->bounded(STRIP_COUNT - 8); 
        d.size = DOLPHIN_SIZE * (0.8 + 0.4 * QRandomGenerator::global()->generateDouble()); // Slight size variation
        
        double stripHeight = static_cast<double>(height()) / STRIP_COUNT;
        double yBase = d.stripIndex * stripHeight;
        
        // Random horizontal position, keeping some margin from edges
        d.x = 100 + QRandomGenerator::global()->bounded(width() - 200);
        d.y = yBase + 10; // Start closer to surface so they are visible immediately
        d.startX = d.x;
        d.startY = d.y;
        
        // Jump velocity
        // Determine jump direction (left-to-right or right-to-left)
        bool jumpRight = QRandomGenerator::global()->bounded(2) == 0;
        double speedX = 25.0 + QRandomGenerator::global()->bounded(20); 
        
        d.vx = jumpRight ? speedX : -speedX;
        d.vy = DOLPHIN_JUMP_VELOCITY - QRandomGenerator::global()->bounded(30); 
        
        // Longer jump duration adjustment (gravity effect remains same, but init velocity is higher)
        // With higher Vy, they will stay in air longer naturally.
        
        d.angle = jumpRight ? -45.0 : 45.0; // Initial nose-up angle
        
        bool placed = false;
        for (auto &existing : m_dolphins) {
            if (!existing.active) {
                existing = d;
                placed = true;
                break;
            }
        }
        if (!placed) m_dolphins.push_back(d);
    }
}

void JolyAnimation::spawnWhale() {
    if (m_time < WHALE_SPAWN_START) return;
    
    // Whales are rare
    if (QRandomGenerator::global()->bounded(WHALE_SPAWN_CHANCE) == 0) {
        // Limit active whales to avoid overcrowding (max 1 usually)
        int activeCount = 0;
        for (const auto &w : m_whales) if (w.active) activeCount++;
        if (activeCount >= 1) return;
        
        Whale w;
        w.active = true;
        w.timer = 0;
        w.size = WHALE_SIZE * (0.9 + 0.2 * QRandomGenerator::global()->generateDouble());
        
        // Background to mid strips (deeper water feel)
        w.stripIndex = 3 + QRandomGenerator::global()->bounded(STRIP_COUNT / 2);
        
        w.x = 200 + QRandomGenerator::global()->bounded(width() - 400);
        
        // Disable breaching for now
        w.isBreaching = false; 
        
        // Initialize Y based on behavior (handled in update/draw)
        double stripHeight = static_cast<double>(height()) / STRIP_COUNT;
        w.y = w.stripIndex * stripHeight + WHALE_SUBMERGED_OFFSET; // Start submerged to match animation
        
        
        bool placed = false;
        // Try to reuse inactive slots
        for (auto &existing : m_whales) {
            if (!existing.active) {
                existing = w;
                placed = true;
                break;
            }
        }
        if (!placed) {
            m_whales.push_back(w);
        }
    }
}

void JolyAnimation::updateAnimation() {
  double dt = 0.016; // Fixed physics step

  // Add elapsed time to accumulator
  if (m_elapsedTimer.isValid()) {
      double elapsed = m_elapsedTimer.restart() / 1000.0;
      // Cap max elapsed time to prevent spiral of death if rendering freezes
      if (elapsed > 0.1) elapsed = 0.1;
      m_physicsAccumulator += elapsed;
  }

  // Consume accumulated time in fixed steps
  while (m_physicsAccumulator >= dt) {
      stepAnimation(dt);
      m_physicsAccumulator -= dt;
  }
  
  update(); 
}

void JolyAnimation::stepAnimation(double dt) {
  m_time += dt;
  m_subtitles.update(dt);
  
  updateWaveDynamics();
  
  double stripHeight = static_cast<double>(height()) / STRIP_COUNT;
  
  double frequency = WAVE_FREQUENCY; 
  double delay = WAVE_STARTUP_DELAY;
  
  double globalAmp = stripHeight * WAVE_AMPLITUDE_FACTOR; 
  double rampFactor = 1.0;
  if (m_time < delay) {
     rampFactor = 0.0;
  } else if (m_time < delay + WAVE_RAMP_DURATION) {
     rampFactor = (m_time - delay) / WAVE_RAMP_DURATION;
  }
  
  // Gradually increase bounciness over time for more dramatic physics
  m_bounciness = std::min(1.0, m_time / BOUNCINESS_RAMP_DURATION);
  


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
              // Check for impact velocity
              double impact = b.vy - vyWater;
              
              // Apply bounce if moving fast relative to water
              if (impact > BOUNCE_THRESHOLD) {
                  // Bounce force proportional to impact and bounciness
                  b.vy = vyWater - impact * m_bounciness * BOUNCE_FORCE;
              } else {
                  // Normal surface tracking
                  b.vy = vyWater;
              }
              
              // Hard impact still causes sinking
              if (impact > HARD_IMPACT_THRESHOLD) {
                   if (!b.sinking) b.sinking = true;
              }
              
              b.y = yWater;
              
              // Calculate tilt based on wave slope
              double slope = stripAmp * stripFreq * qCos(angle);
              double targetTilt = qRadiansToDegrees(qAtan(slope));
              targetTilt += 10.0 * qSin(m_time * 3.0 + b.x * 0.01);
              
              // Smooth interpolation
              b.tilt += (targetTilt - b.tilt) * 0.1;
          } else {
              // Airborne - keep tilt constant or damp it? 
               b.tilt += (0 - b.tilt) * 0.02;
          }

          // Pirate Logic
          if (b.isPirate) {
              b.fireCooldown -= dt;
              if (b.fireCooldown <= 0) {
                  // Find target
                  int targetIdx = -1;
                  double minDist = PIRATE_FIRE_RANGE;
                  
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
                      b.fireCooldown = PIRATE_FIRE_COOLDOWN_MIN + (PIRATE_FIRE_COOLDOWN_MAX - PIRATE_FIRE_COOLDOWN_MIN) * QRandomGenerator::global()->generateDouble(); 
                      
                      Cannonball ball;
                      ball.active = true;
                      
                      ball.x = b.x;
                      ball.y = b.y - 10; // Slightly up (deck level)
                      
                      const auto &target = m_boats[targetIdx];
                      
                      // Ballistic Aiming
                      // Try to hit target in T seconds
                      // T proportional to distance?
                      double dist = minDist;
                      double T = dist / 150.0; // Flight time guess, 300px/s avg horizontal speed
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
              // Sinking logic removed (replaced by hard physics impact)
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
      
      // Drop Orange
      if (m_parrot.hasOrange) {
          m_parrot.dropTimer -= dt;
          if (m_parrot.dropTimer <= 0) {
              m_parrot.hasOrange = false;
              
              OrangeState o;
              o.active = true;
              o.x = m_parrot.x;
              o.y = m_parrot.y + 10; // Drop from below
              o.vx = m_parrot.speedX; // Inherit velocity
              o.vy = 0;
              o.stripIndex = m_parrot.stripIndex;
              o.onWater = false;
              o.bobPhase = 0;
              o.floatTime = 0;
              
              m_oranges.push_back(o);
          }
      }
      
      if (m_parrot.x < -200 || m_parrot.x > width() + 200) {
          m_parrot.active = false;
      }
  }
  
  // Update Oranges
  for (auto it = m_oranges.begin(); it != m_oranges.end();) {
      if (!it->active) {
          it = m_oranges.erase(it);
          continue;
      }
      
      it->vy += GRAVITY * dt;
      it->x += it->vx * dt;
      it->y += it->vy * dt;
      
      if (it->onWater) {
          it->floatTime += dt;
          if (it->floatTime > ORANGE_FLOAT_TIME) {
              it->active = false; // Sink/Disappear
              it = m_oranges.erase(it);
              continue;
          }
          it->vx *= 0.95; // Friction
          
           // Drift
           const auto &strip = m_strips[it->stripIndex];
           it->x += strip.currentSpeed * dt * 0.5;
           it->bobPhase += dt * 5.0;
      }

      // Water Collision / Physics
      double perspectiveFactor = (double)(it->stripIndex + 1) / STRIP_COUNT;
      double stripAmp = globalAmp * rampFactor * perspectiveFactor * m_strips[it->stripIndex].currentAmpFactor;
      double phase = m_strips[it->stripIndex].phase;
      double stripFreq = frequency * (0.9 + 0.2 * qCos(it->stripIndex * 0.5));
      double stripSpeed = m_strips[it->stripIndex].currentSpeed;
      
      double angle = it->x * stripFreq + m_time * stripSpeed + phase;
      double baseY = it->stripIndex * stripHeight;
      double yWater = baseY + stripAmp * qSin(angle);
      double vyWater = stripAmp * qCos(angle) * stripSpeed;
      
      
      if (it->y > yWater) {
          double impact = it->vy - vyWater;
          
          // Apply bounce if moving fast relative to water
          if (impact > BOUNCE_THRESHOLD) {
              it->vy = vyWater - impact * m_bounciness * BOUNCE_FORCE;
          } else {
              it->vy = vyWater;
          }
          
          it->y = yWater;
          it->onWater = true;
      } else {
         // In air (or flying)
      }
      
      if (it->x < -200 || it->x > width() + 200 || it->y > height() + 100) it->active = false;
      
      ++it;
  }
  
  // Update Bottles
  for (auto &b : m_bottles) {
      if (!b.active) continue;
      b.x += b.speed * dt;
      if (b.x < -200 || b.x > width() + 200) b.active = false;
      
      // Bottle Physics (Flying)
      double perspectiveFactor = (double)(b.stripIndex + 1) / STRIP_COUNT;
      double stripAmp = globalAmp * rampFactor * perspectiveFactor * m_strips[b.stripIndex].currentAmpFactor;
      double phase = m_strips[b.stripIndex].phase;
      double stripFreq = frequency * (0.9 + 0.2 * qCos(b.stripIndex * 0.5));
      double stripSpeed = m_strips[b.stripIndex].currentSpeed;
      
      double angle = b.x * stripFreq + m_time * stripSpeed + phase;
      double baseY = b.stripIndex * stripHeight;
      double yWater = baseY + stripAmp * qSin(angle);
      double vyWater = stripAmp * qCos(angle) * stripSpeed;
      
      b.vy += GRAVITY * dt;
      b.y += b.vy * dt;
      
      
      if (b.y > yWater) {
          double impact = b.vy - vyWater;
          
          // Apply bounce if moving fast relative to water
          if (impact > BOUNCE_THRESHOLD) {
              b.vy = vyWater - impact * m_bounciness * BOUNCE_FORCE;
          } else {
              b.vy = vyWater;
          }
          
          b.y = yWater;
          
          // Calculate tilt based on wave slope
          double slope = stripAmp * stripFreq * qCos(angle);
          double targetTilt = qRadiansToDegrees(qAtan(slope));
          targetTilt += 10.0 * qSin(m_time * 3.0 + b.x * 0.01);
          
          // Smooth interpolation
          b.tilt += (targetTilt - b.tilt) * 0.1;
      } else {
          // Airborne - relax tilt back to 0? Or keep momentum? 
          // Smoothly return to 0 if flying for long?
          b.tilt += (0 - b.tilt) * 0.02;
      }
  }

  // Update Dolphins
  for (auto it = m_dolphins.begin(); it != m_dolphins.end();) {
      if (!it->active) {
          it = m_dolphins.erase(it);
          continue;
      }
      
      it->vy += GRAVITY * dt;
      it->x += it->vx * dt;
      it->y += it->vy * dt;
      
      // Calculate angle based on velocity vector
      if (qAbs(it->vx) > 0.1 || qAbs(it->vy) > 0.1) {
          it->angle = qRadiansToDegrees(qAtan2(it->vy, qAbs(it->vx)));
          if (it->vx < 0) it->angle = -it->angle; 
      }
      
      // Check for re-entry into water
      double stripHeight = static_cast<double>(height()) / STRIP_COUNT;
      double yBase = it->stripIndex * stripHeight;
      // Simple water level approximation sufficient for splash check
      double waveY = yBase + 10; 
      
      // If falling and hits water
      if (it->vy > 0 && it->y > waveY) {
          it->active = false; // Splash! Gone.
      }
      
      if (it->x < -200 || it->x > width() + 200) {
          it->active = false;
      }
      
      if (!it->active) {
          it = m_dolphins.erase(it);
      } else {
          ++it;
      }
  }

  // Update Whales
  for (auto it = m_whales.begin(); it != m_whales.end();) {
      if (!it->active) {
          it = m_whales.erase(it);
          continue;
      }
      
      it->timer += dt;
      
      // Whale Physics (Bobbing/Drifting)
      const auto &strip = m_strips[it->stripIndex];
      // Drift with wave
      it->x += strip.currentSpeed * dt * 0.5;
       
      // Calculate wave interaction for Y and Angle
       double perspectiveFactor = (double)(it->stripIndex + 1) / STRIP_COUNT;
       double globalAmp = 20.0; // Hardcoded approximation of globalAmp in draw
       double rampFactor = std::min(m_time / 10.0, 1.0); 
       double stripAmp = globalAmp * rampFactor * perspectiveFactor * strip.currentAmpFactor;
       
       double phase = strip.phase;
       double frequency = 0.02; // Hardcoded freq
       double stripFreq = frequency * (0.9 + 0.2 * qCos(it->stripIndex * 0.5));
       double stripSpeed = strip.currentSpeed;
       
       double stripHeight = static_cast<double>(height()) / STRIP_COUNT; 
       double angle = it->x * stripFreq + m_time * stripSpeed + phase;
       // Rise/Sink Animation Logic
       // Timer 0->1: Rise (Submerged -> Surface)
       // Timer 1->4: Surface
       // Timer 4->5: Sink (Surface -> Submerged)
       
       double diff = WHALE_SUBMERGED_OFFSET - WHALE_SURFACE_OFFSET;
       double animOffset = diff; // Default full submerged
       
       if (it->timer < 1.0) {
           double t = it->timer; // 0 to 1
           animOffset = diff * (1.0 - qSin(t * M_PI_2)); // Smooth rise
       } else if (it->timer < 4.0) {
           animOffset = 0.0; // Surfaced
       } else if (it->timer < 5.0) {
           double t = it->timer - 4.0; // 0 to 1
           animOffset = diff * qSin(t * M_PI_2); // Smooth sink
       }
       
       double baseY = it->stripIndex * stripHeight + WHALE_SURFACE_OFFSET + animOffset;
       
       it->y = baseY + stripAmp * qSin(angle);
       
       // Calculate tilt
       double slope = stripAmp * stripFreq * qCos(angle);
       // Smoothly interpolate angle? Or direct?
       // Boats act directly on slope? No, m_bottles uses direct slope.
       // Let's just set angle directly for flow
       // Reuse 'isBreaching' as 'facingLeft' flag? No, let's use drift direction
       bool facingLeft = (stripSpeed < 0);
       
       // Tilt based on wave slope
       double waveTilt = qRadiansToDegrees(qAtan(slope));
       // Whales are long, maybe average 2 points? Simple slope is fine.
       it->angle = waveTilt * 0.5; // Dampen tilt
       if (facingLeft) it->angle = -it->angle; // Adjust for flip
       
      // Animation lifecycle
      double maxTime = it->isBreaching ? 4.0 : 5.0; // Breach faster (was 6.0)
      
      if (it->timer > maxTime) {
          it->active = false;
          it = m_whales.erase(it);
          continue;
      }
      ++it;
  }
  
  spawnBoat();
  spawnParrot();
  spawnBottle();
  spawnDolphin();
  spawnWhale();
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
    scale *= 1.2; // Increase size by 20% (vs original 1.5x)
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
       p.setPen(Qt::NoPen);
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
    
    // Depth Scaling based on stripIndex
    double scale = 0.4 + 1.2 * ((double)m_parrot.stripIndex / STRIP_COUNT);
    // Parrot default is big, so scale it down generally + depth scale
    p.scale(scale * 0.8, scale * 0.8);
    
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
    
    // Draw Held Orange
    if (m_parrot.hasOrange) {
        p.setBrush(QColor(255, 165, 0)); // Orange
        p.setPen(Qt::NoPen);
        p.drawEllipse(-5, 12, 12, 12);
    }
    
    p.restore();
}

void JolyAnimation::drawOrange(QPainter &p, const OrangeState &orange, double yBase, double amplitude, double frequency, double phase) {
    double y = orange.y;
    
    // Use physics Y, add bobbing not wave calc
    if (orange.onWater) {
          y += 3.0 * qSin(orange.bobPhase); // Bobbing
    }
    
    p.save();
    p.translate(orange.x, y);
    
    double scale = 0.4 + 1.2 * ((double)orange.stripIndex / STRIP_COUNT);
    p.scale(scale, scale);
    
    p.setBrush(QColor(255, 140, 0)); // Darker Orange
    p.setPen(Qt::NoPen);
    p.drawEllipse(-6, -6, 12, 12);
    
    // Highlight
    p.setBrush(QColor(255, 200, 50));
    p.drawEllipse(-2, -3, 4, 3);
    
    p.restore();
}

void JolyAnimation::drawBottle(QPainter &p, const BottleState &bottle, double yBase, double amplitude, double frequency, double phase) {
    double y = bottle.y;
    double tiltAngle = bottle.tilt;

    p.save();
    p.translate(bottle.x, y); // Triangle at waterline
    
    double scale = 0.4 + 1.2 * ((double)bottle.stripIndex / STRIP_COUNT);
    scale *= BOTTLE_SIZE_MULTIPLIER;
    p.scale(scale, scale);
    p.rotate(tiltAngle);
    
    // Bass Beer Bottle
    // Brown glass
    QColor glassColor(139, 69, 19); 
    p.setBrush(glassColor);
    p.setPen(Qt::NoPen);
    
    // Body
    p.drawRect(-8, -15, 16, 25);
    // Neck
    p.drawRect(-4, -30, 8, 15);
    // Cap
    p.setBrush(Qt::black); // Foil/Cap
    p.drawRect(-5, -32, 10, 4);
    
    // Label with Red Triangle
    p.setBrush(QColor(240, 230, 200)); // Cream label
    p.drawRect(-7, -10, 14, 14);
    
    // Red Triangle
    QPolygonF triangle;
    triangle << QPointF(0, -8) << QPointF(5, 0) << QPointF(-5, 0);
    p.setBrush(Qt::red);
    p.drawPolygon(triangle);
    
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

// SVG-inspired Dolphin Drawing
void JolyAnimation::drawDolphin(QPainter &p, const Dolphin &d) {
    p.save();
    p.translate(d.x, d.y);
    
    // Scale based on distance (strip index)
    double distanceScale = 0.5 + 0.8 * ((double)d.stripIndex / STRIP_COUNT);
    
    // Apply global size settings (normalize path ~45px to DOLPHIN_SIZE)
    double sizeScale = DOLPHIN_SIZE / 45.0;
    
    double totalScale = distanceScale * sizeScale;
    p.scale(totalScale, totalScale);
    
    // Horizontal Flip if moving left
    if (d.vx < 0) {
        p.scale(-1.0, 1.0);
        p.rotate(-d.angle); // Rotate in opposite direction of travel? 
        // d.angle is usually calculated absolute? 
        // In update: angle = atan2(vy, abs(vx)).
        // If vx < 0, angle was inverted. 
        // Let's rely on standard rotation:
        // If we flip X, positive rotation becomes negative visual rotation. 
        // If d.angle represents nose pitch:
        // Up-right: +45. Up-left: +45 (but flipped X) -> looks like Up-left.
        // Let's keep rotate(d.angle) and see.
    } else {
        p.rotate(d.angle);
    }
    
    // White paper look
    p.setBrush(Qt::white);
    p.setPen(Qt::NoPen);
    
    QPainterPath path;
    
    // SVG approximation:
    // Tail start (left)
    path.moveTo(-28, 5); 
    
    // Curved back to dorsal fin
    path.cubicTo(-15, -15, -5, -20, 5, -18); 
    
    // Dorsal fin (curved back)
    path.lineTo(2, -28); // Tip of fin
    path.quadTo(5, -20, 15, -15); // Back of fin down to body
    
    // Back to head/melon
    path.cubicTo(25, -12, 35, -5, 38, 0); // Melon/Forehead
    
    // Beak
    path.lineTo(45, 2); // Tip of beak
    path.lineTo(38, 5); // Bottom of beak
    
    // Throat/Belly
    path.cubicTo(30, 4, 10, 6, -20, 5); // Slimmer belly
    
    // Connection to tail flukes
    path.lineTo(-28, 5);
    
    // Tail Flukes
    path.moveTo(-25, 6);
    // path.lighter(110); // slightly different connector? No, keep simple
    path.lineTo(-35, -2); // Top fluke tip
    path.lineTo(-32, 6);  // Center notch
    path.lineTo(-35, 14); // Bottom fluke tip
    path.lineTo(-25, 7);
    
    p.drawPath(path);
    
    // Pectoral Fin (separate shape)
    QPainterPath flipper;
    flipper.moveTo(15, 5);
    flipper.cubicTo(18, 15, 10, 18, 5, 12);
    flipper.lineTo(15, 5);
    p.drawPath(flipper);
    
    p.restore();
}

void JolyAnimation::drawWhale(QPainter &p, const Whale &w, double yBase, double phase) {
    p.save();
    p.translate(w.x, w.y);
    
    double distanceScale = 0.5 + 0.7 * ((double)w.stripIndex / STRIP_COUNT);
    p.scale(distanceScale, distanceScale);
    
    // Horizontal Flip if drifting left
    if (w.stripIndex < STRIP_COUNT && m_strips[w.stripIndex].currentSpeed < 0) {
        p.scale(-1.0, 1.0);
    }
    
    p.rotate(w.angle); // Tilt with wave

    // White paper look
    p.setBrush(Qt::white);
    p.setPen(Qt::NoPen);
    
    if (w.isBreaching) {
        // ... (Breach disabled in logic, but keeping rendering code)
    } else {
        // Spouting Whale (Surface)
        
        // Whale Body (Simplified rounded shape)
        // Body (just upper back visible)
        p.drawChord(-60, -20, 120, 60, 30 * 16, 120 * 16);
        
        // Spout (Particles) - Fireworks/Fountain Effect
        if (w.timer > 1.0 && w.timer < 5.0) { // Longer duration
            p.setBrush(Qt::white);
            
            double spoutStartTime = 1.0;
            double simTime = w.timer - spoutStartTime;
            
            // Generate particles
            for (int i = 0; i < 40; ++i) { 
                double seed = i * 1.337; // Fixed seed per particle
                
                // Random properties based on seed (using sin/cos as pseudo-random)
                // Boosted height by ~30% (80->110 base)
                double speed = 55.0 + 25.0 * qSin(seed * 123.4);
                double angleEmit = -M_PI_2 + (qCos(seed * 43.2) * 0.4); // Upward cone (-90 deg +/- spread ~25deg)
                double size = 2.0 + 2.0 * qAbs(qSin(seed * 9.9));
                
                double vx = speed * qCos(angleEmit);
                double vy = speed * qSin(angleEmit);
                
                // Continuous fountain emission
                double cycleLen = 1.5; // Particle lives 1.5s
                double timeOffset = i * (cycleLen / 40.0); // Stagger emission
                double t = fmod(simTime + timeOffset, cycleLen);
                
                // Don't show if before start time (wrap around logic handles valid stream)
                double globalT = simTime + timeOffset;
                if (globalT < 0) continue;
                
                // Physics: Projectile motion
                double g = 100.0; // Gravity
                
                double px = 20 + vx * t * 0.4; // Scale horizontal spread slightly
                double py = -10 + vy * t + 0.5 * g * t * t;
                
                // Fade out/shrink at end
                double s = size * (1.0 - t/cycleLen);
                
                if (s > 0 && py > -150) // Clip if too high?
                    p.drawEllipse(QPointF(px, py), s, s);
            }
        }
    }
    
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

  // Draw Parrot (if behind all strips? No, parrot flies over everything)
  // Let's draw parrot last for "foreground" flight.
  
  // Draw strips back-to-front
  for (int i = 0; i < STRIP_COUNT; ++i) {
      double yBase = i * stripHeight;
      double amplitude = globalAmp * rampFactor * m_strips[i].currentAmpFactor;
      double ph = m_strips[i].phase;
      double stripFreq = frequency * (0.9 + 0.2 * qCos(i * 0.5));
      
      // 1. Draw Whales & Dolphins (Before wave, so they can dive BEHIND/INTO it)
      for (const auto &whale : m_whales) {
          if (whale.active && whale.stripIndex == i) {
             drawWhale(p, whale, yBase, ph);
          }
      }
      for (const auto &d : m_dolphins) {
          if (d.active && d.stripIndex == i) {
              drawDolphin(p, d);
          }
      }

      QColor color;
      switch (i % 3) {
          case 0: color = QColor(220, 50, 50); break; 
          case 1: color = QColor(50, 200, 50); break; 
          case 2: color = QColor(50, 50, 220); break; 
      }

      double perspectiveFactor = (double)(i + 1) / STRIP_COUNT;
      // Note: Re-using logic from original implementation for amplitude w/ perspective
      double stripAmp = globalAmp * rampFactor * perspectiveFactor * m_strips[i].currentAmpFactor; 
      // Overwrite simplistic 'amplitude' with correct one
      amplitude = stripAmp; 

      p.setPen(Qt::NoPen);
      p.setBrush(color);
      
      QPolygonF stripPoly;
      // Build polygon for gradient fill
      for (int x = 0; x <= w; x += 5) {
          double angle = x * stripFreq + m_time * m_strips[i].currentSpeed + ph;
          double waveY = amplitude * qSin(angle);
          stripPoly << QPointF(x, yBase + waveY);
      }
      double bottomY = (i + 1.5) * stripHeight + amplitude; 
      stripPoly << QPointF(w, bottomY);
      stripPoly << QPointF(0, bottomY);
      
      QPainterPath path;
      path.addPolygon(stripPoly);
      
      // Gradient fill (original style)
      QLinearGradient gradient(0, yBase, 0, bottomY);
      gradient.setColorAt(0, Qt::transparent);
      gradient.setColorAt(0.5, QColor(0, 0, 0, 50)); 
      gradient.setColorAt(1, QColor(0, 0, 0, 150)); 
      
      p.setBrush(color);
      p.drawPolygon(stripPoly);
      p.fillPath(path, gradient);
      
      // 3. Draw Objects ON/IN this strip
      
      // Bottles
      for (const auto &b : m_bottles) {
          if (b.active && b.stripIndex == i) {
             drawBottle(p, b, yBase, amplitude, stripFreq, ph);
          }
      }
      
      // Oranges
      for (const auto &o : m_oranges) {
          if (o.active && o.stripIndex == i) {
              drawOrange(p, o, yBase, amplitude, stripFreq, ph);
          }
      }

      // Boats
      for (const auto &boat : m_boats) {
          if (boat.active && boat.stripIndex == i) { // Draw if on this strip
              drawBoat(p, boat, yBase, amplitude, stripFreq, ph);
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
