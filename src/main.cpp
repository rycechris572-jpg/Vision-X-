#include <Geode/Geode.hpp>
#include <Geode/UI.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/SettingV3.hpp>

#include <algorithm>
#include <cmath>
#include <array>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace vx {
    constexpr int MAX_TICK_TUNING = 80;
    constexpr int MAX_CLICK_TUNING = 30;
    constexpr int MAX_RELEASE_TUNING = 60;

    enum class Tab { Log, Stages, Modes, Tools };

    struct ModeTuning {
        int minTicks = 2;
        int maxTicks = 8;
        int clickTicks = 1;
        int releaseTicks = 1;
    };

    struct Settings {
        bool enabled = true;
        int stage = 1;
        int selectedMode = 0;
        std::array<ModeTuning, 8> modes{};
    };

    static Settings g_settings;
    class VisionXOverlay;
    static VisionXOverlay* g_overlay = nullptr;

    constexpr std::array<const char*, 8> MODE_NAMES = {
        "CUBE", "SHIP", "BALL", "UFO", "WAVE", "ROBOT", "SPIDER", "SWING"
    };
    constexpr std::array<const char*, 8> MODE_MARKS = {
        "C", "S", "B", "U", "W", "R", "SP", "SW"
    };
    constexpr std::array<const char*, 6> STAGE_NAMES = {
        "OFF", "1", "2", "3", "EXTREME", "CUSTOM"
    };

    // Release controls are unavailable for Cube, Ball, UFO, Spider and Swing.
    static bool hasReleaseTuning(int mode) {
        return mode == 1 || mode == 4 || mode == 5;
    }

    static std::string stageName() {
        return STAGE_NAMES[std::clamp(g_settings.stage, 0, 5)];
    }

    static void applyStage(int stage) {
        g_settings.stage = std::clamp(stage, 0, 5);
        if (stage == 0) {
            g_settings.enabled = false;
            return;
        }
        g_settings.enabled = true;
        if (stage == 5) return; // Custom leaves the current per-mode values intact.

        int minTicks = 2;
        int maxTicks = 8;
        switch (stage) {
            case 2: minTicks = 4;  maxTicks = 16; break;
            case 3: minTicks = 8;  maxTicks = 32; break;
            case 4: minTicks = 16; maxTicks = 80; break;
            default: break;
        }
        for (auto& mode : g_settings.modes) {
            mode.minTicks = minTicks;
            mode.maxTicks = maxTicks;
        }
    }

    static void resetTuning() {
        for (auto& mode : g_settings.modes) mode = ModeTuning{};
        g_settings.enabled = true;
        g_settings.stage = 1;
        g_settings.selectedMode = 0;
    }

    class VisionXOverlay final : public CCLayer {
        struct ButtonHitbox {
            CCRect rect;
            CCNode* visual = nullptr;
            std::function<void()> callback;
        };

        static constexpr float PANEL_W = 1300.f;
        static constexpr float PANEL_H = 740.f;

        CCLayerColor* m_shade = nullptr;
        CCLayerColor* m_panel = nullptr;
        CCLayerColor* m_content = nullptr;
        float m_panelScale = 1.f;
        Tab m_tab = Tab::Stages;
        bool m_closing = false;
        int m_pressedButton = -1;
        std::vector<ButtonHitbox> m_buttons;

        const ccColor3B pink = {72, 145, 224};
        const ccColor3B palePink = {50, 91, 145};
        const ccColor3B textColor = {25, 39, 62};
        const ccColor3B muted = {91, 111, 140};
        const ccColor3B dark = {13, 24, 43};
        const ccColor3B cardColor = {237, 244, 252};
        const ccColor3B cardLight = {252, 254, 255};

    public:
        static VisionXOverlay* create() {
            auto ret = new VisionXOverlay();
            if (ret->init()) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

        bool init() override {
            if (!CCLayer::init()) return false;
            auto* director = CCDirector::sharedDirector();
            const auto winSize = director->getWinSize();

            // This layer is attached to OverlayManager after init(). Keep its own
            // local origin at (0, 0); the true viewport center is calculated only
            // after the parent transform exists, in layoutAfterAttach().
            setContentSize(winSize);
            setAnchorPoint({0.f, 0.f});
            setPosition({0.f, 0.f});
            setTouchEnabled(true);
            setTouchMode(kCCTouchesOneByOne);

            m_shade = CCLayerColor::create(
                {5, 4, 12, 0}, winSize.width, winSize.height
            );
            if (!m_shade) return false;
            m_shade->setAnchorPoint({0.f, 0.f});
            m_shade->setPosition({0.f, 0.f});
            addChild(m_shade, 0);

            // CCLayerColor exposes opacity and cascading opacity support; CCNode does not.
            m_panel = CCLayerColor::create({0, 0, 0, 0}, PANEL_W, PANEL_H);
            if (!m_panel) return false;
            m_panel->setContentSize({PANEL_W, PANEL_H});
            m_panel->setAnchorPoint({0.5f, 0.5f});
            // CCLayer ignores its anchor point by default. The panel is positioned
            // by its center, so explicitly restore normal anchor-point transforms.
            m_panel->ignoreAnchorPointForPosition(false);
            m_panel->setPosition({winSize.width / 2.f, winSize.height / 2.f});
            m_panel->setCascadeOpacityEnabled(true);
            m_panel->setScale(0.90f);
            addChild(m_panel, 10);

            buildPanelChrome();
            // Keep the content in an opacity-capable layer so its fade animates all controls.
            m_content = CCLayerColor::create({0, 0, 0, 0}, PANEL_W, PANEL_H);
            if (!m_content) return false;
            m_content->setCascadeOpacityEnabled(true);
            m_content->setContentSize({PANEL_W, PANEL_H});
            m_content->setAnchorPoint({0.5f, 0.5f});
            // Same CCLayer anchor behaviour: align the content layer by its center.
            m_content->ignoreAnchorPointForPosition(false);
            m_content->setPosition({PANEL_W / 2.f, PANEL_H / 2.f});
            m_panel->addChild(m_content, 5);
            return true;
        }

        // MUST be called after this overlay has been added to OverlayManager.
        // convertToNodeSpace now includes the real parent transform, so the
        // panel center is the center of the visible Geometry Dash viewport.
        void layoutAfterAttach() {
            auto* director = CCDirector::sharedDirector();
            const auto visibleSize = director->getVisibleSize();
            const auto visibleOrigin = director->getVisibleOrigin();

            const CCPoint viewportBL = convertToNodeSpace(visibleOrigin);
            const CCPoint viewportTR = convertToNodeSpace({
                visibleOrigin.x + visibleSize.width,
                visibleOrigin.y + visibleSize.height
            });
            const CCSize localViewport = {
                std::max(1.f, viewportTR.x - viewportBL.x),
                std::max(1.f, viewportTR.y - viewportBL.y)
            };
            const CCPoint center = {
                (viewportBL.x + viewportTR.x) * 0.5f,
                (viewportBL.y + viewportTR.y) * 0.5f
            };

            // Stretch the shade to the viewport in this overlay's local space.
            const auto shadeSize = m_shade->getContentSize();
            m_shade->stopAllActions();
            m_shade->setPosition(viewportBL);
            m_shade->setScaleX(localViewport.width / std::max(1.f, shadeSize.width));
            m_shade->setScaleY(localViewport.height / std::max(1.f, shadeSize.height));
            m_shade->setOpacity(0);

            // Center the entire panel around the converted viewport midpoint.
            m_panel->stopAllActions();
            m_panel->setPosition(center);
            constexpr float DESIGN_SCALE = 1.20f;
            const float availableWidth = std::max(1.f, localViewport.width - 40.f);
            const float availableHeight = std::max(1.f, localViewport.height - 40.f);
            const float fit = std::min(
                availableWidth / (PANEL_W * DESIGN_SCALE),
                availableHeight / (PANEL_H * DESIGN_SCALE)
            );
            m_panelScale = DESIGN_SCALE * std::clamp(fit, 0.30f, 1.f);
            m_panel->setScale(m_panelScale * 0.88f);

            redraw();
            m_shade->runAction(CCFadeTo::create(0.18f, 176));
            m_panel->runAction(CCEaseBackOut::create(CCScaleTo::create(0.24f, m_panelScale)));
        }

        void registerWithTouchDispatcher() override {
            // High-priority, swallowing delegate: touches never fall through to GD UI.
            CCTouchDispatcher::get()->addTargetedDelegate(this, -500, true);
        }

        bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
            m_pressedButton = -1;
            const auto point = m_panel->convertToNodeSpace(touch->getLocation());
            for (int i = static_cast<int>(m_buttons.size()) - 1; i >= 0; --i) {
                if (m_buttons[i].rect.containsPoint(point)) {
                    m_pressedButton = i;
                    if (m_buttons[i].visual) m_buttons[i].visual->setScale(0.96f);
                    break;
                }
            }
            return true; // Consume even when the click is outside the panel.
        }

        void ccTouchMoved(CCTouch*, CCEvent*) override {}

        void ccTouchEnded(CCTouch* touch, CCEvent*) override {
            const int index = m_pressedButton;
            m_pressedButton = -1;
            if (index < 0 || index >= static_cast<int>(m_buttons.size())) return;
            const auto point = m_panel->convertToNodeSpace(touch->getLocation());
            const auto hit = m_buttons[index];
            if (hit.rect.containsPoint(point) && hit.callback) hit.callback();
            else if (hit.visual) hit.visual->runAction(CCScaleTo::create(0.10f, 1.f));
        }

        void ccTouchCancelled(CCTouch*, CCEvent*) override {
            if (m_pressedButton >= 0 && m_pressedButton < static_cast<int>(m_buttons.size())) {
                if (m_buttons[m_pressedButton].visual)
                    m_buttons[m_pressedButton].visual->runAction(CCScaleTo::create(0.10f, 1.f));
            }
            m_pressedButton = -1;
        }

        void closeAnimated() {
            if (m_closing) return;
            m_closing = true;
            m_panel->stopAllActions();
            m_panel->runAction(CCEaseIn::create(CCScaleTo::create(0.15f, m_panelScale * 0.92f), 2.f));
            m_shade->runAction(CCSequence::create(
                CCFadeTo::create(0.14f, 0),
                CCCallFunc::create(this, callfunc_selector(VisionXOverlay::finishClose)),
                nullptr
            ));
        }

    private:
        void finishClose() {
            if (g_overlay == this) g_overlay = nullptr;
            removeFromParentAndCleanup(true);
        }

        CCNode* roundedRect(CCSize size, ccColor3B color, GLubyte opacity = 255) {
            auto sprite = cocos2d::extension::CCScale9Sprite::create(
                "square02b_001.png", CCRect(0.f, 0.f, 80.f, 80.f)
            );
            if (sprite) {
                sprite->setContentSize(size);
                sprite->setColor(color);
                sprite->setOpacity(opacity);
                return sprite;
            }
            auto fallback = CCLayerColor::create(
                {color.r, color.g, color.b, opacity}, size.width, size.height
            );
            if (fallback) {
                fallback->setAnchorPoint({0.5f, 0.5f});
                fallback->ignoreAnchorPointForPosition(false);
            }
            return fallback;
        }

        CCNode* makeEyeGraphic(float width, float height, GLubyte alpha) {
            auto eye = CCNode::create();
            auto drawing = CCDrawNode::create();
            if (!eye || !drawing) return nullptr;
            eye->setContentSize({width, height});
            eye->setAnchorPoint({0.5f, 0.5f});

            const float a = static_cast<float>(alpha) / 255.f;
            const float cx = width * 0.5f;
            const float cy = height * 0.5f;
            const float irisR = std::min(height * 0.315f, width * 0.19f);

            // Smooth almond silhouette: two curved lids meeting at pointed corners.
            std::vector<CCPoint> outline;
            outline.reserve(82);
            for (int i = 0; i <= 40; ++i) {
                const float t = static_cast<float>(i) / 40.f;
                const float x = width * (0.055f + 0.89f * t);
                const float arc = std::pow(std::max(0.f, std::sin(3.14159265f * t)), 0.78f);
                outline.push_back(CCPoint(x, cy + height * 0.405f * arc));
            }
            for (int i = 40; i >= 0; --i) {
                const float t = static_cast<float>(i) / 40.f;
                const float x = width * (0.055f + 0.89f * t);
                const float arc = std::pow(std::max(0.f, std::sin(3.14159265f * t)), 0.78f);
                outline.push_back(CCPoint(x, cy - height * 0.405f * arc));
            }
            drawing->drawPolygon(outline.data(), static_cast<unsigned int>(outline.size()),
                ccColor4F{0.025f, 0.065f, 0.15f, a * 0.96f},
                std::max(2.0f, height * 0.018f),
                ccColor4F{0.54f, 0.82f, 1.0f, a});

            // A soft blue iris with layered rings and fine radial fibres.
            drawing->drawDot({cx, cy}, irisR * 1.08f,
                ccColor4F{0.04f, 0.20f, 0.43f, a});
            drawing->drawDot({cx, cy}, irisR * 0.93f,
                ccColor4F{0.08f, 0.43f, 0.82f, a});
            for (int i = 0; i < 36; ++i) {
                const float angle = 6.2831853f * static_cast<float>(i) / 36.f;
                const float inner = irisR * (0.43f + (i % 3) * 0.025f);
                const float outer = irisR * (0.82f - (i % 4) * 0.018f);
                drawing->drawSegment(
                    {cx + std::cos(angle) * inner, cy + std::sin(angle) * inner},
                    {cx + std::cos(angle) * outer, cy + std::sin(angle) * outer},
                    std::max(0.7f, height * 0.0045f),
                    ccColor4F{0.43f, 0.79f, 1.0f, a * 0.72f});
            }
            drawing->drawDot({cx, cy}, irisR * 0.49f,
                ccColor4F{0.025f, 0.10f, 0.25f, a});
            drawing->drawDot({cx, cy}, irisR * 0.34f,
                ccColor4F{0.008f, 0.025f, 0.075f, a});

            // Glossy catchlights and fine lid reflections make the eye read as rounded.
            drawing->drawDot({cx - irisR * 0.30f, cy + irisR * 0.34f},
                irisR * 0.13f, ccColor4F{0.96f, 0.99f, 1.0f, a * 0.98f});
            drawing->drawDot({cx + irisR * 0.34f, cy - irisR * 0.25f},
                irisR * 0.055f, ccColor4F{0.75f, 0.91f, 1.0f, a * 0.92f});
            drawing->drawSegment({width * 0.19f, cy + height * 0.23f},
                {width * 0.40f, cy + height * 0.34f},
                std::max(1.f, height * 0.012f),
                ccColor4F{0.78f, 0.91f, 1.0f, a * 0.78f});
            eye->addChild(drawing);
            return eye;
        }

        void addEyeBackdrop() {
            auto hero = makeEyeGraphic(500.f, 285.f, 128);
            if (hero) {
                hero->setPosition({PANEL_W * 0.67f, PANEL_H * 0.50f});
                hero->setRotation(-2.f);
                hero->runAction(CCRepeatForever::create(CCSequence::create(
                    CCScaleTo::create(4.0f, 1.018f),
                    CCScaleTo::create(4.0f, 0.988f), nullptr)));
                m_panel->addChild(hero, -2);
            }
            struct Eye { float x, y, w, h, rise, duration; GLubyte alpha; };
            const Eye list[] = {
                {335,590,50,29,4,5.0f,82}, {610,604,36,21,3,5.8f,72},
                {940,585,48,28,4,5.4f,84}, {1190,520,42,25,3,6.0f,72},
                {330,360,44,26,3,5.6f,74}, {1200,300,46,27,4,5.8f,78},
                {470,105,40,23,3,6.1f,70}, {1020,100,50,29,4,5.5f,78}
            };
            for (const auto& e : list) {
                auto eye = makeEyeGraphic(e.w, e.h, e.alpha);
                if (!eye) continue;
                eye->setPosition({e.x, e.y});
                eye->runAction(CCRepeatForever::create(CCSequence::create(
                    CCMoveBy::create(e.duration, {0.f, e.rise}),
                    CCMoveBy::create(e.duration, {0.f, -e.rise}), nullptr)));
                m_panel->addChild(eye, -2);
            }
        }

        void addCosmicStars() {
            struct Star { float x, y, r, rise, seconds; GLubyte alpha; };
            const Star list[] = {
                {305,640,9,7,5.0f,160}, {430,560,6,5,4.1f,135},
                {585,660,11,6,5.8f,155}, {760,590,7,5,4.8f,130},
                {910,660,10,6,5.5f,155}, {1085,590,6,4,4.4f,135},
                {1240,625,9,6,5.2f,155}, {315,445,7,5,4.5f,135},
                {1260,410,8,5,5.7f,150}, {335,245,9,6,5.1f,155},
                {1240,235,7,4,4.3f,135}, {530,85,8,5,5.6f,145},
                {830,45,6,4,4.6f,130}, {1110,75,10,6,5.9f,155}
            };
            for (const auto& st : list) {
                auto node = CCNode::create();
                auto draw = CCDrawNode::create();
                if (!node || !draw) continue;
                CCPoint p[10];
                for (int j = 0; j < 10; ++j) {
                    const float angle = -1.57079632679f + static_cast<float>(j) * 0.62831853071f;
                    const float radius = (j % 2 == 0) ? st.r : st.r * 0.36f;
                    p[j] = CCPoint(st.r + std::cos(angle) * radius,
                                   st.r + std::sin(angle) * radius);
                }
                const float alpha = static_cast<float>(st.alpha) / 255.f;
                draw->drawPolygon(p, 10, ccColor4F{0.40f,0.66f,0.95f,alpha*0.45f},
                    1.25f, ccColor4F{0.76f,0.89f,1.f,alpha});
                draw->drawDot({st.r,st.r}, st.r*0.17f,
                    ccColor4F{1.f,1.f,1.f,alpha});
                node->addChild(draw);
                node->setContentSize({st.r*2.f,st.r*2.f});
                node->setAnchorPoint({0.5f,0.5f});
                node->setPosition({st.x,st.y});
                node->runAction(CCRepeatForever::create(CCSpawn::create(
                    CCRotateBy::create(st.seconds,360.f),
                    CCSequence::create(
                        CCMoveBy::create(st.seconds*0.5f,{0.f,st.rise}),
                        CCMoveBy::create(st.seconds*0.5f,{0.f,-st.rise}),nullptr),
                    nullptr)));
                m_panel->addChild(node,-1);
            }
        }

        void addPearlGradient() {
            constexpr int STRIPS = 64;
            const float innerW = PANEL_W - 38.f;
            const float innerH = PANEL_H - 38.f;
            const float stripH = innerH / static_cast<float>(STRIPS);
            auto channel = [](int top, int bottom, float t) -> GLubyte {
                return static_cast<GLubyte>(std::clamp(
                    static_cast<int>(std::lround(top + (bottom - top) * t)), 0, 255));
            };
            for (int i = 0; i < STRIPS; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(STRIPS - 1);
                // Soft white at the top, drifting into a cool pearl-blue at the bottom.
                const ccColor4B color = {
                    channel(253, 211, t), channel(254, 226, t),
                    channel(255, 246, t), static_cast<GLubyte>(255)
                };
                auto strip = CCLayerColor::create(color, innerW, stripH + 1.1f);
                if (!strip) continue;
                strip->setAnchorPoint({0.f, 0.f});
                strip->setPosition({19.f, 19.f + static_cast<float>(i) * stripH});
                m_panel->addChild(strip, -4);
            }
        }

        void buildPanelChrome() {
            auto shadow = roundedRect({PANEL_W+28.f,PANEL_H+28.f},{5,13,28},205);
            shadow->setPosition({PANEL_W/2.f,PANEL_H/2.f-8.f});
            m_panel->addChild(shadow,-6);
            auto body = roundedRect({PANEL_W,PANEL_H},{238,246,255},255);
            body->setPosition({PANEL_W/2.f,PANEL_H/2.f});
            m_panel->addChild(body,-5);
            addPearlGradient();
            auto edge = roundedRect({PANEL_W-10.f,PANEL_H-10.f},{255,255,255},38);
            edge->setPosition({PANEL_W/2.f,PANEL_H/2.f});
            m_panel->addChild(edge,-3);
            addEyeBackdrop();
            addCosmicStars();

            auto line = CCLayerColor::create({93,157,222,245},PANEL_W-70.f,2.5f);
            line->setPosition({35.f,640.f});
            m_panel->addChild(line,1);
            auto glow = CCLabelBMFont::create("VISION X","bigFont.fnt");
            glow->setAnchorPoint({0.5f,0.5f});
            glow->setPosition({PANEL_W*0.5f+1.f,686.f});
            glow->setScale(0.82f);
            glow->setColor({105,159,216});
            m_panel->addChild(glow,2);
            auto logo = CCLabelBMFont::create("VISION X","bigFont.fnt");
            logo->setAnchorPoint({0.5f,0.5f});
            logo->setPosition({PANEL_W*0.5f,688.f});
            logo->setScale(0.82f);
            logo->setColor({27,48,78});
            m_panel->addChild(logo,3);
            auto sub = CCLabelBMFont::create("PRECISION INPUT CONTROL","bigFont.fnt");
            sub->setAnchorPoint({0.5f,0.5f});
            sub->setPosition({PANEL_W*0.5f,661.f});
            sub->setScale(0.19f);
            sub->setColor({83,112,150});
            m_panel->addChild(sub,3);
            auto divider = CCLayerColor::create({155,184,216,220},2.f,555.f);
            divider->setPosition({262.f,52.f});
            m_panel->addChild(divider,2);
        }

        void addText(const std::string& value, CCPoint pos, float scale = 0.32f,
                     ccColor3B color = {25, 39, 62}, CCPoint anchor = {0.f, 0.5f}) {
            auto label = CCLabelBMFont::create(value.c_str(), "bigFont.fnt");
            if (!label) return;
            label->setAnchorPoint(anchor);
            label->setPosition(pos);
            label->setScale(scale);
            label->setColor(color);
            m_content->addChild(label, 2);
        }

        void addCard(CCPoint center, CCSize size, ccColor3B color = {242, 247, 253}) {
            auto shadow = roundedRect({size.width + 8.f, size.height + 8.f},
                {74, 103, 145}, 32);
            shadow->setPosition({center.x + 1.f, center.y - 3.f});
            m_content->addChild(shadow, 0);

            auto rim = roundedRect({size.width + 3.f, size.height + 3.f},
                {168, 190, 219}, 220);
            rim->setPosition(center);
            m_content->addChild(rim, 1);

            auto card = roundedRect({size.width - 2.f, size.height - 2.f},
                color, 248);
            card->setPosition(center);
            m_content->addChild(card, 2);

            auto highlight = CCLayerColor::create(
                {255, 255, 255, 105}, std::max(8.f, size.width - 22.f), 1.5f);
            highlight->setPosition({center.x - size.width * 0.5f + 11.f,
                                     center.y + size.height * 0.5f - 7.f});
            m_content->addChild(highlight, 3);
        }) {
            auto rim = roundedRect({size.width + 3.f, size.height + 3.f},
                {108, 77, 133}, 210);
            rim->setPosition(center);
            m_content->addChild(rim, 0);

            auto card = roundedRect({size.width - 2.f, size.height - 2.f},
                color, 246);
            card->setPosition(center);
            m_content->addChild(card, 1);
        }

        void addButton(const std::string& title, CCPoint center, CCSize size,
                       std::function<void()> callback, bool selected = false,
                       bool quiet = false, float labelScale = 0.30f) {
            auto group = CCNode::create();
            group->setContentSize(size);
            group->setAnchorPoint({0.5f, 0.5f});
            group->setPosition(center);
            const auto bgColor = selected ? ccColor3B{42, 104, 178}
                : quiet ? ccColor3B{247, 250, 255} : ccColor3B{225, 237, 250};
            auto rim = roundedRect({size.width + 3.f, size.height + 3.f},
                selected ? ccColor3B{105, 169, 231} : ccColor3B{171, 194, 222}, 245);
            rim->setPosition({size.width / 2.f, size.height / 2.f});
            group->addChild(rim, -1);
            auto bg = roundedRect({size.width - 2.f, size.height - 2.f},
                bgColor, selected ? 255 : 242);
            bg->setPosition({size.width / 2.f, size.height / 2.f});
            group->addChild(bg, 0);
            auto gloss = CCLayerColor::create(
                {255, 255, 255, selected ? (GLubyte)75 : (GLubyte)150},
                std::max(8.f, size.width - 16.f), 1.f
            );
            gloss->setPosition({8.f, size.height - 6.f});
            group->addChild(gloss, 1);
            auto label = CCLabelBMFont::create(title.c_str(), "bigFont.fnt");
            if (label) {
                label->setPosition({size.width / 2.f, size.height / 2.f});
                label->setScale(labelScale);
                label->setColor(selected ? ccColor3B{255, 255, 255} : textColor);
                group->addChild(label, 1);
            }
            m_content->addChild(group, 3);
            m_buttons.push_back({
                CCRect(center.x - size.width / 2.f, center.y - size.height / 2.f, size.width, size.height),
                group, std::move(callback)
            });
        }

        void addAdjust(const std::string& title, const std::string& value, float x, float y,
                       std::function<void()> minus, std::function<void()> plus) {
            addText(title, {x, y + 28.f}, 0.26f, palePink, {0.5f, 0.5f});
            addText(value, {x, y - 2.f}, 0.40f, textColor, {0.5f, 0.5f});
            addButton("-", {x - 48.f, y - 2.f}, {36.f, 28.f}, std::move(minus), false, true, 0.36f);
            addButton("+", {x + 48.f, y - 2.f}, {36.f, 28.f}, std::move(plus), false, true, 0.36f);
        }

        void redraw() {
            m_buttons.clear();
            m_content->removeAllChildrenWithCleanup(true);
            m_content->stopAllActions();
            m_content->setPosition({PANEL_W / 2.f - 4.f, PANEL_H / 2.f});
            m_content->setOpacity(0);
            drawSidebar();
            drawMainPage();
            m_content->runAction(CCSpawn::create(
                CCFadeTo::create(0.16f, 255),
                CCEaseOut::create(CCMoveTo::create(0.18f, {PANEL_W / 2.f, PANEL_H / 2.f}), 2.f),
                nullptr
            ));
        }

        void drawSidebar() {
            addCard({132.f,360.f},{226.f,575.f},{239,246,254});
            struct Nav { const char* label; Tab tab; };
            constexpr Nav items[] = {
                {"LOG",Tab::Log},{"STAGES",Tab::Stages},{"MODES",Tab::Modes},{"TOOLS",Tab::Tools}
            };
            const float ys[] = {535.f,440.f,345.f,250.f};
            addText("NAVIGATION",{132.f,622.f},0.20f,muted,{0.5f,0.5f});
            for (int i=0;i<4;++i) {
                const auto item=items[i];
                const bool selected=m_tab==item.tab;
                addButton(item.label,{132.f,ys[i]},{186.f,58.f},
                    [this,tab=item.tab]{m_tab=tab;redraw();},selected,!selected,0.31f);
            }
            addButton("X",{1250.f,690.f},{42.f,42.f},[this]{closeAnimated();},false,true,0.35f);
            addText("GEODE OVERLAY",{132.f,144.f},0.18f,muted,{0.5f,0.5f});
            addText("Left Alt = Toggle GUI",{132.f,82.f},0.21f,palePink,{0.5f,0.5f});
        }

        void drawMainPage() {
            switch (m_tab) {
                case Tab::Log: drawLog(); break;
                case Tab::Stages: drawStages(); break;
                case Tab::Modes: drawModes(); break;
                case Tab::Tools: drawTools(); break;
            }
        }

        void drawLog() {
            addText("ACTIVITY LOG", {305.f, 610.f}, 0.42f, palePink);
            addText("SESSION STATUS", {305.f, 578.f}, 0.24f, muted);
            addCard({790.f, 470.f}, {940.f, 125.f});
            addText(g_settings.enabled ? "Configuration enabled" : "Configuration disabled",
                {350.f, 497.f}, 0.32f);
            addText("Current stage: " + stageName(), {350.f, 463.f}, 0.27f, muted);
            addText("Selected mode: " + std::string(MODE_NAMES[g_settings.selectedMode]),
                {350.f, 430.f}, 0.27f, muted);
            addCard({790.f, 290.f}, {940.f, 125.f});
            addText("INPUT TUNING", {350.f, 318.f}, 0.25f, palePink);
            addText("Per-mode timing values are configured in the Stages tab.",
                {350.f, 286.f}, 0.23f);
            addText("This panel displays configuration values only.",
                {350.f, 255.f}, 0.21f, muted);
        }

        void drawStages() {
            addText("STAGE PRESETS",{305.f,610.f},0.34f,palePink);
            addText("Choose a starting profile. Fine-tune every gamemode below.",
                {305.f,585.f},0.18f,muted);
            addCard({790.f,548.f},{970.f,66.f},{248,251,255});
            for (int i = 0; i < 6; ++i) {
                const float x = 390.f + static_cast<float>(i) * 160.f;
                const bool selected = g_settings.stage == i;
                addButton(STAGE_NAMES[i],{x,548.f},{142.f,38.f},
                    [this,i]{applyStage(i);redraw();},selected,!selected,
                    i >= 4 ? 0.22f : 0.27f);
            }

            addText("TICK TUNING",{305.f,480.f},0.31f,palePink);
            addText("Independent minimum and maximum values for each gamemode.",
                {305.f,455.f},0.17f,muted);
            for (int i = 0; i < 8; ++i) {
                const int col = i % 4, row = i / 4;
                drawTuningModeTile(i,{420.f+static_cast<float>(col)*235.f,
                    407.f-static_cast<float>(row)*78.f},false);
            }

            addText("CLICK & RELEASE",{305.f,245.f},0.31f,palePink);
            addText("Per-mode click timing and supported release timing.",
                {305.f,220.f},0.17f,muted);
            for (int i = 0; i < 8; ++i) {
                const int col = i % 4, row = i / 4;
                drawTuningModeTile(i,{420.f+static_cast<float>(col)*235.f,
                    170.f-static_cast<float>(row)*78.f},true);
            }
            addText("Changes are stored per gamemode.",{305.f,28.f},0.17f,muted);
        }

        void drawModes() {
            addText("GAMEMODES",{305.f,610.f},0.35f,palePink);
            addText("Choose a mode to highlight its tuning values in Stages.",
                {305.f,580.f},0.19f,muted);
            for (int i = 0; i < 8; ++i) {
                const int col = i % 4, row = i / 4;
                const float x = 420.f + static_cast<float>(col) * 235.f;
                const float y = 430.f - static_cast<float>(row) * 190.f;
                addCard({x,y},{205.f,145.f},{248,251,255});
                addModeIcon(i,{x,y+30.f},0.78f);
                addText(MODE_NAMES[i],{x,y-28.f},0.23f,palePink,{0.5f,0.5f});
                addButton(g_settings.selectedMode==i?"SELECTED":"SELECT",
                    {x,y-53.f},{118.f,25.f},
                    [this,i]{g_settings.selectedMode=i;redraw();},
                    g_settings.selectedMode==i,g_settings.selectedMode!=i,0.19f);
            }
        }

        void drawTools() {
            addText("TOOLS & SETTINGS",{305.f,610.f},0.35f,palePink);
            addText("Configuration and maintenance",{305.f,580.f},0.19f,muted);
            addCard({790.f,455.f},{940.f,140.f},{248,251,255});
            addText("BUFFER CONFIGURATION",{350.f,488.f},0.25f,palePink);
            addText(g_settings.enabled?"Status: enabled":"Status: disabled",
                {350.f,451.f},0.22f);
            addButton(g_settings.enabled?"DISABLE":"ENABLE",{1160.f,462.f},{150.f,42.f},
                [this]{g_settings.enabled=!g_settings.enabled;redraw();},
                g_settings.enabled,true,0.23f);
            addCard({790.f,265.f},{940.f,140.f},{248,251,255});
            addText("RESET TUNING",{350.f,298.f},0.25f,palePink);
            addText("Restore default ranges for all eight gamemodes.",
                {350.f,261.f},0.20f,muted);
            addButton("RESET",{1160.f,272.f},{150.f,42.f},
                [this]{resetTuning();redraw();},false,true,0.23f);
            addText("Toggle the interface at any time with Left Alt.",
                {305.f,108.f},0.20f,muted);
        }

        void addModeIcon(int modeIndex,CCPoint center,float scale) {
            auto icon = CCNode::create();
            auto draw = CCDrawNode::create();
            if (!icon || !draw) return;
            icon->setContentSize({64.f,64.f});
            icon->setAnchorPoint({0.5f,0.5f});
            icon->setPosition(center);
            icon->setScale(scale);

            const ccColor4F rim = {0.38f,0.68f,0.95f,0.98f};
            const ccColor4F fill = {0.20f,0.49f,0.82f,1.f};
            const ccColor4F light = {0.86f,0.95f,1.f,1.f};
            const ccColor4F deep = {0.035f,0.12f,0.25f,1.f};
            draw->drawDot({32.f,32.f},30.f,ccColor4F{0.13f,0.25f,0.42f,0.16f});
            draw->drawDot({32.f,32.f},27.f,ccColor4F{0.73f,0.84f,0.96f,0.95f});
            draw->drawDot({32.f,32.f},24.f,ccColor4F{0.95f,0.98f,1.f,1.f});

            auto line = [&](CCPoint a, CCPoint b, float width, ccColor4F color) {
                draw->drawSegment(a,b,width,color);
            };
            auto poly = [&](std::initializer_list<CCPoint> points, ccColor4F color) {
                std::vector<CCPoint> p(points);
                if (p.size() >= 3)
                    draw->drawPolygon(p.data(),static_cast<unsigned int>(p.size()),
                        color,2.2f,light);
            };
            auto circleLine = [&](CCPoint c,float radius,float width,ccColor4F color) {
                for (int k=0;k<32;++k) {
                    const float a1=6.2831853f*static_cast<float>(k)/32.f;
                    const float a2=6.2831853f*static_cast<float>(k+1)/32.f;
                    line({c.x+std::cos(a1)*radius,c.y+std::sin(a1)*radius},
                         {c.x+std::cos(a2)*radius,c.y+std::sin(a2)*radius},width,color);
                }
            };

            switch (std::clamp(modeIndex,0,7)) {
                case 0: // Cube
                    poly({{19,19},{45,19},{45,45},{19,45}},fill);
                    line({24,40},{40,40},2.f,light);
                    line({24,24},{40,24},1.5f,rim);
                    break;
                case 1: // Ship
                    poly({{13,29},{48,48},{43,30},{48,16},{13,29}},fill);
                    line({18,29},{39,29},2.f,light);
                    break;
                case 2: // Ball
                    draw->drawDot({32,32},17.f,fill);
                    circleLine({32,32},17.f,2.2f,light);
                    line({21,36},{43,28},2.f,light);
                    break;
                case 3: // UFO
                    poly({{14,28},{20,23},{44,23},{50,28},{46,34},{18,34}},fill);
                    line({23,35},{27,42},2.f,light);
                    line({41,35},{37,42},2.f,light);
                    line({24,43},{40,43},2.f,rim);
                    poly({{23,29},{27,39},{37,39},{41,29}},deep);
                    break;
                case 4: // Wave
                    line({16,19},{29,43},4.f,fill);
                    line({29,43},{39,24},4.f,fill);
                    line({39,24},{49,42},4.f,fill);
                    line({16,19},{29,43},1.3f,light);
                    line({29,43},{39,24},1.3f,light);
                    line({39,24},{49,42},1.3f,light);
                    break;
                case 5: // Robot
                    poly({{23,32},{41,32},{43,45},{21,45}},fill);
                    poly({{25,46},{39,46},{41,53},{23,53}},deep);
                    line({25,32},{20,22},3.f,fill);
                    line({39,32},{44,22},3.f,fill);
                    draw->drawDot({28,40},2.2f,light);
                    draw->drawDot({36,40},2.2f,light);
                    break;
                case 6: // Spider
                    draw->drawDot({32,32},8.f,fill);
                    circleLine({32,32},8.f,1.8f,light);
                    for (int side=-1;side<=1;side+=2) {
                        for (int k=0;k<4;++k) {
                            const float y=22.f+static_cast<float>(k)*7.f;
                            line({32.f+side*5.f,y},{32.f+side*(17.f+(k%2)*3.f),y+(k<2?7.f:-7.f)},2.5f,fill);
                        }
                    }
                    break;
                case 7: // Swing
                    line({32,48},{32,23},2.8f,fill);
                    line({32,48},{45,29},2.8f,fill);
                    draw->drawDot({45,29},7.f,fill);
                    circleLine({45,29},7.f,1.8f,light);
                    line({21,49},{43,49},1.8f,rim);
                    break;
            }
            icon->addChild(draw);
            m_content->addChild(icon,3);
        }

        void addMiniAdjust(const std::string& label,int value,float x,float y,
                           std::function<void()> minus,std::function<void()> plus) {
            addText(label,{x-13.f,y},0.125f,palePink,{1.f,0.5f});
            addButton("<",{x+8.f,y},{17.f,20.f},std::move(minus),false,true,0.18f);
            addText(fmt::format("{}",value),{x+34.f,y},0.20f,textColor,{0.5f,0.5f});
            addButton(">",{x+60.f,y},{17.f,20.f},std::move(plus),false,true,0.18f);
        }

        void drawTuningModeTile(int modeIndex,CCPoint center,bool clickRelease) {
            auto& mode=g_settings.modes[modeIndex];
            addCard(center,{222.f,70.f},{249,252,255});
            addModeIcon(modeIndex,{center.x-77.f,center.y+1.f},0.66f);
            addText(MODE_NAMES[modeIndex],{center.x-77.f,center.y-25.f},
                0.145f,palePink,{0.5f,0.5f});

            if(!clickRelease) {
                addMiniAdjust("MIN",mode.minTicks,center.x+15.f,center.y+14.f,
                    [this,modeIndex]{auto& v=g_settings.modes[modeIndex];v.minTicks=std::max(0,v.minTicks-1);redraw();},
                    [this,modeIndex]{auto& v=g_settings.modes[modeIndex];v.minTicks=std::min(v.maxTicks,v.minTicks+1);redraw();});
                addMiniAdjust("MAX",mode.maxTicks,center.x+15.f,center.y-14.f,
                    [this,modeIndex]{auto& v=g_settings.modes[modeIndex];v.maxTicks=std::max(v.minTicks,v.maxTicks-1);redraw();},
                    [this,modeIndex]{auto& v=g_settings.modes[modeIndex];v.maxTicks=std::min(MAX_TICK_TUNING,v.maxTicks+1);redraw();});
            } else {
                addMiniAdjust("CLICK",mode.clickTicks,center.x+15.f,center.y+14.f,
                    [this,modeIndex]{auto& v=g_settings.modes[modeIndex];v.clickTicks=std::max(0,v.clickTicks-1);redraw();},
                    [this,modeIndex]{auto& v=g_settings.modes[modeIndex];v.clickTicks=std::min(MAX_CLICK_TUNING,v.clickTicks+1);redraw();});
                if(hasReleaseTuning(modeIndex)) {
                    addMiniAdjust("RELEASE",mode.releaseTicks,center.x+15.f,center.y-14.f,
                        [this,modeIndex]{auto& v=g_settings.modes[modeIndex];v.releaseTicks=std::max(0,v.releaseTicks-1);redraw();},
                        [this,modeIndex]{auto& v=g_settings.modes[modeIndex];v.releaseTicks=std::min(MAX_RELEASE_TUNING,v.releaseTicks+1);redraw();});
                } else {
                    addText("RELEASE",{center.x-22.f,center.y-14.f},0.115f,palePink,{1.f,0.5f});
                    addText("N/A",{center.x+45.f,center.y-14.f},0.17f,muted,{0.5f,0.5f});
                }
            }
        }


    };

    static void toggleOverlay() {
        if (g_overlay) {
            g_overlay->closeAnimated();
            return;
        }
        auto overlay = VisionXOverlay::create();
        if (!overlay) return;
        g_overlay = overlay;
        // Attach first, then compute positions using the now-valid parent transform.
        // This order is what prevents the overlay from appearing in the upper-right.
        OverlayManager::get()->addChild(overlay, 100000);
        overlay->layoutAfterAttach();
    }
}

$on_game(Loaded) {
    listenForKeybindSettingPresses("toggle-ui",
        [](Keybind const&, bool down, bool repeat, double) {
            if (down && !repeat) vx::toggleOverlay();
        }
    );
}
