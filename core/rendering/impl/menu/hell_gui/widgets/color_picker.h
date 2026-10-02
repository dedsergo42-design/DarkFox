#pragma once

#include <xdraw.hpp>
#include <string>

namespace hell {

bool inline_color_swatch(const char* id, xdraw::color& color, float x, float y, float size);
void color_row(const char* label, std::vector<xdraw::color*> colors, bool alpha_bar = false);
bool color_picker(const char* id, xdraw::color& color, int pickers_inline = 1);

} // namespace hell