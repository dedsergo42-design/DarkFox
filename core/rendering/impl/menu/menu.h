#pragma once

#include <Windows.h>
#include <xdraw.hpp>
#include <functional>
#include <string>
#include <vector>
#include <array>
#include <chrono>

namespace rendering {

class menu {
public:
    enum class tab : int {
        aimbot = 0,
        visuals,
        misc,
        config,
        lua,
        skins,
        count
    };

    enum class aimbot_subtab : int {
        aimbot = 0,
        antiaim,
        legitbot,
        autofire
    };

    enum class visuals_subtab : int {
        enemy = 0,
        team,
        world,
        other,
        local,
        hud
    };

    enum class skins_subtab : int {
        guns = 0,
        knives,
        gloves,
        agents
    };

    // Menu state
    tab m_current_tab = tab::aimbot;
    int m_current_subtab = 0;
    
    // Subtab state for different tabs
    int m_aimbot_subtab = 0;
    int m_visuals_subtab = 0;
    int m_skins_subtab = 0;

    // Sidebar state
    bool m_sidebar_hovered = false;
    bool m_sidebar_compact = false;
    float m_sidebar_w_anim = 0.f;

    // Menu state
    bool m_init = false;
    bool m_fonts_ready = false;
    int m_font_init_delay = 0;
    bool m_menu_open = false;
    bool m_input_active = false;
    bool m_saved_relative_mouse = false;
    bool m_prev_menu_open = false;
    float m_open_anim = 0.f;
    bool m_menu_dragging = false;
    bool m_menu_resizing = false;
    char m_search_query[128] = {};
    char m_username[32] = "User";

    // Position and size
    float m_x = 100.0f;
    float m_y = 100.0f;
    float m_w = 900.0f;
    float m_h = 520.0f;

    // Methods
    void init();
    void ensure_fonts();
    void render();
    void update_input_state();
    void on_menu_toggle();
    void release_gpu_resources();

private:
    // Drawing methods
    void draw_topbar();
    void draw_categories_column(float h);
    void draw_content_area(float h);
    void draw_footer();
    void handle_drag();

    // Sub-renderers for each tab
    void render_aimbot();
    void render_visuals();
    void render_misc();
    void render_config();
    void render_lua();
    void render_skins();

    // Subtab renderers
    void render_aimbot_main();
    void render_aimbot_right();
    void render_visuals_main();
    void render_visuals_right();
    void render_misc_main();
    void render_misc_right();
    void render_config_main();
    void render_config_right();
    void render_lua_main();
    void render_lua_right();
    void render_skins_main();
    void render_skins_right();

    // Intro rendering
    bool draw_intro();
    void draw_search_results( float x, float y, float w, float h );

private:
    // Intro state
    bool m_intro_finished = false;
    bool m_intro_base_graphics_ready = false;
    float m_intro_elapsed = 0.0f;
    float m_intro_bar_phase = 0.0f;
    float m_intro_assets_ready_at = -1.0f;

    // Audio intro state
    rendering::intro::audio_player m_intro_audio;
    bool m_intro_audio_started = false;
    float m_intro_hold_time = 9.0f;
    HMODULE m_dll_module = nullptr;
};

inline auto g_menu = std::make_unique<menu>();

} // namespace rendering