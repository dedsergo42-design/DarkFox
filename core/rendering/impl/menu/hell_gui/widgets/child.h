#pragma once

#include <xdraw.hpp>
#include <functional>

namespace hell {

bool child(const char* label, float width, float height, std::function<void()> content);
void child_begin(const char* label, float width, float height);
void child_end();

} // namespace hell