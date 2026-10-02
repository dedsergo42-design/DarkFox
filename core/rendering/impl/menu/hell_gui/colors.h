#pragma once

#include <xdraw.hpp>

namespace hell {

namespace colors {

// Text colors
inline xdraw::color m_text_unhovered{ 255, 255, 255, 130 };
inline xdraw::color m_text_hovered{ 255, 255, 255, 210 };
inline xdraw::color m_text_selected{ 255, 255, 255, 255 };

// Background colors
inline xdraw::color m_bg_window{ 13, 13, 16, 165 };
inline xdraw::color m_bg_topbar{ 18, 18, 22, 175 };
inline xdraw::color m_bg_subbar{ 16, 16, 20, 170 };
inline xdraw::color m_bg_sidebar{ 16, 16, 20, 175 };
inline xdraw::color m_bg_content{ 13, 13, 16, 155 };
inline xdraw::color m_bg_panel{ 22, 22, 28, 185 };
inline xdraw::color m_bg_panel_header{ 26, 26, 34, 195 };
inline xdraw::color m_bg_checkbox_off{ 42, 42, 50, 200 };
inline xdraw::color m_bg_toggle_off{ 42, 42, 50, 200 };
inline xdraw::color m_border_outer{ 8, 8, 10, 120 };
inline xdraw::color m_border{ 48, 48, 58, 140 };
inline xdraw::color m_border_light{ 58, 58, 68, 160 };
inline xdraw::color m_separator{ 40, 40, 48, 120 };

// Additional text colors
inline xdraw::color m_text_muted{ 110, 110, 120, 255 };
inline xdraw::color m_text_normal{ 195, 195, 205, 255 };
inline xdraw::color m_text_bright{ 255, 255, 255, 255 };
inline xdraw::color m_text_dim{ 75, 75, 85, 220 };

// Navigation colors
inline xdraw::color m_nav_inactive{ 105, 105, 115, 255 };
inline xdraw::color m_nav_hovered{ 210, 210, 218, 255 };

// Accent colors
inline xdraw::color g_menu_accent_lerp{ 71, 133, 255, 255 };
inline bool g_menu_accent_active = false;

inline xdraw::color accent() {
    if (g_menu_accent_active)
        return g_menu_accent_lerp;
    return tokens::col_accent;
}

inline xdraw::color accent_dim() {
    auto a = accent();
    return xdraw::color{
        static_cast<std::uint8_t>(a.r * 0.50f),
        static_cast<std::uint8_t>(a.g * 0.50f),
        static_cast<std::uint8_t>(a.b * 0.45f),
        255
    };
}

inline xdraw::color nav_selected_bg() {
    return xdraw::color{ 38, 38, 46, 190 };
}

} // namespace colors

} // namespace hell