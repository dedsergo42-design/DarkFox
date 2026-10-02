#pragma once

#include <cctype>

#include <menu/hell_gui/colors.h>

namespace hell::menu_ui {

inline void play_click() {
    // Play menu click sound
}

inline void play_hover() {
    // Play menu hover sound
}

inline bool str_icontains(const char* haystack, const char* needle) {
    if (!needle || !needle[0])
        return true;
    if (!haystack)
        return false;

    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };

    for (const char* h = haystack; *h; ++h) {
        const char* a = h;
        const char* b = needle;
        while (*a && *b && lower(static_cast<unsigned char>(*a)) == lower(static_cast<unsigned char>(*b))) {
            ++a;
            ++b;
        }
        if (!*b)
            return true;
    }
    return false;
}

inline bool is_searching() {
    return false; // Disable search for now
}

inline bool search_matches(const char* label) {
    if (!is_searching())
        return true;
    return str_icontains(label, "");
}

inline void item_tooltip(const char* text) {
    if (!text || !text[0])
        return;
    // Tooltip implementation
}

inline void accent_glow_rect(xdraw::draw_list* dl, xdraw::rect bb, xdraw::color accent, float strength,
                             float rounding = 6.f) {
    if (!dl || strength <= 0.01f)
        return;

    const int a = static_cast<int>(std::clamp(strength, 0.f, 1.f) * 90.f);
    const xdraw::color glow{
        static_cast<std::uint8_t>(accent.r * strength),
        static_cast<std::uint8_t>(accent.g * strength),
        static_cast<std::uint8_t>(accent.b * strength),
        static_cast<std::uint8_t>(a)
    };
    dl->rect_filled({bb.min.x - 1.f, bb.min.y - 1.f}, {bb.max.x + 1.f, bb.max.y + 1.f},
        glow, xdraw::corner_radius{rounding + 1.f});
}

} // namespace hell::menu_ui