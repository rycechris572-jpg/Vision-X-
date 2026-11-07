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
        CCNode* m_panel = nullptr;
        CCNode* m_content = nullptr;
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
            const auto win = CCDirector::sharedDirector()->getWinSize();
            setContentSize(win);
            setTouchEnabled(true);
            setTouchMode(kCCTouchesOneByOne);

            m_shade = CCLayerColor::create({5, 4, 12, 0}, win.width, win.height);
            if (!m_shade) return false;
            addChild(m_shade, 0);

            m_panel = CCNode::create();
            m_panel->setContentSize({PANEL_W, PANEL_H});
            m_panel->setAnchorPoint({0.5f, 0.5f});
            m_panel->setPosition({win.width / 2.f, win.height / 2.f});
            m_panel->setCascadeOpacityEnabled(true);
            const float fit = std::min((win.width - 28.f) / PANEL_W, (win.height - 28.f) / PANEL_H);
            m_panelScale = std::clamp(fit, 0.55f, 1.f);
            m_panel->setScale(m_panelScale * 0.90f);
            addChild(m_panel, 10);

            buildPanelChrome();
            m_content = CCNode::create();
            m_content->setContentSize({PANEL_W, PANEL_H});
            m_content->setAnchorPoint({0.5f, 0.5f});
            m_content->setPosition({PANEL_W / 2.f, PANEL_H / 2.f});
            m_panel->addChild(m_content, 5);
            redraw();

            m_shade->runAction(CCFadeTo::create(0.18f, 176));
            m_panel->runAction(CCEaseBackOut::create(CCScaleTo::create(0.24f, m_panelScale)));
            return true;
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

        void buildPanelChrome() {
            auto shadow = roundedRect({PANEL_W + 8.f, PANEL_H + 8.f}, {0, 0, 0}, 120);
            shadow->setPosition({PANEL_W / 2.f, PANEL_H / 2.f - 4.f});
            m_panel->addChild(shadow, -3);
            auto body = roundedRect({PANEL_W, PANEL_H}, dark, 250);
            body->setPosition({PANEL_W / 2.f, PANEL_H / 2.f});
            m_panel->addChild(body, -2);
            auto rail = roundedRect({138.f, 390.f}, {28, 24, 40}, 255);
            rail->setPosition({77.f, 218.f});
            m_panel->addChild(rail, -1);
            auto accent = CCLayerColor::create({255, 125, 194, 255}, 420.f, 2.f);
            accent->setPosition({166.f, 371.f});
            m_panel->addChild(accent, 1);

            auto logo = CCLabelBMFont::create("VISION X", "bigFont.fnt");
            logo->setAnchorPoint({0.f, 0.5f});
            logo->setPosition({166.f, 405.f});
            logo->setScale(0.57f);
            logo->setColor(palePink);
            m_panel->addChild(logo, 3);

            auto subtitle = CCLabelBMFont::create("PREDICTIVE INPUT BUFFER", "bigFont.fnt");
            subtitle->setAnchorPoint({0.f, 0.5f});
            subtitle->setPosition({169.f, 385.f});
            subtitle->setScale(0.23f);
            subtitle->setColor(muted);
            m_panel->addChild(subtitle, 3);

            auto dot = CCLayerColor::create({108, 235, 175, 255}, 7.f, 7.f);
            dot->setPosition({528.f, 400.f});
            m_panel->addChild(dot, 3);
            auto ready = CCLabelBMFont::create("READY", "bigFont.fnt");
            ready->setAnchorPoint({0.f, 0.5f});
            ready->setPosition({540.f, 404.f});
            ready->setScale(0.22f);
            ready->setColor({171, 230, 190});
            m_panel->addChild(ready, 3);
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
            auto card = roundedRect(size, color, 246);
            card->setPosition(center);
            m_content->addChild(card, 0);
        }

        void addButton(const std::string& title, CCPoint center, CCSize size,
                       std::function<void()> callback, bool selected = false,
                       bool quiet = false, float labelScale = 0.30f) {
            auto group = CCNode::create();
            group->setContentSize(size);
            group->setAnchorPoint({0.5f, 0.5f});
            group->setPosition(center);
            const auto bgColor = selected ? ccColor3B{224, 105, 171}
                : quiet ? ccColor3B{49, 41, 62} : ccColor3B{73, 51, 82};
            auto bg = roundedRect(size, bgColor, selected ? 255 : 235);
            bg->setPosition({size.width / 2.f, size.height / 2.f});
            group->addChild(bg, 0);
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
            addText("NAVIGATION", {31.f, 374.f}, 0.21f, muted);
            for (int i = 0; i < 4; ++i) {
                const auto entry = nav[i];
                const bool selected = !m_showTuning && m_tab == entry.tab;
                addButton(entry.label, {77.f, 321.f - i * 58.f}, {112.f, 40.f},
                    [this, tab = entry.tab] { m_tab = tab; m_showTuning = false; redraw(); },
                    selected, !selected, 0.30f);
            }
            addText("LEFT ALT", {77.f, 52.f}, 0.25f, palePink, {0.5f, 0.5f});
            addText("TOGGLE UI", {77.f, 35.f}, 0.18f, muted, {0.5f, 0.5f});
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
            addText("ACTIVITY LOG", {166.f, 344.f}, 0.39f, palePink);
            addText("SESSION STATUS", {166.f, 300.f}, 0.24f, muted);
            addCard({390.f, 238.f}, {430.f, 92.f});
            addText(g_settings.enabled ? "Configuration enabled" : "Configuration disabled",
                {190.f, 255.f}, 0.31f);
            addText("Current stage: " + stageName(), {190.f, 227.f}, 0.27f, muted);
            addText("Selected mode: " + std::string(MODE_NAMES[g_settings.selectedMode]),
                {190.f, 201.f}, 0.27f, muted);
            addCard({390.f, 116.f}, {430.f, 96.f});
            addText("INPUT TUNING", {190.f, 145.f}, 0.25f, palePink);
            addText("Per-mode values are configured in Stages > Open Tuning.",
                {190.f, 116.f}, 0.22f);
            addText("This panel displays configuration values only.",
                {190.f, 91.f}, 0.20f, muted);
        }

        void drawStages() {
            addText("STAGES", {166.f, 344.f}, 0.40f, palePink);
            addText("Select a preset for the input buffer tuning range.",
                {166.f, 320.f}, 0.22f, muted);
            addCard({390.f, 270.f}, {438.f, 76.f}, {31, 26, 43});
            for (int i = 0; i < 6; ++i) {
                const float x = 195.f + i * 78.f;
                addButton(STAGE_NAMES[i], {x, 270.f}, {i >= 4 ? 70.f : 60.f, 34.f},
                    [this, i] { applyStage(i); redraw(); },
                    g_settings.stage == i, g_settings.stage != i, i >= 4 ? 0.23f : 0.30f);
            }
            addText("TUNING OVERVIEW", {166.f, 205.f}, 0.28f, palePink);
            addCard({390.f, 144.f}, {438.f, 94.f});
            const auto& mode = g_settings.modes[g_settings.selectedMode];
            addText("Selected mode: " + std::string(MODE_NAMES[g_settings.selectedMode]),
                {190.f, 170.f}, 0.26f);
            addText(fmt::format("Tick range: {} - {} ticks", mode.minTicks, mode.maxTicks),
                {190.f, 144.f}, 0.25f, muted);
            addText(fmt::format("Click: {} ticks   |   Release: {}", mode.clickTicks,
                hasReleaseTuning(g_settings.selectedMode) ? fmt::format("{} ticks", mode.releaseTicks) : "N/A"),
                {190.f, 119.f}, 0.23f, muted);
            addButton("OPEN TUNING", {390.f, 69.f}, {220.f, 38.f},
                [this] { m_showTuning = true; m_tuningPage = TuningPage::Tick; redraw(); }, true, false, 0.30f);
            addText("Tick maximum: 80  |  Click maximum: 30  |  Release maximum: 60",
                {166.f, 28.f}, 0.19f, muted);
        }

        void drawModes() {
            addText("GAMEMODES", {166.f, 344.f}, 0.40f, palePink);
            addText("Choose a gamemode to open its individual tuning controls.",
                {166.f, 320.f}, 0.22f, muted);
            drawModeGrid(270.f);
            addText("Each gamemode keeps its own tick, click and release values.",
                {166.f, 104.f}, 0.22f, muted);
            addButton("OPEN SELECTED TUNING", {390.f, 66.f}, {230.f, 38.f},
                [this] { m_showTuning = true; m_tuningPage = TuningPage::Tick; redraw(); }, true, false, 0.25f);
        }

        void drawTools() {
            addText("TOOLS & SETTINGS", {166.f, 344.f}, 0.39f, palePink);
            addText("VISUAL AND CONFIGURATION CONTROLS", {166.f, 315.f}, 0.21f, muted);
            addCard({390.f, 238.f}, {430.f, 88.f});
            addText("BUFFER CONFIGURATION", {190.f, 260.f}, 0.26f, palePink);
            addText(g_settings.enabled ? "Status: enabled" : "Status: disabled", {190.f, 232.f}, 0.25f);
            addButton(g_settings.enabled ? "DISABLE" : "ENABLE", {520.f, 243.f}, {105.f, 34.f},
                [this] { g_settings.enabled = !g_settings.enabled; redraw(); }, g_settings.enabled, true, 0.24f);
            addCard({390.f, 125.f}, {430.f, 100.f});
            addText("RESET TUNING", {190.f, 150.f}, 0.27f, palePink);
            addText("Restore the default ranges for all eight gamemodes.",
                {190.f, 122.f}, 0.21f, muted);
            addButton("RESET", {520.f, 121.f}, {105.f, 34.f},
                [this] { resetTuning(); redraw(); }, false, true, 0.26f);
            addText("Toggle the interface at any time with Left Alt.",
                {166.f, 48.f}, 0.22f, muted);
        }

        void drawModeGrid(float centerY) {
            constexpr float startX = 211.f;
            constexpr float gapX = 91.f;
            constexpr float gapY = 55.f;
            for (int i = 0; i < 8; ++i) {
                const int col = i % 4;
                const int row = i / 4;
                const CCPoint pos = {startX + col * gapX, centerY - row * gapY};
                addButton(std::string(MODE_MARKS[i]) + "  " + MODE_NAMES[i], pos, {82.f, 40.f},
                    [this, i] {
                        g_settings.selectedMode = i;
                        if (!m_showTuning) m_showTuning = true;
                        m_tuningPage = TuningPage::Tick;
                        redraw();
                    }, g_settings.selectedMode == i, g_settings.selectedMode != i, 0.22f);
            }
        }

        void drawTuningPage() {
            const int modeIndex = std::clamp(g_settings.selectedMode, 0, 7);
            auto& mode = g_settings.modes[modeIndex];
            addText("GAMEMODE TUNING", {166.f, 344.f}, 0.37f, palePink);
            addButton("BACK", {563.f, 344.f}, {72.f, 30.f},
                [this] { m_showTuning = false; redraw(); }, false, true, 0.24f);
            addButton("TICK TUNING", {285.f, 307.f}, {142.f, 34.f},
                [this] { m_tuningPage = TuningPage::Tick; redraw(); },
                m_tuningPage == TuningPage::Tick, m_tuningPage != TuningPage::Tick, 0.25f);
            addButton("CLICK & RELEASE", {465.f, 307.f}, {166.f, 34.f},
                [this] { m_tuningPage = TuningPage::ClickRelease; redraw(); },
                m_tuningPage == TuningPage::ClickRelease, m_tuningPage != TuningPage::ClickRelease, 0.23f);
            drawModeGrid(252.f);
            addCard({390.f, 116.f}, {438.f, 94.f}, cardLight);

            if (m_tuningPage == TuningPage::Tick) {
                addText(std::string(MODE_NAMES[modeIndex]) + "  /  TICK RANGE",
                    {190.f, 150.f}, 0.25f, palePink);
                addAdjust("MIN TICKS", fmt::format("{}", mode.minTicks), 292.f, 105.f,
                    [this, modeIndex] { auto& v = g_settings.modes[modeIndex]; v.minTicks = std::max(0, v.minTicks - 1); redraw(); },
                    [this, modeIndex] { auto& v = g_settings.modes[modeIndex]; v.minTicks = std::min(v.maxTicks, v.minTicks + 1); redraw(); });
                addAdjust("MAX TICKS", fmt::format("{}", mode.maxTicks), 488.f, 105.f,
                    [this, modeIndex] { auto& v = g_settings.modes[modeIndex]; v.maxTicks = std::max(v.minTicks, v.maxTicks - 1); redraw(); },
                    [this, modeIndex] { auto& v = g_settings.modes[modeIndex]; v.maxTicks = std::min(MAX_TICK_TUNING, v.maxTicks + 1); redraw(); });
                addText("Per-mode minimum and maximum. Hard cap: 80 ticks.",
                    {166.f, 42.f}, 0.20f, muted);
            } else {
                addText(std::string(MODE_NAMES[modeIndex]) + "  /  CLICK & RELEASE",
                    {190.f, 150.f}, 0.24f, palePink);
                addAdjust("CLICK TICKS", fmt::format("{}", mode.clickTicks), 292.f, 105.f,
                    [this, modeIndex] { auto& v = g_settings.modes[modeIndex]; v.clickTicks = std::max(0, v.clickTicks - 1); redraw(); },
                    [this, modeIndex] { auto& v = g_settings.modes[modeIndex]; v.clickTicks = std::min(MAX_CLICK_TUNING, v.clickTicks + 1); redraw(); });
                if (hasReleaseTuning(modeIndex)) {
                    addAdjust("RELEASE TICKS", fmt::format("{}", mode.releaseTicks), 488.f, 105.f,
                        [this, modeIndex] { auto& v = g_settings.modes[modeIndex]; v.releaseTicks = std::max(0, v.releaseTicks - 1); redraw(); },
                        [this, modeIndex] { auto& v = g_settings.modes[modeIndex]; v.releaseTicks = std::min(MAX_RELEASE_TUNING, v.releaseTicks + 1); redraw(); });
                } else {
                    addText("RELEASE", {488.f, 133.f}, 0.26f, palePink, {0.5f, 0.5f});
                    addText("N/A", {488.f, 105.f}, 0.40f, muted, {0.5f, 0.5f});
                }
                addText("Click cap: 30 ticks  |  Release cap: 60 ticks",
                    {166.f, 42.f}, 0.20f, muted);
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
        // Geode's OverlayManager is installed as the director notification node,
        // so this panel survives scene changes and appears over pause/menu UI.
        OverlayManager::get()->addChild(overlay, 100000);
    }
}

$on_game(Loaded) {
    listenForKeybindSettingPresses("toggle-ui",
        [](Keybind const&, bool down, bool repeat, double) {
            if (down && !repeat) vx::toggleOverlay();
        }
    );
}
