#include "Geometry.hpp"

#include <algorithm>
#include <cmath>

namespace hitborders {

namespace {

constexpr float PI = 3.14159265358979323846f;

inline float cross2(const Vec2& o, const Vec2& a, const Vec2& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

inline float distSq(const Vec2& a, const Vec2& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

inline bool isNearVerticalEdge(const Vec2& a, const Vec2& b) {
    float dx = std::fabs(b.x - a.x);
    float dy = std::fabs(b.y - a.y);
    return dy > 1e-4f && dx < 0.05f * dy;
}

} // namespace

void ConvexPoly::computeBounds() {
    if (count <= 0) {
        bounds = {0.0f, 0.0f, 0.0f, 0.0f};
        return;
    }
    float minX = verts[0].x;
    float maxX = verts[0].x;
    float minY = verts[0].y;
    float maxY = verts[0].y;
    for (int i = 1; i < count; ++i) {
        minX = std::min(minX, verts[i].x);
        maxX = std::max(maxX, verts[i].x);
        minY = std::min(minY, verts[i].y);
        maxY = std::max(maxY, verts[i].y);
    }
    bounds = {minX, minY, maxX, maxY};
}

void ConvexPoly::ensureCCW() {
    if (count < 3) return;
    double area2 = 0.0;
    for (int i = 0; i < count; ++i) {
        const Vec2& p = verts[i];
        const Vec2& q = verts[(i + 1) % count];
        area2 += static_cast<double>(p.x) * q.y - static_cast<double>(q.x) * p.y;
    }
    if (area2 < 0.0) {
        std::reverse(verts.begin(), verts.begin() + count);
    }
}

ConvexPoly makeRoundedRect(const Rect2& inner, float cornerRadius, int arcSegmentsPerCorner) {
    ConvexPoly poly;
    if (cornerRadius <= 1e-4f) {
        poly.verts[0] = {inner.minX, inner.minY};
        poly.verts[1] = {inner.maxX, inner.minY};
        poly.verts[2] = {inner.maxX, inner.maxY};
        poly.verts[3] = {inner.minX, inner.maxY};
        poly.count = 4;
        poly.computeBounds();
        return poly;
    }

    int segs = std::clamp(arcSegmentsPerCorner, 1, 6);
    const Vec2 centers[4] = {
        {inner.maxX, inner.minY}, // bottom-right: -pi/2 .. 0
        {inner.maxX, inner.maxY}, // top-right:    0 .. pi/2
        {inner.minX, inner.maxY}, // top-left:     pi/2 .. pi
        {inner.minX, inner.minY}  // bottom-left:  pi .. 3*pi/2
    };
    const float startAngles[4] = {
        -0.5f * PI,
        0.0f,
        0.5f * PI,
        PI
    };

    for (int c = 0; c < 4; ++c) {
        for (int s = 0; s <= segs; ++s) {
            float angle = startAngles[c] + (0.5f * PI) * (static_cast<float>(s) / static_cast<float>(segs));
            Vec2 pt{
                centers[c].x + cornerRadius * std::cos(angle),
                centers[c].y + cornerRadius * std::sin(angle)
            };
            if (poly.count > 0 && distSq(poly.verts[poly.count - 1], pt) < 1e-6f) {
                continue;
            }
            if (poly.count < ConvexPoly::MAX_VERTS) {
                poly.verts[poly.count++] = pt;
            }
        }
    }

    if (poly.count > 1 && distSq(poly.verts[0], poly.verts[poly.count - 1]) < 1e-6f) {
        --poly.count;
    }

    poly.ensureCCW();
    poly.computeBounds();
    return poly;
}

ConvexPoly makeInflatedRect(const Rect2& rect, const HitboxShape& hitbox) {
    if (!hitbox.isDisc) {
        ConvexPoly poly;
        float x0 = rect.minX - hitbox.halfW;
        float y0 = rect.minY - hitbox.halfH;
        float x1 = rect.maxX + hitbox.halfW;
        float y1 = rect.maxY + hitbox.halfH;
        poly.verts[0] = {x0, y0};
        poly.verts[1] = {x1, y0};
        poly.verts[2] = {x1, y1};
        poly.verts[3] = {x0, y1};
        poly.count = 4;
        poly.bounds = {x0, y0, x1, y1};
        return poly;
    }
    return makeRoundedRect(rect, hitbox.radius, 4);
}

ConvexPoly makeInflatedCircle(const Vec2& center, float objectRadius, const HitboxShape& hitbox) {
    if (!hitbox.isDisc) {
        Rect2 inner{
            center.x - hitbox.halfW,
            center.y - hitbox.halfH,
            center.x + hitbox.halfW,
            center.y + hitbox.halfH
        };
        return makeRoundedRect(inner, objectRadius, 4);
    }
    Rect2 pt{center.x, center.y, center.x, center.y};
    return makeRoundedRect(pt, objectRadius + hitbox.radius, 5);
}

ConvexPoly makeSlopeTriangle(const Rect2& rect, int slopeDirection, bool slopeUphill) {
    // Verified GD slope orientation mapping:
    // floorTop = m_slopeDirection in {1, 3, 5, 6}
    // right-angle corner y = floorTop ? top : bottom
    // right-angle corner x = (slopeUphill == floorTop) ? left : right
    bool floorTop = (slopeDirection == 1 || slopeDirection == 3 ||
                     slopeDirection == 5 || slopeDirection == 6);
    bool rightAngleLeft = (slopeUphill == floorTop);

    float rx = rightAngleLeft ? rect.minX : rect.maxX;
    float ry = floorTop ? rect.maxY : rect.minY;
    float ox = rightAngleLeft ? rect.maxX : rect.minX;
    float oy = floorTop ? rect.minY : rect.maxY;

    ConvexPoly tri;
    tri.verts[0] = {rx, ry};
    tri.verts[1] = {ox, ry};
    tri.verts[2] = {rx, oy};
    tri.count = 3;
    tri.ensureCCW();
    tri.computeBounds();
    return tri;
}

ConvexPoly makeSlopeTriangleFromTransform(const Rect2& rect, bool flipX, bool flipY, float rotationDeg) {
    // In GD, all unrotated unflipped slope sprites start with the right-angle corner
    // at bottom-right (+1, -1). Flips apply in local space, then Cocos2d clockwise rotation.
    int rx = flipX ? -1 : 1;
    int ry = flipY ? 1 : -1;

    int q = static_cast<int>(std::lround(rotationDeg / 90.0f)) % 4;
    if (q < 0) q += 4;

    int rotX = rx;
    int rotY = ry;
    switch (q) {
        case 1: rotX = ry;  rotY = -rx; break; // 90 deg CW
        case 2: rotX = -rx; rotY = -ry; break; // 180 deg
        case 3: rotX = -ry; rotY = rx;  break; // 270 deg CW
        default: break;
    }

    float cornerX = (rotX < 0) ? rect.minX : rect.maxX;
    float cornerY = (rotY < 0) ? rect.minY : rect.maxY;
    float otherX = (rotX < 0) ? rect.maxX : rect.minX;
    float otherY = (rotY < 0) ? rect.maxY : rect.minY;

    ConvexPoly tri;
    tri.verts[0] = {cornerX, cornerY};
    tri.verts[1] = {otherX, cornerY};
    tri.verts[2] = {cornerX, otherY};
    tri.count = 3;
    tri.ensureCCW();
    tri.computeBounds();
    return tri;
}

ConvexPoly convexHull(const Vec2* points, int count) {
    ConvexPoly out;
    if (count <= 0) return out;

    std::array<Vec2, 64> sorted{};
    int n = std::min(count, 64);
    for (int i = 0; i < n; ++i) {
        sorted[i] = points[i];
    }

    std::sort(sorted.begin(), sorted.begin() + n, [](const Vec2& a, const Vec2& b) {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });

    // Deduplicate near-identical points
    int uniqueCount = 0;
    for (int i = 0; i < n; ++i) {
        if (uniqueCount == 0 || distSq(sorted[uniqueCount - 1], sorted[i]) > 1e-6f) {
            sorted[uniqueCount++] = sorted[i];
        }
    }

    if (uniqueCount <= 2) {
        for (int i = 0; i < uniqueCount && i < ConvexPoly::MAX_VERTS; ++i) {
            out.verts[out.count++] = sorted[i];
        }
        out.computeBounds();
        return out;
    }

    std::array<Vec2, 64> hull{};
    int k = 0;

    // Lower hull
    for (int i = 0; i < uniqueCount; ++i) {
        while (k >= 2 && cross2(hull[k - 2], hull[k - 1], sorted[i]) <= 1e-5f) {
            --k;
        }
        hull[k++] = sorted[i];
    }

    // Upper hull
    int lowerSize = k + 1;
    for (int i = uniqueCount - 2; i >= 0; --i) {
        while (k >= lowerSize && cross2(hull[k - 2], hull[k - 1], sorted[i]) <= 1e-5f) {
            --k;
        }
        hull[k++] = sorted[i];
    }

    int hullCount = std::min(k - 1, ConvexPoly::MAX_VERTS);
    for (int i = 0; i < hullCount; ++i) {
        out.verts[i] = hull[i];
    }
    out.count = hullCount;
    out.computeBounds();
    return out;
}

ConvexPoly makeInflatedSlope(const ConvexPoly& tri, const HitboxShape& hitbox) {
    if (tri.count < 3) return tri;

    if (!hitbox.isDisc) {
        std::array<Vec2, 12> pts{};
        int idx = 0;
        const float dx[4] = {-hitbox.halfW, hitbox.halfW, hitbox.halfW, -hitbox.halfW};
        const float dy[4] = {-hitbox.halfH, -hitbox.halfH, hitbox.halfH, hitbox.halfH};
        for (int i = 0; i < 3; ++i) {
            for (int c = 0; c < 4; ++c) {
                pts[idx++] = {tri.verts[i].x + dx[c], tri.verts[i].y + dy[c]};
            }
        }
        return convexHull(pts.data(), idx);
    }

    // Disc Minkowski sum: 8-point disc around each triangle vertex -> convex hull (24 pts)
    constexpr int DISC_SAMPLES = 8;
    std::array<Vec2, 3 * DISC_SAMPLES> pts{};
    int idx = 0;
    for (int i = 0; i < 3; ++i) {
        for (int s = 0; s < DISC_SAMPLES; ++s) {
            float angle = (2.0f * PI * static_cast<float>(s)) / static_cast<float>(DISC_SAMPLES);
            pts[idx++] = {
                tri.verts[i].x + hitbox.radius * std::cos(angle),
                tri.verts[i].y + hitbox.radius * std::sin(angle)
            };
        }
    }
    return convexHull(pts.data(), idx);
}

bool pointInsideConvexPoly(const Vec2& p, const ConvexPoly& poly, float eps) {
    if (poly.count < 3) return false;
    if (p.x < poly.bounds.minX - eps || p.x > poly.bounds.maxX + eps ||
        p.y < poly.bounds.minY - eps || p.y > poly.bounds.maxY + eps) {
        return false;
    }
    for (int k = 0; k < poly.count; ++k) {
        const Vec2& v0 = poly.verts[k];
        const Vec2& v1 = poly.verts[(k + 1) % poly.count];
        float ex = v1.x - v0.x;
        float ey = v1.y - v0.y;
        float len = std::sqrt(ex * ex + ey * ey);
        if (len < 1e-6f) continue;
        float nx = ey / len;
        float ny = -ex / len;
        float dist = (p.x - v0.x) * nx + (p.y - v0.y) * ny;
        if (dist > eps) return false;
    }
    return true;
}

bool polyInsideConvexPoly(const ConvexPoly& inner, const ConvexPoly& outer, float eps) {
    if (inner.count < 3 || outer.count < 3) return false;
    if (inner.bounds.minX < outer.bounds.minX - eps ||
        inner.bounds.maxX > outer.bounds.maxX + eps ||
        inner.bounds.minY < outer.bounds.minY - eps ||
        inner.bounds.maxY > outer.bounds.maxY + eps) {
        return false;
    }
    for (int i = 0; i < inner.count; ++i) {
        if (!pointInsideConvexPoly(inner.verts[i], outer, eps)) {
            return false;
        }
    }
    return true;
}

OutlineMerger::OutlineMerger() {
    m_bucketHead.fill(-1);
    m_bucketStamp.fill(0);
    m_entries.reserve(2048);
    m_visited.reserve(1024);
    m_intervals.reserve(32);
}

int OutlineMerger::hashCell(int cx, int cy) {
    uint32_t h = static_cast<uint32_t>(cx) * 73856093u ^ static_cast<uint32_t>(cy) * 19349663u;
    return static_cast<int>(h & static_cast<uint32_t>(HASH_MASK));
}

void OutlineMerger::insertPolyIntoGrid(int polyIndex, const Rect2& bounds) {
    int minCX = static_cast<int>(std::floor((bounds.minX - CLIP_EPS) / CELL_SIZE));
    int maxCX = static_cast<int>(std::floor((bounds.maxX + CLIP_EPS) / CELL_SIZE));
    int minCY = static_cast<int>(std::floor((bounds.minY - CLIP_EPS) / CELL_SIZE));
    int maxCY = static_cast<int>(std::floor((bounds.maxY + CLIP_EPS) / CELL_SIZE));

    // Guard against degenerate giant polygons
    if (maxCX - minCX > 64) maxCX = minCX + 64;
    if (maxCY - minCY > 64) maxCY = minCY + 64;

    for (int cy = minCY; cy <= maxCY; ++cy) {
        for (int cx = minCX; cx <= maxCX; ++cx) {
            int bucket = hashCell(cx, cy);
            int head = (m_bucketStamp[bucket] == m_gridStamp) ? m_bucketHead[bucket] : -1;
            int entryIdx = static_cast<int>(m_entries.size());
            m_entries.push_back({polyIndex, head});
            m_bucketHead[bucket] = entryIdx;
            m_bucketStamp[bucket] = m_gridStamp;
        }
    }
}

bool OutlineMerger::clipEdgeAgainstPoly(
    const Vec2& a,
    const Vec2& b,
    const ConvexPoly& poly,
    float eps,
    float& outT0,
    float& outT1
) {
    if (poly.count < 3) return false;

    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float tMin = 0.0f;
    float tMax = 1.0f;

    for (int k = 0; k < poly.count; ++k) {
        const Vec2& v0 = poly.verts[k];
        const Vec2& v1 = poly.verts[(k + 1) % poly.count];

        float ex = v1.x - v0.x;
        float ey = v1.y - v0.y;
        float len = std::sqrt(ex * ex + ey * ey);
        if (len < 1e-6f) continue;

        // Outward unit normal for CCW polygon edge (ex, ey) is (ey, -ex) / len
        float nx = ey / len;
        float ny = -ex / len;

        // Inside condition with shrink eps: dot(P(t) - v0, N) <= -eps
        // => dot(a - v0, N) + eps + t * dot(d, N) <= 0
        float num = (a.x - v0.x) * nx + (a.y - v0.y) * ny + eps;
        float denom = dx * nx + dy * ny;

        if (std::fabs(denom) < 1e-7f) {
            if (num > 0.0f) {
                return false;
            }
            continue;
        }

        float t = -num / denom;
        if (denom > 0.0f) {
            if (t < tMax) tMax = t;
        } else {
            if (t > tMin) tMin = t;
        }

        if (tMin >= tMax) {
            return false;
        }
    }

    if (tMax - tMin <= 1e-5f) {
        return false;
    }

    outT0 = tMin;
    outT1 = tMax;
    return true;
}

void OutlineMerger::merge(
    const std::vector<ConvexPoly>& polys,
    std::vector<Segment2>& outSegments,
    bool skipNearVertical
) {
    outSegments.clear();
    const int n = static_cast<int>(polys.size());
    if (n == 0) return;

    ++m_gridStamp;
    if (m_gridStamp == 0) {
        m_bucketStamp.fill(0);
        m_gridStamp = 1;
    }
    m_entries.clear();

    if (static_cast<int>(m_visited.size()) < n) {
        m_visited.assign(n, 0);
        m_visitStamp = 1;
    }

    for (int i = 0; i < n; ++i) {
        if (polys[i].count >= 3) {
            insertPolyIntoGrid(i, polys[i].bounds);
        }
    }

    for (int i = 0; i < n; ++i) {
        const ConvexPoly& poly = polys[i];
        if (poly.count < 3) continue;

        for (int e = 0; e < poly.count; ++e) {
            const Vec2& a = poly.verts[e];
            const Vec2& b = poly.verts[(e + 1) % poly.count];

            if (distSq(a, b) < 1e-6f) continue;
            if (skipNearVertical && isNearVerticalEdge(a, b)) continue;

            Rect2 edgeBounds{
                std::min(a.x, b.x),
                std::min(a.y, b.y),
                std::max(a.x, b.x),
                std::max(a.y, b.y)
            };

            ++m_visitStamp;
            if (m_visitStamp == 0) {
                std::fill(m_visited.begin(), m_visited.end(), 0);
                m_visitStamp = 1;
            }
            m_intervals.clear();

            int minCX = static_cast<int>(std::floor((edgeBounds.minX - CLIP_EPS) / CELL_SIZE));
            int maxCX = static_cast<int>(std::floor((edgeBounds.maxX + CLIP_EPS) / CELL_SIZE));
            int minCY = static_cast<int>(std::floor((edgeBounds.minY - CLIP_EPS) / CELL_SIZE));
            int maxCY = static_cast<int>(std::floor((edgeBounds.maxY + CLIP_EPS) / CELL_SIZE));

            if (maxCX - minCX > 64) maxCX = minCX + 64;
            if (maxCY - minCY > 64) maxCY = minCY + 64;

            for (int cy = minCY; cy <= maxCY; ++cy) {
                for (int cx = minCX; cx <= maxCX; ++cx) {
                    int bucket = hashCell(cx, cy);
                    if (m_bucketStamp[bucket] != m_gridStamp) continue;

                    for (int entryIdx = m_bucketHead[bucket]; entryIdx != -1; entryIdx = m_entries[entryIdx].next) {
                        int j = m_entries[entryIdx].polyIndex;
                        if (j == i || m_visited[j] == m_visitStamp) continue;
                        m_visited[j] = m_visitStamp;

                        const ConvexPoly& other = polys[j];
                        if (!edgeBounds.overlaps(other.bounds, CLIP_EPS * 2.0f)) continue;

                        // Lower-index polygon uses negative eps (boundary counts as inside)
                        // so collinear overlapping edges are drawn exactly once.
                        float eps = (j < i) ? -CLIP_EPS : CLIP_EPS;
                        float t0 = 0.0f;
                        float t1 = 0.0f;
                        if (clipEdgeAgainstPoly(a, b, other, eps, t0, t1)) {
                            m_intervals.push_back({t0, t1});
                        }
                    }
                }
            }

            if (m_intervals.empty()) {
                outSegments.push_back({a, b});
                continue;
            }

            std::sort(m_intervals.begin(), m_intervals.end(), [](const Interval& u, const Interval& v) {
                return u.t0 < v.t0;
            });

            float cursor = 0.0f;
            float dx = b.x - a.x;
            float dy = b.y - a.y;

            for (const Interval& iv : m_intervals) {
                if (iv.t0 > cursor + 1e-4f) {
                    outSegments.push_back({
                        {a.x + dx * cursor, a.y + dy * cursor},
                        {a.x + dx * iv.t0, a.y + dy * iv.t0}
                    });
                }
                if (iv.t1 > cursor) {
                    cursor = iv.t1;
                }
                if (cursor >= 1.0f - 1e-4f) break;
            }

            if (1.0f > cursor + 1e-4f) {
                outSegments.push_back({
                    {a.x + dx * cursor, a.y + dy * cursor},
                    b
                });
            }
        }
    }
}

void OutlineMerger::emitUnmerged(
    const std::vector<ConvexPoly>& polys,
    std::vector<Segment2>& outSegments,
    bool skipNearVertical
) {
    outSegments.clear();
    for (const ConvexPoly& poly : polys) {
        if (poly.count < 3) continue;
        for (int e = 0; e < poly.count; ++e) {
            const Vec2& a = poly.verts[e];
            const Vec2& b = poly.verts[(e + 1) % poly.count];
            if (distSq(a, b) < 1e-6f) continue;
            if (skipNearVertical && isNearVerticalEdge(a, b)) continue;
            outSegments.push_back({a, b});
        }
    }
}

void OutlineMerger::buildBlockerGrid(const std::vector<ConvexPoly>& blockers) {
    const int n = static_cast<int>(blockers.size());
    ++m_gridStamp;
    if (m_gridStamp == 0) {
        m_bucketStamp.fill(0);
        m_gridStamp = 1;
    }
    m_entries.clear();

    if (static_cast<int>(m_visited.size()) < n) {
        m_visited.assign(n, 0);
        m_visitStamp = 1;
    }

    for (int i = 0; i < n; ++i) {
        if (blockers[i].count >= 3) {
            insertPolyIntoGrid(i, blockers[i].bounds);
        }
    }
}

bool OutlineMerger::isPolyInsideAnyBlocker(
    const ConvexPoly& poly,
    const std::vector<ConvexPoly>& blockers,
    int skipIndex
) {
    if (poly.count < 3 || blockers.empty()) return false;

    ++m_visitStamp;
    if (m_visitStamp == 0) {
        std::fill(m_visited.begin(), m_visited.end(), 0);
        m_visitStamp = 1;
    }

    int minCX = static_cast<int>(std::floor((poly.bounds.minX - CLIP_EPS) / CELL_SIZE));
    int maxCX = static_cast<int>(std::floor((poly.bounds.maxX + CLIP_EPS) / CELL_SIZE));
    int minCY = static_cast<int>(std::floor((poly.bounds.minY - CLIP_EPS) / CELL_SIZE));
    int maxCY = static_cast<int>(std::floor((poly.bounds.maxY + CLIP_EPS) / CELL_SIZE));

    if (maxCX - minCX > 64) maxCX = minCX + 64;
    if (maxCY - minCY > 64) maxCY = minCY + 64;

    for (int cy = minCY; cy <= maxCY; ++cy) {
        for (int cx = minCX; cx <= maxCX; ++cx) {
            int bucket = hashCell(cx, cy);
            if (m_bucketStamp[bucket] != m_gridStamp) continue;

            for (int entryIdx = m_bucketHead[bucket]; entryIdx != -1; entryIdx = m_entries[entryIdx].next) {
                int j = m_entries[entryIdx].polyIndex;
                if (j == skipIndex || m_visited[j] == m_visitStamp) continue;
                m_visited[j] = m_visitStamp;

                if (polyInsideConvexPoly(poly, blockers[j], 1e-2f)) {
                    // When culling within the same list (skipIndex >= 0), break ties on identical
                    // polygons by keeping the lower-index polygon.
                    if (skipIndex >= 0 && j > skipIndex && polyInsideConvexPoly(blockers[j], poly, 1e-2f)) {
                        continue;
                    }
                    return true;
                }
            }
        }
    }
    return false;
}

void OutlineMerger::clipSegmentsAgainstBlockers(
    const std::vector<ConvexPoly>& blockers,
    const std::vector<Segment2>& inSegments,
    std::vector<Segment2>& outSegments
) {
    outSegments.clear();
    if (inSegments.empty()) return;
    if (blockers.empty()) {
        outSegments = inSegments;
        return;
    }

    buildBlockerGrid(blockers);

    for (const Segment2& seg : inSegments) {
        const Vec2& a = seg.a;
        const Vec2& b = seg.b;
        if (distSq(a, b) < 1e-6f) continue;

        Rect2 edgeBounds{
            std::min(a.x, b.x),
            std::min(a.y, b.y),
            std::max(a.x, b.x),
            std::max(a.y, b.y)
        };

        ++m_visitStamp;
        if (m_visitStamp == 0) {
            std::fill(m_visited.begin(), m_visited.end(), 0);
            m_visitStamp = 1;
        }
        m_intervals.clear();

        int minCX = static_cast<int>(std::floor((edgeBounds.minX - CLIP_EPS) / CELL_SIZE));
        int maxCX = static_cast<int>(std::floor((edgeBounds.maxX + CLIP_EPS) / CELL_SIZE));
        int minCY = static_cast<int>(std::floor((edgeBounds.minY - CLIP_EPS) / CELL_SIZE));
        int maxCY = static_cast<int>(std::floor((edgeBounds.maxY + CLIP_EPS) / CELL_SIZE));

        if (maxCX - minCX > 64) maxCX = minCX + 64;
        if (maxCY - minCY > 64) maxCY = minCY + 64;

        for (int cy = minCY; cy <= maxCY; ++cy) {
            for (int cx = minCX; cx <= maxCX; ++cx) {
                int bucket = hashCell(cx, cy);
                if (m_bucketStamp[bucket] != m_gridStamp) continue;

                for (int entryIdx = m_bucketHead[bucket]; entryIdx != -1; entryIdx = m_entries[entryIdx].next) {
                    int j = m_entries[entryIdx].polyIndex;
                    if (m_visited[j] == m_visitStamp) continue;
                    m_visited[j] = m_visitStamp;

                    const ConvexPoly& blocker = blockers[j];
                    if (!edgeBounds.overlaps(blocker.bounds, CLIP_EPS * 2.0f)) continue;

                    float t0 = 0.0f;
                    float t1 = 0.0f;
                    // Negative eps so edges lying on or inside the hazard boundary are clipped out
                    if (clipEdgeAgainstPoly(a, b, blocker, -CLIP_EPS, t0, t1)) {
                        m_intervals.push_back({t0, t1});
                    }
                }
            }
        }

        if (m_intervals.empty()) {
            outSegments.push_back(seg);
            continue;
        }

        std::sort(m_intervals.begin(), m_intervals.end(), [](const Interval& u, const Interval& v) {
            return u.t0 < v.t0;
        });

        float cursor = 0.0f;
        float dx = b.x - a.x;
        float dy = b.y - a.y;

        for (const Interval& iv : m_intervals) {
            if (iv.t0 > cursor + 1e-4f) {
                outSegments.push_back({
                    {a.x + dx * cursor, a.y + dy * cursor},
                    {a.x + dx * iv.t0, a.y + dy * iv.t0}
                });
            }
            if (iv.t1 > cursor) cursor = iv.t1;
            if (cursor >= 1.0f - 1e-4f) break;
        }

        if (1.0f > cursor + 1e-4f) {
            outSegments.push_back({
                {a.x + dx * cursor, a.y + dy * cursor},
                b
            });
        }
    }
}

ReachabilityGrid::ReachabilityGrid() {
    m_cells.reserve(MAX_COLS * MAX_ROWS);
    m_queue.reserve(MAX_COLS * MAX_ROWS);
}

void ReachabilityGrid::rasterizePolys(const std::vector<ConvexPoly>& polys) {
    for (const ConvexPoly& poly : polys) {
        if (poly.count < 3) continue;
        if (!poly.bounds.overlaps(m_region)) continue;

        int c0 = std::max(0, static_cast<int>(std::floor((poly.bounds.minX - m_region.minX) / m_cellW)));
        int c1 = std::min(m_cols - 1, static_cast<int>(std::floor((poly.bounds.maxX - m_region.minX) / m_cellW)));
        int r0 = std::max(0, static_cast<int>(std::floor((poly.bounds.minY - m_region.minY) / m_cellH)));
        int r1 = std::min(m_rows - 1, static_cast<int>(std::floor((poly.bounds.maxY - m_region.minY) / m_cellH)));

        for (int r = r0; r <= r1; ++r) {
            float cy = m_region.minY + (static_cast<float>(r) + 0.5f) * m_cellH;
            int rowBase = r * m_cols;
            for (int c = c0; c <= c1; ++c) {
                if (m_cells[rowBase + c] == 1) continue;
                float cx = m_region.minX + (static_cast<float>(c) + 0.5f) * m_cellW;
                // Inset by -1.5 units so tight corridors and floor surfaces are never falsely sealed
                if (pointInsideConvexPoly({cx, cy}, poly, -1.5f)) {
                    m_cells[rowBase + c] = 1;
                }
            }
        }
    }
}

bool ReachabilityGrid::build(
    const Rect2& region,
    const Vec2& seedPos,
    const std::vector<ConvexPoly>& hazards,
    const std::vector<ConvexPoly>& solids
) {
    m_region = region;
    float w = std::max(32.0f, region.width());
    float h = std::max(32.0f, region.height());

    m_cols = std::clamp(static_cast<int>(std::ceil(w / DEFAULT_CELL_SIZE)), 8, MAX_COLS);
    m_rows = std::clamp(static_cast<int>(std::ceil(h / DEFAULT_CELL_SIZE)), 8, MAX_ROWS);
    m_cellW = w / static_cast<float>(m_cols);
    m_cellH = h / static_cast<float>(m_rows);

    m_cells.assign(static_cast<std::size_t>(m_cols * m_rows), 0);
    rasterizePolys(hazards);
    rasterizePolys(solids);

    int sc = std::clamp(static_cast<int>(std::floor((seedPos.x - m_region.minX) / m_cellW)), 0, m_cols - 1);
    int sr = std::clamp(static_cast<int>(std::floor((seedPos.y - m_region.minY) / m_cellH)), 0, m_rows - 1);

    m_queue.clear();
    auto trySeed = [this](int c, int r) {
        if (c < 0 || c >= m_cols || r < 0 || r >= m_rows) return;
        int idx = r * m_cols + c;
        if (m_cells[idx] == 0) {
            m_cells[idx] = 2;
            m_queue.push_back(idx);
        }
    };

    // Seed all open cells in a 3x3 neighborhood around playerPos so touching a floor/slope
    // never misses the adjacent playable corridor.
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            trySeed(sc + dc, sr + dr);
        }
    }
    if (m_queue.empty()) {
        for (int rad = 2; rad <= 5 && m_queue.empty(); ++rad) {
            for (int dr = -rad; dr <= rad; ++dr) {
                for (int dc = -rad; dc <= rad; ++dc) {
                    if (std::abs(dr) != rad && std::abs(dc) != rad) continue;
                    trySeed(sc + dc, sr + dr);
                }
            }
        }
    }

    if (m_queue.empty()) {
        return false;
    }

    std::size_t head = 0;
    while (head < m_queue.size()) {
        int idx = m_queue[head++];
        int c = idx % m_cols;
        int r = idx / m_cols;

        if (c > 0 && m_cells[idx - 1] == 0) {
            m_cells[idx - 1] = 2;
            m_queue.push_back(idx - 1);
        }
        if (c + 1 < m_cols && m_cells[idx + 1] == 0) {
            m_cells[idx + 1] = 2;
            m_queue.push_back(idx + 1);
        }
        if (r > 0 && m_cells[idx - m_cols] == 0) {
            m_cells[idx - m_cols] = 2;
            m_queue.push_back(idx - m_cols);
        }
        if (r + 1 < m_rows && m_cells[idx + m_cols] == 0) {
            m_cells[idx + m_cols] = 2;
            m_queue.push_back(idx + m_cols);
        }
    }

    return true;
}

bool ReachabilityGrid::isPolyReachable(const ConvexPoly& poly) const {
    if (m_cols <= 0 || m_rows <= 0 || poly.count < 3) return true;
    // Keep polygons outside the grid region untouched
    if (!poly.bounds.overlaps(m_region)) return true;

    float probeOffset = std::max(m_cellW, m_cellH) * 0.85f;
    for (int i = 0; i < poly.count; ++i) {
        const Vec2& a = poly.verts[i];
        const Vec2& b = poly.verts[(i + 1) % poly.count];
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-5f) continue;

        // Outward normal for CCW polygon edge (dx, dy) is (dy, -dx) / len
        float nx = (dy / len) * probeOffset;
        float ny = (-dx / len) * probeOffset;

        constexpr float ts[3] = {0.2f, 0.5f, 0.8f};
        for (float t : ts) {
            Vec2 outPt{a.x + dx * t + nx, a.y + dy * t + ny};
            if (isPointReachable(outPt)) {
                return true;
            }
        }
    }
    return false;
}

bool ReachabilityGrid::isPointReachable(const Vec2& p) const {
    if (m_cols <= 0 || m_rows <= 0) return true;
    int c = static_cast<int>(std::floor((p.x - m_region.minX) / m_cellW));
    int r = static_cast<int>(std::floor((p.y - m_region.minY) / m_cellH));
    if (c < 0 || c >= m_cols || r < 0 || r >= m_rows) return true;
    return m_cells[r * m_cols + c] == 2;
}

void ReachabilityGrid::filterSegments(
    const std::vector<Segment2>& inSegments,
    std::vector<Segment2>& outSegments
) const {
    outSegments.clear();
    if (inSegments.empty()) return;

    float probeOffset = std::max(m_cellW, m_cellH) * 0.75f;

    for (const Segment2& seg : inSegments) {
        float dx = seg.b.x - seg.a.x;
        float dy = seg.b.y - seg.a.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-5f) continue;

        float nx = (dy / len) * probeOffset;
        float ny = (-dx / len) * probeOffset;

        bool reachable = false;
        constexpr float ts[3] = {0.25f, 0.5f, 0.75f};
        for (float t : ts) {
            Vec2 pt{seg.a.x + dx * t, seg.a.y + dy * t};
            if (isPointReachable({pt.x + nx, pt.y + ny}) ||
                isPointReachable({pt.x - nx, pt.y - ny})) {
                reachable = true;
                break;
            }
        }

        if (reachable) {
            outSegments.push_back(seg);
        }
    }
}

} // namespace hitborders
