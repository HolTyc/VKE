# The Backrooms — Level 0

An endless first-person walk through the original Backrooms, built on vke.
Run `./build/backrooms`.

**Controls:** mouse — look · WASD — walk · Shift — hurry · F — flashlight ·
Esc — pause menu (resume / fullscreen toggle / quit) ·
F1 — edit mode: the engine editor (Hierarchy / Inspector / RMB+WASD fly camera)
over the live game; F1 or Esc returns to play

## What's where

| File | Role |
| --- | --- |
| `src/LevelGen.hpp` | infinite deterministic layout: walls/pillars from value noise + hashes, fluorescent panel grid (dead / faulty tubes), flicker curve, circle-vs-wall collision |
| `src/main.cpp` | game loop: pooled wall entities (recycled, never destroyed), 7 point lights tracking the nearest live ceiling panels, flashlight (spot light), pause menu (ImGui), F1 play/edit mode toggle (`setEditorVisible`), head bob, mouse look |
| `src/Audio.*` | fully synthesized ambience via miniaudio: 120 Hz fluorescent hum (louder near fixtures, worse near faulty ones), brown-noise rumble, sporadic distant thumps |
| `shaders/` | world-space procedural surfaces: striped wallpaper + grime, mottled damp carpet, ceiling tiles with emissive flickering panels, distance fog |

## Tuning knobs

- Layout density / doorway frequency: thresholds in `LevelGen.hpp` (`wallSouth`/`wallWest`).
- Mood: fog distances + color in each `.frag` (`smoothstep(6, 30, d)`), `renderer().ambientLight` in `main.cpp`.
- Light grid: `panelCell` (every 2nd cell = 8 m pitch); dead/faulty tube ratios in `panelDead` / `flicker` — **these are mirrored in `ceiling.frag`, change both**.
- The ceiling shader receives time through `material.albedo.a` (the engine has no time uniform; rendering is opaque so alpha is unused).
