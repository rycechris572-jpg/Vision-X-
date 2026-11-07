#include <Geode/Geode.hpp>
#include <Geode/UI.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/SettingV3.hpp>

#include <algorithm>
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
    enum class TuningPage { Tick, ClickRelease };

    struct ModeTuning {
        int minTicks = 2;
        int maxTicks = 8;
        int clickTicks = 0;
        int releaseTicks = 0;
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

        static constexpr float PANEL_W = 640.f;
        static constexpr float PANEL_H = 440.f;

        CCLayerColor* m_shade = nullptr;
        CCLayerColor* m_panel = nullptr;
        CCLayerColor* m_content = nullptr;
        float m_panelScale = 1.f;
        Tab m_tab = Tab::Stages;
        TuningPage m_tuningPage = TuningPage::Tick;
        bool m_showTuning = false;
        bool m_closing = false;
        int m_pressedButton = -1;
        std::vector<ButtonHitbox> m_buttons;

        const ccColor3B pink = {255, 132, 198};
        const ccColor3B palePink = {255, 190, 222};
        const ccColor3B textColor = {235, 231, 242};
        const ccColor3B muted = {150, 145, 166};
        const ccColor3B dark = {22, 19, 32};
        const ccColor3B cardColor = {34, 29, 47};
        const ccColor3B cardLight = {45, 37, 59};

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
            return CCLayerColor::create({color.r, color.g, color.b, opacity}, size.width, size.height);
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
            // A low-contrast eye watermark sits in the center of the GUI background.
            auto hero = makeEyeGraphic(340.f, 196.f, 54);
            if (hero) {
                hero->setPosition({320.f, 218.f});
                hero->setRotation(-2.f);
                hero->runAction(CCRepeatForever::create(CCSequence::create(
                    CCScaleTo::create(2.8f, 1.025f),
                    CCScaleTo::create(2.8f, 0.985f),
                    nullptr
                )));
                m_panel->addChild(hero, -2);
            }

            struct FloatingEye {
                float x, y, width, height, rise, duration;
                GLubyte alpha;
            };
            const FloatingEye eyes[] = {
                {76.f, 338.f, 38.f, 22.f, 5.f, 4.0f, 38},
                {216.f, 320.f, 30.f, 18.f, 4.f, 4.8f, 30},
                {520.f, 336.f, 42.f, 24.f, 5.f, 4.4f, 36},
                {580.f, 250.f, 32.f, 19.f, 4.f, 5.2f, 34},
                {58.f, 190.f, 42.f, 24.f, 4.f, 4.6f, 34},
                {566.f, 146.f, 38.f, 22.f, 4.f, 5.0f, 36},
                {172.f, 80.f, 34.f, 20.f, 3.f, 5.4f, 31},
                {440.f, 65.f, 42.f, 24.f, 5.f, 4.7f, 35}
            };

            for (const auto& spec : eyes) {
                auto eye = makeEyeGraphic(spec.width, spec.height, spec.alpha);
                if (!eye) continue;
                eye->setPosition({spec.x, spec.y});
                eye->runAction(CCRepeatForever::create(CCSequence::create(
                    CCMoveBy::create(spec.duration, {0.f, spec.rise}),
                    CCMoveBy::create(spec.duration, {0.f, -spec.rise}),
                    nullptr
                )));
                m_panel->addChild(eye, -2);
            }
        }

        void buildPanelChrome() {
            auto shadow = roundedRect({PANEL_W + 10.f, PANEL_H + 10.f}, {0, 0, 0}, 155);
            shadow->setPosition({PANEL_W / 2.f, PANEL_H / 2.f - 5.f});
            m_panel->addChild(shadow, -4);

            auto body = roundedRect({PANEL_W, PANEL_H}, {15, 11, 25}, 252);
            body->setPosition({PANEL_W / 2.f, PANEL_H / 2.f});
            m_panel->addChild(body, -3);

            addEyeBackdrop();

            auto topLine = CCLayerColor::create({222, 142, 207, 225}, PANEL_W - 56.f, 2.f);
            topLine->setPosition({28.f, 360.f});
            m_panel->addChild(topLine, 1);

            auto logo = CCLabelBMFont::create("VISION X", "bigFont.fnt");
            logo->setAnchorPoint({0.f, 0.5f});
            logo->setPosition({30.f, 405.f});
            logo->setScale(0.62f);
            logo->setColor(palePink);
            m_panel->addChild(logo, 3);

            auto subtitle = CCLabelBMFont::create("PREDICTIVE INPUT BUFFER", "bigFont.fnt");
            subtitle->setAnchorPoint({0.f, 0.5f});
            subtitle->setPosition({32.f, 383.f});
            subtitle->setScale(0.21f);
            subtitle->setColor(muted);
            m_panel->addChild(subtitle, 3);

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
            const auto bgColor = selected ? ccColor3B{231, 133, 196}
                : quiet ? ccColor3B{43, 34, 56} : ccColor3B{91, 61, 105};
            auto rim = roundedRect({size.width + 3.f, size.height + 3.f},
                selected ? ccColor3B{255, 226, 244} : ccColor3B{125, 93, 145}, 245);
            rim->setPosition({size.width / 2.f, size.height / 2.f});
            group->addChild(rim, -1);
            auto bg = roundedRect({size.width - 2.f, size.height - 2.f},
                bgColor, selected ? 255 : 242);
            bg->setPosition({size.width / 2.f, size.height / 2.f});
            group->addChild(bg, 0);
            auto gloss = CCLayerColor::create(
                {255, 235, 248, selected ? (GLubyte)95 : (GLubyte)48},
                std::max(8.f, size.width - 16.f), 1.f
            );
            gloss->setPosition({8.f, size.height - 6.f});
            group->addChild(gloss, 1);
            auto label = CCLabelBMFont::create(title.c_str(), "bigFont.fnt");
            if (label) {
                label->setPosition({size.width / 2.f, size.height / 2.f});
                label->setScale(labelScale);
                label->setColor(selected ? ccColor3B{255, 248, 253} : textColor);
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
            if (m_showTuning) drawTuningPage();
            else drawMainPage();
            m_content->runAction(CCSpawn::create(
                CCFadeTo::create(0.16f, 255),
                CCEaseOut::create(CCMoveTo::create(0.18f, {PANEL_W / 2.f, PANEL_H / 2.f}), 2.f),
                nullptr
            ));
        }

        void drawSidebar() {
            struct Nav { const char* label; Tab tab; };
            constexpr Nav nav[] = {
                {"LOG", Tab::Log}, {"STAGES", Tab::Stages},
                {"MODES", Tab::Modes}, {"TOOLS", Tab::Tools}
            };
            const float positions[] = {230.f, 320.f, 410.f, 500.f};
            for (int i = 0; i < 4; ++i) {
                const auto entry = nav[i];
                const bool selected = !m_showTuning && m_tab == entry.tab;
                addButton(entry.label, {positions[i], 405.f}, {76.f, 28.f},
                    [this, tab = entry.tab] {
                        m_tab = tab;
                        m_showTuning = false;
                        redraw();
                    },
                    selected, !selected, 0.25f);
            }
            addButton("X", {604.f, 405.f}, {25.f, 25.f},
                [this] { closeAnimated(); }, false, true, 0.25f);
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
            addText("STAGE SELECT", {30.f, 337.f}, 0.42f, palePink);
            addText("Choose a preset. Each stage sets the default input lookahead range.",
                {30.f, 310.f}, 0.22f, muted);

            // Six large pills in a dedicated wide selector, matching the reference layout.
            addCard({320.f, 252.f}, {574.f, 104.f}, {29, 22, 39});
            for (int i = 0; i < 6; ++i) {
                const int col = i % 3;
                const int row = i / 3;
                const float x = 126.f + static_cast<float>(col) * 194.f;
                const float y = 275.f - static_cast<float>(row) * 43.f;
                const bool selected = g_settings.stage == i;
                addButton(STAGE_NAMES[i], {x, y}, {174.f, 35.f},
                    [this, i] { applyStage(i); redraw(); },
                    selected, !selected, i >= 4 ? 0.24f : 0.29f);
            }

            addText("TICK TUNING", {30.f, 168.f}, 0.29f, palePink);
            addCard({320.f, 112.f}, {574.f, 88.f});
            const auto& mode = g_settings.modes[g_settings.selectedMode];
            addText("Selected mode: " + std::string(MODE_NAMES[g_settings.selectedMode]),
                {54.f, 138.f}, 0.26f);
            addText(fmt::format("Minimum: {} ticks      Maximum: {} ticks",
                mode.minTicks, mode.maxTicks), {54.f, 111.f}, 0.25f, muted);
            addText(fmt::format("Click: {} ticks      Release: {}",
                mode.clickTicks,
                hasReleaseTuning(g_settings.selectedMode)
                    ? fmt::format("{} ticks", mode.releaseTicks) : "N/A"),
                {54.f, 85.f}, 0.23f, muted);

            addButton("OPEN TUNING", {320.f, 43.f}, {220.f, 34.f},
                [this] { m_showTuning = true; m_tuningPage = TuningPage::Tick; redraw(); },
                true, false, 0.29f);
        }

        void drawModes() {
            addText("GAMEMODES", {30.f, 337.f}, 0.42f, palePink);
            addText("Choose a gamemode to configure its individual tick values.",
                {30.f, 310.f}, 0.22f, muted);
            drawModeGrid(260.f);
            addText("Each gamemode keeps its own values.", {30.f, 125.f}, 0.23f, muted);
            addButton("OPEN SELECTED TUNING", {320.f, 74.f}, {260.f, 38.f},
                [this] { m_showTuning = true; m_tuningPage = TuningPage::Tick; redraw(); },
                true, false, 0.25f);
        }

        void drawTools() {
            addText("TOOLS & SETTINGS", {30.f, 337.f}, 0.41f, palePink);
            addText("VISUAL AND CONFIGURATION CONTROLS", {30.f, 309.f}, 0.22f, muted);
            addCard({320.f, 242.f}, {574.f, 92.f});
            addText("BUFFER CONFIGURATION", {54.f, 266.f}, 0.27f, palePink);
            addText(g_settings.enabled ? "Status: enabled" : "Status: disabled",
                {54.f, 235.f}, 0.25f);
            addButton(g_settings.enabled ? "DISABLE" : "ENABLE", {535.f, 244.f}, {112.f, 35.f},
                [this] { g_settings.enabled = !g_settings.enabled; redraw(); },
                g_settings.enabled, true, 0.24f);
            addCard({320.f, 125.f}, {574.f, 96.f});
            addText("RESET TUNING", {54.f, 151.f}, 0.27f, palePink);
            addText("Restore the default ranges for all eight gamemodes.",
                {54.f, 122.f}, 0.22f, muted);
            addButton("RESET", {535.f, 123.f}, {112.f, 35.f},
                [this] { resetTuning(); redraw(); }, false, true, 0.26f);
            addText("Toggle the interface at any time with Left Alt.",
                {30.f, 45.f}, 0.22f, muted);
        }

        void drawModeGrid(float centerY) {
            constexpr float startX = 106.f;
            constexpr float gapX = 143.f;
            constexpr float gapY = 57.f;
            for (int i = 0; i < 8; ++i) {
                const int col = i % 4;
                const int row = i / 4;
                const CCPoint pos = {
                    startX + static_cast<float>(col) * gapX,
                    centerY - static_cast<float>(row) * gapY
                };
                addButton(std::string(MODE_MARKS[i]) + "  " + MODE_NAMES[i], pos,
                    {130.f, 42.f},
                    [this, i] {
                        g_settings.selectedMode = i;
                        m_showTuning = true;
                        m_tuningPage = TuningPage::Tick;
                        redraw();
                    },
                    g_settings.selectedMode == i, g_settings.selectedMode != i, 0.23f);
            }
        }

        void drawTuningPage() {
            const int modeIndex = std::clamp(g_settings.selectedMode, 0, 7);
            auto& mode = g_settings.modes[modeIndex];

            addText("GAMEMODE TUNING", {30.f, 337.f}, 0.39f, palePink);
            addButton("BACK", {584.f, 337.f}, {72.f, 30.f},
                [this] { m_showTuning = false; redraw(); }, false, true, 0.24f);

            addButton("TICK TUNING", {245.f, 297.f}, {160.f, 34.f},
                [this] { m_tuningPage = TuningPage::Tick; redraw(); },
                m_tuningPage == TuningPage::Tick, m_tuningPage != TuningPage::Tick, 0.25f);
            addButton("CLICK & RELEASE", {438.f, 297.f}, {190.f, 34.f},
                [this] { m_tuningPage = TuningPage::ClickRelease; redraw(); },
                m_tuningPage == TuningPage::ClickRelease,
                m_tuningPage != TuningPage::ClickRelease, 0.23f);

            // Eight compact mode tiles remain separate from the tuning controls.
            for (int i = 0; i < 8; ++i) {
                const float x = 52.f + static_cast<float>(i) * 76.5f;
                addButton(MODE_MARKS[i], {x, 235.f}, {62.f, 32.f},
                    [this, i] { g_settings.selectedMode = i; redraw(); },
                    modeIndex == i, modeIndex != i, 0.26f);
            }

            addCard({320.f, 125.f}, {574.f, 112.f}, {38, 29, 50});
            if (m_tuningPage == TuningPage::Tick) {
                addText(std::string(MODE_NAMES[modeIndex]) + "  /  TICK RANGE",
                    {54.f, 158.f}, 0.26f, palePink);
                addAdjust("MIN TICKS", fmt::format("{}", mode.minTicks), 205.f, 112.f,
                    [this, modeIndex] {
                        auto& v = g_settings.modes[modeIndex];
                        v.minTicks = std::max(0, v.minTicks - 1);
                        redraw();
                    },
                    [this, modeIndex] {
                        auto& v = g_settings.modes[modeIndex];
                        v.minTicks = std::min(v.maxTicks, v.minTicks + 1);
                        redraw();
                    });
                addAdjust("MAX TICKS", fmt::format("{}", mode.maxTicks), 435.f, 112.f,
                    [this, modeIndex] {
                        auto& v = g_settings.modes[modeIndex];
                        v.maxTicks = std::max(v.minTicks, v.maxTicks - 1);
                        redraw();
                    },
                    [this, modeIndex] {
                        auto& v = g_settings.modes[modeIndex];
                        v.maxTicks = std::min(MAX_TICK_TUNING, v.maxTicks + 1);
                        redraw();
                    });
            } else {
                addText(std::string(MODE_NAMES[modeIndex]) + "  /  CLICK & RELEASE",
                    {54.f, 158.f}, 0.25f, palePink);
                addAdjust("CLICK TICKS", fmt::format("{}", mode.clickTicks), 205.f, 112.f,
                    [this, modeIndex] {
                        auto& v = g_settings.modes[modeIndex];
                        v.clickTicks = std::max(0, v.clickTicks - 1);
                        redraw();
                    },
                    [this, modeIndex] {
                        auto& v = g_settings.modes[modeIndex];
                        v.clickTicks = std::min(MAX_CLICK_TUNING, v.clickTicks + 1);
                        redraw();
                    });
                if (hasReleaseTuning(modeIndex)) {
                    addAdjust("RELEASE TICKS", fmt::format("{}", mode.releaseTicks), 435.f, 112.f,
                        [this, modeIndex] {
                            auto& v = g_settings.modes[modeIndex];
                            v.releaseTicks = std::max(0, v.releaseTicks - 1);
                            redraw();
                        },
                        [this, modeIndex] {
                            auto& v = g_settings.modes[modeIndex];
                            v.releaseTicks = std::min(MAX_RELEASE_TUNING, v.releaseTicks + 1);
                            redraw();
                        });
                } else {
                    addText("RELEASE", {435.f, 139.f}, 0.26f, palePink, {0.5f, 0.5f});
                    addText("N/A", {435.f, 111.f}, 0.40f, muted, {0.5f, 0.5f});
                }
            }
            addText("Values are saved independently for each gamemode.",
                {30.f, 42.f}, 0.21f, muted);
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
