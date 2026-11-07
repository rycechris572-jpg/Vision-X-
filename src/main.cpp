#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <string>

using namespace geode::prelude;

// VISION X source foundation: bounded input-buffer settings + in-level control panel.
// Maximum supported tuning is deliberately capped at 60 ticks.
namespace vx {
    constexpr int MAX_BUFFER_TICKS = 60;
    constexpr int MAX_CLICK_TICKS = 30;

    enum class Tab { Log, Stages, Modes, Tools };

    struct Settings {
        bool enabled = true;
        int stage = 1;
        int minTicks = 2;
        int maxTicks = 2;
        int clickMin = 0;
        int clickMax = 0;
        int releaseMin = 0;
        int releaseMax = 0;
        int mode = 0;
    };

    static Settings g_settings;
    static PlayLayer* g_layer = nullptr;
    static CCNode* g_panel = nullptr;
    static CCNode* g_content = nullptr;
    static CCLabelBMFont* g_status = nullptr;
    static Tab g_tab = Tab::Stages;
    static std::chrono::steady_clock::time_point g_lastLog{};
    static int g_lastLogCode = -1;

    static void logThrottled(int code, std::string const& message) {
        auto now = std::chrono::steady_clock::now();
        if (code == g_lastLogCode &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastLog).count() < 250)
            return;
        g_lastLog = now;
        g_lastLogCode = code;
        log::info("[VISION X] {}", message);
    }

    static void setStatus(std::string const& s) {
        if (g_status) g_status->setString(s.c_str());
    }

    static CCMenuItemSpriteExtra* makeButton(
        std::string const& title, SEL_MenuHandler callback, CCNode* target,
        float scale = 0.55f
    ) {
        auto sprite = ButtonSprite::create(title.c_str(), 90, true, "goldFont.fnt", "GJ_button_01.png", 25.f, scale);
        return CCMenuItemSpriteExtra::create(sprite, target, callback);
    }

    class Panel : public CCLayer {
    public:
        static Panel* create() {
            auto ret = new Panel();
            if (ret->init()) { ret->autorelease(); return ret; }
            delete ret;
            return nullptr;
        }

        bool init() override {
            if (!CCLayer::init()) return false;
            auto win = CCDirector::sharedDirector()->getWinSize();

            auto shade = CCLayerColor::create({12, 8, 24, 225});
            shade->setContentSize({330.f, 250.f});
            shade->setPosition({win.width - 348.f, win.height - 270.f});
            shade->setID("visionx-panel-bg");
            addChild(shade);

            auto title = CCLabelBMFont::create("VISION X", "bigFont.fnt");
            title->setScale(0.55f);
            title->setColor({255, 150, 220});
            title->setAnchorPoint({0.f, 0.5f});
            title->setPosition({win.width - 338.f, win.height - 35.f});
            addChild(title);

            auto close = CCMenu::create();
            close->setPosition({win.width - 35.f, win.height - 35.f});
            auto x = CCMenuItemLabel::create(CCLabelBMFont::create("X", "bigFont.fnt"), this, menu_selector(Panel::onClose));
            close->addChild(x);
            addChild(close);

            auto tabs = CCMenu::create();
            tabs->setPosition({win.width - 183.f, win.height - 62.f});
            const char* names[] = {"Log", "Stages", "Modes", "Tools"};
            for (int i = 0; i < 4; ++i) {
                auto item = CCMenuItemLabel::create(CCLabelBMFont::create(names[i], "bigFont.fnt"), this,
                    i == 0 ? menu_selector(Panel::onLog) :
                    i == 1 ? menu_selector(Panel::onStages) :
                    i == 2 ? menu_selector(Panel::onModes) : menu_selector(Panel::onTools));
                item->setScale(0.38f);
                item->setPosition({-105.f + i * 70.f, 0.f});
                tabs->addChild(item);
            }
            addChild(tabs);

            g_content = CCNode::create();
            g_content->setPosition({win.width - 338.f, win.height - 88.f});
            addChild(g_content);
            drawTab();
            return true;
        }

        void onClose(CCObject*) { removeFromParentAndCleanup(true); g_panel = nullptr; }

        void onLog(CCObject*) { g_tab = Tab::Log; drawTab(); }
        void onStages(CCObject*) { g_tab = Tab::Stages; drawTab(); }
        void onModes(CCObject*) { g_tab = Tab::Modes; drawTab(); }
        void onTools(CCObject*) { g_tab = Tab::Tools; drawTab(); }

        void drawTab() {
            if (!g_content) return;
            g_content->removeAllChildrenWithCleanup(true);
            auto addLine = [&](std::string const& text, float y, ccColor3B color = {235, 225, 245}) {
                auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
                label->setAnchorPoint({0.f, 0.5f});
                label->setScale(0.38f);
                label->setColor(color);
                label->setPosition({0.f, y});
                g_content->addChild(label);
            };
            auto addAction = [&](std::string const& text, float x, float y, SEL_MenuHandler cb) {
                auto menu = CCMenu::create();
                menu->setPosition({x, y});
                auto item = makeButton(text, cb, this, 0.42f);
                menu->addChild(item);
                g_content->addChild(menu);
            };

            if (g_tab == Tab::Stages) {
                addLine("STAGE SELECT", 0.f, {255, 150, 220});
                const char* stages[] = {"Off", "1", "2", "3", "Extreme", "Custom"};
                for (int i = 0; i < 6; ++i) {
                    auto menu = CCMenu::create();
                    menu->setPosition({24.f + (i % 3) * 94.f, -28.f - (i / 3) * 31.f});
                    auto item = makeButton(stages[i], this->stageCallbacks()[i], this, 0.36f);
                    menu->addChild(item);
                    g_content->addChild(menu);
                }
                addLine("TICK TUNING  |  MAX 60", -100.f, {255, 150, 220});
                addLine(fmt::format("Minimum: {} ticks", g_settings.minTicks), -126.f);
                addLine(fmt::format("Maximum: {} ticks", g_settings.maxTicks), -150.f);
                addAction("MIN -", 45.f, -177.f, menu_selector(Panel::minDown));
                addAction("MIN +", 115.f, -177.f, menu_selector(Panel::minUp));
                addAction("MAX -", 205.f, -177.f, menu_selector(Panel::maxDown));
                addAction("MAX +", 275.f, -177.f, menu_selector(Panel::maxUp));
                addLine("Click tuning: 0-30 | Release tuning: 0-60", -211.f);
            } else if (g_tab == Tab::Log) {
                addLine("LIVE LOG", 0.f, {255, 150, 220});
                addLine(g_settings.enabled ? "Buffer active" : "Buffer disabled", -35.f);
                addLine(fmt::format("Stage {} | {}-{} ticks", g_settings.stage, g_settings.minTicks, g_settings.maxTicks), -62.f);
                addLine("Spam logging is throttled to 250 ms.", -90.f);
            } else if (g_tab == Tab::Modes) {
                addLine("GAMEMODE TUNING", 0.f, {255, 150, 220});
                addLine("Cube / Ship / Ball / UFO", -35.f);
                addLine("Wave / Robot / Spider / Swing", -65.f);
                addLine(fmt::format("Selected mode slot: {}", g_settings.mode + 1), -100.f);
                addAction("PREVIOUS", 85.f, -145.f, menu_selector(Panel::modeDown));
                addAction("NEXT", 220.f, -145.f, menu_selector(Panel::modeUp));
            } else {
                addLine("TOOLS", 0.f, {255, 150, 220});
                addLine("Click maximum: 30 ticks", -35.f);
                addLine("Release maximum: 60 ticks", -65.f);
                addAction("TOGGLE BUFFER", 150.f, -110.f, menu_selector(Panel::toggle));
            }
        }

        std::array<SEL_MenuHandler, 6> stageCallbacks() {
            return {menu_selector(Panel::stageOff), menu_selector(Panel::stage1),
                    menu_selector(Panel::stage2), menu_selector(Panel::stage3),
                    menu_selector(Panel::stageExtreme), menu_selector(Panel::stageCustom)};
        }

        void setStage(int stage, int ticks) {
            g_settings.stage = stage;
            g_settings.enabled = stage != 0;
            g_settings.minTicks = std::clamp(ticks, 0, MAX_BUFFER_TICKS);
            g_settings.maxTicks = std::clamp(ticks, 0, MAX_BUFFER_TICKS);
            drawTab();
        }
        void stageOff(CCObject*) { setStage(0, 0); }
        void stage1(CCObject*) { setStage(1, 2); }
        void stage2(CCObject*) { setStage(2, 8); }
        void stage3(CCObject*) { setStage(3, 24); }
        void stageExtreme(CCObject*) { setStage(4, 48); }
        void stageCustom(CCObject*) { g_settings.stage = 5; drawTab(); }
        void minDown(CCObject*) { g_settings.minTicks = std::max(0, g_settings.minTicks - 1); drawTab(); }
        void minUp(CCObject*) { g_settings.minTicks = std::min(g_settings.maxTicks, g_settings.minTicks + 1); drawTab(); }
        void maxDown(CCObject*) { g_settings.maxTicks = std::max(g_settings.minTicks, g_settings.maxTicks - 1); drawTab(); }
        void maxUp(CCObject*) { g_settings.maxTicks = std::min(MAX_BUFFER_TICKS, g_settings.maxTicks + 1); drawTab(); }
        void modeDown(CCObject*) { g_settings.mode = (g_settings.mode + 7) % 8; drawTab(); }
        void modeUp(CCObject*) { g_settings.mode = (g_settings.mode + 1) % 8; drawTab(); }
        void toggle(CCObject*) { g_settings.enabled = !g_settings.enabled; drawTab(); }
    };

    static void openPanel() {
        if (!g_layer || g_panel) return;
        auto panel = Panel::create();
        if (!panel) return;
        g_panel = panel;
        g_layer->addChild(panel, 10000);
    }
}

class $modify(VisionXPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        vx::g_layer = this;
        auto menu = CCMenu::create();
        menu->setPosition({25.f, CCDirector::sharedDirector()->getWinSize().height - 28.f});
        auto button = CCMenuItemLabel::create(CCLabelBMFont::create("VX", "bigFont.fnt"), this,
            menu_selector(VisionXPlayLayer::onVisionX));
        button->setColor({255, 145, 215});
        button->setScale(0.55f);
        menu->addChild(button);
        this->addChild(menu, 9999);
        return true;
    }

    void onVisionX(CCObject*) { vx::openPanel(); }

    void onQuit() {
        vx::g_panel = nullptr;
        vx::g_layer = nullptr;
        PlayLayer::onQuit();
    }
};

// Keep the input hook lightweight: settings are bounded and repeated rapid calls
// do not trigger synchronous console output or unbounded scans.
class $modify(VisionXInputLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (vx::g_settings.enabled && button == 1) {
            auto* pl = PlayLayer::get();
            if (pl && pl->m_player1 && pl->m_player1->m_isDart) {
                static int rapid = 0;
                static auto last = std::chrono::steady_clock::time_point{};
                auto now = std::chrono::steady_clock::now();
                auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
                rapid = gap < 34 ? rapid + 1 : 0;
                last = now;
                if (rapid >= 3) vx::logThrottled(3, "rapid wave input detected; bypassing extra scan");
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};
