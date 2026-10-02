#pragma once

#include <xdraw.hpp>
#include <functional>

namespace hell {

void sidebar_category(const char* title, float width);
bool sidebar_item(const char* label, bool selected, float width, float height = 24.f);
bool sidebar_nav(const char* icon, const char* label, bool selected, float width, float height = 30.f, int nav_index = -1);
bool sidebar_icon(const char* icon, const char* id, bool selected, float width, float height = 44.f);
bool sidebar_gs_tab(const char* label, bool selected, float width, float height = 26.f);
bool subnav_tab(const char* label, bool selected, float height = 24.f);
void section_header(const char* title, bool compact = false);
void section_header_centered(const char* title);
void sidebar_separator(float width);
bool toolbar_button(const char* icon, const char* label, float width, float height = 28.f);
void weapon_toolbar(const char* left_label, std::function<void()> content, bool active_indicator);

} // namespace hell