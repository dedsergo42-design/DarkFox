#pragma once

#include <xdraw.hpp>
#include <vector>
#include <string>

namespace hell {

bool begin_combo(const char* label, const char* current_value, bool draw_label = true);
void end_combo();
bool selectable(const char* label, bool selected);
bool combo(const char* label, int& selected_item, const char* items[], int count, bool draw_label = true);
bool multi_combo(const char* label, const char* items[], std::vector<bool>& values, int size, bool draw_label = true);

} // namespace hell