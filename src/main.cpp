#include <Geode/Geode.hpp>
#include <Geode/UI.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/SettingV3.hpp>

#include <algorithm>
#include <cmath>
#include <array>
#include <functional>
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

        const ccColor3B pink = {190, 104, 255};
        const ccColor3B palePink = {232, 198, 255};
        const ccColor3B textColor = {246, 238, 255};
        const ccColor3B muted = {174, 151, 202};
        const ccColor3B dark = {13, 8, 27};
        const ccColor3B cardColor = {27, 17, 45};
        const ccColor3B cardLight = {48, 28, 72};

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

            // Draw the eye directly in Cocos so the design is bundled in the GUI,
            // with no external texture or image asset needed.
            CCPoint outline[] = {
                {width * 0.03f, cy},
                {width * 0.17f, height * 0.76f},
                {width * 0.34f, height * 0.91f},
                {cx, height * 0.97f},
                {width * 0.66f, height * 0.91f},
                {width * 0.83f, height * 0.76f},
                {width * 0.97f, cy},
                {width * 0.83f, height * 0.24f},
                {width * 0.66f, height * 0.09f},
                {cx, height * 0.03f},
                {width * 0.34f, height * 0.09f},
                {width * 0.17f, height * 0.24f}
            };

            drawing->drawPolygon(
                outline, sizeof(outline) / sizeof(outline[0]),
                ccColor4F{0.12f, 0.07f, 0.19f, a * 0.92f},
                std::max(1.f, height * 0.018f),
                ccColor4F{1.f, 0.34f, 0.68f, a}
            );
            drawing->drawDot({cx, cy}, height * 0.30f,
                ccColor4F{1.f, 0.38f, 0.70f, a * 0.90f});
            drawing->drawDot({cx, cy}, height * 0.225f,
                ccColor4F{0.29f, 0.12f, 0.39f, a});
            drawing->drawDot({cx, cy}, height * 0.115f,
                ccColor4F{0.045f, 0.035f, 0.09f, a});
            drawing->drawDot({cx - height * 0.075f, cy + height * 0.075f},
                height * 0.042f, ccColor4F{1.f, 0.88f, 0.96f, a});
            drawing->drawDot({cx + height * 0.07f, cy - height * 0.055f},
                height * 0.020f, ccColor4F{1.f, 0.72f, 0.88f, a});
            drawing->drawSegment(
                {width * 0.20f, height * 0.73f},
                {width * 0.80f, height * 0.73f},
                std::max(0.8f, height * 0.012f),
                ccColor4F{1.f, 0.58f, 0.80f, a * 0.82f}
            );

            eye->addChild(drawing);
            return eye;
        }

        void addEyeBackdrop() {
            auto hero = makeEyeGraphic(470.f, 260.f, 34);
            if (hero) {
                hero->setPosition({PANEL_W * 0.66f, PANEL_H * 0.50f});
                hero->setRotation(-3.f);
                hero->runAction(CCRepeatForever::create(CCSequence::create(
                    CCScaleTo::create(3.2f, 1.035f),
                    CCScaleTo::create(3.2f, 0.975f), nullptr)));
                m_panel->addChild(hero, -2);
            }
            struct Eye { float x, y, w, h, rise, duration; GLubyte alpha; };
            const Eye list[] = {
                {330,590,48,28,5,4.0f,30}, {600,605,34,20,4,4.8f,25},
                {930,585,46,26,5,4.4f,31}, {1190,520,40,24,4,5.2f,26},
                {330,360,42,24,4,4.6f,26}, {1200,300,45,26,4,5.0f,29},
                {470,105,38,22,3,5.4f,25}, {1020,100,48,27,5,4.7f,28}
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
                {305,640,10,8,5.0f,150}, {430,560,7,5,4.1f,125},
                {585,660,12,7,5.8f,145}, {760,590,8,6,4.8f,120},
                {910,660,11,7,5.5f,145}, {1085,590,7,5,4.4f,130},
                {1240,625,10,8,5.2f,145}, {315,445,8,6,4.5f,130},
                {1260,410,9,7,5.7f,140}, {335,245,10,8,5.1f,145},
                {1240,235,8,5,4.3f,125}, {530,85,9,7,5.6f,135},
                {830,45,7,5,4.6f,120}, {1110,75,11,7,5.9f,145}
            };
            for (const auto& st : list) {
                auto node = CCNode::create();
                auto draw = CCDrawNode::create();
                if (!node || !draw) continue;
                CCPoint p[10];
                for (int j = 0; j < 10; ++j) {
                    const float angle = -1.57079632679f + static_cast<float>(j) * 0.62831853071f;
                    const float radius = (j % 2 == 0) ? st.r : st.r * 0.34f;
                  p[j] = CCPoint(
    st.r + std::cos(angle) * radius,
    st.r + std::sin(angle) * radius
);
                }
                const float alpha = static_cast<float>(st.alpha) / 255.f;
                draw->drawPolygon(p, 10, ccColor4F{0.42f,0.16f,0.75f,alpha*0.72f},
                    1.4f, ccColor4F{0.80f,0.48f,1.f,alpha});
                draw->drawDot({st.r,st.r}, st.r*0.20f, ccColor4F{0.93f,0.76f,1.f,alpha});
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

        void buildPanelChrome() {
            auto shadow = roundedRect({PANEL_W+24.f,PANEL_H+24.f},{0,0,0},190);
            shadow->setPosition({PANEL_W/2.f,PANEL_H/2.f-7.f}); m_panel->addChild(shadow,-6);
            auto body = roundedRect({PANEL_W,PANEL_H},{10,6,24},250);
            body->setPosition({PANEL_W/2.f,PANEL_H/2.f}); m_panel->addChild(body,-5);
            auto inset = roundedRect({PANEL_W-12.f,PANEL_H-12.f},{18,10,38},155);
            inset->setPosition({PANEL_W/2.f,PANEL_H/2.f}); m_panel->addChild(inset,-4);
            addEyeBackdrop();
            addCosmicStars();

            auto line = CCLayerColor::create({163,89,235,245},PANEL_W-70.f,3.f);
            line->setPosition({35.f,640.f}); m_panel->addChild(line,1);
            auto glow = CCLabelBMFont::create("VISION X","bigFont.fnt");
            glow->setAnchorPoint({0.5f,0.5f}); glow->setPosition({PANEL_W*0.5f+2.f,686.f});
            glow->setScale(0.82f); glow->setColor({125,53,198}); m_panel->addChild(glow,2);
            auto logo = CCLabelBMFont::create("VISION X","bigFont.fnt");
            logo->setAnchorPoint({0.5f,0.5f}); logo->setPosition({PANEL_W*0.5f,688.f});
            logo->setScale(0.82f); logo->setColor({238,211,255}); m_panel->addChild(logo,3);
            auto sub = CCLabelBMFont::create("COSMIC INPUT CONTROL","bigFont.fnt");
            sub->setAnchorPoint({0.5f,0.5f}); sub->setPosition({PANEL_W*0.5f,661.f});
            sub->setScale(0.19f); sub->setColor({176,135,210}); m_panel->addChild(sub,3);
            auto divider = CCLayerColor::create({135,73,193,220},2.f,555.f);
            divider->setPosition({262.f,52.f}); m_panel->addChild(divider,2);
        }

        void addText(const std::string& value, CCPoint pos, float scale = 0.32f,
                     ccColor3B color = {235, 231, 242}, CCPoint anchor = {0.f, 0.5f}) {
            auto label = CCLabelBMFont::create(value.c_str(), "bigFont.fnt");
            if (!label) return;
            label->setAnchorPoint(anchor);
            label->setPosition(pos);
            label->setScale(scale);
            label->setColor(color);
            m_content->addChild(label, 2);
        }

        void addCard(CCPoint center, CCSize size, ccColor3B color = {34, 29, 47}) {
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
            const auto bgColor = selected ? ccColor3B{91, 36, 145}
                : quiet ? ccColor3B{20, 13, 36} : ccColor3B{54, 29, 82};
            auto rim = roundedRect({size.width + 3.f, size.height + 3.f},
                selected ? ccColor3B{226, 177, 255} : ccColor3B{133, 78, 183}, 245);
            rim->setPosition({size.width / 2.f, size.height / 2.f});
            group->addChild(rim, -1);
            auto bg = roundedRect({size.width - 2.f, size.height - 2.f},
                bgColor, selected ? 255 : 242);
            bg->setPosition({size.width / 2.f, size.height / 2.f});
            group->addChild(bg, 0);
            auto gloss = CCLayerColor::create(
                {236, 213, 255, selected ? (GLubyte)90 : (GLubyte)35},
                std::max(8.f, size.width - 16.f), 1.f
            );
            gloss->setPosition({8.f, size.height - 6.f});
            group->addChild(gloss, 1);
            auto label = CCLabelBMFont::create(title.c_str(), "bigFont.fnt");
            if (label) {
                label->setPosition({size.width / 2.f, size.height / 2.f});
                label->setScale(labelScale);
                label->setColor(selected ? ccColor3B{255, 249, 255} : textColor);
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
            addText("ACTIVITY LOG", {30.f, 337.f}, 0.42f, palePink);
            addText("SESSION STATUS", {30.f, 309.f}, 0.24f, muted);
            addCard({320.f, 244.f}, {574.f, 98.f});
            addText(g_settings.enabled ? "Configuration enabled" : "Configuration disabled",
                {54.f, 269.f}, 0.32f);
            addText("Current stage: " + stageName(), {54.f, 239.f}, 0.27f, muted);
            addText("Selected mode: " + std::string(MODE_NAMES[g_settings.selectedMode]),
                {54.f, 212.f}, 0.27f, muted);
            addCard({320.f, 115.f}, {574.f, 100.f});
            addText("INPUT TUNING", {54.f, 145.f}, 0.25f, palePink);
            addText("Per-mode values are configured in Stages > Open Tuning.",
                {54.f, 115.f}, 0.23f);
            addText("This panel displays configuration values only.",
                {54.f, 87.f}, 0.21f, muted);
        }

        void drawStages() {
            addText("STAGE SELECT",{305.f,620.f},0.34f,palePink);
            addText("Choose a preset, then tune each gamemode below.",{305.f,594.f},0.18f,muted);
            addCard({800.f,551.f},{975.f,94.f},{20,12,39});
            for(int i=0;i<6;++i) {
                const int col=i%3, row=i/3;
                const float x=485.f+static_cast<float>(col)*315.f;
                const float y=574.f-static_cast<float>(row)*39.f;
                const bool selected=g_settings.stage==i;
                addButton(STAGE_NAMES[i],{x,y},{270.f,32.f},
                    [this,i]{applyStage(i);redraw();},selected,!selected,i>=4?0.23f:0.28f);
            }

            addText("TICK TUNING",{305.f,480.f},0.31f,palePink);
            addText("Min and Max values are adjustable for every mode.",{305.f,456.f},0.17f,muted);
            for(int i=0;i<8;++i) {
                const int col=i%4, row=i/4;
                drawTuningModeTile(i,{430.f+static_cast<float>(col)*245.f,
                    407.f-static_cast<float>(row)*83.f},false);
            }

            addText("CLICK & RELEASE",{305.f,238.f},0.31f,palePink);
            addText("Matching controls for Click and Release; unsupported releases show N/A.",
                {305.f,214.f},0.17f,muted);
            for(int i=0;i<8;++i) {
                const int col=i%4, row=i/4;
                drawTuningModeTile(i,{430.f+static_cast<float>(col)*245.f,
                    164.f-static_cast<float>(row)*83.f},true);
            }
            addText("Each mode keeps its own values.",{305.f,22.f},0.17f,muted);
        }

        void drawModes() {
            addText("GAMEMODES",{305.f,620.f},0.35f,palePink);
            addText("Select a mode to highlight its tuning cards in Stages.",{305.f,590.f},0.19f,muted);
            for(int i=0;i<8;++i) {
                const int col=i%4, row=i/4;
                const float x=430.f+static_cast<float>(col)*245.f;
                const float y=450.f-static_cast<float>(row)*180.f;
                addCard({x,y},{210.f,135.f},{22,13,42});
                addModeIcon(i,{x,y+27.f},0.52f);
                addText(MODE_NAMES[i],{x,y-22.f},0.23f,palePink,{0.5f,0.5f});
                addButton(g_settings.selectedMode==i?"SELECTED":"SELECT",{x,y-48.f},{118.f,24.f},
                    [this,i]{g_settings.selectedMode=i;redraw();},
                    g_settings.selectedMode==i,g_settings.selectedMode!=i,0.19f);
            }
        }

        void drawTools() {
            addText("TOOLS & SETTINGS",{305.f,620.f},0.35f,palePink);
            addText("Visual and configuration controls",{305.f,590.f},0.19f,muted);
            addCard({790.f,475.f},{955.f,128.f},{22,13,42});
            addText("BUFFER CONFIGURATION",{350.f,505.f},0.25f,palePink);
            addText(g_settings.enabled?"Status: enabled":"Status: disabled",{350.f,469.f},0.22f);
            addButton(g_settings.enabled?"DISABLE":"ENABLE",{1165.f,480.f},{150.f,42.f},
                [this]{g_settings.enabled=!g_settings.enabled;redraw();},
                g_settings.enabled,true,0.23f);
            addCard({790.f,290.f},{955.f,128.f},{22,13,42});
            addText("RESET TUNING",{350.f,320.f},0.25f,palePink);
            addText("Restore default ranges for all eight gamemodes.",{350.f,284.f},0.20f,muted);
            addButton("RESET",{1165.f,295.f},{150.f,42.f},
                [this]{resetTuning();redraw();},false,true,0.23f);
            addText("Toggle the interface at any time with Left Alt.",{305.f,108.f},0.20f,muted);
        }

        void addModeIcon(int modeIndex,CCPoint center,float scale) {
            static const char* const frames[8] = {
                "player_01_001.png","ship_01_001.png","player_ball_01_001.png","bird_01_001.png",
                "dart_01_001.png","robot_01_01_001.png","spider_01_01_001.png","swing_01_001.png"
            };
            auto badge=CCDrawNode::create();
            if(badge) {
                badge->drawDot({28.f,28.f},27.f,ccColor4F{0.16f,0.07f,0.26f,0.95f});
                badge->drawDot({28.f,28.f},24.f,ccColor4F{0.40f,0.16f,0.60f,0.72f});
                badge->drawDot({28.f,28.f},21.f,ccColor4F{0.06f,0.035f,0.12f,0.98f});
                badge->setPosition({center.x-28.f,center.y-28.f});
                m_content->addChild(badge,2);
            }
            auto icon=CCSprite::createWithSpriteFrameName(frames[std::clamp(modeIndex,0,7)]);
            if(icon) {
                icon->setAnchorPoint({0.5f,0.5f}); icon->setPosition(center);
                icon->setColor({226,190,255}); icon->setOpacity(250); icon->setScale(scale);
                m_content->addChild(icon,3);
            } else {
                auto fallback=CCLabelBMFont::create(MODE_MARKS[std::clamp(modeIndex,0,7)],"bigFont.fnt");
                if(fallback) {
                    fallback->setPosition(center); fallback->setScale(0.28f);
                    fallback->setColor(palePink); m_content->addChild(fallback,3);
                }
            }
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
            addCard(center,{226.f,74.f},{19,12,38});
            addModeIcon(modeIndex,{center.x-77.f,center.y+11.f},0.37f);
            addText(MODE_NAMES[modeIndex],{center.x-77.f,center.y-24.f},
                0.16f,palePink,{0.5f,0.5f});

            if(!clickRelease) {
                addMiniAdjust("MIN",mode.minTicks,center.x+17.f,center.y+15.f,
                    [this,modeIndex]{auto& v=g_settings.modes[modeIndex];v.minTicks=std::max(0,v.minTicks-1);redraw();},
                    [this,modeIndex]{auto& v=g_settings.modes[modeIndex];v.minTicks=std::min(v.maxTicks,v.minTicks+1);redraw();});
                addMiniAdjust("MAX",mode.maxTicks,center.x+17.f,center.y-15.f,
                    [this,modeIndex]{auto& v=g_settings.modes[modeIndex];v.maxTicks=std::max(v.minTicks,v.maxTicks-1);redraw();},
                    [this,modeIndex]{auto& v=g_settings.modes[modeIndex];v.maxTicks=std::min(MAX_TICK_TUNING,v.maxTicks+1);redraw();});
            } else {
                addMiniAdjust("CLICK",mode.clickTicks,center.x+17.f,center.y+15.f,
                    [this,modeIndex]{auto& v=g_settings.modes[modeIndex];v.clickTicks=std::max(0,v.clickTicks-1);redraw();},
                    [this,modeIndex]{auto& v=g_settings.modes[modeIndex];v.clickTicks=std::min(MAX_CLICK_TUNING,v.clickTicks+1);redraw();});
                if(hasReleaseTuning(modeIndex)) {
                    addMiniAdjust("RELEASE",mode.releaseTicks,center.x+17.f,center.y-15.f,
                        [this,modeIndex]{auto& v=g_settings.modes[modeIndex];v.releaseTicks=std::max(0,v.releaseTicks-1);redraw();},
                        [this,modeIndex]{auto& v=g_settings.modes[modeIndex];v.releaseTicks=std::min(MAX_RELEASE_TUNING,v.releaseTicks+1);redraw();});
                } else {
                    addText("RELEASE",{center.x-20.f,center.y-15.f},0.125f,palePink,{1.f,0.5f});
                    addText("N/A",{center.x+44.f,center.y-15.f},0.18f,muted,{0.5f,0.5f});
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
