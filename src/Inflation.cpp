#include "Inflation.hpp"

#include <algorithm>
#include <cmath>

namespace hitborders {

namespace {

inline int32_t quantize05(float v) {
    return static_cast<int32_t>(std::lround(v / constants::SIGNATURE_QUANTIZE));
}

inline uint64_t fnvMix(uint64_t h, uint64_t v) {
    h ^= v;
    h *= 1099511628211ULL;
    return h;
}

inline uint64_t hashHitboxShape(uint64_t h, const HitboxShape& s) {
    h = fnvMix(h, static_cast<uint64_t>(s.isDisc ? 1 : 0));
    h = fnvMix(h, static_cast<uint64_t>(static_cast<uint32_t>(quantize05(s.halfW))));
    h = fnvMix(h, static_cast<uint64_t>(static_cast<uint32_t>(quantize05(s.halfH))));
    h = fnvMix(h, static_cast<uint64_t>(static_cast<uint32_t>(quantize05(s.radius))));
    return h;
}

bool applyPortalToState(GameObjectType type, GameMode& mode, bool& isMini) {
    GameMode prevMode = mode;
    bool prevMini = isMini;

    switch (type) {
        case GameObjectType::CubePortal:   mode = GameMode::Cube; break;
        case GameObjectType::ShipPortal:   mode = GameMode::Ship; break;
        case GameObjectType::BallPortal:   mode = GameMode::Ball; break;
        case GameObjectType::UfoPortal:    mode = GameMode::Ufo; break;
        case GameObjectType::WavePortal:   mode = GameMode::Wave; break;
        case GameObjectType::RobotPortal:  mode = GameMode::Robot; break;
        case GameObjectType::SpiderPortal: mode = GameMode::Spider; break;
        case GameObjectType::SwingPortal:  mode = GameMode::Swing; break;
        case GameObjectType::MiniSizePortal:    isMini = true; break;
        case GameObjectType::RegularSizePortal: isMini = false; break;
        default:
            return false;
    }

    return mode != prevMode || isMini != prevMini;
}

} // namespace

GameMode detectGameMode(PlayerObject* player) {
    if (!player) return GameMode::Cube;
    if (player->m_isShip) return GameMode::Ship;
    if (player->m_isBird) return GameMode::Ufo;
    if (player->m_isBall) return GameMode::Ball;
    if (player->m_isDart) return GameMode::Wave;
    if (player->m_isRobot) return GameMode::Robot;
    if (player->m_isSpider) return GameMode::Spider;
    if (player->m_isSwing) return GameMode::Swing;
    return GameMode::Cube;
}

void getTableHitboxes(GameMode mode, bool isMini, HitboxShape& outSolid, HitboxShape& outHazard) {
    float mult = isMini ? constants::MINI_SCALE_MULTIPLIER : 1.0f;

    switch (mode) {
        case GameMode::Wave: {
            float sh = constants::WAVE_SOLID_HALF_SIZE * mult;
            float hh = constants::WAVE_HAZARD_HALF_SIZE * mult;
            outSolid = HitboxShape::makeBox(sh, sh);
            outHazard = HitboxShape::makeBox(hh, hh);
            break;
        }
        case GameMode::Swing: {
            float r = constants::STANDARD_HALF_SIZE * mult;
            outSolid = HitboxShape::makeDisc(r);
            outHazard = HitboxShape::makeDisc(r);
            break;
        }
        case GameMode::Cube:
        case GameMode::Robot:
        case GameMode::Ball:
        case GameMode::Spider:
        case GameMode::Ship:
        case GameMode::Ufo:
        default: {
            float h = constants::STANDARD_HALF_SIZE * mult;
            outSolid = HitboxShape::makeBox(h, h);
            outHazard = HitboxShape::makeBox(h, h);
            break;
        }
    }
}

PlayerState getLivePlayerState(PlayerObject* player, bool isSpiderTeleport) {
    PlayerState state;
    if (!player) {
        getTableHitboxes(GameMode::Cube, false, state.solidBox, state.hazardBox);
        return state;
    }

    state.mode = detectGameMode(player);
    state.isMini = (player->m_vehicleSize < constants::MINI_VEHICLE_THRESHOLD);
    state.isSpiderTeleport = (state.mode == GameMode::Spider) && isSpiderTeleport;

    switch (state.mode) {
        case GameMode::Ship:
        case GameMode::Ufo: {
            // Live getObjectRect already includes mini and rotation; no extra mini multiplier.
            cocos2d::CCRect rect = player->getObjectRect();
            float hw = rect.size.width * 0.5f;
            float hh = rect.size.height * 0.5f;
            if (hw > 0.5f && hh > 0.5f) {
                state.solidBox = HitboxShape::makeBox(hw, hh);
                state.hazardBox = HitboxShape::makeBox(hw, hh);
            } else {
                getTableHitboxes(state.mode, state.isMini, state.solidBox, state.hazardBox);
            }
            break;
        }
        case GameMode::Swing: {
            // Swingcopter disc radius from live getObjectRect: max(W, H) / 2.
            cocos2d::CCRect rect = player->getObjectRect();
            float r = std::max(rect.size.width, rect.size.height) * 0.5f;
            if (r > 0.5f) {
                state.solidBox = HitboxShape::makeDisc(r);
                state.hazardBox = HitboxShape::makeDisc(r);
            } else {
                getTableHitboxes(state.mode, state.isMini, state.solidBox, state.hazardBox);
            }
            break;
        }
        case GameMode::Cube:
        case GameMode::Robot:
        case GameMode::Ball:
        case GameMode::Spider:
        case GameMode::Wave:
        default: {
            getTableHitboxes(state.mode, state.isMini, state.solidBox, state.hazardBox);
            break;
        }
    }

    if (state.isSpiderTeleport) {
        state.solidBox.halfW = 0.0f;
        state.hazardBox.halfW = 0.0f;
    }

    return state;
}

bool isLookaheadPortalType(GameObjectType type) {
    switch (type) {
        case GameObjectType::CubePortal:
        case GameObjectType::ShipPortal:
        case GameObjectType::BallPortal:
        case GameObjectType::UfoPortal:
        case GameObjectType::WavePortal:
        case GameObjectType::RobotPortal:
        case GameObjectType::SpiderPortal:
        case GameObjectType::SwingPortal:
        case GameObjectType::MiniSizePortal:
        case GameObjectType::RegularSizePortal:
            return true;
        default:
            return false;
    }
}

void buildLookaheadSegments(
    const std::vector<LookaheadSegment>& pastSegments,
    const PlayerState& liveState,
    float playerX,
    float lookaheadMaxX,
    const std::vector<GameObject*>& sortedPortals,
    std::vector<LookaheadSegment>& outSegments
) {
    outSegments.clear();
    if (!pastSegments.empty()) {
        for (const LookaheadSegment& seg : pastSegments) {
            outSegments.push_back(seg);
        }
        // Keep the active (most recent) segment synchronized with live state
        outSegments.back().mode = liveState.mode;
        outSegments.back().isMini = liveState.isMini;
        outSegments.back().solidBox = liveState.solidBox;
        outSegments.back().hazardBox = liveState.hazardBox;
    } else {
        outSegments.push_back({
            -1e9f,
            liveState.mode,
            liveState.isMini,
            liveState.solidBox,
            liveState.hazardBox
        });
    }

    if (sortedPortals.empty()) return;

    GameMode curMode = liveState.mode;
    bool curMini = liveState.isMini;

    // Binary search for first portal near playerX (small margin for slight portal movement)
    auto it = std::lower_bound(
        sortedPortals.begin(),
        sortedPortals.end(),
        playerX - 60.0f,
        [](GameObject* obj, float x) {
            return obj->getPositionX() < x;
        }
    );

    for (; it != sortedPortals.end(); ++it) {
        GameObject* portal = *it;
        if (!portal) continue;
        if (portal->m_isDisabled || portal->m_isDisabled2 ||
            portal->m_isGroupDisabled || portal->m_isGroupDisabledTemp ||
            portal->m_isNoTouch) {
            continue;
        }

        cocos2d::CCRect rect = portal->getObjectRect();
        float px = rect.getMidX();
        if (px <= playerX) continue;
        if (px > lookaheadMaxX) break;

        if (applyPortalToState(portal->m_objectType, curMode, curMini)) {
            HitboxShape solid{};
            HitboxShape hazard{};
            getTableHitboxes(curMode, curMini, solid, hazard);
            outSegments.push_back({px, curMode, curMini, solid, hazard});
        }
    }
}

const LookaheadSegment& selectSegmentForX(
    const std::vector<LookaheadSegment>& segments,
    float centerX
) {
    // Typically 1 to 6 segments: reverse scan is branch-predictor friendly and O(1) in practice.
    for (std::size_t i = segments.size(); i > 1; --i) {
        if (segments[i - 1].startX <= centerX) {
            return segments[i - 1];
        }
    }
    return segments[0];
}

uint64_t computeHitboxSignature(
    const std::vector<LookaheadSegment>& segments,
    bool isSpiderTeleport
) {
    uint64_t h = 14695981039346656037ULL;
    h = fnvMix(h, static_cast<uint64_t>(isSpiderTeleport ? 1 : 0));
    h = fnvMix(h, static_cast<uint64_t>(segments.size()));

    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& seg = segments[i];
        if (i > 0) {
            h = fnvMix(h, static_cast<uint64_t>(static_cast<uint32_t>(quantize05(seg.startX))));
        }
        h = hashHitboxShape(h, seg.solidBox);
        h = hashHitboxShape(h, seg.hazardBox);
    }
    return h;
}

} // namespace hitborders
