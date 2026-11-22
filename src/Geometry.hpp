#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace hitborders {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect2 {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;

    float width() const { return maxX - minX; }
    float height() const { return maxY - minY; }
    Vec2 center() const { return {(minX + maxX) * 0.5f, (minY + maxY) * 0.5f}; }

    bool overlaps(const Rect2& other, float pad = 0.0f) const {
        return !(maxX + pad < other.minX || minX - pad > other.maxX ||
                 maxY + pad < other.minY || minY - pad > other.maxY);
    }

    bool contains(const Rect2& inner) const {
        return inner.minX >= minX && inner.maxX <= maxX &&
               inner.minY >= minY && inner.maxY <= maxY;
    }
};

struct Segment2 {
    Vec2 a{};
    Vec2 b{};
};

struct HitboxShape {
    bool isDisc = false;
    float halfW = 15.0f;
    float halfH = 15.0f;
    float radius = 15.0f;

    static HitboxShape makeBox(float hw, float hh) {
        return {false, hw, hh, 0.0f};
    }

    static HitboxShape makeDisc(float r) {
        return {true, r, r, r};
    }
};

struct ConvexPoly {
    static constexpr int MAX_VERTS = 32;
    std::array<Vec2, MAX_VERTS> verts{};
    int count = 0;
    Rect2 bounds{};

    void computeBounds();
    void ensureCCW();
};

// Pure geometry builders (CCW convex polygons)
ConvexPoly makeRoundedRect(const Rect2& inner, float cornerRadius, int arcSegmentsPerCorner = 4);
ConvexPoly makeInflatedRect(const Rect2& rect, const HitboxShape& hitbox);
ConvexPoly makeInflatedCircle(const Vec2& center, float objectRadius, const HitboxShape& hitbox);
ConvexPoly makeSlopeTriangle(const Rect2& rect, int slopeDirection, bool slopeUphill);
ConvexPoly makeSlopeTriangleFromTransform(const Rect2& rect, bool flipX, bool flipY, float rotationDeg);
ConvexPoly makeInflatedSlope(const ConvexPoly& tri, const HitboxShape& hitbox);
ConvexPoly convexHull(const Vec2* points, int count);

bool pointInsideConvexPoly(const Vec2& p, const ConvexPoly& poly, float eps = 1e-3f);
bool polyInsideConvexPoly(const ConvexPoly& inner, const ConvexPoly& outer, float eps = 1e-2f);

// Reusable spatial-hash + Cyrus-Beck outline merger (zero per-frame heap churn after warmup)
class OutlineMerger {
public:
    static constexpr float CELL_SIZE = 64.0f;
    static constexpr float CLIP_EPS = 1e-3f;
    static constexpr int HASH_BUCKETS = 4096;
    static constexpr int HASH_MASK = HASH_BUCKETS - 1;

    OutlineMerger();

    void merge(
        const std::vector<ConvexPoly>& polys,
        std::vector<Segment2>& outSegments,
        bool skipNearVertical = false
    );

    static void emitUnmerged(
        const std::vector<ConvexPoly>& polys,
        std::vector<Segment2>& outSegments,
        bool skipNearVertical = false
    );

    // Build spatial hash for blockers, then remove segments covered by any blocker polygon.
    void clipSegmentsAgainstBlockers(
        const std::vector<ConvexPoly>& blockers,
        const std::vector<Segment2>& inSegments,
        std::vector<Segment2>& outSegments
    );

    // Check if a polygon is completely contained inside any single blocker polygon.
    // If skipIndex >= 0, ignores blockers[skipIndex] (used for culling hazards inside hazards).
    bool isPolyInsideAnyBlocker(
        const ConvexPoly& poly,
        const std::vector<ConvexPoly>& blockers,
        int skipIndex = -1
    );

    // Prepare spatial hash for a set of blocker polygons (used before isPolyInsideAnyBlocker).
    void buildBlockerGrid(const std::vector<ConvexPoly>& blockers);

private:
    struct GridEntry {
        int polyIndex;
        int next;
    };

    struct Interval {
        float t0;
        float t1;
    };

    std::array<int, HASH_BUCKETS> m_bucketHead{};
    std::array<uint32_t, HASH_BUCKETS> m_bucketStamp{};
    uint32_t m_gridStamp = 1;

    std::vector<GridEntry> m_entries;
    std::vector<uint32_t> m_visited;
    uint32_t m_visitStamp = 1;
    std::vector<Interval> m_intervals;

    static int hashCell(int cx, int cy);
    void insertPolyIntoGrid(int polyIndex, const Rect2& bounds);
    static bool clipEdgeAgainstPoly(
        const Vec2& a,
        const Vec2& b,
        const ConvexPoly& poly,
        float eps,
        float& outT0,
        float& outT1
    );
};

// Coarse 2D grid flood-fill from the player center to identify reachable playable space.
// Used by the opt-in "Only Reachable Borders" filter to remove borders inside walls or sealed pockets.
class ReachabilityGrid {
public:
    static constexpr float DEFAULT_CELL_SIZE = 8.0f;
    static constexpr int MAX_COLS = 256;
    static constexpr int MAX_ROWS = 128;

    ReachabilityGrid();

    // Build grid over region, rasterize blockers, and BFS from seedPos.
    // Returns false if no open seed cell was found near seedPos (e.g. player died inside a hazard).
    bool build(
        const Rect2& region,
        const Vec2& seedPos,
        const std::vector<ConvexPoly>& hazards,
        const std::vector<ConvexPoly>& solids
    );

    // Filter segments, keeping only those adjacent to at least one reachable cell.
    void filterSegments(
        const std::vector<Segment2>& inSegments,
        std::vector<Segment2>& outSegments
    ) const;

    // Returns true if any point just outside the polygon's boundary touches a reachable cell.
    // Polygons buried inside hazard walls or sealed pockets return false.
    bool isPolyReachable(const ConvexPoly& poly) const;

    bool isPointReachable(const Vec2& p) const;

private:
    Rect2 m_region{};
    float m_cellW = DEFAULT_CELL_SIZE;
    float m_cellH = DEFAULT_CELL_SIZE;
    int m_cols = 0;
    int m_rows = 0;
    std::vector<uint8_t> m_cells; // 0 = open, 1 = blocked, 2 = reachable
    std::vector<int> m_queue;

    void rasterizePolys(const std::vector<ConvexPoly>& polys);
};

} // namespace hitborders
