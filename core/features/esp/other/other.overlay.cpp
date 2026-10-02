#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/steam/steam.hpp>
#include <core/rendering/rendering.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

namespace features::esp::other {

	// === Nonagon стилевые константы ===
	namespace darkfox_style {
		inline const xdraw::color& k_accent = tokens::col_accent;  // красный акцент
		inline const xdraw::color& k_bg = tokens::col_dark;   // тёмный фон
		inline const xdraw::color& k_pill_bg = tokens::col_card;   // фон для внутренних пилюль
		inline const xdraw::color& k_text_bright = tokens::col_text;// белый текст
		inline const xdraw::color& k_text_dim = tokens::col_text_dim;// приглушённый текст
		constexpr xdraw::color k_text_muted {140, 140, 150, 200};// ещё более приглушённый
		constexpr xdraw::color k_hud_bg     {10, 13, 20, 210};   // DarkFox Glass — полупрозрачное тёмное стекло
		constexpr xdraw::color k_hud_border {31, 39, 53, 255};   // DarkFox Glass — тонкая рамка
	}

	namespace detail {

		struct avatar_cache {
			struct entry {
				Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture {};
				bool attempted {};
			};

			std::unordered_map<std::uintptr_t, entry> m_entries {};

			[[nodiscard]] ID3D11ShaderResourceView* get (std::uintptr_t steam_id) {
				auto it = this->m_entries.find (steam_id);
				if (it != this->m_entries.end ()) {
					return it->second.texture.Get ();
				}

				auto& e = this->m_entries [steam_id];
				e.attempted = true;

				const auto image_handle = steam::friends::get_medium_friend_avatar (steam_id);
				if (image_handle <= 0) {
					return nullptr;
				}

				std::uint32_t w {}, h {};
				if (!steam::utils::get_image_size (image_handle, &w, &h) || !w || !h) {
					return nullptr;
				}

				std::vector<std::uint8_t> rgba (w * h * 4);
				if (!steam::utils::get_image_rgba (image_handle, rgba.data (), static_cast<int>(rgba.size ()))) {
					return nullptr;
				}

				e.texture = xdraw::create_srv_from_rgba (rgba.data (), static_cast<int>(w), static_cast<int>(h));
				return e.texture.Get ();
			}

			void clear () {
				this->m_entries.clear ();
			}
		};

	} // namespace detail

	void overlay::on_render (xdraw::draw_list& draw_list) {
		// Ранние выходы по конфигу: обе фичи раньше вызывались безусловно и
		// лезли в игровую память (SEH-чтения) каждый кадр даже выключенными.
		if (settings::g_esp.m_other.bomb_timer.value) {
			this->add_bomb (draw_list);
		}
		if (settings::g_esp.m_other.spectator_list.value) {
			this->add_spectators (draw_list);
		}
	}

	void overlay::add_bomb (xdraw::draw_list& draw_list) {
		using namespace darkfox_style;

		const auto local = systems::g_local.get ();
		if (!local.is_valid () || !systems::g_entities.exists (local.view_controller ())) {
			return;
		}

		const auto planted_c4 = memory::read<std::uintptr_t> (addresses::globals::planted_c4);
		const auto global_vars = memory::read<std::uintptr_t> (addresses::globals::global_vars);

		if (!planted_c4 || !global_vars) {
			return;
		}

		const auto current_time = memory::read<float> (global_vars + 0x30);
		const auto blow_time = memory::read<float> (planted_c4 + SCHEMA ("C_PlantedC4", "m_flC4Blow"_hash));
		const auto has_exploded = memory::read<bool> (planted_c4 + SCHEMA ("C_PlantedC4", "m_bHasExploded"_hash));
		const auto bomb_defused = memory::read<bool> (planted_c4 + SCHEMA ("C_PlantedC4", "m_bBombDefused"_hash));

		if (bomb_defused) {
			return;
		}

		const auto time_remaining = blow_time - current_time;
		const auto is_exploding = has_exploded || time_remaining <= 0.0f;

		if (is_exploding && time_remaining < -2.0f) {
			return;
		}

		const auto bomb_site = memory::read<int> (planted_c4 + SCHEMA ("C_PlantedC4", "m_nBombSite"_hash));
		const auto being_defused = memory::read<bool> (planted_c4 + SCHEMA ("C_PlantedC4", "m_bBeingDefused"_hash));
		const auto timer_length = memory::read<float> (planted_c4 + SCHEMA ("C_PlantedC4", "m_flTimerLength"_hash));

		const auto calculate_bomb_damage = [&] () -> float {
			const auto view_pawn = local.view_pawn ();
			if (!view_pawn) {
				return 0.0f;
			}

			const auto c4_scene_node = memory::read<std::uintptr_t> (planted_c4 + SCHEMA ("C_BaseEntity", "m_pGameSceneNode"_hash));
			const auto pawn_scene_node = memory::read<std::uintptr_t> (view_pawn + SCHEMA ("C_BaseEntity", "m_pGameSceneNode"_hash));

			if (!c4_scene_node || !pawn_scene_node) {
				return 0.0f;
			}

			const auto c4_origin = memory::read<math::vector3> (c4_scene_node + SCHEMA ("CGameSceneNode", "m_vecAbsOrigin"_hash));
			const auto pawn_origin = memory::read<math::vector3> (pawn_scene_node + SCHEMA ("CGameSceneNode", "m_vecAbsOrigin"_hash));

			const auto distance = (c4_origin - pawn_origin).length ();

			constexpr auto default_damage {650.0f};
			constexpr auto default_radius {2275.0f};

			const auto sigma = default_radius / 3.0f;
			auto damage = default_damage * std::exp (-(distance * distance) / (2.0f * sigma * sigma));

			const auto armor = memory::read<int> (view_pawn + SCHEMA ("C_CSPlayerPawn", "m_ArmorValue"_hash));

			if (armor > 0) {
				constexpr auto armor_ratio = 0.5f;
				constexpr auto armor_bonus = 0.5f;

				auto armor_absorbed = damage * armor_ratio;
				auto armor_cost = (damage - armor_absorbed) * armor_bonus;

				if (armor_cost > static_cast<float>(armor)) {
					armor_cost = static_cast<float>(armor) * (1.0f / armor_bonus);
					armor_absorbed = damage - armor_cost;
				}

				damage = armor_absorbed;
			}

			return std::floor (damage);
		}();

		const auto [screen_w, screen_h] = xdraw::viewport_size ();
		const bool use_blur = settings::g_misc.m_interface.hud_blur.value;

		constexpr auto h {26.0f};
		constexpr auto top_offset {175.0f};
		constexpr auto r {6.0f};
		constexpr auto accent_line_h {2.0f};
		constexpr auto pad_x {12.0f};
		constexpr auto separator_pad {8.0f};
		constexpr auto text_nudge {0.5f};

		auto timer_color = [&] () -> xdraw::color {
			if (is_exploding) {
				return {255, 100, 100, 255};
			}

			const auto frac = timer_length > 0.0f ? time_remaining / timer_length : 1.0f;

			if (frac > 0.5f) {
				return k_accent;
			} else if (frac > 0.2f) {
				const auto t = (frac - 0.2f) / 0.3f;
				return
				{
					255,
					static_cast<std::uint8_t>(140 + static_cast<int>(40 * t)),
					static_cast<std::uint8_t>(100 + static_cast<int>(40 * t)),
					255
				};
			} else {
				const auto t = frac / 0.2f;
				return
				{
					255,
					static_cast<std::uint8_t>(100 + static_cast<int>(40 * t)),
					static_cast<std::uint8_t>(80 + static_cast<int>(20 * t)),
					255
				};
			}
		}();

		// собираем сегменты как у watermark
		struct segment {
			std::string text;
			xdraw::color color;
		};

		std::vector<segment> segments;
		segments.reserve (8);

		// site
		const auto site_label = bomb_site == 0 ? "A plant" : "B plant";
		segments.push_back ({site_label, k_text_bright});

		// разделитель
		segments.push_back ({"|", k_text_muted});

		// damage
		const auto damage = static_cast<int>(calculate_bomb_damage);
		const auto view_pawn = local.view_pawn ();
		const auto health = view_pawn ? memory::read<int> (view_pawn + SCHEMA ("C_BaseEntity", "m_iHealth"_hash)) : 0;
		const auto will_kill = health <= damage && damage > 0;
		const auto health_col = will_kill ? xdraw::color {255, 100, 100, 255} : xdraw::color {180, 210, 150, 255};

		char health_buf [32] {};
		std::snprintf (health_buf, sizeof (health_buf), "-%d hp", damage);
		segments.push_back ({health_buf, health_col});

		// разделитель
		segments.push_back ({"|", k_text_muted});

		// timer
		char timer_buf [32] {};
		if (is_exploding) {
			strncpy_s (timer_buf, sizeof (timer_buf), being_defused ? "defusing" : "exploding", _TRUNCATE);
		} else if (being_defused) {
			std::snprintf (timer_buf, sizeof (timer_buf), "%.1fs defusing", time_remaining);
		} else {
			std::snprintf (timer_buf, sizeof (timer_buf), "%.1fs", time_remaining);
		}
		segments.push_back ({timer_buf, timer_color});

		// измерения
		float total_w = pad_x;
		std::vector<float> seg_widths (segments.size ());
		float text_h_max = 0.0f;

		for (std::size_t i = 0; i < segments.size (); ++i) {
			const auto [tw, th] = xdraw::measure_text (segments [i].text.c_str ());
			seg_widths [i] = tw;
			text_h_max = std::max (text_h_max, th);
			total_w += tw;
			if (i + 1 < segments.size ()) total_w += separator_pad;
		}
		total_w += pad_x;

		const auto x = (static_cast<float>(screen_w) - total_w) * 0.5f;
		const auto y = top_offset;

		// блюр
		if (use_blur)
			draw_list.rect_filled_blurred (x, y, total_w, h, xdraw::corner_radius {r});

		// тёмный фон (без верхних 2 пикселей)
		draw_list.rect_filled (x, y + accent_line_h, total_w, h - accent_line_h, k_bg, xdraw::corner_radius {r});

		// красная акцентная линия
		draw_list.rect_filled (x, y, total_w, accent_line_h, k_accent);

		// текст
		auto cx = x + pad_x;
		const auto text_y = y + accent_line_h + (h - accent_line_h - text_h_max) * 0.5f + text_nudge;

		for (std::size_t i = 0; i < segments.size (); ++i) {
			draw_list.text (cx, text_y, segments [i].text.c_str (), segments [i].color);
			cx += seg_widths [i];
			if (i + 1 < segments.size ()) cx += separator_pad;
		}
	}

	void overlay::add_spectators (xdraw::draw_list& draw_list) {
		using namespace darkfox_style;

		const auto local = systems::g_local.get ();
		if (!local.is_valid () || !systems::g_entities.exists (local.view_controller ())) {
			return;
		}

		const auto game_rules = memory::read<std::uintptr_t> (addresses::globals::game_rules);
		if (!game_rules || memory::read<int> (game_rules + SCHEMA ("C_CSGameRules", "m_gamePhase"_hash)) >= 4) {
			return;
		}

		const auto local_controller = local.controller;
		const auto view_controller = local.view_controller ();
		const auto view_pawn = local.view_pawn ();
		if (!view_pawn) {
			return;
		}

		const auto [screen_w, screen_h] = xdraw::viewport_size ();
		const bool use_blur = settings::g_misc.m_interface.hud_blur.value;

		constexpr auto margin_right {16.0f};
		constexpr auto row_spacing {4.0f};
		constexpr auto row_h {26.0f};
		constexpr auto header_h {26.0f};
		constexpr auto r {6.0f};
		constexpr auto text_pad_x {10.0f};
		constexpr auto text_nudge {0.5f};
		constexpr auto avatar_size {18.0f};
		constexpr auto avatar_pad {4.0f};
		constexpr auto min_pill_w {140.0f};

		struct spectator_entry {
			char name [128];
			std::uintptr_t steam_id;
		};

		spectator_entry entries [32] {};
		auto count {0};

		for (const auto& player : systems::g_entities.get_by_type (systems::entities::type::player)) {
			if (player.ptr == view_controller || player.ptr == local_controller || count >= 32) {
				continue;
			}

			if (memory::read<bool> (player.ptr + SCHEMA ("CCSPlayerController", "m_bPawnIsAlive"_hash))) {
				continue;
			}

			const auto obs_pawn_handle = memory::read<std::uint32_t> (player.ptr + SCHEMA ("CCSPlayerController", "m_hObserverPawn"_hash));
			if (!obs_pawn_handle || obs_pawn_handle == 0xffffffff) {
				continue;
			}

			const auto obs_pawn = systems::g_entities.lookup (obs_pawn_handle);
			if (!obs_pawn) {
				continue;
			}

			const auto observer_services = memory::safe_read<std::uintptr_t> (obs_pawn + SCHEMA ("C_BasePlayerPawn", "m_pObserverServices"_hash)).value_or (0);
			if (!observer_services || (observer_services >> 48) != 0) {
				continue;
			}

			const auto observer_target_handle = memory::safe_read<std::uint32_t> (observer_services + SCHEMA ("CPlayer_ObserverServices", "m_hObserverTarget"_hash)).value_or (0);
			if (!observer_target_handle) {
				continue;
			}

			const auto observer_target = systems::g_entities.lookup (observer_target_handle);
			if (observer_target != view_pawn) {
				continue;
			}

			const auto name_ptr = memory::read<std::uintptr_t> (player.ptr + SCHEMA ("CCSPlayerController", "m_sSanitizedPlayerName"_hash));
			if (!name_ptr) {
				continue;
			}

			auto name = memory::read_string (name_ptr, 127);
			std::ranges::transform (name, name.begin (), [] (unsigned char c) { return std::tolower (c); });

			auto& e = entries [count++];
			strncpy_s (e.name, name.c_str (), sizeof (e.name) - 1);
			e.name [sizeof (e.name) - 1] = '\0';
			e.steam_id = memory::read<std::uintptr_t> (player.ptr + SCHEMA ("CBasePlayerController", "m_steamID"_hash));
		}

		if (count <= 0) {
			return;
		}

		static detail::avatar_cache avatars {};

		char header_buf [64];
		std::snprintf (header_buf, sizeof (header_buf), "SPECTATORS (%d)", count);
		const auto [header_tw, header_th] = xdraw::measure_text (header_buf);

		// вычисляем максимальную ширину среди всех pill'ов
		float max_pill_w = std::max (min_pill_w, header_tw + text_pad_x * 2.0f);

		// Кэш ширин и наличия аватара: раньше measure_text звался дважды на строку,
		// а avatars.get -- трижды (промах кэша = синхронный дёрг Steam API).
		std::vector<float> row_name_w (static_cast<std::size_t> (count));
		std::vector<bool> row_has_avatar (static_cast<std::size_t> (count));

		for (auto i = 0; i < count; ++i) {
			const auto [nw, nh] = xdraw::measure_text (entries [i].name);
			const auto has_avatar = avatars.get (entries [i].steam_id) != nullptr;
			const auto row_w = text_pad_x + (has_avatar ? avatar_size + avatar_pad : 0.0f) + nw + text_pad_x;
			max_pill_w = std::max (max_pill_w, row_w);

			row_name_w [static_cast<std::size_t> (i)] = nw;
			row_has_avatar [static_cast<std::size_t> (i)] = has_avatar;
		}

		// позиция: правый край, вертикально по центру
		const auto total_h = header_h + row_spacing + (static_cast<float> (count) * (row_h + row_spacing));
		const auto base_y = (static_cast<float> (screen_h) - total_h) * 0.5f;
		const auto x = static_cast<float> (screen_w) - max_pill_w - margin_right;

		// === header ===
		{
			// блюр
			if (use_blur)
				draw_list.rect_filled_blurred (x, base_y, max_pill_w, header_h, xdraw::corner_radius {r});

			// тёмное стекло
			draw_list.rect_filled (x, base_y, max_pill_w, header_h, k_hud_bg, xdraw::corner_radius {r});

			// DarkFox Glass — рамка
			draw_list.rect (x, base_y, max_pill_w, header_h, k_hud_border, xdraw::corner_radius {r}, 1.0f);

			// заголовок "SPECTATORS (N)" слева
			draw_list.text (x + text_pad_x, base_y + (header_h - header_th) * 0.5f + text_nudge, header_buf, k_accent);

			// тонкая разделительная линия под заголовком
			draw_list.rect_filled (x + text_pad_x, base_y + header_h - 1.0f, max_pill_w - text_pad_x * 2.0f, 1.0f, k_hud_border);
		}

		// === rows ===
		auto ry = base_y + header_h + row_spacing;

		for (auto i = 0; i < count; ++i) {
			const auto& e = entries [i];
			const auto idx = static_cast<std::size_t> (i);
			const auto nw = row_name_w [idx];
			const auto nh = header_th;
			const auto has_avatar = row_has_avatar [idx];
			const auto avatar_tex = has_avatar ? avatars.get (e.steam_id) : nullptr;

			// блюр
			if (use_blur)
				draw_list.rect_filled_blurred (x, ry, max_pill_w, row_h, xdraw::corner_radius {r});

			// тёмное стекло
			draw_list.rect_filled (x, ry, max_pill_w, row_h, k_hud_bg, xdraw::corner_radius {r});

			// DarkFox Glass — рамка
			draw_list.rect (x, ry, max_pill_w, row_h, k_hud_border, xdraw::corner_radius {r}, 1.0f);

			constexpr auto dot {6.0f};
			constexpr auto dot_pad {6.0f};
			auto text_x = x + text_pad_x;

			// маркер-точка (цвет по статусу — наблюдает за тобой)
			draw_list.rect_filled (text_x, ry + (row_h - dot) * 0.5f, dot, dot, k_accent, xdraw::corner_radius {dot * 0.5f});
			text_x += dot + dot_pad;

			// аватар слева если есть
			if (has_avatar) {
				const auto ax = text_x;
				const auto ay = ry + (row_h - avatar_size) * 0.5f;
				draw_list.image (ax, ay, avatar_size, avatar_size, avatar_tex, xdraw::corner_radius {4.0f}, xdraw::color {255, 255, 255, 255});
				text_x += avatar_size + avatar_pad;
			}

			// имя (белым)
			draw_list.text (text_x, ry + (row_h - nh) * 0.5f + text_nudge, e.name, k_text_bright);

			ry += row_h + row_spacing;
		}
	}

} // namespace features::esp::other