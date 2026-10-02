#include <pch/pch.hpp>
#include <core/settings.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include "../../rendering.hpp"

#include "menu.widgets.hpp"

namespace rendering {

	namespace widgets {

		// ═══════════════════════════════════════════════════════════
		// CHECKBOX
		// ═══════════════════════════════════════════════════════════
		bool checkbox (const char* label, bool& value, float label_spacing) {
			const auto label_w = xdraw::measure_text (label).first;
			const auto checkbox_size = 16.0f;
			const auto rounding = 3.0f;
			const auto total_w = label_w + label_spacing + checkbox_size;
			const auto h = 20.0f;

			const auto row = xui::layout::item (total_w, h);
			const auto hover = xui::ctx ().input.in_rect (row);

			auto& dl = xui::draw::current ();
			const auto anim = xui::anim::lerp (xui::fnv1a ("chk") + (uintptr_t) label, hover ? 1.0f : 0.0f, 14.0f);

			// Checkbox background
			auto bg = tokens::col_card;
			bg.a = static_cast<uint8_t>(180.0f + 60.0f * anim);
			dl.rect_filled (row.x, row.y + (h - checkbox_size) * 0.5f, checkbox_size, checkbox_size, bg, {rounding});

			// Active state - green accent
			if (value) {
				auto active_bg = tokens::col_accent_active;
				active_bg.a = static_cast<uint8_t>(255.0f * (0.5f + 0.5f * anim));
				dl.rect_filled (row.x + 1, row.y + (h - checkbox_size) * 0.5f + 1, checkbox_size - 2, checkbox_size - 2, active_bg, {rounding - 1});
			}

			// Label
			const auto label_x = row.x + checkbox_size + label_spacing;
			const auto label_col = xui::lerp (tokens::col_text_dim, tokens::col_text, anim * 0.5f);
			dl.text (label_x, row.y + (h - xdraw::measure_text (label).second) * 0.5f, label, label_col);

			// Click interaction
			if (hover && xui::ctx ().input.mouse_clicked && !xui::ctx ().overlay_blocking ()) {
				value = !value;
			}

			return value;
		}

		bool checkbox (const char* label, xui::setting& value, float label_spacing) {
			return checkbox (label, value.value, label_spacing);
		}

		// ═══════════════════════════════════════════════════════════
		// SLIDER FLOAT
		// ═══════════════════════════════════════════════════════════
		bool slider_float (const char* label, float& value, float min, float max, const char* format, float width) {
			const auto label_w = xdraw::measure_text (label).first;
			const auto pill_w = width;
			const auto slider_w = 120.0f;
			const auto total_w = label_w + 12.0f + slider_w + 10.0f + pill_w;
			const auto h = 24.0f;

			const auto row = xui::layout::item (total_w, h);
			const auto hover = xui::ctx ().input.in_rect (row);

			auto& dl = xui::draw::current ();
			const auto anim = xui::anim::lerp (xui::fnv1a ("slid") + (uintptr_t) label, hover ? 1.0f : 0.0f, 14.0f);

			const auto track_y = row.y + (h - 2) * 0.5f;
			const auto track_x = row.x + label_w + 12.0f;

			// Track background
			dl.rect_filled (track_x, track_y, slider_w, 2, tokens::col_card, {1.0f});

			// Track fill (accent color)
			const auto t = (value - min) / (max - min);
			const auto fill_w = slider_w * t;
			auto fill_col = tokens::col_accent;
			fill_col.a = static_cast<uint8_t>(255.0f * (0.5f + 0.5f * anim));
			dl.rect_filled (track_x, track_y, fill_w, 2, fill_col, {1.0f});

			// Thumb
			const auto thumb_x = track_x + fill_w;
			auto thumb_col = tokens::col_accent;
			thumb_col.a = static_cast<uint8_t>(255.0f * (0.5f + 0.5f * anim));
			dl.circle_filled (thumb_x, row.y + h * 0.5f, 7.0f, thumb_col);

			// Label
			const auto label_col = xui::lerp (tokens::col_text_dim, tokens::col_text, anim * 0.5f);
			dl.text (row.x, row.y + (h - xdraw::measure_text (label).second) * 0.5f, label, label_col);

			// Value pill
			char value_str [32];
			snprintf (value_str, sizeof (value_str), format, value);
			const auto pill_x = track_x + slider_w + 10.0f;
			auto pill_bg = tokens::col_dropdown;
			pill_bg.a = static_cast<uint8_t>(200.0f + 55.0f * anim);
			dl.rect_filled (pill_x, row.y + (h - 18) * 0.5f, pill_w, 18, pill_bg, {9.0f});

			const auto val_w = xdraw::measure_text (value_str).first;
			const auto val_col = tokens::col_accent;
			dl.text (pill_x + pill_w * 0.5f - val_w * 0.5f, row.y + (h - xdraw::measure_text (value_str).second) * 0.5f, value_str, val_col);

			// Interaction
			if (hover && xui::ctx ().input.mouse_down && !xui::ctx ().overlay_blocking ()) {
				const auto mouse_t = std::clamp ((xui::ctx ().input.mouse_x - track_x) / slider_w, 0.0f, 1.0f);
				value = min + mouse_t * (max - min);
			}

			return hover && xui::ctx ().input.mouse_clicked && !xui::ctx ().overlay_blocking ();
		}

		bool slider_int (const char* label, int& value, int min, int max, const char* format, float width) {
			float fvalue = static_cast<float>(value);
			const bool changed = slider_float (label, fvalue, static_cast<float>(min), static_cast<float>(max), format, width);
			if (changed) {
				value = static_cast<int>(fvalue);
			}
			return changed;
		}

		// ═══════════════════════════════════════════════════════════
		// DROPDOWN (template)
		// ═══════════════════════════════════════════════════════════
		template<typename T>
		bool dropdown (const char* label, T& value, const char* const* items, int count, float width) {
			const auto label_w = xdraw::measure_text (label).first;
			const auto total_w = label_w + 12.0f + width;
			const auto h = 24.0f;

			const auto row = xui::layout::item (total_w, h);
			const auto hover = xui::ctx ().input.in_rect (row);

			auto& dl = xui::draw::current ();
			const auto anim = xui::anim::lerp (xui::fnv1a ("dd") + (uintptr_t) label, hover ? 1.0f : 0.0f, 14.0f);

			// Label
			const auto label_col = xui::lerp (tokens::col_text_dim, tokens::col_text, anim * 0.5f);
			dl.text (row.x, row.y + (h - xdraw::measure_text (label).second) * 0.5f, label, label_col);

			// Dropdown pill
			const auto pill_x = row.x + label_w + 12.0f;
			auto pill_bg = tokens::col_dropdown;
			pill_bg.a = static_cast<uint8_t>(200.0f + 55.0f * anim);
			dl.rect_filled (pill_x, row.y + (h - 18) * 0.5f, width, 18, pill_bg, {9.0f});

			// Selected value
			const auto idx = static_cast<int>(value);
			const char* selected = (idx >= 0 && idx < count) ? items [idx] : "???";
			const auto val_w = xdraw::measure_text (selected).first;
			const auto val_col = tokens::col_text;
			dl.text (pill_x + width * 0.5f - val_w * 0.5f, row.y + (h - xdraw::measure_text (selected).second) * 0.5f, selected, val_col);

			// Interaction (simple cycle)
			if (hover && xui::ctx ().input.mouse_clicked && !xui::ctx ().overlay_blocking ()) {
				value = static_cast<T> ((idx + 1) % count);
			}

			return hover && xui::ctx ().input.mouse_clicked && !xui::ctx ().overlay_blocking ();
		}

		// Explicit template instantiations
		template bool dropdown<int> (const char*, int&, const char* const*, int, float);
		template bool dropdown<float> (const char*, float&, const char* const*, int, float);

		// ═══════════════════════════════════════════════════════════
		// BUTTON
		// ═══════════════════════════════════════════════════════════
		bool button (const char* label, float w, float h) {
			const auto row = xui::layout::item (w, h);
			const auto hover = xui::ctx ().input.in_rect (row);

			auto& dl = xui::draw::current ();
			const auto anim = xui::anim::lerp (xui::fnv1a ("btn") + (uintptr_t) label, hover ? 1.0f : 0.0f, 14.0f);

			// Button background
			auto bg = tokens::col_card;
			bg.a = static_cast<uint8_t>(180.0f + 60.0f * anim);
			dl.rect_filled (row.x, row.y, w, h, bg, {tokens::btn_rounding});

			// Text
			const auto text_col = tokens::col_accent;
			const auto [tw, th] = xdraw::measure_text (label);
			dl.text (row.x + w * 0.5f - tw * 0.5f, row.y + (h - th) * 0.5f, label, text_col);

			if (hover && xui::ctx ().input.mouse_clicked && !xui::ctx ().overlay_blocking ()) {
				return true;
			}
			return false;
		}

		// ═══════════════════════════════════════════════════════════
		// SEPARATOR WITH LABEL
		// ═══════════════════════════════════════════════════════════
		void separator_with_label (const char* label) {
			const auto h = 24.0f;
			const auto row = xui::layout::item (0, h);
			const auto [avail_w, _] = xui::layout::avail ();

			auto& dl = xui::draw::current ();

			// Text
			const auto label_w = xdraw::measure_text (label).first;
			const auto label_x = row.x + (avail_w - label_w) * 0.5f;
			const auto label_col = tokens::col_text_dim;
			dl.text (label_x, row.y + (h - xdraw::measure_text (label).second) * 0.5f, label, label_col);

			// Lines
			const auto line_y = row.y + h * 0.5f;
			const auto line_color = tokens::col_border;
			dl.line (row.x, line_y, label_x - 10.0f, line_y, line_color, 1.0f);
			dl.line (label_x + label_w + 10.0f, line_y, row.x + avail_w, line_y, line_color, 1.0f);
		}

		// ═══════════════════════════════════════════════════════════
		// BEGIN CARD / END CARD (уже было)
		// ═══════════════════════════════════════════════════════════

		// Хранилище состояний карточек (open/closed) по имени
		static std::unordered_map<std::string, bool>& get_card_states () {
			static std::unordered_map<std::string, bool> states;
			return states;
		}

		// Стэк открытых карточек (для end_card)
		static std::vector<bool>& get_card_stack () {
			static std::vector<bool> stack;
			return stack;
		}

		bool begin_card (const char* title, float width, float height, bool default_open) {
			auto& states = get_card_states ();
			auto it = states.find (title);
			if (it == states.end ()) {
				states [title] = default_open;
				it = states.find (title);
			}
			bool& open = it->second;

			const auto header_h = 32.0f;
			const auto row = xui::layout::item (width, header_h);
			const auto hover = xui::ctx ().input.in_rect (row);

			auto& dl = xui::draw::current ();

			// Фон заголовка карточки
			auto bg = tokens::col_card;
			dl.rect_filled (row.x, row.y, width, header_h, bg,
				xdraw::corner_radius {tokens::card_rounding});

			// Название карточки
			const auto th = xdraw::measure_text (title).second;
			dl.text (row.x + 14.0f, row.y + (header_h - th) * 0.5f,
				title, tokens::col_text);

			// Accent line under title
			dl.rect_filled( row.x + 14.0f, row.y + header_h - 7.0f, 34.0f, 1.5f,
				tokens::col_accent.alpha( 100 ), xdraw::corner_radius{ 0.75f } );

			// Стрелка ▲ / ▼ справа
			{
				const auto arrow_size = 6.0f;
				const auto arrow_cx = row.x + width - 16.0f;
				const auto arrow_cy = row.y + header_h * 0.5f;
				auto arrow_col = tokens::col_text_dim;

				if (open) {
					dl.line (arrow_cx - arrow_size, arrow_cy + arrow_size * 0.4f,
						arrow_cx, arrow_cy - arrow_size * 0.4f, arrow_col, 1.5f);
					dl.line (arrow_cx, arrow_cy - arrow_size * 0.4f,
						arrow_cx + arrow_size, arrow_cy + arrow_size * 0.4f, arrow_col, 1.5f);
				} else {
					dl.line (arrow_cx - arrow_size, arrow_cy - arrow_size * 0.4f,
						arrow_cx, arrow_cy + arrow_size * 0.4f, arrow_col, 1.5f);
					dl.line (arrow_cx, arrow_cy + arrow_size * 0.4f,
						arrow_cx + arrow_size, arrow_cy - arrow_size * 0.4f, arrow_col, 1.5f);
				}
			}

			// Клик по заголовку — свернуть/развернуть
			if (hover && xui::ctx ().input.mouse_clicked && !xui::ctx ().overlay_blocking ()) {
				open = !open;
			}

			get_card_stack ().push_back (open);

			if (!open) return false;

			// Открываем child под заголовком
			char child_id [128];
			snprintf (child_id, sizeof (child_id), "##card_%s", title);

			const float body_h = (height > 0) ? height : 0.0f;
			return xui::begin_child (child_id, width, body_h);
		}

		void end_card () {
			auto& stack = get_card_stack ();
			if (stack.empty ()) return;

			bool was_open = stack.back ();
			stack.pop_back ();

			if (was_open) {
				xui::end_child ();
			}
		}

	} // namespace widgets

} // namespace rendering