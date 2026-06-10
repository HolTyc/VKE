#pragma once

// Deterministic, infinite Level 0 layout. Everything is a pure function of
// integer cell coordinates, so the world is endless and consistent without
// storing any state. The fluorescent-panel functions (panelDead / panelHash /
// flicker) MUST stay in sync with shaders/ceiling.frag, which draws the
// matching emissive panels from the same formulas.

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>

namespace bk {

constexpr float kCell      = 4.0f;  // room grid pitch, metres
constexpr float kWallH     = 3.0f;  // ceiling height
constexpr float kWallT     = 0.22f; // wall thickness
constexpr float kEyeHeight = 1.65f;
constexpr float kPanelY    = 2.85f; // point lights sit just below the ceiling

inline float fract(float v) { return v - std::floor(v); }

inline float hash21(float x, float y) {
    return fract(std::sin(x * 127.1f + y * 311.7f) * 43758.5453f);
}

// Smooth value noise — used so walls cluster into longer runs instead of
// being scattered single segments.
inline float vnoise(float x, float y) {
    float ix = std::floor(x), iy = std::floor(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    float a = hash21(ix, iy);
    float b = hash21(ix + 1, iy);
    float c = hash21(ix, iy + 1);
    float d = hash21(ix + 1, iy + 1);
    return glm::mix(glm::mix(a, b, fx), glm::mix(c, d, fx), fy);
}

// The 3x3 cells around the origin are kept open so the player never spawns
// inside a wall.
inline bool nearSpawn(int cx, int cz) {
    return cx >= -1 && cx <= 1 && cz >= -1 && cz <= 1;
}

// Wall on the south edge of cell (cx, cz): the line z = cz*kCell,
// spanning x in [cx*kCell, (cx+1)*kCell].
inline bool wallSouth(int cx, int cz) {
    if (nearSpawn(cx, cz) || nearSpawn(cx, cz - 1)) return false;
    if (vnoise(cx * 0.43f + 13.7f, cz * 0.43f) < 0.60f) return false;
    return hash21(cx * 7.31f, cz * 3.17f) > 0.22f; // carve occasional doorways
}

// Wall on the west edge of cell (cx, cz): the line x = cx*kCell.
inline bool wallWest(int cx, int cz) {
    if (nearSpawn(cx, cz) || nearSpawn(cx - 1, cz)) return false;
    if (vnoise(cx * 0.43f, cz * 0.43f + 71.3f) < 0.60f) return false;
    return hash21(cx * 3.97f, cz * 8.21f) > 0.22f;
}

// Free-standing square pillar at the south-west corner of cell (cx, cz).
inline bool pillar(int cx, int cz) {
    if (nearSpawn(cx, cz)) return false;
    return hash21(cx * 5.13f, cz * 9.41f) > 0.97f;
}

// ---- fluorescent panels (rows: 8 m pitch in x, 4 m pitch in z) --------------

inline bool panelCell(int cx, int cz) { (void)cz; return cx % 2 == 0; }

inline glm::vec2 panelCenter(int cx, int cz) {
    return {cx * kCell + 2.0f, cz * kCell + 2.0f};
}

// Wrap to [0, 512) before hashing so float32 in the shader agrees with us
// far from the origin (the dead/faulty pattern repeats every 512 m).
inline glm::vec2 panelHashCoord(glm::vec2 c) {
    return {c.x - 512.0f * std::floor(c.x / 512.0f),
            c.y - 512.0f * std::floor(c.y / 512.0f)};
}

inline bool panelDead(glm::vec2 c) {
    c = panelHashCoord(c);
    return fract(std::sin(c.x * 39.3468f + c.y * 11.135f) * 14375.5453f) < 0.07f;
}

inline float panelHash(glm::vec2 c) {
    c = panelHashCoord(c);
    return fract(std::sin(c.x * 12.9898f + c.y * 78.233f) * 43758.5453f);
}

inline float flicker(float t, float h) {
    if (h > 0.86f) { // faulty tube: hard stutter
        float s = std::sin(t * 9.0f + h * 40.0f) * std::sin(t * 23.0f + h * 70.0f) *
                  std::sin(t * 4.7f + h * 13.0f);
        return s > -0.3f ? 1.0f : 0.12f;
    }
    return 0.97f + 0.03f * std::sin(t * 7.0f + h * 30.0f);
}

// ---- collision --------------------------------------------------------------

inline void pushOutOfBox(glm::vec2& p, float r,
                         float xmin, float xmax, float zmin, float zmax) {
    glm::vec2 closest{glm::clamp(p.x, xmin, xmax), glm::clamp(p.y, zmin, zmax)};
    glm::vec2 d = p - closest;
    float dd = glm::dot(d, d);
    if (dd >= r * r) return;
    if (dd > 1e-9f) {
        p = closest + d * (r / std::sqrt(dd));
    } else {
        p.y = zmax + r; // degenerate (centre inside the box): eject along +z
    }
}

// Slides a circle of radius r out of all walls/pillars near p (p is x/z).
inline glm::vec2 collide(glm::vec2 p, float r) {
    int cx = static_cast<int>(std::floor(p.x / kCell));
    int cz = static_cast<int>(std::floor(p.y / kCell));
    const float ht = kWallT * 0.5f;
    for (int z = cz - 1; z <= cz + 2; ++z) {
        for (int x = cx - 1; x <= cx + 2; ++x) {
            float wx = x * kCell, wz = z * kCell;
            if (wallSouth(x, z))
                pushOutOfBox(p, r, wx - ht, wx + kCell + ht, wz - ht, wz + ht);
            if (wallWest(x, z))
                pushOutOfBox(p, r, wx - ht, wx + ht, wz - ht, wz + kCell + ht);
            if (pillar(x, z))
                pushOutOfBox(p, r, wx - 0.3f, wx + 0.3f, wz - 0.3f, wz + 0.3f);
        }
    }
    return p;
}

} // namespace bk
