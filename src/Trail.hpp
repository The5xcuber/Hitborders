#pragma once

#include "Geometry.hpp"

#include <cstddef>
#include <vector>

namespace hitborders {

struct TrailSegment {
    Vec2 a{};
    Vec2 b{};
    bool isWave = false;
};

// Pure C++ stream-simplifying trail tracker.
// Commits a point only on:
//  - real corner (distance of previous sample to line from last committed to new sample > CORNER_TOL)
//  - wave/non-wave transition
//  - run length exceeding MAX_RUN_LENGTH
// Uncommitted head is queried every frame so the rendered path never lags.
class TrailTracker {
public:
    static constexpr float CORNER_TOL = 0.1f;
    static constexpr float MAX_RUN_LENGTH = 40.0f;
    static constexpr std::size_t MAX_SEGMENTS = 6000;

    TrailTracker();

    void clear();
    void pushSample(const Vec2& pos, bool isWave);

    const std::vector<TrailSegment>& committed() const { return m_committed; }
    std::size_t drawnCount() const { return m_drawnCount; }
    void markAllDrawn() { m_drawnCount = m_committed.size(); }
    void invalidateDrawn() { m_drawnCount = 0; }

    bool needsFullRebuild() const { return m_needsFullRebuild; }
    void clearFullRebuildFlag() { m_needsFullRebuild = false; }

    bool hasHead() const { return m_hasLastCommitted && m_hasPrevSample; }
    TrailSegment headSegment() const {
        return {m_lastCommitted, m_prevSample, m_headIsWave};
    }

private:
    std::vector<TrailSegment> m_committed;
    std::size_t m_drawnCount = 0;
    bool m_needsFullRebuild = false;

    bool m_hasLastCommitted = false;
    bool m_hasPrevSample = false;
    Vec2 m_lastCommitted{};
    Vec2 m_prevSample{};
    bool m_headIsWave = false;

    void appendCommitted(const Vec2& to, bool isWave);
};

} // namespace hitborders
