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
