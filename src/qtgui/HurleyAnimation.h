#pragma once

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include "SubtitleOverlay.h"

// ============================================================================
// HurleyAnimation - WWI Desert Air Battle Easter Egg
// Celebrates Frank Hurley (1885-1962), Australian pilot and pioneering
// photomontage artist who documented WWI with iconic color photographs.
//
// Scene: Sand dunes with parallax scrolling, hot desert sun, and a 2D air
// battle between paper-cut Allied (white) and Enemy (black) WWI biplanes.
// The hero plane (larger white) is tracked by the camera. Shot planes fall
// with smoke particles; pilots sometimes eject with parachutes.
// ============================================================================

class HurleyAnimation : public QWidget {
  Q_OBJECT
public:
  explicit HurleyAnimation(QWidget *parent = nullptr);
  ~HurleyAnimation() override;

  void startAnimation();
  void stopAnimation();

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void keyReleaseEvent(QKeyEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
  void updateAnimation();

private:
  // -----------------------------------------------------------------------
  // Dune strip state — same wave-math as JolyAnimation but sandy/yellow
  // -----------------------------------------------------------------------
  struct DuneStrip {
    double phase;          // Wave phase offset
    double currentSpeed;   // Current scroll speed (world units/s)
    double targetSpeed;    // Lerp target
    double currentAmpFactor;
    double targetAmpFactor;
  };

  // -----------------------------------------------------------------------
  // Airplane state
  // -----------------------------------------------------------------------
  enum class PlaneState { Flying, Falling, Landed };
  enum class Team { Allied, Enemy };

  struct Airplane {
    bool active = false;
    bool facingRight = true;
    double x, y;           // World coordinates
    double vx, vy;         // Velocity (world px/s)
    double angle;          // Pitch relative to heading (degrees)
    double angularVel;     // Current pitch rate (deg/s)
    double throttle;       // Engine throttle 0..1
    double propPhase;      // Propeller rotation phase (rad)
    double targetAngle;    // Desired pitch (degrees)
    Team team;
    bool isHero = false;   // The one big white plane
    int health;            // Hits remaining
    double frontShootCooldown; // Seconds until next front shot
    double rearShootCooldown;  // Seconds until next rear shot
    double bombCooldown;       // Seconds until next bomb drop
    double jinkTimer;      // Seconds until next jink / direction change
    double spinAngle;      // When falling: independent tumble angle (degrees)
    PlaneState state = PlaneState::Flying;
    double landedTimer;    // Time spent on ground before fading
    double fallY;          // Target Y on 2nd-from-bottom dune for landing
    bool pilotEjected;     // Has pilot already been spawned?
    double smokeAccum;     // Fractional smoke emission accumulator
    double takeoffTimer;   // Seconds of initial ground-skimming take-off

    // Stored physics state for commonization between simulation and debug drawing
    struct {
      double fwdX, fwdY;
      double upX, upY;
      double thrustX, thrustY;
      double liftX, liftY;
    } phys;
  };

  // -----------------------------------------------------------------------
  // Bullet / tracer projectile
  // -----------------------------------------------------------------------
  struct Bullet {
    bool active = false;
    double x, y;
    double vx, vy;
    Team team;
    int shooterIdx;
  };

  // -----------------------------------------------------------------------
  // Smoke particle
  // -----------------------------------------------------------------------
  struct SmokeParticle {
    double x, y;
    double vx, vy;
    double life;      // 0..1, decreases over time
    double size;
    double alpha;
    bool   isFire = false;
  };

  // -----------------------------------------------------------------------
  // Bomb / Ordinance
  // -----------------------------------------------------------------------
  struct Bomb {
    bool active = false;
    double x, y;
    double vx, vy;
    int shooterIdx;
  };

  // -----------------------------------------------------------------------
  // Ejected pilot with parachute
  // -----------------------------------------------------------------------
  enum class PilotState { Falling, Landed };

  struct Pilot {
    bool active = false;
    double x, y;
    double vx, vy;
    PilotState state = PilotState::Falling;
    bool parachuteOpen;
    bool parachuteDestroyed = false;
    double landedTimer;
    double fallY;    // Target landing Y
    Team team;
    bool hasCamera = false;
    double immunityTimer = 0.0;
  };

  // -----------------------------------------------------------------------
  // Timer / physics state
  // -----------------------------------------------------------------------
  QTimer *m_animTimer;
  QElapsedTimer m_elapsedTimer;
  double m_physicsAccumulator;
  double m_time;

  // -----------------------------------------------------------------------
  // Camera scroll (world X offset)
  // -----------------------------------------------------------------------
  double m_cameraX;        // World X of left edge of viewport
  double m_cameraVel;      // Smooth camera velocity

  // -----------------------------------------------------------------------
  // Dune layers
  // DUNE_BG layers are in the upper sky zone — tighter, smaller — parallax
  // DUNE_FG layers are in the lower ground zone — wider, larger
  // -----------------------------------------------------------------------
  static constexpr int DUNE_BG_COUNT = 6;  // Background (parallax, slow)
  static constexpr int DUNE_FG_COUNT = 6;  // Foreground (fast)

  DuneStrip m_duneBg[DUNE_BG_COUNT];
  DuneStrip m_duneFg[DUNE_FG_COUNT];

  // -----------------------------------------------------------------------
  // Game objects
  // -----------------------------------------------------------------------
  static constexpr int MAX_PLANES    = 24;
  static constexpr int MAX_BULLETS   = 40;
  static constexpr int MAX_PILOTS    = 6;
  static constexpr int MAX_SMOKE     = 200;
  static constexpr int MAX_BOMBS     = 10;

  Airplane m_planes[MAX_PLANES];
  Bullet   m_bullets[MAX_BULLETS];
  Pilot    m_pilots[MAX_PILOTS];
  Bomb     m_bombs[MAX_BOMBS];

  // Smoke uses a ring-buffer approach via a vector for simplicity
  std::vector<SmokeParticle> m_smoke;

  SubtitleOverlay m_subtitles;

  // -----------------------------------------------------------------------
  // Hero plane index & manual control
  // -----------------------------------------------------------------------
  int    m_heroIdx;
  bool   m_heroManualControl;
  double m_autopilotMessageTimer;
  bool   m_heroManualFire;
  bool   m_heroSpaceFire;
  bool   m_isMouseDown;
  QPoint m_mousePos;
  double m_autoFireMessageTimer;

  // -----------------------------------------------------------------------
  // Score & Sun Expression
  // -----------------------------------------------------------------------
  enum class SunExpression { Normal, Happy, Sad };
  int           m_score;
  SunExpression m_sunExpression;
  double        m_sunExpressionTimer;

  void setSunExpression(SunExpression expr, double duration);

  // -----------------------------------------------------------------------
  // Spawn timing
  // -----------------------------------------------------------------------
  double m_alliedSpawnCooldown;
  double m_enemySpawnCooldown;
  double m_bombCooldown;

  // -----------------------------------------------------------------------
  // Internal methods
  // -----------------------------------------------------------------------
  void stepAnimation(double dt);
  double getGroundY(double worldX);

  // Initialization
  void initDunes();
  void spawnHero();
  int findBestPlaneSlot(bool isHero);

  // Spawning
  void spawnAllied();
  void spawnEnemy();

  // Update
  void updateDunes(double dt);
  void updatePlanes(double dt);
  void updateBullets(double dt);
  void updateSmoke(double dt);
  void updatePilots(double dt);
  void updateBombs(double dt);
  void updateCamera(double dt);

  // Shoot
  void fireFrontFromPlane(int planeIdx);
  void fireRearFromPlane(int planeIdx);
  void spawnSmoke(double x, double y, double vx, double vy, int count, bool isFire = false);
  void spawnPilot(const Airplane &plane);
  void spawnPilotGuaranteed(Airplane &airplane);

  // Drawing helpers
  void drawSky(QPainter &p);
  void drawSun(QPainter &p);
  void drawDuneBgStrips(QPainter &p);
  void drawDuneFgStrips(QPainter &p, int startIdx, int endIdx);
  void drawPlane(QPainter &p, const Airplane &plane);
  void drawBullets(QPainter &p);
  void drawBomb(QPainter &p, const Bomb &b);
  void drawSmoke(QPainter &p);
  void drawPilot(QPainter &p, const Pilot &pilot);

  // Helper: Y position on the 2nd-from-bottom foreground dune surface at world X
  double duneLandingY(double worldX) const;

  // Convert world X to screen X
  double toScreenX(double worldX) const { return worldX - m_cameraX; }
  double toWorldX(double screenX) const { return screenX + m_cameraX; }

  bool m_debugEnabled;
};
