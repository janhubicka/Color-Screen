// HurleyAnimation.cpp
//
// Easter-egg animation celebrating Frank Hurley (1885-1962), the Australian
// pilot and pioneer color-photomontage artist who documented WWI.
//
// Scene: Desert sand dunes with parallax scrolling, a blazing sun, and a
// 2D 8-bit style air battle between paper-cut Allied (white) and Enemy (black)
// WWI biplanes.  The hero Allied plane is slightly larger and the camera
// tracks it.  Shot planes fall with smoke particles; pilots eject with
// parachutes and land on the second-from-bottom dune.

#include "HurleyAnimation.h"
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QtMath>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

// ============================================================================
// ANIMATION CONSTANTS
// ============================================================================

// --- Dune physics ---
constexpr double DUNE_WAVE_FREQ_BG = 0.008;  // Tighter BG dunes → 3D illusion
constexpr double DUNE_WAVE_FREQ_FG = 0.016;  // Wider FG dunes
constexpr double DUNE_BG_SPEED     = 0.15;   // BG scrolls slowly (parallax)
constexpr double DUNE_FG_SPEED     = 0.50;   // FG scrolls faster

// --- Gravity ---
constexpr double GRAVITY           = 720.0;  // More punchy gravity

// --- Physics Revisited ---
constexpr double PLANE_CRUISE_SPEED  = 160.0;
constexpr double PLANE_THRUST_CONST  = 195.0;  // Total thrust (scaled with dampening)
constexpr double PLANE_LIFT_CONST    = 0.028;  // Lift = v_para^2 * K
constexpr double PLANE_SLOWDOWN      = 0.50;  // 50% parasitic energy loss per second
constexpr double PLANE_INDUCED_DRAG_CONST = 0.00018; // Slowdown increment per unit of lift
constexpr double PLANE_MAX_PITCH     = 140.0;
constexpr double PLANE_PITCH_ACCEL   = 600.0;
constexpr double PLANE_STALL_ANGLE   = 40.0;
constexpr double PLANE_CHASE_RANGE   = 850.0;
// --- Plane config ---
constexpr double PLANE_HERO_SCALE        = 1.60;
constexpr double PLANE_NORMAL_SCALE      = 1.0;
constexpr double PLANE_SHOOT_RANGE       = 360.0;
constexpr double PLANE_SHOOT_COOLDOWN    = 1.1;
constexpr double PLANE_HERO_SHOOT_CD     = 0.65;
constexpr int    PLANE_HEALTH_NORMAL     = 2;
constexpr int    PLANE_HEALTH_HERO       = 4;
constexpr double PLANE_JINK_INTERVAL_MIN = 1.0;   // Seconds between AI decisions
constexpr double PLANE_JINK_INTERVAL_MAX = 3.0;
constexpr double PLANE_JINK_ANGLE        = 35.0;  // Max heading offset for a jink (deg)
constexpr double PLANE_FALL_SPIN_SPEED   = 220.0; // deg/s tumble when shot down
constexpr double PLANE_LANDED_DURATION   = 4.0;

// --- Spawn ---
constexpr double ALLIED_SPAWN_COOLDOWN_BASE = 6.0;
constexpr double ENEMY_SPAWN_COOLDOWN_BASE  = 6.0;
constexpr int    MAX_ALLIED                 = 4;
constexpr int    MAX_ENEMY                  = 4;

// --- Bullet ---
constexpr double BULLET_SPEED        = 450.0;
constexpr double BULLET_LIFETIME     = 1.4;

// --- Smoke ---
constexpr double SMOKE_LIFE_INITIAL  = 2.0;
constexpr double SMOKE_EMIT_RATE     = 12.0;

// --- Pilot ---
constexpr double PILOT_EJECT_CHANCE  = 0.40;
constexpr double PARACHUTE_DRAG      = 0.92;

// --- Camera ---
constexpr double CAMERA_LERP         = 3.5;
constexpr double CAMERA_LEAD_LEFT    = 0.333;
constexpr double CAMERA_LEAD_RIGHT   = 0.500;

// ============================================================================
// HELPERS
// ============================================================================

static double randRange(double lo, double hi) {
    return lo + QRandomGenerator::global()->generateDouble() * (hi - lo);
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

HurleyAnimation::HurleyAnimation(QWidget *parent)
    : QWidget(parent),
      m_physicsAccumulator(0.0),
      m_time(0.0),
      m_cameraX(0.0),
      m_cameraVel(0.0),
      m_heroIdx(-1),
      m_alliedSpawnCooldown(1.0),
      m_enemySpawnCooldown(2.5),
      m_debugEnabled(qEnvironmentVariableIsSet("CSDEBUG"))
{
    m_animTimer = new QTimer(this);
    connect(m_animTimer, &QTimer::timeout, this, &HurleyAnimation::updateAnimation);

    // Subtitle sequence honouring Frank Hurley
    m_subtitles.start(
        "Frank Hurley",
        "1885\u20131962",
        "Australian pilot & war photographer \u2014 pioneer of WWI photomontage color"
    );

    initDunes();
    m_smoke.reserve(MAX_SMOKE);
}

HurleyAnimation::~HurleyAnimation() = default;

// ============================================================================
// START / STOP
// ============================================================================

void HurleyAnimation::startAnimation() {
    m_elapsedTimer.start();
    m_animTimer->start(16); // ~60 FPS
}

void HurleyAnimation::stopAnimation() {
    m_animTimer->stop();
}

// ============================================================================
// DUNE INITIALIZATION
// ============================================================================

void HurleyAnimation::initDunes() {
    // Background dunes — tighter, smaller amplitude for 3D perspective
    for (int i = 0; i < DUNE_BG_COUNT; ++i) {
        m_duneBg[i].phase           = i * 1.3 + i * i * 0.07;
        m_duneBg[i].currentSpeed    = DUNE_BG_SPEED * (0.7 + 0.6 * qSin(i * 0.5));
        m_duneBg[i].targetSpeed     = m_duneBg[i].currentSpeed;
        m_duneBg[i].currentAmpFactor = 0.55 + 0.45 * (double)i / DUNE_BG_COUNT;
        m_duneBg[i].targetAmpFactor  = m_duneBg[i].currentAmpFactor;
    }
    // Foreground dunes — wider, larger amplitude
    for (int i = 0; i < DUNE_FG_COUNT; ++i) {
        m_duneFg[i].phase           = i * 0.9 + i * i * 0.04;
        m_duneFg[i].currentSpeed    = DUNE_FG_SPEED * (0.8 + 0.4 * qSin(i * 0.7));
        m_duneFg[i].targetSpeed     = m_duneFg[i].currentSpeed;
        m_duneFg[i].currentAmpFactor = 0.6 + 0.4 * (double)i / DUNE_FG_COUNT;
        m_duneFg[i].targetAmpFactor  = m_duneFg[i].currentAmpFactor;
    }
}

// ============================================================================
// DUNE DYNAMICS UPDATE
// ============================================================================

void HurleyAnimation::updateDunes(double dt) {
    // Occasionally nudge targets for organic movement
    auto nudge = [&](DuneStrip *strips, int count) {
        if (QRandomGenerator::global()->bounded(120) < 1) {
            int idx = QRandomGenerator::global()->bounded(count);
            strips[idx].targetSpeed = DUNE_FG_SPEED * (0.5 + QRandomGenerator::global()->generateDouble());
            strips[idx].targetAmpFactor = 0.5 + 0.5 * QRandomGenerator::global()->generateDouble();
        }
        for (int i = 0; i < count; ++i) {
            strips[i].currentSpeed     += (strips[i].targetSpeed     - strips[i].currentSpeed)     * 0.003;
            strips[i].currentAmpFactor += (strips[i].targetAmpFactor - strips[i].currentAmpFactor) * 0.003;
            strips[i].phase            += strips[i].currentSpeed * dt;
        }
    };
    nudge(m_duneBg, DUNE_BG_COUNT);
    nudge(m_duneFg, DUNE_FG_COUNT);
}

// ============================================================================
// LANDING Y HELPER
// Returns the Y screen coordinate of the surface of the 2nd-from-bottom
// foreground dune at screen X SCREENX, given current camera offset.
// ============================================================================

double HurleyAnimation::duneLandingY(double worldX) const {
    // 2nd from bottom = index DUNE_FG_COUNT - 2
    int i = DUNE_FG_COUNT - 2;
    int h = height();
    double zoneTop = h * 0.45;
    double zoneH   = h * 0.58; // Match drawDuneFgStrips
    double stripH  = zoneH / DUNE_FG_COUNT;
    
    double yBase = zoneTop + i * stripH;
    double perspect = 0.5 + 0.5 * ((double)(i + 1) / DUNE_FG_COUNT);
    double amp      = stripH * 0.65 * m_duneFg[i].currentAmpFactor * perspect;
    double freq     = DUNE_WAVE_FREQ_FG * (0.85 + 0.3 * qCos(i * 0.5));
    
    double angle = worldX * freq + m_duneFg[i].phase;
    return yBase + amp * qSin(angle);
}

double HurleyAnimation::getGroundY(double worldX) {
    return duneLandingY(worldX);
}

// ============================================================================
// SPAWN helpers
// ============================================================================

void HurleyAnimation::spawnHero() {
    // Find free slot
    for (int i = 0; i < MAX_PLANES; ++i) {
        if (!m_planes[i].active) {
            Airplane &a     = m_planes[i];
            a.active        = true;
            a.team          = Team::Allied;
            a.isHero        = true;
            a.health        = PLANE_HEALTH_HERO;
            a.shootCooldown = 0.5;
            a.jinkTimer     = randRange(1.0, 2.0);
            a.spinAngle     = 0;
            a.state         = PlaneState::Flying;
            a.landedTimer   = 0;
            a.pilotEjected  = false;
            a.smokeAccum    = 0.0;
            a.facingRight   = true;
            a.angle         = 0.0;     // Pitch 0 is level flight
            a.targetAngle   = 0.0;
            a.angularVel    = 0.0;
            a.throttle      = 1.0;     // Full power for take-off
            a.takeoffTimer  = 4.5;     // 4.5 seconds of horizontal take-off skimming
            // Start behind the second dune from bottom
            a.x = m_cameraX + width() * 0.25;
            a.y = duneLandingY(a.x) - 4; // Start right on the surface
            
            a.vx = PLANE_CRUISE_SPEED * 0.4; // Start slower for the ground run
            a.vy = 0;
            a.targetAngle = 0.0; // Horizontal take-off run
            a.angle = 0.0;
            m_heroIdx = i;
            return;
        }
    }
}

void HurleyAnimation::spawnAllied() {
    if (m_alliedSpawnCooldown > 0.0) return;

    // Count active allied
    int cnt = 0;
    for (int i = 0; i < MAX_PLANES; ++i)
        if (m_planes[i].active && m_planes[i].team == Team::Allied
            && m_planes[i].state == PlaneState::Flying) cnt++;
    if (cnt >= MAX_ALLIED) return;

    for (int i = 0; i < MAX_PLANES; ++i) {
        if (!m_planes[i].active) {
            Airplane &a    = m_planes[i];
            a.active       = true;
            a.team         = Team::Allied;
            a.isHero       = false;
            a.health       = PLANE_HEALTH_NORMAL;
            a.shootCooldown = randRange(0.2, PLANE_SHOOT_COOLDOWN);
            a.jinkTimer    = randRange(PLANE_JINK_INTERVAL_MIN, PLANE_JINK_INTERVAL_MAX);
            a.spinAngle    = 0;
            a.state        = PlaneState::Flying;
            a.landedTimer  = 0;
            a.pilotEjected = false;
            a.smokeAccum   = 0.0;
            a.facingRight  = true;
            a.angle        = 0.0;   // Level pitch
            a.targetAngle  = 0.0;
            a.angularVel   = 0.0;
            a.throttle     = 0.6;
            // Spawn at left edge of visible world
            a.x  = m_cameraX - 60;
            a.y  = height() * randRange(0.08, 0.38);
            a.vx = PLANE_CRUISE_SPEED * randRange(0.7, 1.3);
            a.vy = 0;
            m_alliedSpawnCooldown = ALLIED_SPAWN_COOLDOWN_BASE * randRange(0.6, 1.4);
            return;
        }
    }
}

void HurleyAnimation::spawnEnemy() {
    if (m_enemySpawnCooldown > 0.0) return;

    int cnt = 0;
    for (int i = 0; i < MAX_PLANES; ++i)
        if (m_planes[i].active && m_planes[i].team == Team::Enemy
            && m_planes[i].state == PlaneState::Flying) cnt++;
    if (cnt >= MAX_ENEMY) return;

    for (int i = 0; i < MAX_PLANES; ++i) {
        if (!m_planes[i].active) {
            Airplane &a    = m_planes[i];
            a.active       = true;
            a.team         = Team::Enemy;
            a.isHero       = false;
            a.health       = PLANE_HEALTH_NORMAL;
            a.shootCooldown = randRange(0.2, PLANE_SHOOT_COOLDOWN);
            a.jinkTimer    = randRange(PLANE_JINK_INTERVAL_MIN, PLANE_JINK_INTERVAL_MAX);
            a.spinAngle    = 0;
            a.state        = PlaneState::Flying;
            a.landedTimer  = 0;
            a.pilotEjected = false;
            a.smokeAccum   = 0.0;
            a.facingRight  = false; // Facing left
            a.angle        = 0.0;   // Level pitch for both Allied/Enemy
            a.targetAngle  = 0.0;
            a.angularVel   = 0.0;
            a.throttle     = 0.6;
            // Spawn at right edge of visible world
            a.x  = m_cameraX + width() + 60;
            a.y  = height() * randRange(0.08, 0.38);
            a.vx = -PLANE_CRUISE_SPEED * randRange(0.7, 1.3);
            a.vy = 0;
            m_enemySpawnCooldown = ENEMY_SPAWN_COOLDOWN_BASE * randRange(0.6, 1.4);
            return;
        }
    }
}

// ============================================================================
// FIRE
// ============================================================================

void HurleyAnimation::fireFromPlane(int planeIdx) {
    const Airplane &shooter = m_planes[planeIdx];

    // Find nearest enemy in range
    double bestDist = PLANE_SHOOT_RANGE;
    int    bestTarget = -1;
    for (int j = 0; j < MAX_PLANES; ++j) {
        if (j == planeIdx) continue;
        const Airplane &t = m_planes[j];
        if (!t.active || t.state != PlaneState::Flying) continue;
        if (t.team == shooter.team) continue;
        double dx = t.x - shooter.x;
        double dy = t.y - shooter.y;
        double dist = qSqrt(dx*dx + dy*dy);
        if (dist < bestDist) {
            bestDist  = dist;
            bestTarget = j;
        }
    }
    if (bestTarget < 0) return;

    // Find free bullet slot
    for (int k = 0; k < MAX_BULLETS; ++k) {
        if (!m_bullets[k].active) {
            Bullet &b = m_bullets[k];
            b.active = true;
            b.team   = shooter.team;
            b.x      = shooter.x;
            b.y      = shooter.y;

            // Aim at predicted position
            const Airplane &tgt = m_planes[bestTarget];
            double T    = bestDist / BULLET_SPEED;
            double predX = tgt.x + tgt.vx * T * 0.6;
            double predY = tgt.y + tgt.vy * T * 0.3;
            double dx    = predX - b.x;
            double dy    = predY - b.y;
            double len   = qSqrt(dx*dx + dy*dy);
            if (len < 1.0) len = 1.0;
            b.vx = BULLET_SPEED * dx / len;
            b.vy = BULLET_SPEED * dy / len;
            break;
        }
    }
}

// ============================================================================
// SMOKE
// ============================================================================

void HurleyAnimation::spawnSmoke(double x, double y, double vx, double vy, int count) {
    if ((int)m_smoke.size() >= MAX_SMOKE) return;
    for (int i = 0; i < count; ++i) {
        if ((int)m_smoke.size() >= MAX_SMOKE) break;
        SmokeParticle sp;
        sp.x    = x + randRange(-6, 6);
        sp.y    = y + randRange(-4, 4);
        sp.vx   = vx * 0.25 + randRange(-30, 30);
        sp.vy   = vy * 0.15 + randRange(-25, 10);
        sp.life = 1.0;
        sp.size = randRange(5.0, 14.0);
        sp.alpha = randRange(0.55, 0.9);
        m_smoke.push_back(sp);
    }
}

// ============================================================================
// PILOT EJECTION
// ============================================================================

void HurleyAnimation::spawnPilot(const Airplane &plane) {
    // Regular ejection: roll the dice
    if (QRandomGenerator::global()->generateDouble() > PILOT_EJECT_CHANCE) return;

    for (int i = 0; i < MAX_PILOTS; ++i) {
        if (!m_pilots[i].active) {
            Pilot &pilot      = m_pilots[i];
            pilot.active      = true;
            pilot.team        = plane.team;
            pilot.x           = plane.x;
            pilot.y           = plane.y - 12;
            pilot.vx          = plane.vx * 0.2;
            pilot.vy          = -180.0;  // Strong eject upward
            pilot.state       = PilotState::Falling;
            pilot.parachuteOpen = false;
            pilot.landedTimer = 0;
            pilot.fallY       = duneLandingY(plane.x);
            return;
        }
    }
}

// Bypasses the random survival chance for emergency situations
void HurleyAnimation::spawnPilotGuaranteed(Airplane &a) {
    if (a.pilotEjected) return;
    a.pilotEjected = true;
    for (int i = 0; i < MAX_PILOTS; ++i) {
        if (!m_pilots[i].active) {
            Pilot &pilot        = m_pilots[i];
            pilot.active        = true;
            pilot.team          = a.team;
            pilot.x             = a.x;
            pilot.y             = a.y - 12;
            pilot.vx            = a.vx * 0.2;
            pilot.vy            = -200.0; // Extra strong eject for emergency
            pilot.state         = PilotState::Falling;
            pilot.parachuteOpen = false;
            pilot.landedTimer   = 0;
            pilot.fallY         = getGroundY(a.x);
            return;
        }
    }
}

// ============================================================================
// UPDATE PLANES
// ============================================================================

void HurleyAnimation::updatePlanes(double dt) {
    double h = height();
    double airYMin = h * 0.04;
    double airYMax = h * 0.65; // Expanded downwards to allow flight behind FG dunes

    for (int i = 0; i < MAX_PLANES; ++i) {
        Airplane &a = m_planes[i];
        if (!a.active) continue;

        if (a.state == PlaneState::Flying) {
            // ------------------------------------------------------------------
            // 1. AI & Control
            // ------------------------------------------------------------------
            double groundY = getGroundY(a.x);

            if (!a.pilotEjected) {
                a.jinkTimer -= dt;
                if (a.jinkTimer <= 0) {
                    // Default: wander and maintain altitude
                    double midY = (airYMin + airYMax) * 0.5;
                    double altFactor = (midY - a.y) / (h * 0.15);
                    double baseA = -altFactor * 40.0; 
                    double wander = randRange(-PLANE_JINK_ANGLE, PLANE_JINK_ANGLE);
                    a.targetAngle = baseA + wander;

                    // Pursuit: Look for nearest enemy
                    double bestDist = PLANE_CHASE_RANGE;
                    int bestTarget = -1;
                    for (int j = 0; j < MAX_PLANES; ++j) {
                        const Airplane &t = m_planes[j];
                        if (!t.active || t.state != PlaneState::Flying || t.team == a.team) continue;
                        double dist = qSqrt(qPow(t.x - a.x, 2) + qPow(t.y - a.y, 2));
                        if (dist < bestDist) {
                            // Check if in front quadrant (roughly)
                            double dx = (t.x - a.x) * (a.facingRight ? 1 : -1);
                            if (dx > 0) { // Target is in front
                                bestDist = dist;
                                bestTarget = j;
                            }
                        }
                    }

                    // Apply Pitch Authority: At low speeds, restrict target angle to maintain lift
                    double speed = qSqrt(a.vx * a.vx + a.vy * a.vy);
                    double speedRel = speed / PLANE_CRUISE_SPEED;
                    // Authority is 1.0 at cruise speed, falls off squared below cruise
                    double authority = qBound(0.15, speedRel * speedRel, 1.2);

                    if (bestTarget >= 0) {
                        const Airplane &t = m_planes[bestTarget];
                        double dx = (t.x - a.x) * (a.facingRight ? 1 : -1);
                        double dy = t.y - a.y;
                        double rawAngle = qRadiansToDegrees(qAtan2(dy, dx));
                        // Clamp pursuit angle based on authority
                        a.targetAngle = rawAngle * authority;
                        a.jinkTimer = 0.5; // Frequent updates while chasing
                    } else {
                        // 10% chance: do a full loop (Only if speed is high enough!)
                        if (authority > 0.8 && QRandomGenerator::global()->bounded(100) < 10) {
                            a.targetAngle = (randRange(0, 1) > 0.5 ? 360.0 : -360.0);
                            a.jinkTimer = 2.5; 
                        } else {
                            // Regular wandering, also scaled by authority
                            double limitedWander = randRange(-PLANE_JINK_ANGLE * authority, 
                                                             PLANE_JINK_ANGLE * authority);
                            a.targetAngle = baseA * authority + limitedWander;
                            a.jinkTimer = randRange(PLANE_JINK_INTERVAL_MIN, PLANE_JINK_INTERVAL_MAX);
                        }
                    }
                }

                // Ceiling Awareness: Every frame penalty if too high
                if (a.y < airYMin + 100.0) {
                    double danger = (airYMin + 100.0 - a.y) / 100.0;
                    // Force target angle to be at least N degrees down
                    double minDown = danger * 60.0; 
                    if (a.targetAngle < minDown) a.targetAngle = minDown;
                }

                // Pitch control: AngularVel accelerates toward targetAngle
                double err = a.targetAngle - a.angle;
                // Shortest path wrap
                while (err >  180) err -= 360;
                while (err < -180) err += 360;

                double wantedAngVel = qBound(-PLANE_MAX_PITCH, err * 5.0, PLANE_MAX_PITCH);
                double acc = (wantedAngVel - a.angularVel) * 10.0;
                acc = qBound(-PLANE_PITCH_ACCEL, acc, PLANE_PITCH_ACCEL);
                a.angularVel += acc * dt;
                a.angle      += a.angularVel * dt;

                // Throttle control: adaptive to help with altitude
                double targetCruise = PLANE_CRUISE_SPEED;
                double midY = (airYMin + airYMax) * 0.5;
                double speed = qSqrt(a.vx * a.vx + a.vy * a.vy);

                // 1. Predictive Panic Recovery: If imminent crash, pull up and throttle up
                double predictedY = a.y + a.vy * 1.5; // Look ahead 1.5 seconds
                bool panic = (predictedY > groundY) && (a.takeoffTimer <= 0);

                if (panic) {
                    targetCruise = 260.0; // Panic speed!
                    a.targetAngle = -15.0; // Force a horizontal-to-climb attitude
                } else if (a.y > midY + 50.0) {
                    // Regular recovery logic
                    targetCruise += 120.0;
                    a.targetAngle = a.targetAngle * 0.65; 
                }

                // 2. Speed Governor: If too fast, kill engine to stabilize
                if (speed > 240.0 && !panic) {
                    targetCruise = 100.0;
                }

                // Standard altitude-based throttle bias
                if (a.y > midY + 40.0) targetCruise += 40.0;
                if (a.y < midY - 40.0) targetCruise -= 40.0;

                double speedErr = targetCruise - speed;
                double responseK = panic ? 0.15 : 0.02; // React much faster if crashing!
                a.throttle += speedErr * responseK * dt; 
                a.throttle = qBound(0.2, a.throttle, 1.0);
                
                // Advance propeller phase based on engine speed
                a.propPhase += dt * (25.0 + a.throttle * 90.0);

                // Take-off logic: keep nose horizontal and handle skimming
                if (a.takeoffTimer > 0) {
                    a.takeoffTimer -= dt;
                    a.targetAngle = 0; // Forced horizontal run
                }
            }

            // ------------------------------------------------------------------
            // 2. Physics Simulation: Lateral Stability & Aerodynamics
            // ------------------------------------------------------------------
            // ------------------------------------------------------------------
            // 2. Physics Simulation: Vector-Based Inertia & Uplift
            // ------------------------------------------------------------------
            double rad = qDegreesToRadians(a.angle);
            double cosA = cos(rad);
            double sinA = sin(rad);

            double fxHead = a.facingRight ? 1.0 : -1.0;
            // The sprite transformation is Rotate(A) * Scale(fxHead, 1)
            // Forward (Global) = R(A) * S(fxHead, 1) * (1, 0)  = (fxHead*cosA, fxHead*sinA)
            // Up (Global)      = R(A) * S(fxHead, 1) * (0, -1) = (sinA, -cosA)
            a.phys.fwdX = fxHead * cosA;
            a.phys.fwdY = fxHead * sinA;
            a.phys.upX  = sinA;
            a.phys.upY  = -cosA;

            // Inertia only in the direction of airplane nose
            double v_para = std::max (a.vx * a.phys.fwdX + a.vy * a.phys.fwdY, 0.0);

            // Thrust and Lift vectors (Quadratic Lift)
            double thrust = a.throttle * PLANE_THRUST_CONST;
            double lift   = (v_para * v_para) * PLANE_LIFT_CONST;

            a.phys.thrustX = a.phys.fwdX * thrust;
            a.phys.thrustY = a.phys.fwdY * thrust;
            a.phys.liftX   = a.phys.upX * lift;
            a.phys.liftY   = a.phys.upY * lift;

            // Movement is combination of inertia * slowdown + forces
            double dvx = a.phys.thrustX + a.phys.liftX;
            double dvy = a.phys.thrustY + a.phys.liftY + GRAVITY;

            // V_new = (V_old + Force * dt)
            a.vx += dvx * dt;
            a.vy += dvy * dt;

            // --- Dynamic Slowdown (Parasitic + Induced Drag) ---
            // 1. Parasitic: Increases with speed
            double currentSpeed = qSqrt(a.vx * a.vx + a.vy * a.vy);
            double dynamicSlowdown = PLANE_SLOWDOWN * (currentSpeed / PLANE_CRUISE_SPEED);
            
            // 2. Induced: Increases with lift (more maneuvering = more bleed)
            double inducedSlowdown = lift * PLANE_INDUCED_DRAG_CONST;
            double totalSlowdown = dynamicSlowdown + inducedSlowdown;
            
            // Apply exponential decay using the user's stable pow() formula.
            // We cap the coefficient to prevent numerical instability at extreme speeds.
            double dragFactor = pow(1.0 - qMin(0.99, totalSlowdown), dt);
            a.vx *= dragFactor;
            a.vy *= dragFactor;

            // --- Directional Stability (Vaning Effect / Carving) ---
            // We strictly dampen the "sideways" velocity (sideslip) relative to the nose.
            // This forces the aircraft to track its longitudinal axis like a vane.
            double n_para = a.vx * a.phys.fwdX + a.vy * a.phys.fwdY;
            double n_perp = a.vx * a.phys.upX  + a.vy * a.phys.upY;
            
#if 1
            // Kill 95% of lateral speed per second
            n_perp *= pow (1.0 - 0.95, dt); 
            
            a.vx = n_para * a.phys.fwdX + n_perp * a.phys.upX;
            a.vy = n_para * a.phys.fwdY + n_perp * a.phys.upY;
#endif


            // Integrate Position
            a.x  += a.vx * dt;
            a.y  += a.vy * dt;


            // No hard bounce at top anymore, handled by AI awareness
            // if (a.y < airYMin) { a.y = airYMin; a.vy = qAbs(a.vy) * 0.4; }
            
            // Ground crash check

            // Predictive Emergency Catapult: Bailing out just before a high-speed impact
            if (!a.pilotEjected && a.vy > 50.0) {
                if (a.y + a.vy * (dt * 1.5) >= groundY) { // Look ahead slightly
                    spawnPilotGuaranteed(a);
                }
            }

            if (a.y >= groundY) {
                if (a.takeoffTimer > 0) {
                    // Ground-skimming immunity: slide along the sand
                    a.y = groundY;
                    if (a.vy > 0) a.vy = 0;
                } else {
                    // High-speed collision or failed take-off
                    a.y = groundY;
                    a.state = PlaneState::Falling;
                    a.fallY = groundY;
                    a.health = 0;
                    // Immediate explosion puff
                    spawnSmoke(a.x, a.y, 0, 0, 15);
                }
            }

            // ---- Shooting ----
            if (!a.pilotEjected) {
                a.shootCooldown -= dt;
                if (a.shootCooldown <= 0) {
                    double cd = a.isHero ? PLANE_HERO_SHOOT_CD : PLANE_SHOOT_COOLDOWN;
                    fireFromPlane(i);
                    a.shootCooldown = cd * randRange(0.7, 1.3);
                }
            }

            // ---- Despawn ----
            double screenX = toScreenX(a.x);
            if (screenX < -400 || screenX > width() + 400) {
                if (!a.isHero) a.active = false;
            }

        } else if (a.state == PlaneState::Falling) {
            // Tumble and fall (simple gravity + drag)
            a.vy += GRAVITY * dt;
            a.vx *= (1.0 - 0.5 * dt);
            a.x  += a.vx * dt;
            a.y  += a.vy * dt;
            a.spinAngle += PLANE_FALL_SPIN_SPEED * dt;
            a.angle = a.spinAngle;

            a.smokeAccum += dt * SMOKE_EMIT_RATE;
            while (a.smokeAccum >= 1.0) {
                spawnSmoke(a.x, a.y, a.vx, a.vy, 1);
                a.smokeAccum -= 1.0;
            }

            if (a.y >= a.fallY) {
                a.y = a.fallY; a.vy = 0; a.vx = 0; a.state = PlaneState::Landed;
                if (!a.pilotEjected) { a.pilotEjected = true; spawnPilot(a); }
            }
        } else if (a.state == PlaneState::Landed) {
            a.landedTimer += dt;
            if (a.landedTimer > PLANE_LANDED_DURATION) {
                a.active = false;
                if (i == m_heroIdx) { m_heroIdx = -1; spawnHero(); }
            }
        }
    }
}

// ============================================================================
// UPDATE BULLETS
// ============================================================================

void HurleyAnimation::updateBullets(double dt) {
    for (int k = 0; k < MAX_BULLETS; ++k) {
        Bullet &b = m_bullets[k];
        if (!b.active) continue;

        b.x += b.vx * dt;
        b.y += b.vy * dt;

        // Simple lifetime check (by distance or out of screen)
        double sx = toScreenX(b.x);
        if (sx < -50 || sx > width() + 50 || b.y < -50 || b.y > height() + 50) {
            b.active = false;
            continue;
        }

        // Collision with enemy planes
        for (int i = 0; i < MAX_PLANES; ++i) {
            Airplane &a = m_planes[i];
            if (!a.active || a.state != PlaneState::Flying) continue;
            if (a.team == b.team) continue;
            double dx = b.x - a.x;
            double dy = b.y - a.y;
            double r  = a.isHero ? 22.0 : 14.0;
            if (dx*dx + dy*dy < r*r) {
                // Hit!
                spawnSmoke(a.x, a.y, a.vx, a.vy, 4);
                a.health--;
                b.active = false;

                if (a.health <= 0) {
                    a.state = PlaneState::Falling;
                    a.fallY = duneLandingY(a.x);
                    // Spawn pilot before state change
                    if (!a.pilotEjected) {
                        a.pilotEjected = true;
                        spawnPilot(a);
                    }
                }
                break;
            }
        }
    }
}

// ============================================================================
// UPDATE SMOKE
// ============================================================================

void HurleyAnimation::updateSmoke(double dt) {
    for (auto &sp : m_smoke) {
        sp.x    += sp.vx * dt;
        sp.y    += sp.vy * dt;
        sp.vy   += 12.0 * dt;  // Slight upward drift cancels gravity a bit
        sp.size += 8.0 * dt;   // Expanding puff
        sp.life -= dt / SMOKE_LIFE_INITIAL;
    }
    m_smoke.erase(
        std::remove_if(m_smoke.begin(), m_smoke.end(),
                       [](const SmokeParticle &sp){ return sp.life <= 0; }),
        m_smoke.end());
}



void HurleyAnimation::updatePilots(double dt) {
    for (int i = 0; i < MAX_PILOTS; ++i) {
        Pilot &pilot = m_pilots[i];
        if (!pilot.active) continue;

        if (pilot.state == PilotState::Falling) {
            // Emergency proximity trigger or vertical fall trigger
            bool tooLow = (pilot.fallY - pilot.y) < 64.0;
            if (!pilot.parachuteOpen && (pilot.vy > 20.0 || tooLow)) {
                pilot.parachuteOpen = true;
            }
            pilot.vy += GRAVITY * dt;
            if (pilot.parachuteOpen) {
                pilot.vy *= (1.0 - (1.0 - PARACHUTE_DRAG) * dt * 60.0);
                pilot.vy = qMin(pilot.vy, 60.0);
                pilot.vx *= (1.0 - 1.5 * dt);
            }
            pilot.x += pilot.vx * dt;
            pilot.y += pilot.vy * dt;

            double curGroundY = getGroundY(pilot.x) - 1.0;
            if (pilot.y >= curGroundY) {
                pilot.y   = curGroundY;
                pilot.vy  = 0;
                pilot.vx  = 0;
                pilot.state = PilotState::Landed;
                pilot.landedTimer = 0.0;
            }
        } else {
            pilot.landedTimer += dt;
            if (pilot.landedTimer > 6.0) {
                pilot.active = false;
            }
        }
    }
}


// ============================================================================
// CAMERA UPDATE
// ============================================================================

void HurleyAnimation::updateCamera(double dt) {
    if (m_heroIdx < 0 || !m_planes[m_heroIdx].active) return;
    const Airplane &hero = m_planes[m_heroIdx];

    double heroScreenX = toScreenX(hero.x);
    double w = width();

    // Target camera so hero stays between 1/4 and 3/4
    double targetCamX = m_cameraX;

    if (heroScreenX < w * CAMERA_LEAD_LEFT) {
        targetCamX = hero.x - w * CAMERA_LEAD_LEFT;
    } else if (heroScreenX > w * CAMERA_LEAD_RIGHT) {
        targetCamX = hero.x - w * CAMERA_LEAD_RIGHT;
    }

    // Smooth pan via lerp
    m_cameraX += (targetCamX - m_cameraX) * CAMERA_LERP * dt;
}

// ============================================================================
// STEP ANIMATION
// ============================================================================

void HurleyAnimation::stepAnimation(double dt) {
    m_time += dt;

    updateDunes(dt);

    // Spawn cooldowns
    m_alliedSpawnCooldown -= dt;
    m_enemySpawnCooldown  -= dt;

    // Ensure hero exists
    if (m_heroIdx < 0) spawnHero();

    // Spawn wingmen after 3 seconds
    if (m_time > 3.0) {
        spawnAllied();
        spawnEnemy();
    }

    updatePlanes(dt);
    updateBullets(dt);
    updateSmoke(dt);
    updatePilots(dt);
    updateCamera(dt);

    m_subtitles.update(dt);
}

// ============================================================================
// UPDATE SLOT
// ============================================================================

void HurleyAnimation::updateAnimation() {
    double dt = 0.016;
    if (m_elapsedTimer.isValid()) {
        double elapsed = m_elapsedTimer.restart() / 1000.0;
        if (elapsed > 0.1) elapsed = 0.1;
        m_physicsAccumulator += elapsed;
    }
    while (m_physicsAccumulator >= dt) {
        stepAnimation(dt);
        m_physicsAccumulator -= dt;
    }
    update();
}

// ============================================================================
// DRAWING: SKY
// ============================================================================

void HurleyAnimation::drawSky(QPainter &p) {
    int w = width(), h = height();
    // Warm desert sky: deep periwinkle blue at top → amber/peach near horizon
    QLinearGradient sky(0, 0, 0, h * 0.5);
    sky.setColorAt(0.0, QColor(70,  95, 160));
    sky.setColorAt(0.4, QColor(130, 160, 210));
    sky.setColorAt(0.75, QColor(220, 195, 140));
    sky.setColorAt(1.0,  QColor(235, 175, 90));
    p.fillRect(0, 0, w, (int)(h * 0.55), sky);
}

// ============================================================================
// DRAWING: SUN
// ============================================================================

void HurleyAnimation::drawSun(QPainter &p) {
    // Blazing hot sun in upper-right of sky
    double sunX = width() * 0.80;
    double sunY = height() * 0.10;
    double sunR = qMin(width(), height()) * 0.07;

    // --- 1. Rays (Behind Sun) ---
    p.save();
    p.translate(sunX, sunY);
    p.rotate(m_time * 20.0); // Slow rotation
    p.setBrush(QColor(255, 210, 50, 180));
    p.setPen(Qt::NoPen);
    int numRays = 12;
    for (int i = 0; i < numRays; ++i) {
        QPolygonF ray;
        double rInner = sunR * 0.9;
        double rOuter = sunR * 1.6;
        double angleStep = (M_PI * 2.0) / numRays;
        double curA = i * angleStep;
        
        // Triangular ray
        ray << QPointF(rInner * qCos(curA - 0.1), rInner * qSin(curA - 0.1));
        ray << QPointF(rOuter * qCos(curA),       rOuter * qSin(curA));
        ray << QPointF(rInner * qCos(curA + 0.1), rInner * qSin(curA + 0.1));
        p.drawPolygon(ray);
    }
    p.restore();

    // --- 2. Sun Glow ---
    double shimmer = 0.5 + 0.5 * qSin(m_time * 3.7);
    for (int ring = 4; ring >= 1; --ring) {
        double rr = sunR * (1.0 + ring * 0.25 + shimmer * 0.08 * ring);
        int alpha = (int)(30.0 / ring * (0.6 + shimmer * 0.4));
        QRadialGradient corona(sunX, sunY, rr);
        corona.setColorAt(0.6, QColor(255, 230, 100, alpha));
        corona.setColorAt(1.0, QColor(255, 180, 50, 0));
        p.setBrush(corona);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(sunX, sunY), rr, rr);
    }

    // --- 3. Core Disc ---
    QRadialGradient sunGrad(sunX - sunR*0.25, sunY - sunR*0.25, sunR * 1.1);
    sunGrad.setColorAt(0.0, QColor(255, 255, 210));
    sunGrad.setColorAt(0.5, QColor(255, 235, 80));
    sunGrad.setColorAt(1.0, QColor(255, 180, 20));
    p.setBrush(sunGrad);
    p.drawEllipse(QPointF(sunX, sunY), sunR, sunR);

    // --- 4. Face (Eyes tracking Hero) ---
    bool heroSurprised = false;
    double hx = width() * 0.5; // Default focus
    double hy = height() * 0.3;
    if (m_heroIdx >= 0 && m_planes[m_heroIdx].active) {
        const auto &hero = m_planes[m_heroIdx];
        hx = toScreenX(hero.x);
        hy = hero.y;
        if (hero.health < PLANE_HEALTH_HERO || hero.state != PlaneState::Flying) {
            heroSurprised = true;
        }
    }

    // Normalized look vector from sun to hero
    double dx = hx - sunX;
    double dy = hy - sunY;
    double dist = qMax(1.0, qSqrt(dx*dx + dy*dy));
    double ux = dx / dist;
    double uy = dy / dist;

    // Draw Eyes
    p.setPen(QPen(Qt::black, 0.8));
    p.setBrush(Qt::white);
    for (int i : {-1, 1}) {
        double ex = sunX + i * sunR * 0.35;
        double ey = sunY - sunR * 0.15;
        double eyeR = sunR * 0.18;
        p.drawEllipse(QPointF(ex, ey), eyeR, eyeR);
        
        // Pupil
        p.setBrush(Qt::black);
        double px = ex + ux * eyeR * 0.5;
        double py = ey + uy * eyeR * 0.5;
        p.drawEllipse(QPointF(px, py), eyeR * 0.4, eyeR * 0.4);
        p.setBrush(Qt::white);
    }

    // Draw Mouth
    p.setPen(QPen(QColor(100, 50, 0), 2.0, Qt::SolidLine, Qt::RoundCap));
    if (heroSurprised) {
        // Surprised 'O'
        p.setBrush(QColor(150, 40, 0));
        p.drawEllipse(QPointF(sunX, sunY + sunR * 0.4), sunR * 0.15, sunR * 0.22);
    } else {
        // Happy Smile
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(sunX - sunR*0.3, sunY + sunR*0.05, sunR*0.6, sunR*0.6), 210*16, 120*16);
    }
}

// ============================================================================
// DRAWING: BACKGROUND DUNES (parallax, upper portion, slow scroll)
// The BG dunes are drawn in the upper ~55% of the viewport but pressed into
// the lower part of that zone to look like distant dunes.
// Parallax: they shift at 0.3x the camera movement.
// ============================================================================

void HurleyAnimation::drawDuneBgStrips(QPainter &p) {
    int w = width(), h = height();
    double zoneTop = h * 0.28;        // BG dunes start near horizon
    double zoneH   = h * 0.22;        // How tall the BG zone is
    double stripH  = zoneH / DUNE_BG_COUNT;

    // BG parallax offset: 30% of camera X
    double bgOffsetX = m_cameraX * 0.3;

    for (int i = 0; i < DUNE_BG_COUNT; ++i) {
        double yBase = zoneTop + i * stripH;

        // Perspective: strips closer to bottom are slightly larger
        double perspect = 0.35 + 0.65 * ((double)(i + 1) / DUNE_BG_COUNT);
        double amp      = stripH * 0.55 * m_duneBg[i].currentAmpFactor * perspect;
        double freq     = DUNE_WAVE_FREQ_BG * (0.85 + 0.3 * qCos(i * 0.6));

        // Sandy gold gradient per strip: lighter at top, richer at bottom
        double t = (double)i / (DUNE_BG_COUNT - 1);
        QColor sandTop(220 - (int)(t*20), 195 - (int)(t*40), 100 + (int)(t*30));
        QColor sandBot(195 - (int)(t*20), 165 - (int)(t*30), 70 + (int)(t*20));

        QPolygonF poly;
        for (int sx = 0; sx <= w; sx += 4) {
            double worldX = sx + bgOffsetX;
            double angle  = worldX * freq + m_duneBg[i].phase;
            double waveY  = amp * qSin(angle);
            poly << QPointF(sx, yBase + waveY);
        }
        double bottomY = yBase + stripH * 1.2 + amp;
        poly << QPointF(w, bottomY) << QPointF(0, bottomY);

        // Fill with sandy gradient
        QLinearGradient grad(0, yBase, 0, bottomY);
        grad.setColorAt(0.0, sandTop);
        grad.setColorAt(1.0, sandBot);
        p.setBrush(grad);
        p.setPen(Qt::NoPen);
        p.drawPolygon(poly);

        // Subtle shadow at crest
        QPainterPath path;
        path.addPolygon(poly);
        QLinearGradient shadow(0, yBase, 0, bottomY);
        shadow.setColorAt(0.0, QColor(0, 0, 0, 0));
        shadow.setColorAt(0.3, QColor(0, 0, 0, 20));
        shadow.setColorAt(1.0, QColor(80, 50, 10, 80));
        p.fillPath(path, shadow);
    }
}

// ============================================================================
// DRAWING: FOREGROUND DUNES (fast scroll, lower portion)
// ============================================================================

void HurleyAnimation::drawDuneFgStrips(QPainter &p, int startIdx, int endIdx) {
    int w = width(), h = height();
    double zoneTop = h * 0.45;
    double zoneH   = h * 0.58;
    double stripH  = zoneH / DUNE_FG_COUNT;

    // FG dunes move at full camera speed
    double fgOffsetX = m_cameraX;

    for (int i = startIdx; i <= endIdx && i < DUNE_FG_COUNT; ++i) {
        double yBase = zoneTop + i * stripH;

        double perspect = 0.5 + 0.5 * ((double)(i + 1) / DUNE_FG_COUNT);
        double amp      = stripH * 0.65 * m_duneFg[i].currentAmpFactor * perspect;
        double freq     = DUNE_WAVE_FREQ_FG * (0.85 + 0.3 * qCos(i * 0.5));

        double t = (double)i / (DUNE_FG_COUNT - 1);
        // FG dunes are richer, more saturated gold/orange
        QColor sandTop(230 - (int)(t*15), 180 - (int)(t*30), 80 + (int)(t*20));
        QColor sandBot(190 - (int)(t*15), 145 - (int)(t*25), 45 + (int)(t*15));

        QPolygonF poly;
        for (int sx = 0; sx <= w; sx += 4) {
            double worldX = sx + fgOffsetX;
            double angle  = worldX * freq + m_duneFg[i].phase;
            double waveY  = amp * qSin(angle);
            poly << QPointF(sx, yBase + waveY);
        }
        double bottomY = yBase + stripH * 1.3 + amp;
        poly << QPointF(w, bottomY) << QPointF(0, bottomY);

        QLinearGradient grad(0, yBase, 0, bottomY);
        grad.setColorAt(0.0, sandTop);
        grad.setColorAt(1.0, sandBot);
        p.setBrush(grad);
        p.setPen(Qt::NoPen);
        p.drawPolygon(poly);

        QPainterPath path;
        path.addPolygon(poly);
        QLinearGradient shadow(0, yBase, 0, bottomY);
        shadow.setColorAt(0.0, QColor(0, 0, 0, 0));
        shadow.setColorAt(0.4, QColor(0, 0, 0, 25));
        shadow.setColorAt(1.0, QColor(80, 45, 0, 100));
        p.fillPath(path, shadow);
    }
}

// ============================================================================
// DRAWING: PLANE
// Paper-cut WWI biplane silhouette using QPainterPath
// ============================================================================

void HurleyAnimation::drawPlane(QPainter &p, const Airplane &plane) {
    double sx = toScreenX(plane.x);
    double sy = plane.y;

    p.save();
    p.translate(sx, sy);

    // Rotate by the actual flight angle so the nose always points where
    // the plane is going (including loops and inverted flight).
    // angle=0 → facing right, angle=180 → facing left, etc.
    p.rotate(plane.angle);

    // Scale: hero is bigger, mirrored if facing left
    double faceScale = plane.facingRight ? 1.0 : -1.0;
    double scale = plane.isHero ? PLANE_HERO_SCALE : PLANE_NORMAL_SCALE;
    p.scale(faceScale * scale, scale);

    // Colors
    QColor bodyColor    = (plane.team == Team::Allied) ? Qt::white           : Qt::black;
    QColor outlineColor = (plane.team == Team::Allied) ? QColor(80, 80, 80)  : QColor(50, 50, 50);
    QColor accentColor  = (plane.team == Team::Allied) ? QColor(200,200,200) : QColor(70, 70, 70);

    p.setBrush(bodyColor);
    p.setPen(QPen(outlineColor, 0.7));

    // =====================================================================
    // SOPWITH CAMEL silhouette (side view, nose pointing right at angle=0)
    //
    // Key features of the Camel:
    //  - Short stubby fuselage, rounded nose cowling
    //  - Distinctive "hump" forward of cockpit (twin Vickers guns)
    //  - Upper wing slightly ahead of lower wing (stagger)
    //  - Short rounded tail
    // =====================================================================

    // -- Fuselage (Path 3) --
    QPainterPath fuse;
    fuse.moveTo( 39.88,  -3.11);
    fuse.lineTo( 39.80,   5.00);
    fuse.lineTo( 18.06,   6.22);
    fuse.lineTo(-19.66,   3.67);
    fuse.lineTo(-22.52,   6.45);
    fuse.lineTo(-25.39,   8.32);
    fuse.lineTo(-28.75,   9.82);
    fuse.lineTo(-33.40,  10.97);
    fuse.lineTo(-37.42,  10.62);
    fuse.lineTo(-39.43,   8.76);
    fuse.lineTo(-39.82,   5.01);
    fuse.lineTo(-39.61,   2.22);
    fuse.lineTo(-37.73,  -1.17);
    fuse.lineTo(-30.07,  -3.01);
    fuse.lineTo(-22.70,  -2.40);
    fuse.lineTo(-19.48,  -1.57);
    fuse.lineTo(  2.14,  -5.65);
    fuse.lineTo( 23.36,  -6.24);
    fuse.lineTo( 24.62,  -5.92);
    fuse.closeSubpath();
    p.drawPath(fuse);

    // -- Top Wing (Path 5) --
    QPainterPath topWing;
    topWing.moveTo( 24.05,  -5.35);
    topWing.lineTo( 24.19,  -8.90);
    topWing.lineTo(  6.37,  -8.65);
    topWing.lineTo(  3.00,  -5.21);
    topWing.lineTo( 10.54,  -4.01);
    topWing.lineTo( 18.51,  -4.37);
    topWing.closeSubpath();
    p.drawPath(topWing);

    // -- Bottom Wing (Path 4) --
    QPainterPath bottomWing;
    bottomWing.moveTo( 29.97,  13.63);
    bottomWing.lineTo( 29.16,   7.12);
    bottomWing.lineTo( 12.38,   8.65);
    bottomWing.lineTo(  9.64,  14.20);
    bottomWing.lineTo( 17.65,  15.19);
    bottomWing.lineTo( 25.55,  14.54);
    bottomWing.closeSubpath();
    p.drawPath(bottomWing);

    // -- Struts (Paths 11 & 12) --
    p.setPen(QPen(outlineColor, 0.7));
    QPainterPath struts;
    struts.moveTo( 26.72,   9.79);
    struts.lineTo( 22.18,  -5.02);
    struts.lineTo( 19.93,  -4.62);
    struts.lineTo( 24.92,  10.04);
    struts.moveTo( 18.29,  11.92);
    struts.lineTo( 12.36,  -4.09);
    struts.lineTo( 10.91,  -4.03);
    struts.lineTo( 17.10,  12.11);
    p.drawPath(struts);

    // -- Propeller (Yellow Dot Anchor 41.38, -1.81) --
    double pLen = 15.0 * qSin(plane.propPhase);
    double pLen2 = 15.0 * qSin(plane.propPhase + M_PI * 0.5);
    p.setPen(QPen(QColor(100, 100, 100, 150), 1.2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(41.4, -1.8 - pLen), QPointF(41.4, -1.8 + pLen));
    if (plane.throttle > 0.4) {
        p.setPen(QPen(QColor(120, 120, 120, 60), 2.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(41.4, -1.8 - pLen2), QPointF(41.4, -1.8 + pLen2));
    }

    // -- Pilot (Green Circle, conditional) --
    if (!plane.pilotEjected) {
        p.setBrush(Qt::green);
        p.setPen(QPen(Qt::black, 0.5));
        p.drawEllipse(QPointF(10, -5), 4, 4); // Placed in cockpit area
    }

    // -- Insignia (SVG Centered at -4.42, 0.87) --
    p.setPen(Qt::NoPen);
    double rx = -4.4, ry = 0.9;
    if (plane.team == Team::Allied) {
        p.setBrush(QColor(0, 0, 180));
        p.drawEllipse(QPointF(rx, ry), 6, 6);
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(rx, ry), 4, 4);
        p.setBrush(QColor(200, 0, 0));
        p.drawEllipse(QPointF(rx, ry), 2, 2);
    } else {
        p.setBrush(Qt::black);
        p.setPen(QPen(Qt::white, 0.8));
        p.drawRect(QRectF(rx - 5, ry - 1.5, 10, 3));
        p.drawRect(QRectF(rx - 1.5, ry - 5, 3, 10));
    }

    p.restore();

    // --- Physics Debug Visualization ---
    if (m_debugEnabled) {
        p.save();
        p.translate(sx, sy);
        p.setRenderHint(QPainter::Antialiasing, false);

        // 1. Thrust (Red) - Along the nose
        p.setPen(QPen(Qt::red, 2.0));
        p.drawLine(QPointF(0, 0), QPointF(plane.phys.thrustX * 0.5, plane.phys.thrustY * 0.5));

        // 2. Lift (Green) - Perpendicular to nose
        p.setPen(QPen(Qt::green, 2.0));
        p.drawLine(QPointF(0, 0), QPointF(plane.phys.liftX * 0.5, plane.phys.liftY * 0.5));

        // 3. Gravity (Yellow) - Always down
        p.setPen(QPen(Qt::yellow, 1.5));
        p.drawLine(QPointF(0, 0), QPointF(0, GRAVITY * 0.05));

        // 4. Velocity (Cyan) - Current vector
        p.setPen(QPen(Qt::cyan, 2.0));
        p.drawLine(QPointF(0, 0), QPointF(plane.vx * 0.2, plane.vy * 0.2));

        p.restore();
    }
}

// ============================================================================
// DRAWING: BULLETS (tracer lines)
// ============================================================================

void HurleyAnimation::drawBullets(QPainter &p) {
    for (int k = 0; k < MAX_BULLETS; ++k) {
        const Bullet &b = m_bullets[k];
        if (!b.active) continue;

        double sx = toScreenX(b.x);
        double sy = b.y;

        // Short tracer line
        double len = 12.0;
        double speed = qSqrt(b.vx*b.vx + b.vy*b.vy);
        double tailX = sx - b.vx / speed * len;
        double tailY = sy - b.vy / speed * len;

        QColor tracerColor = (b.team == Team::Allied) ? QColor(255, 220, 80) : QColor(255, 80, 80);
        p.setPen(QPen(tracerColor, 2.0));
        p.drawLine(QPointF(sx, sy), QPointF(tailX, tailY));

        // Bright tip dot
        p.setBrush(Qt::white);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(sx, sy), 1.5, 1.5);
    }
}

// ============================================================================
// DRAWING: SMOKE
// ============================================================================

void HurleyAnimation::drawSmoke(QPainter &p) {
    p.setPen(Qt::NoPen);
    for (const auto &sp : m_smoke) {
        double sx = toScreenX(sp.x);
        int alpha = (int)(sp.alpha * sp.life * 200);
        if (alpha < 5) continue;
        int gray = 60 + (int)((1.0 - sp.life) * 120);
        p.setBrush(QColor(gray, gray, gray, alpha));
        p.drawEllipse(QPointF(sx, sp.y), sp.size, sp.size);
    }
}

// ============================================================================
// DRAWING: PILOT (stick figure + parachute)
// ============================================================================

void HurleyAnimation::drawPilot(QPainter &p, const Pilot &pilot) {
    double sx = toScreenX(pilot.x);
    double sy = pilot.y;

    p.save();
    p.translate(sx, sy);

    QColor bodyCol = (pilot.team == Team::Allied) ? Qt::white : Qt::black;
    QColor outlineCol = (pilot.team == Team::Allied) ? Qt::darkGray : Qt::white;

    // Parachute canopy (semicircle)
    if (pilot.parachuteOpen) {
        double cR = 22.0;
        p.setBrush(QColor(240, 240, 240, 220));
        p.setPen(QPen(QColor(180,150,80), 1));
        // Draw semicircle above pilot
        p.drawChord(QRectF(-cR, -cR * 2 - 14, cR*2, cR*2), 0, 180*16);

        // Rigging lines
        p.setPen(QPen(QColor(160,130,70), 0.8));
        for (int li = -2; li <= 2; li++) {
            double rx = li * cR * 0.45;
            p.drawLine(QPointF(rx, -14), QPointF(0, 0));
        }
    }

    // Stick figure body
    p.setPen(QPen(outlineCol, 1.5));
    p.setBrush(bodyCol);

    // Head
    p.drawEllipse(QPointF(0, -4), 3.5, 3.5);
    // Body
    p.drawLine(QPointF(0, -0.5), QPointF(0, 8));
    // Arms
    p.drawLine(QPointF(-5, 3), QPointF(5, 3));
    // Legs
    p.drawLine(QPointF(0, 8), QPointF(-4, 14));
    p.drawLine(QPointF(0, 8), QPointF( 4, 14));

    // Helmet / goggles
    p.setBrush(QColor(100, 80, 50));
    p.setPen(Qt::NoPen);
    p.drawChord(QRectF(-3.5, -8, 7, 5), 0, 180*16);

    p.restore();
}

// ============================================================================
// PAINT EVENT
// ============================================================================

void HurleyAnimation::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 1. Sky gradient
    drawSky(p);

    // 2. Sun
    drawSun(p);

    // 3. Background dunes (parallax, slow)
    drawDuneBgStrips(p);

    // 4. Foreground dunes: far-middle (indices 0..3)
    // DUNE_FG_COUNT is 6. Index 5 is bottom, 4 is second-from-bottom.
    // We draw 0, 1, 2, 3 here.
    drawDuneFgStrips(p, 0, DUNE_FG_COUNT - 3);

    // 5. Interactive Layer (Smoke, Planes, Bullets, Pilots)
    // These now happen "behind" the second dune from the bottom.
    drawSmoke(p);

    for (int i = 0; i < MAX_PLANES; ++i) {
        if (m_planes[i].active)
            drawPlane(p, m_planes[i]);
    }

    drawBullets(p);

    // Draw Falling pilots in this middle layer too
    for (int i = 0; i < MAX_PILOTS; ++i) {
        if (m_pilots[i].active && m_pilots[i].state == PilotState::Falling)
            drawPilot(p, m_pilots[i]);
    }

    // 6. Closest Dunes: index 4 (surface) and index 5 (closest)
    drawDuneFgStrips(p, DUNE_FG_COUNT - 2, DUNE_FG_COUNT - 1);

    // 7. Landed Pilots (on the surface of the closest dunes)
    for (int i = 0; i < MAX_PILOTS; ++i) {
        if (m_pilots[i].active && m_pilots[i].state == PilotState::Landed)
            drawPilot(p, m_pilots[i]);
    }

    // 8. Subtitle overlay
    m_subtitles.paint(&p, rect());
}

// ============================================================================
// RESIZE EVENT
// ============================================================================

void HurleyAnimation::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
}
