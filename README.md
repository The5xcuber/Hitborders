# Hitborders

Geode mod for Geometry Dash 2.2081 (Geode 5.x, min 5.11.1). Made with AI (Gemini Neon 4)

Draws the level as the player's center line sees it: every solid, hazard, slope, saw, and interactable is inflated by the active (or upcoming lookahead) player hitbox (Minkowski sum), with stencil-flattened fills, merged outer borders, and a stream-simplified player center path.

## Build

```bash
export GEODE_SDK=/path/to/geode
geode build
```

Or with CMake directly:

```bash
cmake -B build
cmake --build build
```

## Settings (Live, No Restart)

Divided into three sections in the Geode settings menu:

- **General**:
  - `hitborders-enabled` (bool, default `false`): Master toggle. Turning OFF restores stock player visibility, opacity, wave trail, layout colors, and clears the overlay.
  - `player-opacity` (float, default `0.25`, range `0.0` to `1.0`): Opacity of the player icon (`0.0` hides it, `1.0` full).
  - `hide-wave-trail` (bool, default `false`): Disables the stock Geometry Dash wave trail while Hitborders is enabled.
  - `layout-mode` (bool, default `false`): Hides decoration objects (`GameObjectType::Decoration`), forces color channels to white, and uses a flat dark grey background and ground.
- **Hitborders & Merging**:
  - `line-thickness` (float, default `3.0`, range `1.0` to `8.0`): Line thickness in real screen pixels.
  - `merge-outlines` (bool, default `true`): Merges overlapping outlines (spatial hash + Cyrus-Beck) and flattens overlapping interior fills via the OpenGL stencil buffer (zero alpha stacking).
  - `fill-opacity` (float, default `0.25`, range `0.0` to `0.7`): Flat interior fill opacity for inflated solids and hazards.
  - `show-interactables` (bool, default `true`): Draws inflated trigger outlines for orbs, pads, and portals (never merged).
  - `glow` (bool, default `false`): Draws a wider translucent glow pass behind outlines and the center path.
  - `hide-inside-hazards` (bool, default `false`): Opt-in setting that hides hazards, solids, and interactables completely enclosed inside hazard hitboxes.
  - `only-reachable-borders` (bool, default `false`): Opt-in flood-fill from the player center that hides borders buried inside walls or unreachable sealed pockets.
- **Colors**:
  - `color-hazard`, `color-solid`, `color-path`, `color-wave-path`, `color-portal` (`rgb` pickers).

## Source Files

- `src/Common.hpp`: Tunable hitbox constants, colors, settings loader, and real-pixel line width helper.
- `src/Geometry.hpp` / `src/Geometry.cpp`: Pure C++20 2D geometry (convex polygons, rounded rects, slope triangles, Minkowski sums, 2D convex hull, spatial hash + Cyrus-Beck outline merger, and 2D grid reachability BFS).
- `src/Inflation.hpp` / `src/Inflation.cpp`: Per-gamemode live and table hitbox resolution, spider teleport detection, frozen past + future portal lookahead segments, and rebuild signature quantization.
- `src/Trail.hpp` / `src/Trail.cpp`: Stream-simplified player center path with uncommitted head support and 6000-point history cap.
- `src/Overlay.hpp` / `src/Overlay.cpp`: `FlatFillLayer` (stencil-merged uniform fill) and `HitbordersOverlay` attached to `PlayLayer::m_objectLayer` (zOrder 10000).
- `src/Hooks.cpp`: Geode `$modify` hooks for `PlayLayer::setupHasCompleted`, `PlayLayer::resetLevel`, and `PlayerObject::update`.

## Known Limitations

- Non-90-degree rotated solids, hazards, and slopes are approximated by their axis-aligned bounding box (`GameObject::getObjectRect()`).
- Dynamic mover detection identifies objects whose group IDs are targeted by move, rotate, scale, follow, or keyframe triggers (`EffectGameObject` and its transform/follow/keyframe subclasses). Objects moved by exotic or indirect spawn/trigger chains that do not set standard target group fields may be classified as static until the view rebuilds.
