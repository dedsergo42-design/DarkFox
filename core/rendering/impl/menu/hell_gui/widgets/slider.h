#pragma once

#include <xdraw.hpp>
#include <string>

namespace hell {

bool slider_int(const char* label, int& value, int min, int max);
bool slider_float(const char* label, float& value, float min, float max);
bool slider_int(const char* label, int& value, int min, int max, const char* format);
bool slider_float(const char* label, float& value, float min, float max, const char* format);
bool slider_none(const char* label, int& value, int min, int max, const char* format = "%d");
bool min_dmg_slider(const char* label, int& value, int min, int max, const char* format = "%d");

} // namespace hell