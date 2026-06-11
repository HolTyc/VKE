#pragma once

// Captain Clark — the thing that walks Level 0.
//
// ECS integration: CaptainClarkComponent (a plain vke::Component subclass)
// holds the AI state; spawnCaptainClark() builds the entity (Transform +
// MeshRendererComponent with the placeholder captain_clark.obj + the logic
// component); updateCaptainClarks() is the per-frame system, driven from
// Application::onUpdate via scene().forEach.
//
// Behavior:
//   * Wander — walks the maze grid cell-to-cell, only through openings
//     (reuses LevelGen's wallSouth/wallWest as the navigation graph), with a
//     bias against turning back, sliding-collided like the player.
//   * Chase — when the player is close AND visible (line-of-sight march
//     through the maze), he locks on and pursues; losing sight for a couple
//     of seconds drops him back to wandering at his last known heading.
//   * Jumpscare — within arm's reach he locks in place screaming
//     (jumpscare.mp3 at full volume; reported to the game layer for the VHS
//     glitch burst + camera shake), then scatters back into the maze.
//   * If the player outruns him completely he silently relocates a few cells
//     out of sight, so there is always something in the dark nearby.
//
// Audio: each Clark carries an AudioSourceComponent with three positional mp3
// clips (idle / chase / jumpscare); state changes swap which one is playing.

#include "AudioSystem.hpp"
#include "LevelGen.hpp"

#include <vke/vke.hpp>

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>

namespace bk {

constexpr float kClarkRadius      = 0.35f;
constexpr float kClarkWalkSpeed   = 1.5f;
constexpr float kClarkChaseSpeed  = 3.6f;  // between the player's walk and run
constexpr float kClarkSightDist   = 13.0f; // starts chasing inside this + LOS
constexpr float kClarkLoseDist    = 20.0f; // gives up beyond this
constexpr float kClarkLoseSight   = 2.5f;  // seconds without LOS before giving up
constexpr float kClarkCatchDist   = 0.85f;
constexpr float kClarkRespawnDist = 48.0f; // fell hopelessly behind: relocate
constexpr float kClarkScareTime   = 1.4f;  // seconds frozen in the player's face

struct CaptainClarkComponent : vke::Component {
    enum class State { Wander, Chase, Jumpscare };

    State     state      = State::Wander;
    State     soundState = State::Wander; // which clip is currently playing
    glm::vec2 waypoint{0.0f};      // wander target (cell centre, x/z)
    glm::vec2 dir{0.0f, -1.0f};    // last movement direction (x/z)
    float     yaw        = 0.0f;   // smoothed facing, degrees
    float     animPhase  = 0.0f;   // drives the shamble bob/sway
    float     lostTimer  = 0.0f;   // seconds since the player was last seen
    float     scareTimer = 0.0f;   // Jumpscare countdown
    uint32_t  rng        = 0x9e3779b9u;
};

// xorshift32 — deterministic per-monster randomness, no <random> machinery.
inline float clarkRand(CaptainClarkComponent& c) {
    c.rng ^= c.rng << 13;
    c.rng ^= c.rng >> 17;
    c.rng ^= c.rng << 5;
    return static_cast<float>(c.rng & 0xFFFFFF) / static_cast<float>(0x1000000);
}

// Is the edge between cell (cx,cz) and its neighbour in direction d open?
// d is one of (+1,0) (-1,0) (0,+1) (0,-1).
inline bool cellOpen(int cx, int cz, int dx, int dz) {
    if (dx > 0) return !wallWest(cx + 1, cz);
    if (dx < 0) return !wallWest(cx, cz);
    if (dz > 0) return !wallSouth(cx, cz + 1);
    return !wallSouth(cx, cz);
}

// A point is inside geometry if the collision solver displaces a tiny probe.
inline bool pointBlocked(glm::vec2 p) {
    glm::vec2 pushed = collide(p, 0.06f);
    return glm::dot(pushed - p, pushed - p) > 1e-6f;
}

// Cheap LOS: march the segment in short steps through the maze.
inline bool hasLineOfSight(glm::vec2 from, glm::vec2 to) {
    glm::vec2 d = to - from;
    float dist = glm::length(d);
    if (dist < 1e-3f) return true;
    glm::vec2 step = d / dist * 0.35f;
    int steps = static_cast<int>(dist / 0.35f);
    glm::vec2 p = from;
    for (int i = 0; i < steps; ++i) {
        p += step;
        if (pointBlocked(p)) return false;
    }
    return true;
}

inline glm::vec2 cellCenter(int cx, int cz) {
    return {cx * kCell + kCell * 0.5f, cz * kCell + kCell * 0.5f};
}

// Picks the next wander waypoint: a random open neighbouring cell, preferring
// not to double back unless dead-ended.
inline void pickWanderWaypoint(CaptainClarkComponent& c, glm::vec2 pos) {
    int cx = static_cast<int>(std::floor(pos.x / kCell));
    int cz = static_cast<int>(std::floor(pos.y / kCell));

    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int open[4], openCount = 0;
    int backtrack = -1;
    for (int i = 0; i < 4; ++i) {
        if (!cellOpen(cx, cz, dirs[i][0], dirs[i][1])) continue;
        // The direction most opposed to the current heading is the backtrack.
        if (glm::dot(c.dir, glm::vec2(dirs[i][0], dirs[i][1])) < -0.5f) {
            backtrack = i;
            continue;
        }
        open[openCount++] = i;
    }
    int choice;
    if (openCount > 0)
        choice = open[static_cast<int>(clarkRand(c) * openCount) % openCount];
    else if (backtrack >= 0)
        choice = backtrack; // dead end: turn around
    else { // sealed cell (shouldn't happen) — stay put
        c.waypoint = cellCenter(cx, cz);
        return;
    }
    c.waypoint = cellCenter(cx + dirs[choice][0], cz + dirs[choice][1]);
}

// Drops the monster at a random cell ring around the player, out of sight if
// the dice allow. Used for the initial out-run respawn and after a catch.
inline void scatterClark(CaptainClarkComponent& c, vke::Transform& t, glm::vec2 player) {
    for (int attempt = 0; attempt < 12; ++attempt) {
        float ang  = clarkRand(c) * 6.2831853f;
        float dist = (6.0f + clarkRand(c) * 4.0f) * kCell;
        glm::vec2 p = player + glm::vec2(std::cos(ang), std::sin(ang)) * dist;
        p = collide(p, kClarkRadius); // settle out of any wall
        if (attempt < 11 && hasLineOfSight(player, p)) continue; // prefer hidden
        t.position = {p.x, 0.0f, p.y};
        c.state = CaptainClarkComponent::State::Wander;
        c.waypoint = p;
        return;
    }
}

// Builds the monster entity: Transform + MeshRenderer (rigged GLB, baked at
// bind pose — skeletal animation is stubbed in the engine loader) + the logic
// component + a positional AudioSource carrying the three state clips. The
// engine has no texture sampling yet, so the skin is the material albedo.
inline uint32_t spawnCaptainClark(vke::Scene& scene, vke::Renderer3D& renderer,
                                  glm::vec3 position) {
    auto& clark = scene.createEntity("Captain Clark");
    clark.transform().position = position;

    auto& mr = clark.add<vke::MeshRendererComponent>();
    mr.mesh = renderer.loadModel("backrooms/models/captain_clark_rigged_fixed.glb");
    mr.material.albedo    = {0.16f, 0.15f, 0.17f, 1.0f}; // wet-dark silhouette
    mr.material.shininess = 8.0f;
    mr.material.specular  = 0.25f; // faint fluorescent sheen so he reads in the dark
    mr.material.shader    = "basic";

    auto& audio = clark.add<AudioSourceComponent>();
    audio.load("idle",      "backrooms/sound/idle.mp3");
    audio.load("chase",     "backrooms/sound/chase.mp3");
    audio.load("jumpscare", "backrooms/sound/jumpscare.mp3");
    audio.setPosition(position);
    audio.play("idle", true); // wandering from frame one

    auto& cc = clark.add<CaptainClarkComponent>();
    cc.rng ^= clark.id() * 0x45d9f3bu; // de-sync multiple Clarks
    cc.waypoint = {position.x, position.z};
    return clark.id();
}

struct ClarkStatus {
    bool  caught  = false;  // a Clark started his jumpscare this frame
    float nearest = 1e9f;   // distance to the closest Clark (VHS interference)
};

// Per-frame system for every entity carrying a CaptainClarkComponent.
inline ClarkStatus updateCaptainClarks(vke::Scene& scene, glm::vec3 playerPos, float dt) {
    ClarkStatus status;
    glm::vec2 player{playerPos.x, playerPos.z};

    scene.forEach<CaptainClarkComponent>([&](vke::Entity& e, CaptainClarkComponent& c) {
        auto& t = e.transform();
        glm::vec2 pos{t.position.x, t.position.z};
        glm::vec2 toPlayer = player - pos;
        float dist = glm::length(toPlayer);
        status.nearest = std::min(status.nearest, dist);

        using State = CaptainClarkComponent::State;

        auto& audio = *e.get<AudioSourceComponent>();
        audio.setPosition(t.position);

        // Swaps the playing clip whenever the state changed since last sync.
        auto syncSound = [&] {
            if (c.soundState == c.state) return;
            c.soundState = c.state;
            audio.stopAll();
            switch (c.state) {
            case State::Wander:    audio.play("idle", true);             break;
            case State::Chase:     audio.play("chase", true);            break;
            case State::Jumpscare: audio.play("jumpscare", false, 1.0f); break; // max volume
            }
        };

        if (c.state == State::Jumpscare) {
            // Locked in the player's face, screaming; no movement until done.
            c.scareTimer -= dt;
            c.yaw = glm::degrees(std::atan2(-toPlayer.x, -toPlayer.y));
            t.rotation = {0.0f, c.yaw, 0.0f};
            if (c.scareTimer <= 0.0f) {
                scatterClark(c, t, player); // melts back into the maze (-> Wander)
                syncSound();
            }
            return;
        }

        if (dist > kClarkRespawnDist) {
            scatterClark(c, t, player);
            syncSound();
            return;
        }

        if (dist < kClarkCatchDist) {
            status.caught = true; // game layer: VHS glitch burst + camera shake
            c.state = State::Jumpscare;
            c.scareTimer = kClarkScareTime;
            syncSound();
            return;
        }

        // ---- state transitions ------------------------------------------
        bool seesPlayer = dist < (c.state == State::Chase ? kClarkLoseDist
                                                          : kClarkSightDist) &&
                          hasLineOfSight(pos, player);
        if (seesPlayer) {
            c.state = State::Chase;
            c.lostTimer = 0.0f;
        } else if (c.state == State::Chase) {
            c.lostTimer += dt;
            if (c.lostTimer > kClarkLoseSight || dist > kClarkLoseDist) {
                c.state = State::Wander;
                // Keep marching toward where he last saw the player.
                pickWanderWaypoint(c, pos);
            }
        }
        syncSound(); // Wander <-> Chase clip swap

        // ---- movement -----------------------------------------------------
        float speed;
        glm::vec2 wish;
        if (c.state == State::Chase) {
            speed = kClarkChaseSpeed;
            wish  = toPlayer / std::max(dist, 1e-4f);
        } else {
            speed = kClarkWalkSpeed;
            glm::vec2 toWaypoint = c.waypoint - pos;
            if (glm::dot(toWaypoint, toWaypoint) < 0.09f) { // reached the cell
                pickWanderWaypoint(c, pos);
                toWaypoint = c.waypoint - pos;
            }
            float len = glm::length(toWaypoint);
            wish = len > 1e-4f ? toWaypoint / len : c.dir;
        }

        glm::vec2 next = collide(pos + wish * speed * dt, kClarkRadius);
        glm::vec2 moved = next - pos;
        float movedLen = glm::length(moved);

        // Wedged on a pillar or wall lip while wandering: re-roll the route.
        if (c.state == State::Wander && movedLen < speed * dt * 0.25f)
            pickWanderWaypoint(c, pos);

        if (movedLen > 1e-5f) c.dir = moved / movedLen;
        t.position.x = next.x;
        t.position.z = next.y;

        // ---- facing & shamble ----------------------------------------------
        // Transform forward is -Z rotated by yaw, so yaw = atan2(-x, -z).
        float targetYaw = glm::degrees(std::atan2(-c.dir.x, -c.dir.y));
        float delta = std::remainder(targetYaw - c.yaw, 360.0f);
        c.yaw += delta * std::min(dt * 8.0f, 1.0f);

        c.animPhase += movedLen * 3.2f;
        float sway = std::sin(c.animPhase) * 4.0f;          // roll, degrees
        float bob  = std::abs(std::sin(c.animPhase)) * 0.05f;
        t.position.y = bob;
        t.rotation = {0.0f, c.yaw, sway};
    });

    return status;
}

} // namespace bk
