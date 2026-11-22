#pragma once

#include "Common.hpp"
#include "Geometry.hpp"
#include "Inflation.hpp"
#include "Trail.hpp"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace hitborders {

// Stencil-backed flat fill layer: draws all overlapping polygons into the OpenGL stencil buffer
// and shades one single quad at exact fillOpacity so overlapping interiors never stack alpha.
class FlatFillLayer : public cocos2d::CCNode {
public:
    static FlatFillLayer* create();

    void clear();
    void beginBuild(const Rect2& bounds, const cocos2d::ccColor4F& fillColor, bool useStencilMerge);
    void addFillPoly(const ConvexPoly& poly);
    void addBlockerPoly(const ConvexPoly& poly);
    void endBuild();

protected:
    bool initLayer();
    void draw() override;

private:
    cocos2d::CCDrawNode* m_maskNode = nullptr;
    cocos2d::CCDrawNode* m_blockerNode = nullptr;
    cocos2d::CCDrawNode* m_quadNode = nullptr;
    cocos2d::ccColor4F m_fillColor{0.0f, 0.0f, 0.0f, 0.0f};
    Rect2 m_bounds{};
    bool m_useStencilMerge = true;
    bool m_hasFill = false;
    bool m_hasBlockers = false;
};

class HitbordersOverlay : public cocos2d::CCNode {
public:
    static HitbordersOverlay* create(PlayLayer* playLayer);

    void onPlayerStep(PlayerObject* player);
    void onResetLevel();

protected:
    bool initOverlay(PlayLayer* playLayer);
    void update(float dt) override;

private:
    enum class ObjectCategory : uint8_t {
        Solid = 0,
        Hazard,
        Slope,
        Interactable
    };

    struct CachedObject {
        GameObject* obj = nullptr;
        float sortX = 0.0f;
        ObjectCategory category = ObjectCategory::Solid;
    };

    struct InteractablePoly {
        ConvexPoly poly{};
        cocos2d::ccColor4F color{};
        float objScale = 1.0f;
    };

    struct DecorationEntry {
        GameObject* obj = nullptr;
        bool origVisible = true;
    };

    struct SavedColorAction {
        ColorAction* action = nullptr;
        cocos2d::ccColor3B color{};
        cocos2d::ccColor3B fromColor{};
        cocos2d::ccColor3B toColor{};
        float opacity = 1.0f;
    };

    PlayLayer* m_playLayer = nullptr;

    // Draw layers (all configured with GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
    FlatFillLayer* m_staticSolidFill = nullptr;
    FlatFillLayer* m_staticHazardFill = nullptr;
    cocos2d::CCDrawNode* m_staticGlowNode = nullptr;
    cocos2d::CCDrawNode* m_staticOutlineNode = nullptr;

    FlatFillLayer* m_dynamicSolidFill = nullptr;
    FlatFillLayer* m_dynamicHazardFill = nullptr;
    cocos2d::CCDrawNode* m_dynamicGlowNode = nullptr;
    cocos2d::CCDrawNode* m_dynamicOutlineNode = nullptr;

    cocos2d::CCDrawNode* m_interactableNode = nullptr;

    cocos2d::CCDrawNode* m_trailGlowNode = nullptr;
    cocos2d::CCDrawNode* m_trailCommittedNode = nullptr;
    cocos2d::CCDrawNode* m_trailHeadNode = nullptr;

    // Cached object pointers (never cache rects)
    std::vector<CachedObject> m_staticObjects;
    std::vector<CachedObject> m_dynamicObjects;
    std::vector<GameObject*> m_lookaheadPortals;
    std::vector<DecorationEntry> m_decorations;

    // Frozen past mode/size segments so lookahead never retroactively changes passed hitborders
    std::vector<LookaheadSegment> m_pastSegments;

    // Layout mode saved state for clean restore
    bool m_layoutActive = false;
    cocos2d::ccColor3B m_savedBgColor{255, 255, 255};
    cocos2d::ccColor3B m_savedG1Color{255, 255, 255};
    cocos2d::ccColor3B m_savedG2Color{255, 255, 255};
    std::vector<SavedColorAction> m_savedColorActions;

    // Reusable work buffers (no per-frame heap allocations in hot paths)
    std::vector<LookaheadSegment> m_segments;
    std::vector<ConvexPoly> m_solidPolys;
    std::vector<ConvexPoly> m_hazardPolys;
    std::vector<InteractablePoly> m_interactablePolys;
    std::vector<Segment2> m_mergedSegments;
    std::vector<Segment2> m_clippedSegments;
    std::vector<Segment2> m_reachSegments;
    std::vector<Segment2> m_interactSegsIn;
    std::vector<Segment2> m_interactSegsOut;
    OutlineMerger m_merger;
    ReachabilityGrid m_reachGrid;

    // Trail state
    TrailTracker m_trail;
    bool m_hasPrevStep = false;
    Vec2 m_prevStepPos{};
    bool m_steppedThisFrame = false;
    bool m_spiderTeleportThisFrame = false;

    // Rebuild tracking
    bool m_wasEnabled = false;
    bool m_playerModified = false;
    bool m_forceStaticRebuild = true;
    bool m_dynamicHadVisible = false;
    Rect2 m_builtRect{};
    uint64_t m_lastSignature = 0;
    float m_lastStaticLineRadius = -1.0f;
    float m_lastTrailLineRadius = -1.0f;
    bool m_lastMergeOutlines = true;
    float m_lastFillOpacity = -1.0f;
    bool m_lastShowInteractables = true;
    bool m_lastGlow = false;
    bool m_lastHideInsideHazards = false;
    bool m_lastOnlyReachable = false;
    cocos2d::ccColor3B m_lastHazardColor{255, 51, 51};
    cocos2d::ccColor3B m_lastSolidColor{51, 140, 255};
    cocos2d::ccColor3B m_lastPathColor{255, 235, 51};
    cocos2d::ccColor3B m_lastWavePathColor{255, 140, 26};
    cocos2d::ccColor3B m_lastPortalColor{173, 179, 191};

    void cacheLevelObjects();
    static void collectDynamicGroups(cocos2d::CCArray* objects, std::unordered_set<short>& outGroups);
    static bool objectHasDynamicGroup(GameObject* obj, const std::unordered_set<short>& dynamicGroups);
    static bool classifyObject(GameObject* obj, ObjectCategory& outCategory);
    static bool isObjectActive(GameObject* obj, ObjectCategory category);
    static cocos2d::ccColor4F getInteractableColor(GameObjectType type, const Settings& settings);

    void updatePastSegments(const PlayerState& liveState, float playerX);
    Rect2 computeViewWorldRect() const;
    void appendObjectPolygon(
        GameObject* obj,
        ObjectCategory category,
        const std::vector<LookaheadSegment>& segments,
        const Settings& settings
    );
    void appendGroundPolygons(const Rect2& buildBounds);

    void rebuildStaticGeometry(const Rect2& viewRect, const Vec2& playerPos, const Settings& settings, float lineRadius, bool skipNearVertical);
    void rebuildDynamicGeometry(const Rect2& viewRect, const Vec2& playerPos, const Settings& settings, float lineRadius, bool skipNearVertical);
    void renderGeometryLayers(
        FlatFillLayer* solidFillLayer,
        FlatFillLayer* hazardFillLayer,
        cocos2d::CCDrawNode* glowNode,
        cocos2d::CCDrawNode* outlineNode,
        cocos2d::CCDrawNode* interactNode,
        const Rect2& buildBounds,
        const Vec2& playerPos,
        const Settings& settings,
        float lineRadius,
        bool skipNearVertical
    );

    void updateTrailNodes(const Settings& settings, float lineRadius, PlayerObject* player);
    void clearAllDrawNodes();
    void applyPlayerAppearance(float opacity, bool hideWaveTrail);
    void restorePlayerAppearance();
    void applyLayoutMode(bool enable);
    void restoreLayoutMode();
};

} // namespace hitborders
