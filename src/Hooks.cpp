#include "Common.hpp"
#include "Overlay.hpp"

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

using namespace geode::prelude;

namespace {

constexpr int OVERLAY_TAG = 0x48424F56; // 'HBOV'

hitborders::HitbordersOverlay* getOverlay(PlayLayer* pl) {
    if (!pl || !pl->m_objectLayer) return nullptr;
    return typeinfo_cast<hitborders::HitbordersOverlay*>(
        pl->m_objectLayer->getChildByTag(OVERLAY_TAG)
    );
}

} // namespace

class $modify(HitbordersPlayLayer, PlayLayer) {
    struct Fields {
        hitborders::HitbordersOverlay* m_overlay = nullptr;
    };

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        if (!this->m_objectLayer) return;
        if (auto* existing = getOverlay(this)) {
            m_fields->m_overlay = existing;
            return;
        }

        if (auto* overlay = hitborders::HitbordersOverlay::create(this)) {
            overlay->setTag(OVERLAY_TAG);
            this->m_objectLayer->addChild(overlay, hitborders::constants::OVERLAY_Z_ORDER);
            m_fields->m_overlay = overlay;
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();

        auto* overlay = m_fields->m_overlay ? m_fields->m_overlay : getOverlay(this);
        if (overlay) {
            overlay->onResetLevel();
        }
    }
};

class $modify(HitbordersPlayerObject, PlayerObject) {
    void update(float dt) {
        PlayerObject::update(dt);

        auto* pl = PlayLayer::get();
        if (!pl || this != pl->m_player1) return;

        if (auto* overlay = getOverlay(pl)) {
            overlay->onPlayerStep(this);
        }
    }
};
