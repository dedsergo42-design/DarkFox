#pragma once

#include <string>
#include <array>
#include <functional>

#include <external/xdraw/xdraw.hpp>
#include <external/xdraw/xui/xui.hpp>

#include "../../rendering.hpp"

namespace rendering {

	namespace widgets {

		// UI Components with new design

		// Checkbox - square 16x16, rounded 3-4px, no checkmark
		// Active: filled with accent color, no checkmark
		bool checkbox(const char* label, bool& value, float label_spacing = 8.0f);
		bool checkbox(const char* label, xui::setting& value, float label_spacing = 8.0f);

		// Slider - thin track 2px, filled with accent, round thumb 14px
		// Value shown in pill on right
		bool slider_float(const char* label, float& value, float min, float max, const char* format = "%.3f", float width = 120.0f);
		bool slider_int(const char* label, int& value, int min, int max, const char* format = "%d", float width = 120.0f);

		// Dropdown - dark pill on right side
		template<typename T>
		bool dropdown(const char* label, T& value, const char* const* items, int count, float width = 120.0f);

		// Input field - dark rectangle, rounded 4px, white text
		bool text_input(const char* label, char* buf, size_t buf_size, const char* placeholder = "", float width = 200.0f);
		bool text_input(const char* label, std::string& value, const char* placeholder = "", float width = 200.0f);

		// Button - flat text with accent color
		bool button(const char* label, float w = 80.0f, float h = 24.0f);

		// Settings gear icon (orange) for expandable settings
		bool settings_gear(const char* label, bool& expanded, float spacing = 8.0f);

		// Color picker icon (magenta) for color settings
		bool color_picker_button(const char* label, xdraw::color& color, float spacing = 8.0f);

		// Card with accordion - collapsible section
		bool card_begin(const char* label, bool& expanded, float width = 300.0f, float title_spacing = 12.0f);
		bool card_begin(const char* label, xui::setting& expanded, float width = 300.0f, float title_spacing = 12.0f);
		void card_end();

		// Category card - for sidebar categories
		bool category_card(const char* label, bool& expanded, int subtab_count = 0);

		// Separator with text label
		void separator_with_label(const char* label);

		// Card with title and collapse arrow - Nonagon style
		// begin_card(title, width, height) returns true if expanded
		bool begin_card(const char* title, float width, float height = 0.0f, bool default_open = true);
		void end_card();

	} // namespace widgets

} // namespace rendering
