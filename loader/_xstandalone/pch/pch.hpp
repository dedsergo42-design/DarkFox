#pragma once

// Урезанный PCH для standalone-сборки xdraw/xui вне DLL-проекта.
// Полный pch/pch.hpp тянет phnt/zydis/poly2d/bc7/xorstr — для лоадера
// это лишние мегабайты и лишние зависимости.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <cmath>
#include <numbers>
#include <chrono>
#include <array>
#include <span>
#include <vector>
#include <deque>
#include <memory>
#include <algorithm>
#include <numeric>
#include <ranges>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <optional>
#include <variant>
#include <format>
