#pragma once

#include <xdraw.hpp>
#include <functional>
#include <string>

namespace hell {

bool checkbox(const char* label, bool& value, std::function<void()> content = nullptr, const char* tooltip = nullptr);
bool checkbox(const char* label, bool* value_ptr, std::function<void()> content = nullptr, const char* tooltip = nullptr);
float calc_checkbox_size();

} // namespace hell