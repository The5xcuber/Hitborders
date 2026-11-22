#include "Trail.hpp"

#include <cmath>

namespace hitborders {

namespace {

inline float pointToLineDistance(const Vec2& p, const Vec2& a, const Vec2& b) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float lenSq = dx * dx + dy * dy;
    if (lenSq < 1e-8f) {
        float px = p.x - a.x;
        float py = p.y - a.y;
        return std::sqrt(px * px + py * py);
    }
    float area2 = std::fabs((p.x - a.x) * dy - (p.y - a.y) * dx);
    return area2 / std::sqrt(lenSq);
}

inline float distSq(const Vec2& a, const Vec2& b) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    return dx * dx + dy * dy;
}

} // namespace

TrailTracker::TrailTracker() {
    m_committed.reserve(MAX_SEGMENTS + 64);
}

void TrailTracker::clear() {
    m_committed.clear();
    m_drawnCount = 0;
    m_needsFullRebuild = true;
    m_hasLastCommitted = false;
    m_hasPrevSample = false;
    m_headIsWave = false;
}

void TrailTracker::appendCommitted(const Vec2& to, bool isWave) {
    if (distSq(m_lastCommitted, to) < 1e-6f) {
        return;
    }
    m_committed.push_back({m_lastCommitted, to, isWave});
    m_lastCommitted = to;

    // Trim oldest chunk if history exceeds cap
    if (m_committed.size() > MAX_SEGMENTS) {
        constexpr std::size_t TRIM_CHUNK = 512;
        std::size_t eraseCount = std::min(TRIM_CHUNK, m_committed.size() - MAX_SEGMENTS + TRIM_CHUNK);
        m_committed.erase(m_committed.begin(), m_committed.begin() + static_cast<std::ptrdiff_t>(eraseCount));
        m_drawnCount = 0;
        m_needsFullRebuild = true;
    }
}

void TrailTracker::pushSample(const Vec2& pos, bool isWave) {
    if (!m_hasLastCommitted) {
        m_lastCommitted = pos;
        m_prevSample = pos;
        m_headIsWave = isWave;
        m_hasLastCommitted = true;
        m_hasPrevSample = true;
        return;
    }

    if (distSq(m_prevSample, pos) < 1e-6f && isWave == m_headIsWave) {
        return;
    }

    // 1. Wave / non-wave transition: commit up to previous sample with old color
    if (isWave != m_headIsWave) {
        appendCommitted(m_prevSample, m_headIsWave);
        m_headIsWave = isWave;
        m_prevSample = pos;
        return;
    }

    // 2. Real corner check: distance from previous sample to line(lastCommitted -> pos)
    float cornerDist = pointToLineDistance(m_prevSample, m_lastCommitted, pos);
    if (cornerDist > CORNER_TOL) {
        appendCommitted(m_prevSample, m_headIsWave);
        m_prevSample = pos;
        return;
    }

    // 3. Max run length exceeded
    if (distSq(m_lastCommitted, pos) > MAX_RUN_LENGTH * MAX_RUN_LENGTH) {
        appendCommitted(pos, m_headIsWave);
        m_prevSample = pos;
        return;
    }

    m_prevSample = pos;
}

} // namespace hitborders
