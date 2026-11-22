#pragma once

#include "Common.hpp"
#include "Geometry.hpp"

#include <cstdint>
#include <vector>

namespace hitborders {

enum class GameMode : uint8_t {
    Cube = 0,
    Ship,
    Ball,
    Ufo,
    Wave,
    Robot,
    Spider,
    Swing
};

struct PlayerState {
    GameMode mode = GameMode::Cube;
    bool isMini = false;
    bool isSpiderTeleport = false;
    HitboxShape solidBox{};
    HitboxShape hazardBox{};
};

struct LookaheadSegment {
    float startX = -1e9f;
    GameMode mode = GameMode::Cube;
    bool isMini = false;
    HitboxShape solidBox{};
    HitboxShape hazardBox{};
};

// Detect active gamemode from PlayerObject flags
GameMode detectGameMode(PlayerObject* player);

// Table hitboxes for predicted modes (mini multiplies table values only)
void getTableHitboxes(GameMode mode, bool isMini, HitboxShape& outSolid, HitboxShape& outHazard);

// Live player state (Ship/UFO/Swing use live getObjectRect without extra mini multiplier)
PlayerState getLivePlayerState(PlayerObject* player, bool isSpiderTeleport);

// Check if a GameObjectType is a lookahead mode or size portal
bool isLookaheadPortalType(GameObjectType type);

// Build lookahead segments into outSegments using frozen past segments + live current + future portals.
void buildLookaheadSegments(
    const std::vector<LookaheadSegment>& pastSegments,
    const PlayerState& liveState,
    float playerX,
    float lookaheadMaxX,
    const std::vector<GameObject*>& sortedPortals,
    std::vector<LookaheadSegment>& outSegments
);

// Find the active segment for an object at centerX (last segment with startX <= centerX)
const LookaheadSegment& selectSegmentForX(
    const std::vector<LookaheadSegment>& segments,
    float centerX
);

// Quantized signature (0.5-unit quantization) for rebuild change detection
uint64_t computeHitboxSignature(
    const std::vector<LookaheadSegment>& segments,
    bool isSpiderTeleport
);

} // namespace hitborders
