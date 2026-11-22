#pragma once

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>

namespace hitborders {

// Tunable hitbox constants
namespace constants {
    constexpr float STANDARD_HALF_SIZE = 15.0f;
    // Mini multiplier: spec 0.5, actual GD mini scale 0.6
    constexpr float MINI_SCALE_MULTIPLIER = 0.6f;
    constexpr float MINI_VEHICLE_THRESHOLD = 0.9f;

    // Tunable wave half-sizes (solid vs hazard are separate)
    constexpr float WAVE_SOLID_HALF_SIZE = 1.5f;
    constexpr float WAVE_HAZARD_HALF_SIZE = 3.0f;

    // Spider teleport detection threshold (Y delta in one physics step)
    constexpr float SPIDER_TELEPORT_Y_JUMP = 25.0f;

    // View rebuild margins (multiples of screen width / height)
    constexpr float BUILD_MARGIN_X = 1.0f;
    constexpr float BUILD_MARGIN_Y = 0.3f;
    constexpr float LOOKAHEAD_EXTRA_SCREENS = 1.5f;

    // Hitbox signature quantization step
    constexpr float SIGNATURE_QUANTIZE = 0.5f;

    // Overlay z-order inside PlayLayer::m_objectLayer
    constexpr int OVERLAY_Z_ORDER = 10000;
} // namespace constants

namespace colors {
    constexpr cocos2d::ccColor4F INTERACT_YELLOW = {1.0f, 0.9f, 0.15f, 0.9f};
    constexpr cocos2d::ccColor4F INTERACT_PINK = {1.0f, 0.35f, 0.8f, 0.9f};
    constexpr cocos2d::ccColor4F INTERACT_ORANGE = {1.0f, 0.45f, 0.1f, 0.9f};
    constexpr cocos2d::ccColor4F INTERACT_CYAN = {0.15f, 0.9f, 1.0f, 0.9f};
    constexpr cocos2d::ccColor4F INTERACT_GREEN = {0.2f, 0.95f, 0.35f, 0.9f};
    constexpr cocos2d::ccColor4F INTERACT_PURPLE = {0.72f, 0.3f, 1.0f, 0.9f};
    constexpr cocos2d::ccColor4F INTERACT_WHITE = {0.95f, 0.95f, 0.95f, 0.9f};

    constexpr cocos2d::ccColor3B LAYOUT_WHITE = {255, 255, 255};
    constexpr cocos2d::ccColor3B LAYOUT_BG_GREY = {44, 48, 56};
    constexpr cocos2d::ccColor3B LAYOUT_GROUND_GREY = {36, 40, 48};

    inline cocos2d::ccColor4F from3B(const cocos2d::ccColor3B& c, float a = 1.0f) {
        return {
            static_cast<float>(c.r) / 255.0f,
            static_cast<float>(c.g) / 255.0f,
            static_cast<float>(c.b) / 255.0f,
            a
        };
    }

    inline cocos2d::ccColor4F withAlpha(const cocos2d::ccColor4F& c, float a) {
        return {c.r, c.g, c.b, a};
    }

    inline bool equal3B(const cocos2d::ccColor3B& a, const cocos2d::ccColor3B& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    }
} // namespace colors

struct Settings {
    bool enabled = false;
    float playerOpacity = 0.25f;
    bool hideWaveTrail = false;
    bool layoutMode = false;

    float lineThickness = 3.0f;
    bool mergeOutlines = true;
    float fillOpacity = 0.25f;
    bool showInteractables = true;
    bool glow = false;
    bool hideInsideHazards = false;
    bool onlyReachableBorders = false;

    cocos2d::ccColor3B hazardColor = {255, 51, 51};
    cocos2d::ccColor3B solidColor = {51, 140, 255};
    cocos2d::ccColor3B pathColor = {255, 235, 51};
    cocos2d::ccColor3B wavePathColor = {255, 140, 26};
    cocos2d::ccColor3B portalColor = {173, 179, 191};
};

inline Settings loadSettings() {
    auto* mod = geode::Mod::get();
    Settings s;
    s.enabled = mod->getSettingValue<bool>("hitborders-enabled");
    s.playerOpacity = static_cast<float>(mod->getSettingValue<double>("player-opacity"));
    s.hideWaveTrail = mod->getSettingValue<bool>("hide-wave-trail");
    s.layoutMode = mod->getSettingValue<bool>("layout-mode");

    s.lineThickness = static_cast<float>(mod->getSettingValue<double>("line-thickness"));
    s.mergeOutlines = mod->getSettingValue<bool>("merge-outlines");
    s.fillOpacity = static_cast<float>(mod->getSettingValue<double>("fill-opacity"));
    s.showInteractables = mod->getSettingValue<bool>("show-interactables");
    s.glow = mod->getSettingValue<bool>("glow");
    s.hideInsideHazards = mod->getSettingValue<bool>("hide-inside-hazards");
    s.onlyReachableBorders = mod->getSettingValue<bool>("only-reachable-borders");

    s.hazardColor = mod->getSettingValue<cocos2d::ccColor3B>("color-hazard");
    s.solidColor = mod->getSettingValue<cocos2d::ccColor3B>("color-solid");
    s.pathColor = mod->getSettingValue<cocos2d::ccColor3B>("color-path");
    s.wavePathColor = mod->getSettingValue<cocos2d::ccColor3B>("color-wave-path");
    s.portalColor = mod->getSettingValue<cocos2d::ccColor3B>("color-portal");
    return s;
}

// Compute segment radius in m_objectLayer world units corresponding to real screen pixels.
// Uses winSize.height / glView->getFrameSize().height / objectLayerScale (never getWinSizeInPixels).
inline float computeWorldLineRadius(cocos2d::CCNode* objectLayer, float thicknessPx) {
    auto* director = cocos2d::CCDirector::get();
    if (!director) return 0.5f;
    auto* glView = director->getOpenGLView();
    if (!glView) return 0.5f;

    auto winSize = director->getWinSize();
    auto frameSize = glView->getFrameSize();
    if (frameSize.height <= 0.0f) return 0.5f;

    float pointsPerPixel = winSize.height / frameSize.height;
    float layerScale = objectLayer ? std::fabs(objectLayer->getScale()) : 1.0f;
    if (layerScale < 1e-4f) layerScale = 1.0f;

    float worldWidth = (thicknessPx * pointsPerPixel) / layerScale;
    return std::max(0.05f, worldWidth * 0.5f);
}

inline void configureDrawNodeBlend(cocos2d::CCDrawNode* node) {
    if (!node) return;
    node->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
}

} // namespace hitborders
