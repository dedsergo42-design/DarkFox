#include <pch/pch.hpp>
#include <utilities/math/math.hpp>
#include <core/settings.hpp>

#include "../../rendering.hpp"
#include "menu.widgets.hpp"
#include "theme.hpp"
#include "ui.hpp"

namespace rendering {

	namespace detail {

		std::string search_buf {};
		std::vector<std::wstring> config_list {};
		auto selected {-1};
		auto needs_refresh {true};
		auto confirm_delete {false};
		auto confirm_reset {false};
		auto confirm_timer {0.0f};

		// Where the "save default" button writes. Same folder as the normal
		// configs so it is one click away from "open folder"; the extension is
		// .json because the file is meant to be opened in an editor, not
		// double-clicked.
		inline constexpr wchar_t k_default_profile_filename [] {L"DarkFox-default-profile.json"};

		static inline std::wstring default_profile_path () {
			const auto dir = config::files::get_configs_directory ();
			if (dir.empty ()) {
				return {};
			}

			return dir + L"\\" + k_default_profile_filename;
		}

		static inline void wide_to_utf8 (const std::wstring& wide, char* out, int out_size) {
			WideCharToMultiByte (CP_UTF8, 0, wide.c_str (), -1, out, out_size, nullptr, nullptr);
		}

		static inline std::wstring utf8_to_wide (const std::string& utf8) {
			wchar_t buf [128] {};
			MultiByteToWideChar (CP_UTF8, 0, utf8.c_str (), -1, buf, 128);
			return buf;
		}

		static inline bool config_matches_search (const std::wstring& wname) {
			if (detail::search_buf.empty ()) {
				return true;
			}

			char narrow [128] {};
			wide_to_utf8 (wname, narrow, sizeof (narrow));

			std::string lower_name {narrow};
			std::string lower_search {detail::search_buf};

			for (auto& c : lower_name) {
				c = static_cast<char>(std::tolower (c));
			}

			for (auto& c : lower_search) {
				c = static_cast<char>(std::tolower (c));
			}

			return lower_name.find (lower_search) != std::string::npos;
		}

		static inline std::string selected_name () {
			if (detail::selected < 0 || detail::selected >= static_cast<int> (detail::config_list.size ())) {
				return {};
			}

			char narrow [128] {};
			wide_to_utf8 (detail::config_list [detail::selected], narrow, sizeof (narrow));
			return narrow;
		}

		static inline void reset_defaults () {
			auto& reg = config::detail::get_registry ();

			for (auto& f : reg.fields) {
				char key_str [12];
				std::snprintf (key_str, sizeof (key_str), "%08x", f.key);

				auto def = reg.defaults.find (key_str);
				if (def != reg.defaults.end ()) {
					config::serial::json_to_field (*def, f);
				}
			}

			settings::finalize_binds ();
		}

		// Dumps the *current* state as the file that ships as the default
		// profile. Written through a plain CreateFile so the output is exactly
		// what config::defaults::get() would hand nlohmann -- pretty printed,
		// one field per line, no re-encoding step in between.
		static inline bool write_default_profile (const std::string& json, const std::wstring& path) {
			const auto handle = CreateFileW (
				path.c_str (), GENERIC_WRITE, 0, nullptr,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (handle == INVALID_HANDLE_VALUE) {
				return false;
			}

			DWORD written {};
			const auto ok = WriteFile (
				handle, json.data (),
				static_cast<DWORD> (json.size ()), &written, nullptr);
			CloseHandle (handle);

			return ok && written == json.size ();
		}

	} // namespace detail

	void menu::draw_config (float group_w) {
		(void) group_w;

		if (detail::needs_refresh) {
			detail::config_list = config::files::list ();
			detail::needs_refresh = false;

			if (detail::selected >= static_cast<int>(detail::config_list.size ())) {
				detail::selected = -1;
			}
		}

		const auto dt = xdraw::delta_time ();

		if (detail::confirm_delete || detail::confirm_reset) {
			detail::confirm_timer += dt;

			if (detail::confirm_timer > 3.0f) {
				detail::confirm_delete = false;
				detail::confirm_reset = false;
			}
		}

		auto& dl = xui::draw::current ();
		const auto& s = xui::ctx ().style;
		const auto& input = xui::ctx ().input;

		xui::layout::set_cursor (this->m_body_x - this->m_x, this->m_body_y - this->m_y);

		if (!xui::begin_child ("##cfg_panel", this->m_body_w, this->m_body_h, false)) {
			return;
		}

		static std::string new_config_name {};
		xui::text_input ("##new_cfg_name", new_config_name, 64, "new config...");

		if (xui::button ("Create", 80.0f, 24.0f)) {
			std::string cfg_name = new_config_name;
			bool saved = false;

			if (cfg_name.empty ()) {
				int counter = 0;
				std::wstring final_name;
				do {
					char buf [64] {};
					if (counter == 0)
						std::snprintf (buf, sizeof (buf), "default");
					else
						std::snprintf (buf, sizeof (buf), "default%d", counter);
					final_name = detail::utf8_to_wide (buf);
					counter++;
				} while (std::find (detail::config_list.begin (), detail::config_list.end (), final_name) != detail::config_list.end ());

				saved = config::files::save (final_name);
			} else {
				saved = config::files::save (detail::utf8_to_wide (cfg_name));
			}

			if (saved)
				g_notifications.add ("config created");
			else
				g_notifications.add ("config save failed");

			detail::needs_refresh = true;
			new_config_name.clear ();
		}

		xui::text_input ("##cfg_search", detail::search_buf, 64, "search configs...");

		constexpr auto btn_h {28.0f};
		const auto [avail_w, avail_h] = xui::layout::avail ();
		const auto list_h = std::max (80.0f, avail_h - btn_h * 2.0f - s.item_spacing_y * 2.0f);

		if (xui::begin_child ("##cfg_list", avail_w, list_h, true)) {
			const auto row_w = xui::layout::avail ().first;
			constexpr auto row_h {28.0f};
			auto visible_rows {0};

			for (auto i = 0; i < static_cast<int> (detail::config_list.size ()); ++i) {
				const auto& wname = detail::config_list [i];

				if (!detail::config_matches_search (wname)) {
					continue;
				}

				char narrow [128] {};
				detail::wide_to_utf8 (wname, narrow, sizeof (narrow));

				const auto row = xui::layout::item (row_w, row_h);
				const auto is_selected = (detail::selected == i);
				const auto is_hovered = input.in_rect (row);

				if (is_hovered && input.mouse_clicked && !xui::ctx ().overlay_blocking ()) {
					detail::selected = i;
					detail::confirm_delete = false;
					detail::confirm_reset = false;
				}

				const auto hover_anim = xui::anim::lerp (xui::fnv1a ("cfgrow") + i, is_hovered ? 1.0f : 0.0f, 14.0f);
				const auto sel_anim = xui::anim::lerp (xui::fnv1a ("cfgsel") + i, is_selected ? 1.0f : 0.0f, 10.0f);

				if (sel_anim > 0.01f) {
					dl.rect_filled (row.x, row.y, row.w, row.h, theme::pal ().accent_soft.alpha (static_cast<std::uint8_t>(70.0f * sel_anim)), xdraw::corner_radius {6.0f});
				} else if (hover_anim > 0.01f) {
					dl.rect_filled (row.x, row.y, row.w, row.h, theme::pal ().surface_hover.alpha (static_cast<std::uint8_t>(255.0f * hover_anim * 0.5f)), xdraw::corner_radius {6.0f});
				}

				const auto [tw, th] = xdraw::measure_text (narrow);
				const auto text_col = is_selected
					? xui::lerp (theme::pal ().text, theme::pal ().accent, sel_anim)
					: xui::lerp (theme::pal ().text_muted, theme::pal ().text, hover_anim);

				dl.text (row.x + 10.0f, row.y + (row.h - th) * 0.5f, narrow, text_col);

				visible_rows++;
			}

			if (visible_rows == 0) {
				const auto row = xui::layout::item (row_w, row_h);
				dl.text (row.x + 10.0f, row.y + 6.0f, detail::config_list.empty () ? "no configs found" : "no matches", theme::pal ().text_muted);
			}

			xui::end_child ();
		}

		const auto btn_w = (avail_w - s.item_spacing_x * 3.0f) / 4.0f;
		const auto has_selection = detail::selected >= 0 && detail::selected < static_cast<int> (detail::config_list.size ());
		const auto save_name = has_selection ? detail::selected_name () : detail::search_buf;
		const auto can_save = !save_name.empty ();

		if (xui::button ("save", btn_w, btn_h) && can_save) {
			auto saved = config::files::save (detail::utf8_to_wide (save_name));
			g_notifications.add (saved ? "config saved" : "config save failed");
			detail::needs_refresh = true;
		}

		xui::layout::same_line ();

		if (detail::confirm_reset) {
			if (xui::button ("confirm", btn_w, btn_h)) {
				detail::reset_defaults ();
				detail::confirm_reset = false;
			}
		} else if (xui::button ("reset", btn_w, btn_h)) {
			detail::confirm_reset = true;
			detail::confirm_delete = false;
			detail::confirm_timer = 0.0f;
		}

		xui::layout::same_line ();

		if (detail::confirm_delete) {
			if (xui::button ("confirm", btn_w, btn_h) && has_selection) {
				config::files::remove (detail::config_list [detail::selected]);
				detail::selected = -1;
				detail::needs_refresh = true;
				detail::confirm_delete = false;
			}
		} else if (xui::button ("delete", btn_w, btn_h) && has_selection) {
			detail::confirm_delete = true;
			detail::confirm_reset = false;
			detail::confirm_timer = 0.0f;
		}

		xui::layout::same_line ();

		if (xui::button ("load", btn_w, btn_h) && has_selection) {
			auto loaded = config::files::load (detail::config_list [detail::selected]);
			if (loaded) {
				settings::finalize_binds ();
				g_notifications.add ("config loaded");
			} else {
				g_notifications.add ("config load failed");
			}
		}

		if (xui::button ("open folder", avail_w, btn_h)) {
			config::files::open_directory ();
			g_notifications.add ("opening configs folder");
		}

		// Second row: the shipped-profile escape hatch. Takes the four-button
		// row above and repeats the split so both rows have the same rhythm.
		const auto half_w = (avail_w - s.item_spacing_x) * 0.5f;

		if (xui::button ("save as default", half_w, btn_h)) {
			const auto path = detail::default_profile_path ();

			if (path.empty ()) {
				g_notifications.add ("default profile: no path");
			} else if (detail::write_default_profile (config::export_default_profile (), path)) {
				g_notifications.add ("default profile written");
			} else {
				g_notifications.add ("default profile write failed");
			}
		}

		xui::layout::same_line ();

		if (xui::button ("copy default path", half_w, btn_h)) {
			const auto path = detail::default_profile_path ();

			if (!path.empty () && OpenClipboard (nullptr)) {
				EmptyClipboard ();

				const auto bytes = (path.size () + 1) * sizeof (wchar_t);
				if (auto* mem = GlobalAlloc (GMEM_MOVEABLE, bytes)) {
					if (auto* dst = GlobalLock (mem)) {
						std::memcpy (dst, path.c_str (), bytes);
						GlobalUnlock (mem);
						SetClipboardData (CF_UNICODETEXT, mem);
					}
				}

				CloseClipboard ();
				g_notifications.add ("default profile path copied");
			} else {
				g_notifications.add ("clipboard unavailable");
			}
		}

		xui::end_child ();
	}

} // namespace rendering