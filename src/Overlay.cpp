#include "Overlay.hpp"

#include <algorithm>
#include <array>
#include <cmath>

using namespace geode::prelude;

namespace hitborders {

namespace {

inline bool isMoverTriggerObjectID(int id) {
    switch (id) {
        case 901:  // Move
        case 1346: // Rotate
        case 1347: // Follow
        case 1814: // Follow Player Y
        case 2067: // Scale
        case 3006: // Area Move
        case 3007: // Area Rotate
        case 3008: // Area Scale
        case 3016: // Advanced Follow
        case 3032: // Keyframe
        case 3033: // Keyframe Anim
            return true;
        default:
            return false;
    }
}

inline void drawConvexPolyFill(
    CCDrawNode* node,
    const ConvexPoly& poly,
    const ccColor4F& fillColor
) {
    if (!node || poly.count < 3) return;
    std::array<CCPoint, ConvexPoly::MAX_VERTS> pts{};
    for (int i = 0; i < poly.count; ++i) {
        pts[i] = ccp(poly.verts[i].x, poly.verts[i].y);
    }
    constexpr ccColor4F transparent = {0.0f, 0.0f, 0.0f, 0.0f};
    node->drawPolygon(
        pts.data(),
        static_cast<unsigned int>(poly.count),
        fillColor,
        0.0f,
        transparent
    );
}

inline void drawSegmentList(
    CCDrawNode* node,
    const std::vector<Segment2>& segments,
    float radius,
    const ccColor4F& color
) {
    if (!node) return;
    // CCDrawNode::drawSegment already has round caps and an AA fringe.
    // Never extend endpoints by the radius.
    for (const Segment2& seg : segments) {
        node->drawSegment(ccp(seg.a.x, seg.a.y), ccp(seg.b.x, seg.b.y), radius, color);
    }
}

inline void drawPolyOutlineUnmerged(
    CCDrawNode* node,
    const ConvexPoly& poly,
    float radius,
    const ccColor4F& color
) {
    if (!node || poly.count < 3) return;
    for (int i = 0; i < poly.count; ++i) {
        const Vec2& a = poly.verts[i];
        const Vec2& b = poly.verts[(i + 1) % poly.count];
        node->drawSegment(ccp(a.x, a.y), ccp(b.x, b.y), radius, color);
    }
}

} // namespace

// ============================================================================
// FlatFillLayer: Stencil-merged uniform fill with zero alpha stacking
// ============================================================================

FlatFillLayer* FlatFillLayer::create() {
    auto* ret = new FlatFillLayer();
    if (ret && ret->initLayer()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool FlatFillLayer::initLayer() {
    if (!CCNode::init()) return false;

    m_maskNode = CCDrawNode::create();
    m_blockerNode = CCDrawNode::create();
    m_quadNode = CCDrawNode::create();

    configureDrawNodeBlend(m_maskNode);
    configureDrawNodeBlend(m_blockerNode);
    configureDrawNodeBlend(m_quadNode);

    // Keep as invisible children so their lifetime is managed automatically,
    // while FlatFillLayer::draw() invokes their draw() under stencil control.
    m_maskNode->setVisible(false);
    m_blockerNode->setVisible(false);
    m_quadNode->setVisible(false);

    this->addChild(m_maskNode);
    this->addChild(m_blockerNode);
    this->addChild(m_quadNode);
    return true;
}

void FlatFillLayer::clear() {
    if (m_maskNode) m_maskNode->clear();
    if (m_blockerNode) m_blockerNode->clear();
    if (m_quadNode) m_quadNode->clear();
    m_hasFill = false;
    m_hasBlockers = false;
}

void FlatFillLayer::beginBuild(const Rect2& bounds, const ccColor4F& fillColor, bool useStencilMerge) {
    clear();
    m_bounds = bounds;
    m_fillColor = fillColor;
    m_useStencilMerge = useStencilMerge;
}

void FlatFillLayer::addFillPoly(const ConvexPoly& poly) {
    if (poly.count < 3 || m_fillColor.a <= 0.005f) return;
    ccColor4F col = m_useStencilMerge ? ccColor4F{1.0f, 1.0f, 1.0f, 1.0f} : m_fillColor;
    drawConvexPolyFill(m_maskNode, poly, col);
    m_hasFill = true;
}

void FlatFillLayer::addBlockerPoly(const ConvexPoly& poly) {
    if (!m_useStencilMerge || poly.count < 3) return;
    drawConvexPolyFill(m_blockerNode, poly, {1.0f, 1.0f, 1.0f, 1.0f});
    m_hasBlockers = true;
}

void FlatFillLayer::endBuild() {
    if (!m_hasFill || !m_useStencilMerge || m_fillColor.a <= 0.005f) return;
    constexpr float PAD = 200.0f;
    CCPoint quad[4] = {
        ccp(m_bounds.minX - PAD, m_bounds.minY - PAD),
        ccp(m_bounds.maxX + PAD, m_bounds.minY - PAD),
        ccp(m_bounds.maxX + PAD, m_bounds.maxY + PAD),
        ccp(m_bounds.minX - PAD, m_bounds.maxY + PAD)
    };
    constexpr ccColor4F transparent = {0.0f, 0.0f, 0.0f, 0.0f};
    m_quadNode->drawPolygon(quad, 4, m_fillColor, 0.0f, transparent);
}

void FlatFillLayer::draw() {
    if (!m_hasFill || m_fillColor.a <= 0.005f) return;

    if (!m_useStencilMerge) {
        m_maskNode->draw();
        return;
    }

    GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);

    // 1. Write 1 into stencil for every fill polygon (no color output)
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    m_maskNode->draw();

    // 2. Punch out blocker polygons (e.g. hazards subtracting from solid fill)
    if (m_hasBlockers) {
        glStencilFunc(GL_ALWAYS, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        m_blockerNode->draw();
    }

    // 3. Shade ONE single flat quad at exact fillOpacity where stencil == 1
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    m_quadNode->draw();

    glStencilMask(0xFF);
    if (!prevStencil) {
        glDisable(GL_STENCIL_TEST);
    }
}

// ============================================================================
// HitbordersOverlay
// ============================================================================

HitbordersOverlay* HitbordersOverlay::create(PlayLayer* playLayer) {
    auto* ret = new HitbordersOverlay();
    if (ret && ret->initOverlay(playLayer)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool HitbordersOverlay::initOverlay(PlayLayer* playLayer) {
    if (!CCNode::init()) return false;
    m_playLayer = playLayer;

    auto makeNode = [this](int zOrder) -> CCDrawNode* {
        auto* n = CCDrawNode::create();
        configureDrawNodeBlend(n);
        this->addChild(n, zOrder);
        return n;
    };

    auto makeFill = [this](int zOrder) -> FlatFillLayer* {
        auto* f = FlatFillLayer::create();
        this->addChild(f, zOrder);
        return f;
    };

    m_staticSolidFill = makeFill(1);
    m_staticHazardFill = makeFill(2);
    m_staticGlowNode = makeNode(3);
    m_staticOutlineNode = makeNode(4);

    m_dynamicSolidFill = makeFill(5);
    m_dynamicHazardFill = makeFill(6);
    m_dynamicGlowNode = makeNode(7);
    m_dynamicOutlineNode = makeNode(8);

    m_interactableNode = makeNode(9);

    m_trailGlowNode = makeNode(10);
    m_trailCommittedNode = makeNode(11);
    m_trailHeadNode = makeNode(12);

    m_pastSegments.reserve(32);
    m_segments.reserve(48);
    m_solidPolys.reserve(512);
    m_hazardPolys.reserve(512);
    m_interactablePolys.reserve(128);
    m_mergedSegments.reserve(4096);
    m_clippedSegments.reserve(4096);
    m_reachSegments.reserve(4096);
    m_interactSegsIn.reserve(64);
    m_interactSegsOut.reserve(64);

    cacheLevelObjects();
    this->scheduleUpdate();
    return true;
}

void HitbordersOverlay::collectDynamicGroups(CCArray* objects, std::unordered_set<short>& outGroups) {
    if (!objects) return;
    unsigned int count = objects->count();
    for (unsigned int i = 0; i < count; ++i) {
        auto* obj = static_cast<GameObject*>(objects->objectAtIndex(i));
        if (!obj) continue;

        auto* eff = typeinfo_cast<EffectGameObject*>(obj);
        if (!eff) continue;

        bool isMover = isMoverTriggerObjectID(obj->m_objectID)
            || typeinfo_cast<TransformTriggerGameObject*>(eff) != nullptr
            || typeinfo_cast<AdvancedFollowTriggerObject*>(eff) != nullptr
            || typeinfo_cast<KeyframeAnimTriggerObject*>(eff) != nullptr
            || eff->m_moveOffset.x != 0.0f
            || eff->m_moveOffset.y != 0.0f
            || eff->m_lockToPlayerX
            || eff->m_lockToPlayerY
            || eff->m_lockToCameraX
            || eff->m_lockToCameraY
            || eff->m_useMoveTarget
            || eff->m_rotationDegrees != 0.0f
            || eff->m_times360 != 0
            || eff->m_followXMod != 0.0f
            || eff->m_followYMod != 0.0f
            || eff->m_followYSpeed != 0.0f
            || eff->m_isDynamicMode;

        if (!isMover) continue;

        if (eff->m_targetGroupID > 0 && eff->m_targetGroupID <= 32767) {
            outGroups.insert(static_cast<short>(eff->m_targetGroupID));
        }
    }
}

bool HitbordersOverlay::objectHasDynamicGroup(GameObject* obj, const std::unordered_set<short>& dynamicGroups) {
    if (!obj || dynamicGroups.empty() || !obj->m_groups || obj->m_groupCount <= 0) {
        return false;
    }
    int count = std::min<int>(obj->m_groupCount, 10);
    for (int i = 0; i < count; ++i) {
        short gid = (*obj->m_groups)[i];
        if (gid > 0 && dynamicGroups.find(gid) != dynamicGroups.end()) {
            return true;
        }
    }
    return false;
}

bool HitbordersOverlay::classifyObject(GameObject* obj, ObjectCategory& outCategory) {
    if (!obj) return false;
    switch (obj->m_objectType) {
        case GameObjectType::Solid:
        case GameObjectType::Breakable:
            outCategory = ObjectCategory::Solid;
            return true;

        case GameObjectType::Hazard:
        case GameObjectType::AnimatedHazard:
            outCategory = ObjectCategory::Hazard;
            return true;

        case GameObjectType::Slope:
            outCategory = ObjectCategory::Slope;
            return true;

        // Pads
        case GameObjectType::YellowJumpPad:
        case GameObjectType::PinkJumpPad:
        case GameObjectType::GravityPad:
        case GameObjectType::RedJumpPad:
        case GameObjectType::SpiderPad:
        // Orbs
        case GameObjectType::YellowJumpRing:
        case GameObjectType::PinkJumpRing:
        case GameObjectType::GravityRing:
        case GameObjectType::GreenRing:
        case GameObjectType::DropRing:
        case GameObjectType::RedJumpRing:
        case GameObjectType::CustomRing:
        case GameObjectType::DashRing:
        case GameObjectType::GravityDashRing:
        case GameObjectType::SpiderOrb:
        case GameObjectType::TeleportOrb:
        // Portals
        case GameObjectType::InverseGravityPortal:
        case GameObjectType::NormalGravityPortal:
        case GameObjectType::GravityTogglePortal:
        case GameObjectType::ShipPortal:
        case GameObjectType::CubePortal:
        case GameObjectType::BallPortal:
        case GameObjectType::UfoPortal:
        case GameObjectType::WavePortal:
        case GameObjectType::RobotPortal:
        case GameObjectType::SpiderPortal:
        case GameObjectType::SwingPortal:
        case GameObjectType::RegularSizePortal:
        case GameObjectType::MiniSizePortal:
        case GameObjectType::InverseMirrorPortal:
        case GameObjectType::NormalMirrorPortal:
        case GameObjectType::DualPortal:
        case GameObjectType::SoloPortal:
        case GameObjectType::TeleportPortal:
            outCategory = ObjectCategory::Interactable;
            return true;

        default:
            return false;
    }
}

bool HitbordersOverlay::isObjectActive(GameObject* obj, ObjectCategory category) {
    if (!obj) return false;
    if (obj->m_isDisabled || obj->m_isDisabled2 ||
        obj->m_isGroupDisabled || obj->m_isGroupDisabledTemp) {
        return false;
    }
    if (category != ObjectCategory::Interactable && obj->m_isNoTouch) {
        return false;
    }
    return true;
}

ccColor4F HitbordersOverlay::getInteractableColor(GameObjectType type, const Settings& settings) {
    switch (type) {
        case GameObjectType::YellowJumpPad:
        case GameObjectType::YellowJumpRing:
            return colors::INTERACT_YELLOW;

        case GameObjectType::PinkJumpPad:
        case GameObjectType::PinkJumpRing:
            return colors::INTERACT_PINK;

        case GameObjectType::RedJumpPad:
        case GameObjectType::RedJumpRing:
        case GameObjectType::DropRing:
            return colors::INTERACT_ORANGE;

        case GameObjectType::GravityPad:
        case GameObjectType::GravityRing:
        case GameObjectType::InverseGravityPortal:
        case GameObjectType::NormalGravityPortal:
        case GameObjectType::GravityTogglePortal:
            return colors::INTERACT_CYAN;

        case GameObjectType::GreenRing:
        case GameObjectType::DashRing:
        case GameObjectType::GravityDashRing:
            return colors::INTERACT_GREEN;

        case GameObjectType::SpiderOrb:
        case GameObjectType::SpiderPad:
        case GameObjectType::TeleportOrb:
        case GameObjectType::TeleportPortal:
            return colors::INTERACT_PURPLE;

        case GameObjectType::CustomRing:
            return colors::INTERACT_WHITE;

        default:
            return colors::from3B(settings.portalColor, 0.85f);
    }
}

void HitbordersOverlay::cacheLevelObjects() {
    m_staticObjects.clear();
    m_dynamicObjects.clear();
    m_lookaheadPortals.clear();
    m_decorations.clear();

    if (!m_playLayer || !m_playLayer->m_objects) return;
    CCArray* objects = m_playLayer->m_objects;
    unsigned int count = objects->count();

    std::unordered_set<short> dynamicGroups;
    dynamicGroups.reserve(128);
    collectDynamicGroups(objects, dynamicGroups);

    m_staticObjects.reserve(count / 2);
    m_dynamicObjects.reserve(256);
    m_lookaheadPortals.reserve(64);
    m_decorations.reserve(count / 2);

    for (unsigned int i = 0; i < count; ++i) {
        auto* obj = static_cast<GameObject*>(objects->objectAtIndex(i));
        if (!obj) continue;

        if (obj->m_objectType == GameObjectType::Decoration) {
            m_decorations.push_back({obj, obj->isVisible()});
            continue;
        }

        if (isLookaheadPortalType(obj->m_objectType)) {
            m_lookaheadPortals.push_back(obj);
        }

        ObjectCategory category = ObjectCategory::Solid;
        if (!classifyObject(obj, category)) continue;

        CachedObject entry{obj, obj->getPositionX(), category};
        if (objectHasDynamicGroup(obj, dynamicGroups)) {
            m_dynamicObjects.push_back(entry);
        } else {
            m_staticObjects.push_back(entry);
        }
    }

    std::sort(m_staticObjects.begin(), m_staticObjects.end(), [](const CachedObject& a, const CachedObject& b) {
        return a.sortX < b.sortX;
    });

    std::sort(m_lookaheadPortals.begin(), m_lookaheadPortals.end(), [](GameObject* a, GameObject* b) {
        return a->getPositionX() < b->getPositionX();
    });
}

void HitbordersOverlay::updatePastSegments(const PlayerState& liveState, float playerX) {
    if (m_pastSegments.empty()) {
        m_pastSegments.push_back({
            -1e9f,
            liveState.mode,
            liveState.isMini,
            liveState.solidBox,
            liveState.hazardBox
        });
        return;
    }

    // Handle practice mode checkpoint respawn (player jumped backward in X)
    while (m_pastSegments.size() > 1 && playerX < m_pastSegments.back().startX - 10.0f) {
        m_pastSegments.pop_back();
    }

    LookaheadSegment& cur = m_pastSegments.back();
    if (liveState.mode != cur.mode || liveState.isMini != cur.isMini) {
        // Freeze the previous segment at its existing mode/size and start a new one at playerX
        if (playerX > cur.startX + 1.0f) {
            m_pastSegments.push_back({
                playerX,
                liveState.mode,
                liveState.isMini,
                liveState.solidBox,
                liveState.hazardBox
            });
        } else {
            cur.mode = liveState.mode;
            cur.isMini = liveState.isMini;
            cur.solidBox = liveState.solidBox;
            cur.hazardBox = liveState.hazardBox;
        }
    } else {
        // Same mode/size: update live hitbox on current segment only (past segments stay frozen)
        cur.solidBox = liveState.solidBox;
        cur.hazardBox = liveState.hazardBox;
    }
}

Rect2 HitbordersOverlay::computeViewWorldRect() const {
    auto* director = CCDirector::get();
    if (!director || !m_playLayer || !m_playLayer->m_objectLayer) {
        return {0.0f, 0.0f, 569.0f, 320.0f};
    }

    CCSize winSize = director->getWinSize();
    CCNode* layer = m_playLayer->m_objectLayer;

    CCPoint c0 = layer->convertToNodeSpace(ccp(0.0f, 0.0f));
    CCPoint c1 = layer->convertToNodeSpace(ccp(winSize.width, 0.0f));
    CCPoint c2 = layer->convertToNodeSpace(ccp(winSize.width, winSize.height));
    CCPoint c3 = layer->convertToNodeSpace(ccp(0.0f, winSize.height));

    float minX = std::min({c0.x, c1.x, c2.x, c3.x});
    float maxX = std::max({c0.x, c1.x, c2.x, c3.x});
    float minY = std::min({c0.y, c1.y, c2.y, c3.y});
    float maxY = std::max({c0.y, c1.y, c2.y, c3.y});
    return {minX, minY, maxX, maxY};
}

void HitbordersOverlay::appendObjectPolygon(
    GameObject* obj,
    ObjectCategory category,
    const std::vector<LookaheadSegment>& segments,
    const Settings& settings
) {
    if (!isObjectActive(obj, category)) return;
    if (category == ObjectCategory::Interactable && !settings.showInteractables) return;

    CCRect ccRect = obj->getObjectRect();
    if (ccRect.size.width <= 0.01f && ccRect.size.height <= 0.01f) return;

    Rect2 rect{
        ccRect.origin.x,
        ccRect.origin.y,
        ccRect.origin.x + ccRect.size.width,
        ccRect.origin.y + ccRect.size.height
    };
    Vec2 center = rect.center();
    const LookaheadSegment& seg = selectSegmentForX(segments, center.x);

    float objScale = std::max(std::fabs(obj->m_scaleX), std::fabs(obj->m_scaleY));
    if (objScale < 1e-3f) objScale = 1.0f;

    if (category == ObjectCategory::Slope) {
        bool isHazardSlope = obj->m_slopeIsHazard;
        const HitboxShape& box = isHazardSlope ? seg.hazardBox : seg.solidBox;
        bool flipX = obj->m_isFlipX || obj->m_scaleX < 0.0f;
        bool flipY = obj->m_isFlipY || obj->m_scaleY < 0.0f;
        ConvexPoly tri = makeSlopeTriangleFromTransform(rect, flipX, flipY, obj->getRotation());
        ConvexPoly inflated = makeInflatedSlope(tri, box);
        if (isHazardSlope) {
            m_hazardPolys.push_back(inflated);
        } else {
            m_solidPolys.push_back(inflated);
        }
        return;
    }

    if (category == ObjectCategory::Interactable) {
        ConvexPoly inflated;
        if (obj->m_objectRadius > 0.5f) {
            inflated = makeInflatedCircle(center, obj->m_objectRadius * objScale, seg.solidBox);
        } else {
            inflated = makeInflatedRect(rect, seg.solidBox);
        }
        m_interactablePolys.push_back({inflated, getInteractableColor(obj->m_objectType, settings), objScale});
        return;
    }

    bool isHazard = (category == ObjectCategory::Hazard);
    const HitboxShape& box = isHazard ? seg.hazardBox : seg.solidBox;

    ConvexPoly inflated;
    if (obj->m_objectRadius > 0.5f) {
        inflated = makeInflatedCircle(center, obj->m_objectRadius * objScale, box);
    } else {
        inflated = makeInflatedRect(rect, box);
    }

    if (isHazard) {
        m_hazardPolys.push_back(inflated);
    } else {
        m_solidPolys.push_back(inflated);
    }
}

void HitbordersOverlay::renderGeometryLayers(
    FlatFillLayer* solidFillLayer,
    FlatFillLayer* hazardFillLayer,
    CCDrawNode* glowNode,
    CCDrawNode* outlineNode,
    CCDrawNode* interactNode,
    const Rect2& buildBounds,
    const Vec2& playerPos,
    const Settings& settings,
    float lineRadius,
    bool skipNearVertical
) {
    if (solidFillLayer) solidFillLayer->clear();
    if (hazardFillLayer) hazardFillLayer->clear();
    if (glowNode) glowNode->clear();
    if (outlineNode) outlineNode->clear();
    if (interactNode) interactNode->clear();

    const ccColor4F solidCol = colors::from3B(settings.solidColor, 1.0f);
    const ccColor4F hazardCol = colors::from3B(settings.hazardColor, 1.0f);

    // Build reachability flood-fill from player center when either hide-inside-hazards
    // or only-reachable-borders is enabled. This detects solids/interactables enclosed
    // inside rings/clusters of hazards (not just inside a single hazard polygon).
    bool reachBuilt = false;
    if ((settings.hideInsideHazards || settings.onlyReachableBorders) &&
        (!m_solidPolys.empty() || !m_hazardPolys.empty())) {
        reachBuilt = m_reachGrid.build(buildBounds, playerPos, m_hazardPolys, m_solidPolys);
    }

    const bool cullByHazards = settings.hideInsideHazards && !m_hazardPolys.empty();
    if (cullByHazards) {
        m_merger.buildBlockerGrid(m_hazardPolys);

        // 1. Remove hazards completely enclosed inside other hazards
        int writeIdx = 0;
        const int origHazCount = static_cast<int>(m_hazardPolys.size());
        for (int i = 0; i < origHazCount; ++i) {
            if (!m_merger.isPolyInsideAnyBlocker(m_hazardPolys[i], m_hazardPolys, i)) {
                if (writeIdx != i) {
                    m_hazardPolys[writeIdx] = m_hazardPolys[i];
                }
                ++writeIdx;
            }
        }
        m_hazardPolys.resize(static_cast<std::size_t>(writeIdx));

        // Rebuild hazard blocker grid after self-culling
        m_merger.buildBlockerGrid(m_hazardPolys);

        // 2. Remove solids enclosed inside a hazard OR walled off from reachable space
        //    (removes both fill and outline of unreachable blocks inside hazard mountains).
        //    Interactables are intentionally exempt because they remain triggerable inside hazards.
        m_solidPolys.erase(
            std::remove_if(m_solidPolys.begin(), m_solidPolys.end(), [this, reachBuilt](const ConvexPoly& p) {
                if (m_merger.isPolyInsideAnyBlocker(p, m_hazardPolys)) return true;
                if (reachBuilt && !m_reachGrid.isPolyReachable(p)) return true;
                return false;
            }),
            m_solidPolys.end()
        );
    }

    const bool useReachability = settings.onlyReachableBorders && reachBuilt;

    // Flat merged interior fills (stencil buffer prevents alpha stacking when mergeOutlines is ON)
    if (settings.fillOpacity > 0.005f) {
        if (solidFillLayer && !m_solidPolys.empty()) {
            solidFillLayer->beginBuild(
                buildBounds,
                colors::withAlpha(solidCol, settings.fillOpacity),
                settings.mergeOutlines
            );
            for (const ConvexPoly& p : m_solidPolys) {
                solidFillLayer->addFillPoly(p);
            }
            // Punch out hazard areas from solid fill so blue never bleeds inside red
            if (settings.mergeOutlines && (cullByHazards || !m_hazardPolys.empty())) {
                for (const ConvexPoly& h : m_hazardPolys) {
                    solidFillLayer->addBlockerPoly(h);
                }
            }
            solidFillLayer->endBuild();
        }

        if (hazardFillLayer && !m_hazardPolys.empty()) {
            hazardFillLayer->beginBuild(
                buildBounds,
                colors::withAlpha(hazardCol, settings.fillOpacity),
                settings.mergeOutlines
            );
            for (const ConvexPoly& h : m_hazardPolys) {
                hazardFillLayer->addFillPoly(h);
            }
            hazardFillLayer->endBuild();
        }
    }

    // Solids outline
    if (!m_solidPolys.empty()) {
        if (settings.mergeOutlines) {
            m_merger.merge(m_solidPolys, m_mergedSegments, skipNearVertical);
        } else {
            OutlineMerger::emitUnmerged(m_solidPolys, m_mergedSegments, skipNearVertical);
        }

        const std::vector<Segment2>* solidSegs = &m_mergedSegments;
        if (cullByHazards || settings.mergeOutlines) {
            if (!m_hazardPolys.empty()) {
                m_merger.clipSegmentsAgainstBlockers(m_hazardPolys, *solidSegs, m_clippedSegments);
                solidSegs = &m_clippedSegments;
            }
        }
        if (useReachability) {
            m_reachGrid.filterSegments(*solidSegs, m_reachSegments);
            solidSegs = &m_reachSegments;
        }

        if (settings.glow && glowNode) {
            drawSegmentList(glowNode, *solidSegs, lineRadius * 2.5f, colors::withAlpha(solidCol, 0.25f));
        }
        drawSegmentList(outlineNode, *solidSegs, lineRadius, solidCol);
    }

    // Hazards outline (drawn after solids so hazards stay crisp on top)
    if (!m_hazardPolys.empty()) {
        if (settings.mergeOutlines) {
            m_merger.merge(m_hazardPolys, m_mergedSegments, skipNearVertical);
        } else {
            OutlineMerger::emitUnmerged(m_hazardPolys, m_mergedSegments, skipNearVertical);
        }

        const std::vector<Segment2>* hazardSegs = &m_mergedSegments;
        if (useReachability) {
            m_reachGrid.filterSegments(*hazardSegs, m_reachSegments);
            hazardSegs = &m_reachSegments;
        }

        if (settings.glow && glowNode) {
            drawSegmentList(glowNode, *hazardSegs, lineRadius * 2.5f, colors::withAlpha(hazardCol, 0.25f));
        }
        drawSegmentList(outlineNode, *hazardSegs, lineRadius, hazardCol);
    }

    // Interactables (outline only, never merged, always visible even inside hazards)
    if (interactNode && !m_interactablePolys.empty()) {
        for (const InteractablePoly& ip : m_interactablePolys) {
            float scaleFactor = std::clamp(ip.objScale, 0.6f, 2.0f);
            float interactRadius = lineRadius * 0.85f * scaleFactor;
            drawPolyOutlineUnmerged(interactNode, ip.poly, interactRadius, ip.color);
        }
    }
}

void HitbordersOverlay::appendGroundPolygons(const Rect2& buildBounds) {
    if (!m_playLayer || !m_playLayer->m_objectLayer || m_segments.empty()) return;

    float minX = buildBounds.minX - 120.0f;
    float maxX = buildBounds.maxX + 120.0f;

    // Determine optional ceiling Y in m_objectLayer space (active in ship/ball/ufo/wave/swing/spider)
    bool hasCeiling = false;
    float ceilingY = 0.0f;
    if (m_playLayer->m_groundLayer2 && m_playLayer->m_groundLayer2->isVisible()) {
        CCPoint worldPos = m_playLayer->m_groundLayer2->convertToWorldSpace(ccp(0.0f, 0.0f));
        CCPoint localPos = m_playLayer->m_objectLayer->convertToNodeSpace(worldPos);
        if (localPos.y > 60.0f && localPos.y < 10000.0f) {
            hasCeiling = true;
            ceilingY = localPos.y;
        }
    }

    // Split ground strips by lookahead segments so ground hitborder height steps cleanly at portals
    for (std::size_t i = 0; i < m_segments.size(); ++i) {
        float segStart = (i == 0) ? minX : std::max(minX, m_segments[i].startX);
        float segEnd = (i + 1 < m_segments.size()) ? std::min(maxX, m_segments[i + 1].startX) : maxX;
        if (segEnd <= segStart + 0.5f) continue;

        const HitboxShape& solidBox = m_segments[i].solidBox;
        float inflateH = solidBox.isDisc ? solidBox.radius : solidBox.halfH;

        // Bottom floor ground (surface at y = 0, inflated up to y = +inflateH)
        if (buildBounds.minY <= inflateH + 60.0f) {
            HitboxShape verticalOnly = HitboxShape::makeBox(0.0f, inflateH);
            m_solidPolys.push_back(makeInflatedRect({segStart, -300.0f, segEnd, 0.0f}, verticalOnly));
        }

        // Top ceiling ground (surface at y = ceilingY, inflated down to y = ceilingY - inflateH)
        if (hasCeiling && buildBounds.maxY >= ceilingY - inflateH - 60.0f) {
            HitboxShape verticalOnly = HitboxShape::makeBox(0.0f, inflateH);
            m_solidPolys.push_back(makeInflatedRect({segStart, ceilingY, segEnd, ceilingY + 300.0f}, verticalOnly));
        }
    }
}

void HitbordersOverlay::rebuildStaticGeometry(
    const Rect2& viewRect,
    const Vec2& playerPos,
    const Settings& settings,
    float lineRadius,
    bool skipNearVertical
) {
    float viewW = std::max(100.0f, viewRect.width());
    float viewH = std::max(100.0f, viewRect.height());

    m_builtRect = {
        viewRect.minX - viewW * constants::BUILD_MARGIN_X,
        viewRect.minY - viewH * constants::BUILD_MARGIN_Y,
        viewRect.maxX + viewW * constants::BUILD_MARGIN_X,
        viewRect.maxY + viewH * constants::BUILD_MARGIN_Y
    };

    m_solidPolys.clear();
    m_hazardPolys.clear();
    m_interactablePolys.clear();

    constexpr float X_PAD = 160.0f;
    constexpr float Y_PAD = 160.0f;

    auto it = std::lower_bound(
        m_staticObjects.begin(),
        m_staticObjects.end(),
        m_builtRect.minX - X_PAD,
        [](const CachedObject& entry, float x) {
            return entry.sortX < x;
        }
    );

    for (; it != m_staticObjects.end(); ++it) {
        if (it->sortX > m_builtRect.maxX + X_PAD) break;

        GameObject* obj = it->obj;
        if (!obj) continue;

        float objY = obj->getPositionY();
        if (objY < m_builtRect.minY - Y_PAD || objY > m_builtRect.maxY + Y_PAD) {
            continue;
        }

        appendObjectPolygon(obj, it->category, m_segments, settings);
    }

    appendGroundPolygons(m_builtRect);

    renderGeometryLayers(
        m_staticSolidFill,
        m_staticHazardFill,
        m_staticGlowNode,
        m_staticOutlineNode,
        m_interactableNode,
        m_builtRect,
        playerPos,
        settings,
        lineRadius,
        skipNearVertical
    );
}

void HitbordersOverlay::rebuildDynamicGeometry(
    const Rect2& viewRect,
    const Vec2& playerPos,
    const Settings& settings,
    float lineRadius,
    bool skipNearVertical
) {
    if (m_dynamicObjects.empty()) return;

    float padX = std::max(120.0f, viewRect.width() * 0.25f);
    float padY = std::max(120.0f, viewRect.height() * 0.25f);
    Rect2 dynRect{
        viewRect.minX - padX,
        viewRect.minY - padY,
        viewRect.maxX + padX,
        viewRect.maxY + padY
    };

    m_solidPolys.clear();
    m_hazardPolys.clear();
    m_interactablePolys.clear();

    for (const CachedObject& entry : m_dynamicObjects) {
        GameObject* obj = entry.obj;
        if (!obj) continue;
        if (!isObjectActive(obj, entry.category)) continue;

        CCRect ccRect = obj->getObjectRect();
        Rect2 r{
            ccRect.origin.x,
            ccRect.origin.y,
            ccRect.origin.x + ccRect.size.width,
            ccRect.origin.y + ccRect.size.height
        };
        if (!dynRect.overlaps(r, 60.0f)) continue;

        appendObjectPolygon(obj, entry.category, m_segments, settings);
    }

    bool hasVisible = !m_solidPolys.empty() || !m_hazardPolys.empty() || !m_interactablePolys.empty();
    if (!hasVisible) {
        if (m_dynamicHadVisible) {
            m_dynamicSolidFill->clear();
            m_dynamicHazardFill->clear();
            m_dynamicGlowNode->clear();
            m_dynamicOutlineNode->clear();
            m_dynamicHadVisible = false;
        }
        return;
    }

    m_dynamicHadVisible = true;
    renderGeometryLayers(
        m_dynamicSolidFill,
        m_dynamicHazardFill,
        m_dynamicGlowNode,
        m_dynamicOutlineNode,
        m_dynamicOutlineNode,
        dynRect,
        playerPos,
        settings,
        lineRadius,
        skipNearVertical
    );
}

void HitbordersOverlay::updateTrailNodes(const Settings& settings, float lineRadius, PlayerObject* player) {
    const ccColor4F normalCol = colors::from3B(settings.pathColor, 1.0f);
    const ccColor4F waveCol = colors::from3B(settings.wavePathColor, 1.0f);

    bool thicknessChanged = (m_lastTrailLineRadius < 0.0f) ||
        (std::fabs(lineRadius - m_lastTrailLineRadius) > m_lastTrailLineRadius * 0.02f);
    bool colorOrGlowChanged = (settings.glow != m_lastGlow) ||
        !colors::equal3B(settings.pathColor, m_lastPathColor) ||
        !colors::equal3B(settings.wavePathColor, m_lastWavePathColor);

    if (thicknessChanged || colorOrGlowChanged || m_trail.needsFullRebuild()) {
        m_trailCommittedNode->clear();
        m_trailGlowNode->clear();
        m_trail.invalidateDrawn();
        m_trail.clearFullRebuildFlag();
        m_lastTrailLineRadius = lineRadius;
    }

    const auto& segs = m_trail.committed();
    for (std::size_t i = m_trail.drawnCount(); i < segs.size(); ++i) {
        const TrailSegment& s = segs[i];
        ccColor4F col = s.isWave ? waveCol : normalCol;
        if (settings.glow) {
            m_trailGlowNode->drawSegment(
                ccp(s.a.x, s.a.y),
                ccp(s.b.x, s.b.y),
                lineRadius * 2.5f,
                colors::withAlpha(col, 0.25f)
            );
        }
        m_trailCommittedNode->drawSegment(ccp(s.a.x, s.a.y), ccp(s.b.x, s.b.y), lineRadius, col);
    }
    m_trail.markAllDrawn();

    // Uncommitted head + current player center dot drawn every frame so the path never lags
    m_trailHeadNode->clear();
    if (m_trail.hasHead()) {
        TrailSegment head = m_trail.headSegment();
        ccColor4F col = head.isWave ? waveCol : normalCol;
        if (settings.glow) {
            m_trailHeadNode->drawSegment(
                ccp(head.a.x, head.a.y),
                ccp(head.b.x, head.b.y),
                lineRadius * 2.5f,
                colors::withAlpha(col, 0.25f)
            );
        }
        m_trailHeadNode->drawSegment(ccp(head.a.x, head.a.y), ccp(head.b.x, head.b.y), lineRadius, col);
        m_trailHeadNode->drawDot(ccp(head.b.x, head.b.y), lineRadius * 1.4f, col);
    } else if (player) {
        CCPoint p = player->getPosition();
        ccColor4F col = player->m_isDart ? waveCol : normalCol;
        m_trailHeadNode->drawDot(p, lineRadius * 1.4f, col);
    }
}

void HitbordersOverlay::clearAllDrawNodes() {
    if (m_staticSolidFill) m_staticSolidFill->clear();
    if (m_staticHazardFill) m_staticHazardFill->clear();
    if (m_staticGlowNode) m_staticGlowNode->clear();
    if (m_staticOutlineNode) m_staticOutlineNode->clear();
    if (m_dynamicSolidFill) m_dynamicSolidFill->clear();
    if (m_dynamicHazardFill) m_dynamicHazardFill->clear();
    if (m_dynamicGlowNode) m_dynamicGlowNode->clear();
    if (m_dynamicOutlineNode) m_dynamicOutlineNode->clear();
    if (m_interactableNode) m_interactableNode->clear();
    if (m_trailGlowNode) m_trailGlowNode->clear();
    if (m_trailCommittedNode) m_trailCommittedNode->clear();
    if (m_trailHeadNode) m_trailHeadNode->clear();
    m_dynamicHadVisible = false;
}

void HitbordersOverlay::applyPlayerAppearance(float opacity, bool hideWaveTrail) {
    if (!m_playLayer) return;

    float clamped = std::clamp(opacity, 0.0f, 1.0f);
    unsigned char opByte = static_cast<unsigned char>(clamped * 255.0f);
    bool visible = clamped > 0.005f;

    auto applyOne = [visible, opByte, hideWaveTrail](PlayerObject* p) {
        if (!p) return;
        p->setVisible(visible);
        if (visible) {
            p->setOpacity(opByte);
        }
        if (p->m_waveTrail) {
            p->m_waveTrail->setVisible(!hideWaveTrail);
        }
    };

    applyOne(m_playLayer->m_player1);
    if (m_playLayer->m_gameState.m_isDualMode) {
        applyOne(m_playLayer->m_player2);
    }
    m_playerModified = true;
}

void HitbordersOverlay::restorePlayerAppearance() {
    if (!m_playLayer || !m_playerModified) return;

    auto restoreOne = [](PlayerObject* p) {
        if (!p) return;
        p->setVisible(true);
        p->setOpacity(255);
        if (p->m_waveTrail) p->m_waveTrail->setVisible(true);
    };

    restoreOne(m_playLayer->m_player1);
    if (m_playLayer->m_player2) {
        restoreOne(m_playLayer->m_player2);
    }
    m_playerModified = false;
}

void HitbordersOverlay::applyLayoutMode(bool enable) {
    if (!m_playLayer) return;
    if (!enable) {
        if (m_layoutActive) {
            restoreLayoutMode();
        }
        return;
    }

    if (!m_layoutActive) {
        if (m_playLayer->m_background) {
            m_savedBgColor = m_playLayer->m_background->getColor();
        }
        if (m_playLayer->m_groundLayer && m_playLayer->m_groundLayer->m_ground1Sprite) {
            m_savedG1Color = m_playLayer->m_groundLayer->m_ground1Sprite->getColor();
        }
        if (m_playLayer->m_groundLayer && m_playLayer->m_groundLayer->m_ground2Sprite) {
            m_savedG2Color = m_playLayer->m_groundLayer->m_ground2Sprite->getColor();
        }

        m_savedColorActions.clear();
        if (auto* em = m_playLayer->m_effectManager) {
            m_savedColorActions.reserve(em->m_colorActionVector.size());
            for (ColorAction* ca : em->m_colorActionVector) {
                if (!ca) continue;
                m_savedColorActions.push_back({
                    ca,
                    ca->m_color,
                    ca->m_fromColor,
                    ca->m_toColor,
                    ca->m_currentOpacity
                });
            }
        }

        for (const DecorationEntry& entry : m_decorations) {
            if (entry.obj) {
                entry.obj->setVisible(false);
            }
        }
        m_layoutActive = true;
    }

    if (m_playLayer->m_background) {
        m_playLayer->m_background->setColor(colors::LAYOUT_BG_GREY);
    }
    auto tintGround = [](GJGroundLayer* gl) {
        if (!gl) return;
        if (gl->m_ground1Sprite) gl->m_ground1Sprite->setColor(colors::LAYOUT_GROUND_GREY);
        if (gl->m_ground2Sprite) gl->m_ground2Sprite->setColor(colors::LAYOUT_GROUND_GREY);
        if (gl->m_lineSprite) gl->m_lineSprite->setColor(colors::LAYOUT_WHITE);
    };
    tintGround(m_playLayer->m_groundLayer);
    tintGround(m_playLayer->m_groundLayer2);

    if (auto* em = m_playLayer->m_effectManager) {
        for (ColorAction* ca : em->m_colorActionVector) {
            if (!ca) continue;
            ccColor3B target = (ca->m_colorID == 1000) ? colors::LAYOUT_BG_GREY
                             : (ca->m_colorID == 1001 || ca->m_colorID == 1009) ? colors::LAYOUT_GROUND_GREY
                             : colors::LAYOUT_WHITE;
            ca->m_color = target;
            ca->m_fromColor = target;
            ca->m_toColor = target;
            ca->m_currentOpacity = 1.0f;
        }
        for (ColorActionSprite* cas : em->m_colorActionSpriteVector) {
            if (!cas) continue;
            ccColor3B target = (cas->m_colorID == 1000) ? colors::LAYOUT_BG_GREY
                             : (cas->m_colorID == 1001 || cas->m_colorID == 1009) ? colors::LAYOUT_GROUND_GREY
                             : colors::LAYOUT_WHITE;
            cas->m_color = target;
            cas->m_opacity = 255.0f;
        }
    }
}

void HitbordersOverlay::restoreLayoutMode() {
    if (!m_playLayer || !m_layoutActive) return;

    for (const DecorationEntry& entry : m_decorations) {
        if (entry.obj) {
            entry.obj->setVisible(entry.origVisible);
        }
    }

    if (m_playLayer->m_background) {
        m_playLayer->m_background->setColor(m_savedBgColor);
    }
    if (m_playLayer->m_groundLayer) {
        if (m_playLayer->m_groundLayer->m_ground1Sprite) {
            m_playLayer->m_groundLayer->m_ground1Sprite->setColor(m_savedG1Color);
        }
        if (m_playLayer->m_groundLayer->m_ground2Sprite) {
            m_playLayer->m_groundLayer->m_ground2Sprite->setColor(m_savedG2Color);
        }
    }

    for (const SavedColorAction& saved : m_savedColorActions) {
        if (!saved.action) continue;
        saved.action->m_color = saved.color;
        saved.action->m_fromColor = saved.fromColor;
        saved.action->m_toColor = saved.toColor;
        saved.action->m_currentOpacity = saved.opacity;
    }
    m_savedColorActions.clear();
    m_layoutActive = false;
}

void HitbordersOverlay::onPlayerStep(PlayerObject* player) {
    if (!player || !m_wasEnabled) return;

    CCPoint p = player->getPosition();
    Vec2 pos{p.x, p.y};
    if (player->m_isSpider && m_hasPrevStep &&
        std::fabs(pos.y - m_prevStepPos.y) > constants::SPIDER_TELEPORT_Y_JUMP) {
        m_spiderTeleportThisFrame = true;
    }

    m_prevStepPos = pos;
    m_hasPrevStep = true;
    m_steppedThisFrame = true;
    m_trail.pushSample(pos, player->m_isDart);
}

void HitbordersOverlay::onResetLevel() {
    clearAllDrawNodes();
    m_trail.clear();
    m_pastSegments.clear();
    m_hasPrevStep = false;
    m_steppedThisFrame = false;
    m_spiderTeleportThisFrame = false;
    m_forceStaticRebuild = true;
    m_lastStaticLineRadius = -1.0f;
    m_lastTrailLineRadius = -1.0f;
}

void HitbordersOverlay::update(float /*dt*/) {
    if (!m_playLayer) return;

    Settings settings = loadSettings();
    if (!settings.enabled) {
        if (m_wasEnabled) {
            restorePlayerAppearance();
            restoreLayoutMode();
            clearAllDrawNodes();
            m_trail.clear();
            m_pastSegments.clear();
            m_hasPrevStep = false;
            m_steppedThisFrame = false;
            m_spiderTeleportThisFrame = false;
            m_forceStaticRebuild = true;
            m_wasEnabled = false;
        }
        return;
    }

    if (!m_wasEnabled) {
        m_wasEnabled = true;
        m_forceStaticRebuild = true;
    }

    applyPlayerAppearance(settings.playerOpacity, settings.hideWaveTrail);
    applyLayoutMode(settings.layoutMode);

    PlayerObject* player = m_playLayer->m_player1;
    if (player && !m_steppedThisFrame) {
        onPlayerStep(player);
    }
    m_steppedThisFrame = false;

    Rect2 viewRect = computeViewWorldRect();
    float lineRadius = computeWorldLineRadius(m_playLayer->m_objectLayer, settings.lineThickness);

    bool isSpiderTeleport = m_spiderTeleportThisFrame;
    m_spiderTeleportThisFrame = false;

    PlayerState liveState = getLivePlayerState(player, isSpiderTeleport);
    Vec2 playerPos = player ? Vec2{player->getPositionX(), player->getPositionY()} : viewRect.center();
    float lookaheadMaxX = viewRect.maxX + viewRect.width() * constants::LOOKAHEAD_EXTRA_SCREENS;

    updatePastSegments(liveState, playerPos.x);
    buildLookaheadSegments(m_pastSegments, liveState, playerPos.x, lookaheadMaxX, m_lookaheadPortals, m_segments);
    uint64_t signature = computeHitboxSignature(m_segments, liveState.isSpiderTeleport);

    bool viewLeftBuilt = !m_builtRect.contains(viewRect);
    bool lineChanged = (m_lastStaticLineRadius < 0.0f) ||
        (std::fabs(lineRadius - m_lastStaticLineRadius) > m_lastStaticLineRadius * 0.02f);
    bool styleChanged = (settings.mergeOutlines != m_lastMergeOutlines) ||
        (std::fabs(settings.fillOpacity - m_lastFillOpacity) > 1e-3f) ||
        (settings.showInteractables != m_lastShowInteractables) ||
        (settings.glow != m_lastGlow) ||
        (settings.hideInsideHazards != m_lastHideInsideHazards) ||
        (settings.onlyReachableBorders != m_lastOnlyReachable) ||
        !colors::equal3B(settings.hazardColor, m_lastHazardColor) ||
        !colors::equal3B(settings.solidColor, m_lastSolidColor) ||
        !colors::equal3B(settings.portalColor, m_lastPortalColor);

    if (m_forceStaticRebuild || viewLeftBuilt || signature != m_lastSignature || lineChanged || styleChanged) {
        rebuildStaticGeometry(viewRect, playerPos, settings, lineRadius, liveState.isSpiderTeleport);
        m_lastSignature = signature;
        m_lastStaticLineRadius = lineRadius;
        m_lastMergeOutlines = settings.mergeOutlines;
        m_lastFillOpacity = settings.fillOpacity;
        m_lastShowInteractables = settings.showInteractables;
        m_lastHideInsideHazards = settings.hideInsideHazards;
        m_lastOnlyReachable = settings.onlyReachableBorders;
        m_lastHazardColor = settings.hazardColor;
        m_lastSolidColor = settings.solidColor;
        m_lastPortalColor = settings.portalColor;
        m_forceStaticRebuild = false;
    }

    rebuildDynamicGeometry(viewRect, playerPos, settings, lineRadius, liveState.isSpiderTeleport);
    updateTrailNodes(settings, lineRadius, player);
    m_lastGlow = settings.glow;
    m_lastPathColor = settings.pathColor;
    m_lastWavePathColor = settings.wavePathColor;
}

} // namespace hitborders
