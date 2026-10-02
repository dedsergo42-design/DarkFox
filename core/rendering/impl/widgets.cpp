#include <pch/pch.hpp>
#include <utilities/math/math.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/steam/steam.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

#include "../rendering.hpp"
#include <d3d11.h>
#include "waifu.hpp"
#include <utilities/security/security.hpp>

namespace rendering {

	namespace svgs {

		// Красивая буква "N" в стиле Nonagon (две полоски + диагональ)
		constexpr auto watermark_logo = R"(<svg width="14" height="14" viewBox="0 0 14 14" fill="none" xmlns="http://www.w3.org/2000/svg">
			<path d="M 2 1 L 5 1 L 5 13 L 2 13 Z" fill="#111111"/>
			<path d="M 9 1 L 12 1 L 12 13 L 9 13 Z" fill="#111111"/>
			<path d="M 5 1 L 9 13 L 5 13 Z" fill="#111111" opacity="0.85"/>
		</svg>)";

	// === Hotkey badge icons ===
	// Stroked white so they can be tinted to the badge accent at draw time;
	// picked by keyword from the bind name, with the dot as the fallback.
	constexpr auto bind_damage = R"(<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M13 2 4 14h7l-1 8 9-12h-7z"/></svg>)";
	constexpr auto bind_chance = R"(<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="8"/><circle cx="12" cy="12" r="3"/></svg>)";
	constexpr auto bind_double = R"(<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 5l7 7-7 7M12 5l7 7-7 7"/></svg>)";
	constexpr auto bind_force = R"(<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3v18M5 10l7-7 7 7"/></svg>)";
	constexpr auto bind_body = R"(<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="7" r="3.2"/><path d="M6 20c0-3.6 2.7-6 6-6s6 2.4 6 6"/></svg>)";
	constexpr auto bind_left = R"(<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M15 5l-7 7 7 7"/></svg>)";
	constexpr auto bind_right = R"(<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 5l7 7-7 7"/></svg>)";
	constexpr auto bind_move = R"(<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 17l5-10 4 6 3-4 4 8z"/></svg>)";
	constexpr auto bind_peek = R"(<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M2 12s3.5-6 10-6 10 6 10 6-3.5 6-10 6-10-6-10-6z"/><circle cx="12" cy="12" r="2.5"/></svg>)";
	constexpr auto bind_generic = R"(<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="4.5"/></svg>)";

	// === Tab icons (top bar) ===
	inline constexpr auto tab_rage = R"(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="2.5"/><path d="M12 2v6M12 16v6M2 12h6M16 12h6"/></svg>)";
	inline constexpr auto tab_legit = R"(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="1.6" stroke-linecap="round"><circle cx="12" cy="12" r="3"/><path d="M12 6v3M12 15v3M6 12h3M15 12h3"/></svg>)";
	inline constexpr auto tab_visuals = R"(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7-10-7-10-7z"/><circle cx="12" cy="12" r="3"/></svg>)";
	inline constexpr auto tab_misc = R"(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round"><path d="M4 8h10M18 8h2M4 16h2M10 16h10"/><circle cx="15" cy="8" r="2"/><circle cx="8" cy="16" r="2"/></svg>)";
	inline constexpr auto tab_skins = R"(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3a9 9 0 0 0 0 18c1.5 0 2-1 2-2 0-1.5 1-2 2-2h1a4 4 0 0 0 4-4c0-5-4-8-9-8z"/><circle cx="7.5" cy="11" r="1"/><circle cx="11" cy="7.5" r="1"/><circle cx="15.5" cy="8.5" r="1"/></svg>)";
	inline constexpr auto tab_settings = R"(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M19.1 4.9L17 7M7 17l-2.1 2.1"/></svg>)";

} // namespace svgs

	// === DarkFox cc стилевые константы ===
	namespace darkfox_style {
		inline const xdraw::color& k_accent = tokens::col_accent;  // White акцент
		inline const xdraw::color& k_bg = tokens::col_dark;
		inline const xdraw::color& k_pill_bg = tokens::col_card;
		inline const xdraw::color& k_text_bright = tokens::col_text;
		inline const xdraw::color& k_text_dim = tokens::col_text_dim;
		constexpr xdraw::color k_text_muted {120, 122, 130, 200};
		constexpr xdraw::color k_hud_bg     {14, 14, 16, 150};   // Прозрачное тёмное стекло
		constexpr xdraw::color k_hud_border {255, 255, 255, 45};  // Тонкий белый контур
		constexpr xdraw::color k_ping_good {245, 246, 250, 255};
		constexpr xdraw::color k_ping_mid  {170, 172, 180, 255};
		constexpr xdraw::color k_ping_bad  {110, 112, 120, 255};
	}

	void hud_widgets::draw () {
		auto& dl = xdraw::get ();

		if (settings::g_misc.m_watermark.enabled.value) {
			this->watermark (dl);
		}

		if (settings::g_misc.m_hud.m_keybinds.enabled.value) {
			this->keybinds (dl);
		}

		if (settings::g_misc.m_spectators.enabled.value) {
			this->spectators (dl);
		}
		g_notifications.draw ();
		g_waifu.draw ();
	}

	void hud_widgets::try_load_avatar () {
		// Уже загружена
		if (this->m_avatar.resource) {
			return;
		}

		// Ретрай раз в секунду
		this->m_avatar_retry_delay -= xdraw::delta_time ();
		if (this->m_avatar_retry_delay > 0.0f) {
			return;
		}
		this->m_avatar_retry_delay = 1.0f;

		const auto steam_id = steam::user::get_steam_id ();
		if (!steam_id) {
			return;
		}

		// Аватарка
		const auto image = steam::friends::get_medium_friend_avatar (steam_id);
		if (image <= 0) {
			return;
		}

		std::uint32_t width {}, height {};
		if (!steam::utils::get_image_size (image, &width, &height) || !width || !height) {
			return;
		}

		std::vector<std::uint8_t> rgba (width * height * 4);
		if (!steam::utils::get_image_rgba (image, rgba.data (), static_cast<int>(rgba.size ()))) {
			return;
		}

		this->m_avatar.resource = xdraw::create_srv_from_rgba (rgba.data (), static_cast<int>(width), static_cast<int>(height));
		this->m_avatar.width = static_cast<int>(width);
		this->m_avatar.height = static_cast<int>(height);
	}

	void hud_widgets::watermark (xdraw::draw_list& draw_list) {
		using namespace darkfox_style;

		// Загрузка аватарки (один раз)
		this->try_load_avatar ();

		// Загрузка лого (один раз)
		if (!this->m_logo_loaded) {
			this->m_logo.resource = xdraw::load_svg (svgs::watermark_logo, 1.0f, &this->m_logo.width, &this->m_logo.height);
			this->m_logo_loaded = true;
		}

		const auto [screen_w, screen_h] = xdraw::viewport_size ();
		const auto framerate = xdraw::framerate ();
		const auto local = systems::g_local.get ();
		const auto& wm = settings::g_misc.m_watermark;

		constexpr auto h {30.0f};
		constexpr auto margin {14.0f};
		constexpr auto r {8.0f};
		constexpr auto pad_x {14.0f};
		constexpr auto separator_pad {12.0f};
		constexpr auto text_nudge {0.5f};
		constexpr auto icon_s {12.0f};
		constexpr auto icon_pad {6.0f};

		// ── time ────────────────────────────────────────────────────────────
		SYSTEMTIME st {};
		GetLocalTime (&st);
		char time_buf [16] {};
		std::snprintf (time_buf, sizeof (time_buf), "%02d:%02d", st.wHour, st.wMinute);

		// ── fps ─────────────────────────────────────────────────────────────
		static auto smoothed_fps {0.0f};
		if (smoothed_fps == 0.0f) smoothed_fps = framerate;
		smoothed_fps += (framerate - smoothed_fps) * std::min (2.0f * xdraw::delta_time (), 1.0f);
		char fps_buf [16] {};
		std::snprintf (fps_buf, sizeof (fps_buf), "%.0f fps", smoothed_fps);

		// ── ping ────────────────────────────────────────────────────────────
		auto ping {0};
		if (local.is_alive && local.controller && systems::g_entities.exists (local.controller))
			ping = memory::read<std::uint32_t> (local.controller + SCHEMA ("CCSPlayerController", "m_iPing"_hash));
		char ping_buf [16] {};
		std::snprintf (ping_buf, sizeof (ping_buf), "%d ms", ping);

		xdraw::color k_ping_color;
		if (ping == 0)
			 k_ping_color = k_text_muted;
		else if (ping > 80)
			 k_ping_color = k_ping_bad;
		else if (ping > 40)
			 k_ping_color = k_ping_mid;
		else
			 k_ping_color = k_ping_good;

		struct segment { std::string text; xdraw::color color; const char* icon; };
		std::vector<segment> segments;
		segments.reserve (16);

		// show_velocity and show_tick had toggles in the menu and no segment here
		// at all, so ticking them did nothing. show_user and show_map still have
		// no data source reachable from this build -- there is no persona-name or
		// level-name accessor -- so those two stay unhandled rather than faked.
		char vel_buf [24] {};
		char tick_buf [24] {};

		{
			const auto& prestate = systems::g_prediction.pre ();
			std::snprintf (vel_buf, sizeof (vel_buf), "%.0f", prestate.networked_velocity.length_2d ());
			std::snprintf (tick_buf, sizeof (tick_buf), "%.0f tick", 1.0f / cstypes::tick_interval);
		}

		if (wm.show_ping.value)     segments.push_back ({ping_buf, k_ping_color, "ping"});
		if (wm.show_fps.value)      segments.push_back ({fps_buf, k_text_bright, "fps"});
		if (wm.show_velocity.value) segments.push_back ({vel_buf, k_text_bright, "vel"});
		if (wm.show_tick.value)     segments.push_back ({tick_buf, k_text_dim, "tick"});
		if (wm.show_time.value)     segments.push_back ({time_buf, k_text_dim, "time"});

		// ── измеряем ширину ───────────────────────────────────────────────
		const auto brand = std::string {"DarkFox"};
		const auto sub   = std::string {"cheat"};
		const auto brand_w = xdraw::measure_text (brand).first;
		const auto sub_w   = xdraw::measure_text (sub).first;

		float total_w = pad_x + 14.0f + 10.0f + brand_w + 10.0f + sub_w + 14.0f;
		std::vector<float> seg_w (segments.size ());
		float text_h_max = 0.0f;
		for (std::size_t i = 0; i < segments.size (); ++i) {
			const auto [tw, th] = xdraw::measure_text (segments [i].text.c_str ());
			seg_w [i] = tw;
			text_h_max = std::max (text_h_max, th);
			total_w += icon_s + icon_pad + tw;
			if (i + 1 < segments.size ()) total_w += separator_pad;
		}
		total_w += pad_x;

		// ── smooth width ────────────────────────────────────────────────────
		static auto smoothed_w {0.0f};
		if (smoothed_w == 0.0f) smoothed_w = total_w;
		smoothed_w += (total_w - smoothed_w) * std::min (8.0f * xdraw::delta_time (), 1.0f);

		const auto w = smoothed_w;
		const auto x = static_cast<float>(screen_w) - w - margin;
		const auto y = margin;

		const auto wm_accent = wm.color.value;
		const auto pill_r = h * 0.5f;

		draw_list.rect_filled (x, y, w, h, xdraw::color {14, 15, 19, 150}, xdraw::corner_radius {pill_r});
		draw_list.rect (x, y, w, h, wm_accent.alpha (86), xdraw::corner_radius {pill_r}, 1.0f);

		// Leading accent stripe, same as the badges.
		draw_list.rect_filled (x + 4.0f, y + 6.0f, 3.0f, h - 12.0f, wm_accent, xdraw::corner_radius {1.5f});

		const auto text_y = y + (h - text_h_max) * 0.5f + text_nudge;
		const auto icon_y = y + (h - icon_s) * 0.5f;

		// Маленькие иконки-символы к статусам
		auto draw_icon = [&] (const char* type, float ix, float iy, float s, xdraw::color c) {
			if (std::strcmp (type, "fps") == 0) {
				// молния
				std::vector<float> p {
					ix + s * 0.55f, iy,
					ix + s * 0.20f, iy + s * 0.50f,
					ix + s * 0.45f, iy + s * 0.50f,
					ix + s * 0.30f, iy + s * 1.00f,
					ix + s * 0.85f, iy + s * 0.30f,
					ix + s * 0.55f, iy + s * 0.30f,
					ix + s * 0.70f, iy,
				};
				draw_list.polyline (p, c, false, 1.5f);
			} else if (std::strcmp (type, "ping") == 0) {
				// антенна
				draw_list.line (ix + s * 0.50f, iy + s * 0.85f, ix + s * 0.15f, iy + s * 0.25f, c, 1.5f);
				draw_list.line (ix + s * 0.50f, iy + s * 0.85f, ix + s * 0.85f, iy + s * 0.25f, c, 1.5f);
				draw_list.circle_filled (ix + s * 0.50f, iy + s * 0.90f, 1.6f, c, 6);
			} else if (std::strcmp (type, "vel") == 0) {
				// speed lines
				draw_list.line (ix + s * 0.10f, iy + s * 0.32f, ix + s * 0.80f, iy + s * 0.32f, c, 1.5f);
				draw_list.line (ix + s * 0.25f, iy + s * 0.58f, ix + s * 0.90f, iy + s * 0.58f, c, 1.5f);
				draw_list.line (ix + s * 0.10f, iy + s * 0.84f, ix + s * 0.65f, iy + s * 0.84f, c, 1.5f);
			} else if (std::strcmp (type, "tick") == 0) {
				// pulse
				std::vector<float> p {
					ix + s * 0.05f, iy + s * 0.60f,
					ix + s * 0.32f, iy + s * 0.60f,
					ix + s * 0.45f, iy + s * 0.20f,
					ix + s * 0.60f, iy + s * 0.90f,
					ix + s * 0.72f, iy + s * 0.60f,
					ix + s * 0.95f, iy + s * 0.60f,
				};
				draw_list.polyline (p, c, false, 1.5f);
			} else if (std::strcmp (type, "time") == 0) {
				// часы
				draw_list.circle (ix + s * 0.50f, iy + s * 0.50f, s * 0.42f, c, 1.4f);
				draw_list.line (ix + s * 0.50f, iy + s * 0.50f, ix + s * 0.50f, iy + s * 0.25f, c, 1.4f);
				draw_list.line (ix + s * 0.50f, iy + s * 0.50f, ix + s * 0.68f, iy + s * 0.58f, c, 1.4f);
			}
		};

		// Акцентный ромб-бренд
		// The brand mark is off by default now: it read as a stray shape rather
		// than as a logo, and there was no way to turn it off.
		if (wm.show_icon.value) {
			draw_list.rect_filled (x + pad_x, y + h * 0.5f - 4.0f, 8.0f, 8.0f, wm_accent, xdraw::corner_radius {2.0f});
		}

		float cx = x + pad_x + 14.0f + 10.0f;
		draw_list.text (cx, text_y, brand.c_str (), k_text_bright);
		cx += brand_w + 10.0f;
		draw_list.text (cx, text_y, sub.c_str (), wm_accent);
		cx += sub_w + 14.0f;

		for (std::size_t i = 0; i < segments.size (); ++i) {
			draw_icon (segments [i].icon, cx, icon_y, icon_s, segments [i].color);
			cx += icon_s + icon_pad;
			draw_list.text (cx, text_y, segments [i].text.c_str (), segments [i].color);
			cx += seg_w [i];
			if (i + 1 < segments.size ()) {
				draw_list.rect_filled (cx, y + 7.0f, 1.0f, h - 14.0f, xdraw::color {255, 255, 255, 28});
				cx += separator_pad;
			}
		}
	}


	void hud_widgets::keybinds (xdraw::draw_list& draw_list) {
		using namespace darkfox_style;

		static animation::fade container_alpha;
		static animation::spring smoothed_base_y;

		const auto screen_h = xdraw::viewport_size ().second;

		// Per-badge animation state, kept alive across frames and keyed on the
		// setting rather than on a slot in the array below.
		struct pill_state {
			const void* key;
			float appear;   // 0 hidden .. 1 fully in
			float ring;     // animated fill, starts at 0 so it sweeps in
			float y;        // animated row position
			float width;    // remembered so a leaving badge keeps its shape
			bool  placed;   // y has been seeded at least once
			bool  seen;
		};

		struct bind_entry {
			// The setting this row came from. Animation state is keyed on it, so a
			// badge keeps its own ring across frames no matter how the list around
			// it changes.
			const void* key;
			pill_state* slot;
			// Each badge is sized to its own contents. A shared width would line
			// the right edges up, but it also pads a two-letter bind out to the
			// width of the longest one, which is not how the reference reads.
			float width;
			ID3D11ShaderResourceView* icon;
			char tag [8];
			const char* name;
			char value [32];
			bool has_value_pill;
			// 0..1 share of the setting's own slider range, or -1 when the bind
			// carries no number. Drives the progress ring on the badge.
			float ring;
			xui::bind_mode mode;
			std::pair<float, float> name_size {};
			std::pair<float, float> value_size {};
			std::pair<float, float> status_size {};
		};

		bind_entry entries [32] {};
		auto count {0};

		const auto& ctx = features::combat::g_shared.ctx ();
		const auto has_weapon = ctx.valid && ctx.weapon_type >= cstypes::weapon_type::pistol && ctx.weapon_type <= cstypes::weapon_type::lmg;

		for (const auto setting : xui::binds::all ()) {
			if (!setting || setting->bind.key == 0 || !setting->bind.active || count >= 32) {
				continue;
			}

			auto is_rage_group {false};
			for (auto i = 0u; i < settings::combat::ragebot::k_group_count; ++i) {
				const auto& g = settings::g_combat.m_ragebot.groups [i];
				if (setting == &g.min_damage_override || setting == &g.hitchance_override || setting == &g.force_shot || setting == &g.force_shot_air || setting == &g.body_aim || setting == &g.silent || setting == &g.no_spread) {
					is_rage_group = true;
					break;
				}
			}

			if (is_rage_group) {
				if (!settings::g_combat.m_ragebot.enabled || !has_weapon) {
					continue;
				}

				const auto active_group = &settings::g_combat.m_ragebot.get_group (ctx.weapon_type);
				auto is_active {false};

				for (auto i = 0u; i < settings::combat::ragebot::k_group_count; ++i) {
					const auto& g = settings::g_combat.m_ragebot.groups [i];
					if (&g == active_group) {
						if (setting == &g.min_damage_override || setting == &g.hitchance_override || setting == &g.force_shot || setting == &g.force_shot_air || setting == &g.body_aim) {
							is_active = true;
						}
						break;
					}
				}

				if (!is_active) {
					continue;
				}

				auto& e = entries [count++];
				e.key = setting;
				e.slot = nullptr;
				e.width = 0.0f;
				e.icon = nullptr;
				e.name = setting->name.c_str ();
				e.mode = setting->bind.mode;

				if (setting == &active_group->min_damage_override) {
					const auto dmg = active_group->min_damage_override_value.value;
					std::snprintf (e.value, sizeof (e.value), "%d", dmg);
					e.has_value_pill = true;
					// 130 is the slider's own ceiling in the ragebot menu; the ring
					// has to mean the same thing the user set it with.
					e.ring = std::clamp (static_cast<float> (dmg) / 130.0f, 0.0f, 1.0f);
				} else if (setting == &active_group->hitchance_override) {
					const auto hc = active_group->hitchance_override_value.value;
					std::snprintf (e.value, sizeof (e.value), "%d%%", hc);
					e.has_value_pill = true;
					e.ring = std::clamp (static_cast<float> (hc) / 100.0f, 0.0f, 1.0f);
				} else {
					e.value [0] = '\0';
					e.has_value_pill = false;
					e.ring = -1.0f;
				}
				continue;
			}

			auto& e = entries [count++];
			e.key = setting;
			e.slot = nullptr;
			e.width = 0.0f;
			e.icon = nullptr;
			e.name = setting->name.c_str ();
			e.mode = setting->bind.mode;
			e.value [0] = '\0';
			e.has_value_pill = false;
			e.ring = -1.0f;
		}

		if (count > 0)
			container_alpha.fade_in (0.2f);
		else
			container_alpha.fade_out (0.2f);

		container_alpha.update ();
		if (!container_alpha.visible ())
			return;

		const auto master_alpha = container_alpha.alpha ();
		const bool use_blur = settings::g_misc.m_interface.hud_blur.value;

		// ── DarkFox unified keybinds panel (with real DT charge ring) ──

		// Реальное состояние зарядки doubletap: готов, когда следующий
		// выстрел разрешён (m_nNextPrimaryAttackTick <= текущий тик), есть
		// патроны и мы не в перезарядке. Кольцо заполняется по ходу
		// восстановления между выстрелами.
		static float dt_charge = 1.0f;
		static int dt_charge_start = -1;
		static int dt_charge_end = -1;
		static float dt_pulse = 0.0f;

		{
			bool dt_in_list = false;
			for (auto i = 0; i < count; ++i) {
				if (std::strstr (entries [i].name, "doubletap") != nullptr || std::strstr (entries [i].name, "dt") != nullptr) {
					dt_in_list = true;
					break;
				}
			}

			const auto local = systems::g_local.get ();
			if (dt_in_list && ctx.valid && ctx.weapon && local.controller) {
				const auto next_primary = memory::read<int> (ctx.weapon + SCHEMA ("C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash));
				const auto tick_base = memory::read<int> (local.controller + SCHEMA ("CBasePlayerController", "m_nTickBase"_hash));
				const auto clip = memory::read<int> (ctx.weapon + SCHEMA ("C_BasePlayerWeapon", "m_iClip1"_hash));
				const auto reloading = memory::read<bool> (ctx.weapon + SCHEMA ("C_CSWeaponBase", "m_bInReload"_hash));
				const bool ready = (tick_base >= next_primary) && clip > 0 && !reloading;

				if (ready) {
					dt_charge = 1.0f; dt_charge_start = -1; dt_charge_end = -1;
				} else {
					if (dt_charge_start < 0) {
						dt_charge_start = tick_base;
						dt_charge_end = std::max (next_primary, tick_base + 1);
					} else if (next_primary > dt_charge_end) {
						dt_charge_end = next_primary;
					}
					const float dur = std::max (1.0f, static_cast<float> (dt_charge_end - dt_charge_start));
					dt_charge = std::clamp (static_cast<float> (tick_base - dt_charge_start) / dur, 0.0f, 1.0f);
				}
				dt_pulse += xdraw::delta_time () * 3.0f;
			} else {
				dt_charge = 1.0f; dt_charge_start = -1; dt_charge_end = -1;
			}
		}

		// ── badge layout ──────────────────────────────────────────────────
		// One rounded badge per bind rather than a single panel, so a bind that
		// appears or disappears moves only itself.
		//
		// Each badge owns persistent state keyed on the setting it belongs to,
		// not on its slot in the entries array: binds come and go, so slot 0 is a
		// different bind from one frame to the next, and animating on the slot
		// makes a new badge inherit the previous occupant's ring.
		if (settings::g_misc.m_hud.m_keybinds.style.value == settings::misc::hud::keybinds_cfg::layout_style::pills) {
			auto& cfg = settings::g_misc.m_hud.m_keybinds;
			auto& dl = draw_list;

			const auto [vp_w, vp_h] = xdraw::viewport_size ();
			const auto screen_w = static_cast<float> (vp_w);
			const auto screen_hf = static_cast<float> (vp_h);

			constexpr auto pill_h {32.0f};
			constexpr auto pill_gap {7.0f};
			constexpr auto pad_x {14.0f};
			constexpr auto edge_w {3.0f};
			constexpr auto ring_r {9.0f};
			constexpr auto icon_s {14.0f};
			constexpr auto slide_in {26.0f}; // how far left a badge starts

			static std::array<pill_state, 32> states {};

			const auto dt = std::clamp (xdraw::delta_time (), 0.0f, 0.1f);
			const auto approach = [dt] (float current, float target, float speed) {
				return current + (target - current) * std::clamp (dt * speed, 0.0f, 1.0f);
			};

			for (auto& st : states)
				st.seen = false;

			// Icons are rasterised once and cached; load_svg goes through the
			// device, so doing it per frame per badge would be absurd.
			struct icon_slot { const char* svg; Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> tex; };
			static icon_slot icon_cache [] {
				{ svgs::bind_damage,  {} }, // 0
				{ svgs::bind_chance,  {} }, // 1
				{ svgs::bind_double,  {} }, // 2
				{ svgs::bind_force,   {} }, // 3
				{ svgs::bind_body,    {} }, // 4
				{ svgs::bind_left,    {} }, // 5
				{ svgs::bind_right,   {} }, // 6
				{ svgs::bind_move,    {} }, // 7
				{ svgs::bind_peek,    {} }, // 8
				{ svgs::bind_generic, {} }, // 9
			};

			for (auto& slot : icon_cache) {
				if (!slot.tex)
					slot.tex = xdraw::load_svg (slot.svg, 1.0f);
			}

			// A badge is only as compact as its label. Setting names are written
			// for the menu -- "min damage override", "force shot in air" -- and
			// putting those on a badge is what made the row stretch across the
			// screen. Each bind gets a short tag instead, matched longest-first so
			// "force left" is a manual and not a force shot.
			struct badge_style { const char* needle; const char* tag; int icon; };
			static constexpr badge_style k_styles [] {
				{ "min damage override",   "MD",  0 },
				{ "hit chance override",   "HC",  1 },
				{ "force shot on ground",  "FS",  3 },
				{ "force shot in air",     "FA",  3 },
				{ "force b-aim",           "BA",  4 },
				{ "force left",            "AA<", 5 },
				{ "force right",           "AA>", 6 },
				{ "doubletap",             "DT",  2 },
				{ "no spread",             "NS",  1 },
				{ "silent",                "SA",  1 },
				{ "thirdperson",           "TP",  8 },
				{ "quick peek",            "AP",  9 },
				{ "autopeek",              "AP",  9 },
				{ "duck peek",             "DP",  8 },
				{ "slowwalk",              "SW",  7 },
				{ "fastladder",            "FL",  7 },
				{ "jumpbug",               "JB",  7 },
				{ "edgejump",              "EJ",  7 },
				{ "edgestop",              "ES",  7 },
				{ "edgebug",               "EB",  7 },
				{ "hvh strafer",           "HS",  7 },
				{ "valve strafer",         "VS",  7 },
				{ "airstrafe",             "AS",  7 },
				{ "bhop",                  "BH",  7 },
				{ "auto stop",             "AS",  3 },
			};

			// Anything not in the table falls back to the initials of its words,
			// which still reads as a tag rather than a sentence.
			const auto style_for = [&] (const char* name, char* tag_out, std::size_t tag_cap) -> ID3D11ShaderResourceView* {
				for (const auto& st : k_styles) {
					if (std::strstr (name, st.needle)) {
						std::snprintf (tag_out, tag_cap, "%s", st.tag);
						return icon_cache [st.icon].tex.Get ();
					}
				}

				std::size_t out {0};
				bool at_word_start {true};
				for (const char* p = name; *p && out + 1 < tag_cap && out < 3; ++p) {
					if (*p == ' ' || *p == '-' || *p == '_') { at_word_start = true; continue; }
					if (at_word_start) {
						tag_out [out++] = static_cast<char> (std::toupper (static_cast<unsigned char> (*p)));
						at_word_start = false;
					}
				}
				tag_out [out] = '\0';
				return icon_cache [9].tex.Get ();
			};

			// Widest badge only decides the drag target; each badge draws at its own.
			auto block_w {0.0f};
			for (auto i = 0; i < count; ++i) {
				entries [i].icon = style_for (entries [i].name, entries [i].tag, sizeof (entries [i].tag));
				entries [i].name_size = xdraw::measure_text (entries [i].tag);

				auto w = edge_w + 5.0f + icon_s + 6.0f + entries [i].name_size.first + pad_x;
				if (entries [i].has_value_pill && cfg.show_values.value) {
					entries [i].value_size = xdraw::measure_text (entries [i].value);
					w += entries [i].value_size.first + 8.0f;
				}
				if (entries [i].ring >= 0.0f && cfg.show_rings.value)
					w += ring_r * 2.0f + 8.0f;

				entries [i].width = w;
				block_w = std::max (block_w, w);
			}

			// Bind each live entry to its slot, creating one on first sight. A new
			// badge starts fully out and with an empty ring, so pressing a damage
			// bind sweeps the ring up to its value instead of snapping there.
			for (auto i = 0; i < count; ++i) {
				pill_state* slot = nullptr;

				for (auto& st : states) {
					if (st.key == entries [i].key) { slot = &st; break; }
				}
				if (!slot) {
					for (auto& st : states) {
						if (!st.key) {
							slot = &st;
							*slot = pill_state {entries [i].key, 0.0f, 0.0f, 0.0f, 0.0f, false, false};
							break;
						}
					}
				}
				if (!slot)
					continue;

				slot->seen = true;
				entries [i].slot = slot;
			}

			const auto live_h = static_cast<float> (count) * pill_h
				+ std::max (0, count - 1) * pill_gap;

			auto origin_x = std::clamp (cfg.position_x.value, 0.0f, 1.0f) * screen_w;
			auto origin_y = std::clamp (cfg.position_y.value, 0.0f, 1.0f) * screen_hf;

			// Drag, but only with the menu open: the same click in game is a shot.
			{
				static bool dragging {false};
				static float grab_x {0.0f}, grab_y {0.0f};

				const auto& input = xui::ctx ().input;
				const xui::rect block {origin_x, origin_y, block_w, live_h};
				const bool over = input.in_rect (block);

				if (g_menu.is_open ()) {
					if (!dragging && over && input.mouse_clicked) {
						dragging = true;
						grab_x = input.mouse_x - origin_x;
						grab_y = input.mouse_y - origin_y;
					}
					if (dragging) {
						if (!input.mouse_down) {
							dragging = false;
						} else {
							origin_x = std::clamp (input.mouse_x - grab_x, 0.0f, std::max (screen_w - block_w, 0.0f));
							origin_y = std::clamp (input.mouse_y - grab_y, 0.0f, std::max (screen_hf - live_h, 0.0f));
							cfg.position_x.value = origin_x / std::max (screen_w, 1.0f);
							cfg.position_y.value = origin_y / std::max (screen_hf, 1.0f);
						}
					}
					if (over || dragging) {
						dl.rect (origin_x - 5.0f, origin_y - 5.0f, block_w + 10.0f, live_h + 10.0f,
							xdraw::color {255, 255, 255, 40}, xdraw::corner_radius {12.0f}, 1.0f);
					}
				} else {
					dragging = false;
				}
			}

			const auto accent = cfg.color.value;

			// Advance every slot, live or leaving, then draw. Leaving badges keep
			// their last row so they slide out from where they stood.
			auto row = 0;
			for (auto i = 0; i < count; ++i) {
				if (auto* st = entries [i].slot) {
					const auto target_y = origin_y + static_cast<float> (row) * (pill_h + pill_gap);
					st->y = st->placed ? approach (st->y, target_y, 16.0f) : target_y;
					st->placed = true;
					st->appear = approach (st->appear, 1.0f, 11.0f);
				st->width = entries [i].width;
					if (entries [i].ring >= 0.0f)
						st->ring = approach (st->ring, entries [i].ring, 9.0f);
					++row;
				}
			}

			const auto draw_badge = [&] (const pill_state& st, const char* name,
				const std::pair<float, float>& name_size, const char* value,
				const std::pair<float, float>& value_size, bool has_value, float ring_target,
				float badge_w, ID3D11ShaderResourceView* icon)
			{
				const auto fade = std::clamp (st.appear, 0.0f, 1.0f);
				if (fade <= 0.01f)
					return;

				// Ease-out so the slide decelerates into place rather than arriving
				// at constant speed.
				const auto eased = 1.0f - (1.0f - fade) * (1.0f - fade);
				const auto x = origin_x - slide_in * (1.0f - eased);
				const auto y = st.y;
				const auto a = master_alpha * fade;

				const auto tint = [a] (const xdraw::color& c, float scale) {
					return c.alpha (static_cast<std::uint8_t> (std::clamp (c.a * a * scale, 0.0f, 255.0f)));
				};

				const auto radius = pill_h * 0.5f;

				// The only glow in the interface: a soft contour on the badge, not
				// a halo behind the text.
				if (cfg.outer_glow.value) {
					auto& glow = xdraw::get_glow ();
					const auto strength = std::clamp (cfg.glow_strength.value, 0.0f, 2.0f);
					glow.rect (x - 1.0f, y - 1.0f, badge_w + 2.0f, pill_h + 2.0f,
						tint (accent, 0.20f * strength), xdraw::corner_radius {radius + 1.0f}, 2.5f);
				}

				dl.rect_filled (x, y, badge_w, pill_h,
					xdraw::color {14, 15, 19, static_cast<std::uint8_t> (150 * a)},
					xdraw::corner_radius {radius});
				dl.rect (x, y, badge_w, pill_h, tint (accent, 0.34f),
					xdraw::corner_radius {radius}, 1.0f);

				dl.rect_filled (x + 4.0f, y + 6.0f, edge_w, pill_h - 12.0f,
					tint (accent, 1.0f), xdraw::corner_radius {edge_w * 0.5f});

				if (icon) {
					dl.image (x + edge_w + 5.0f, y + (pill_h - icon_s) * 0.5f, icon_s, icon_s,
						icon, tint (accent, 1.0f));
				}

				// Label carries the accent too, as in the reference -- the badge
				// reads as one coloured object rather than white text on a tinted
				// chip.
				dl.text (x + edge_w + 5.0f + icon_s + 6.0f, y + (pill_h - name_size.second) * 0.5f,
					name, tint (accent, 1.0f));

				auto tail_x = x + badge_w - pad_x;

				if (ring_target >= 0.0f && cfg.show_rings.value) {
					const auto cx = tail_x - ring_r;
					const auto cy = y + pill_h * 0.5f;

					dl.circle (cx, cy, ring_r, tint (xdraw::color {255, 255, 255, 255}, 0.14f), 2.0f, 28);

					// No arc primitive in the renderer, so the filled portion is a
					// polyline walked from twelve o'clock clockwise.
					const auto shown = std::clamp (st.ring, 0.0f, 1.0f);
					if (shown > 0.001f) {
						const auto steps = std::max (2, static_cast<int> (36.0f * shown) + 1);
						std::vector<float> pts;
						pts.reserve (static_cast<std::size_t> (steps) * 2u);

						for (auto sgi = 0; sgi < steps; ++sgi) {
							const auto t = static_cast<float> (sgi) / static_cast<float> (steps - 1);
							const auto ang = -std::numbers::pi_v<float> * 0.5f
								+ t * shown * 2.0f * std::numbers::pi_v<float>;
							pts.push_back (cx + std::cosf (ang) * ring_r);
							pts.push_back (cy + std::sinf (ang) * ring_r);
						}

						if (pts.size () >= 4)
							dl.polyline (pts, tint (accent, 1.0f), false, 2.2f);
					}

					tail_x -= ring_r * 2.0f + 8.0f;
				}

				if (has_value && cfg.show_values.value) {
					dl.text (tail_x - value_size.first, y + (pill_h - value_size.second) * 0.5f,
						value, tint (accent, 1.0f));
				}
			};

			for (auto i = 0; i < count; ++i) {
				if (const auto* st = entries [i].slot) {
					draw_badge (*st, entries [i].tag, entries [i].name_size,
						entries [i].value, entries [i].value_size,
						entries [i].has_value_pill, entries [i].ring,
						entries [i].width, entries [i].icon);
				}
			}

			// Badges whose bind was released this frame slide back out and only
			// then give up their slot, so releasing a key is as smooth as pressing.
			for (auto& st : states) {
				if (!st.key || st.seen)
					continue;

				st.appear = approach (st.appear, 0.0f, 13.0f);
				if (st.appear <= 0.01f) {
					st = pill_state {};
					continue;
				}

				draw_badge (st, "", {0.0f, 0.0f}, "", {0.0f, 0.0f}, false, -1.0f, st.width, nullptr);
			}

			return;
		}

		constexpr auto row_h2 {30.0f};
		constexpr auto row_spacing2 {8.0f};
		constexpr auto header_h2 {28.0f};
		constexpr auto r2 {8.0f};
		constexpr auto pad_x2 {14.0f};

		float max_w = 130.0f;
		for (auto i = 0; i < count; ++i) {
			entries [i].name_size = xdraw::measure_text (entries [i].name);
			const auto nw = entries [i].name_size.first;
			float total = pad_x2 + 22.0f + nw + pad_x2;
			if (entries [i].has_value_pill) {
				entries [i].value_size = xdraw::measure_text (entries [i].value);
				total += entries [i].value_size.first + pad_x2;
			}
			const bool is_dt = std::strstr (entries [i].name, "doubletap") != nullptr || std::strstr (entries [i].name, "dt") != nullptr;
			if (is_dt) total += 40.0f;
			max_w = std::max (max_w, total);
		}

		const float panel_h = header_h2 + 10.0f + static_cast<float> (count) * (row_h2 + row_spacing2) + 6.0f;
		const float target_base_y = static_cast<float> (screen_h) * 0.5f - panel_h * 0.5f;
		smoothed_base_y.set_target (target_base_y);
		smoothed_base_y.update ();

		const auto base_y = smoothed_base_y.value ();
		const auto x = 16.0f;

		if (master_alpha > 0.01f && use_blur)
			draw_list.rect_filled_blurred (x, base_y, max_w, panel_h, xdraw::corner_radius {r2});

		draw_list.rect_filled_gradient (x, base_y, max_w, panel_h,
			xdraw::color {16, 20, 30, 200}, xdraw::color {10, 13, 20, 200},
			xdraw::color {10, 13, 20, 200}, xdraw::color {16, 20, 30, 200},
			xdraw::corner_radius {r2});
		draw_list.rect (x, base_y, max_w, panel_h, k_hud_border, xdraw::corner_radius {r2}, 1.2f);

		// Header
		const auto [hw, hh] = xdraw::measure_text ("KEYBINDS");
		draw_list.text (x + pad_x2, base_y + (header_h2 - hh) * 0.5f + 0.5f, "KEYBINDS",
			xdraw::color {k_accent.r, k_accent.g, k_accent.b, static_cast<std::uint8_t> (k_accent.a * master_alpha)});
		draw_list.rect_filled (x + pad_x2, base_y + header_h2 - 1.0f, max_w - pad_x2 * 2.0f, 1.0f,
			xdraw::color {0, 190, 255, static_cast<std::uint8_t> (60.0f * master_alpha)});

		float ry = base_y + header_h2 + 8.0f;
		for (auto i = 0; i < count; ++i) {
			const auto& e = entries [i];
			const float row_x = x + 8.0f;
			const float row_w = max_w - 16.0f;
			const float row_y = ry;

			draw_list.rect_filled (row_x, row_y, row_w, row_h2,
				xdraw::color {255, 255, 255, static_cast<std::uint8_t> (8.0f * master_alpha)}, xdraw::corner_radius {r2 - 2.0f});
			draw_list.rect (row_x, row_y, row_w, row_h2,
				xdraw::color {0, 190, 255, static_cast<std::uint8_t> (40.0f * master_alpha)}, xdraw::corner_radius {r2 - 2.0f}, 1.0f);

			const bool is_dt = std::strstr (e.name, "doubletap") != nullptr || std::strstr (e.name, "dt") != nullptr;

			if (is_dt) {
				const float cx = row_x + pad_x2 + 9.0f;
				const float cy = row_y + row_h2 * 0.5f;
				const float rad = 8.0f;

				// track
				draw_list.circle (cx, cy, rad,
					xdraw::color {255, 255, 255, static_cast<std::uint8_t> (40.0f * master_alpha)}, 1.2f);

				// real charge arc
				auto ring_col = xdraw::color {k_accent.r, k_accent.g, k_accent.b,
					static_cast<std::uint8_t> (k_accent.a * master_alpha)};
				if (dt_charge >= 0.999f)
					ring_col.a = static_cast<std::uint8_t> ((0.6f + 0.4f * std::sinf (dt_pulse)) * 255.0f * master_alpha);

				if (dt_charge > 0.001f) {
					const int segs = 44;
					const float start = -1.5707963f;
					const float endv = start + dt_charge * 6.2831853f;
					std::vector<float> pts; pts.reserve (static_cast<std::size_t> (segs + 1) * 2);
					for (int s = 0; s <= segs; ++s) {
						const float a = start + (endv - start) * static_cast<float> (s) / static_cast<float> (segs);
						pts.push_back (cx + std::cosf (a) * rad);
						pts.push_back (cy + std::sinf (a) * rad);
					}
					draw_list.polyline (pts, ring_col, false, 2.0f);
				}

				draw_list.text (cx + rad + 8.0f, row_y + (row_h2 - e.name_size.second) * 0.5f + 0.5f, e.name,
					xdraw::color {k_text_bright.r, k_text_bright.g, k_text_bright.b, static_cast<std::uint8_t> (k_text_bright.a * master_alpha)});

				char pct [16];
				std::snprintf (pct, sizeof (pct), "%d%%", static_cast<int> (dt_charge * 100.0f));
				const auto [pw, ph] = xdraw::measure_text (pct);
				draw_list.text (row_x + row_w - pad_x2 - pw, row_y + (row_h2 - ph) * 0.5f + 0.5f, pct,
					dt_charge >= 0.999f
						? xdraw::color {k_accent.r, k_accent.g, k_accent.b, static_cast<std::uint8_t> (k_accent.a * master_alpha)}
						: xdraw::color {k_text_dim.r, k_text_dim.g, k_text_dim.b, static_cast<std::uint8_t> (k_text_dim.a * master_alpha)});
			} else {
				// status dot
				draw_list.circle_filled (row_x + pad_x2 + 3.0f, row_y + row_h2 * 0.5f, 3.0f,
					xdraw::color {k_accent.r, k_accent.g, k_accent.b, static_cast<std::uint8_t> (k_accent.a * master_alpha)}, 8);

				draw_list.text (row_x + pad_x2 + 14.0f, row_y + (row_h2 - e.name_size.second) * 0.5f + 0.5f, e.name,
					xdraw::color {k_text_bright.r, k_text_bright.g, k_text_bright.b, static_cast<std::uint8_t> (k_text_bright.a * master_alpha)});

				if (e.has_value_pill) {
					const auto [vw, vh] = e.value_size;
					draw_list.text (row_x + row_w - pad_x2 - vw, row_y + (row_h2 - vh) * 0.5f + 0.5f, e.value,
						xdraw::color {k_text_dim.r, k_text_dim.g, k_text_dim.b, static_cast<std::uint8_t> (k_text_dim.a * master_alpha)});
				}
			}

			ry += row_h2 + row_spacing2;
		}
	}

	void hud_widgets::spectators (xdraw::draw_list& draw_list) {
		using namespace darkfox_style;

		if (!settings::g_misc.m_spectators.enabled.value)
			return;

		static animation::fade container_alpha;
		static animation::spring smoothed_base_y;

		const auto local = systems::g_local.get ();
		if (!local.is_alive || !local.controller)
			return;

		struct spec_info {
			char name [128];
			int ping;
		};

		spec_info specs [32] {};
		auto count {0};

		const auto& players = systems::g_entities.get_by_type (systems::entities::type::player);
		for (const auto& player : players) {
			if (player.ptr == local.controller || count >= 32)
				continue;

			if (memory::read<bool> (player.ptr + SCHEMA ("CCSPlayerController", "m_bPawnIsAlive"_hash)))
				continue;

			const auto obs_pawn_handle = memory::read<std::uint32_t> (player.ptr + SCHEMA ("CCSPlayerController", "m_hObserverPawn"_hash));
			if (!obs_pawn_handle || obs_pawn_handle == 0xffffffff)
				continue;

			const auto obs_pawn = systems::g_entities.lookup (obs_pawn_handle);
			if (!obs_pawn)
				continue;

			const auto observer_services = memory::safe_read<std::uintptr_t> (obs_pawn + SCHEMA ("C_BasePlayerPawn", "m_pObserverServices"_hash)).value_or (0);
			if (!observer_services || (observer_services >> 48) != 0)
				continue;

			const auto observer_target_handle = memory::safe_read<std::uint32_t> (observer_services + SCHEMA ("CPlayer_ObserverServices", "m_hObserverTarget"_hash)).value_or (0);
			if (!observer_target_handle)
				continue;

			const auto observer_target = systems::g_entities.lookup (observer_target_handle);
			if (observer_target != local.pawn)
				continue;

			const auto name_ptr = memory::read<std::uintptr_t> (player.ptr + SCHEMA ("CCSPlayerController", "m_sSanitizedPlayerName"_hash));
			if (name_ptr) {
				const auto nm = memory::read_string (name_ptr);
				std::strncpy (specs [count].name, nm.c_str (), sizeof (specs [count].name) - 1);
				specs [count].name [sizeof (specs [count].name) - 1] = '\0';
			} else {
				std::strcpy (specs [count].name, "Unknown");
			}

			specs [count].ping = memory::read<std::uint32_t> (player.ptr + SCHEMA ("CCSPlayerController", "m_iPing"_hash));
			++count;
		}

		if (count > 0)
			container_alpha.fade_in (0.2f);
		else
			container_alpha.fade_out (0.2f);

		container_alpha.update ();
		if (!container_alpha.visible ())
			return;

		const auto master_alpha = container_alpha.alpha ();
		const auto [screen_w, screen_h] = xdraw::viewport_size ();
		const bool use_blur = settings::g_misc.m_interface.hud_blur.value;

		constexpr auto row_h {28.0f};
		constexpr auto row_spacing {5.0f};
		constexpr auto header_h {30.0f};
		constexpr auto r {8.0f};
		constexpr auto pad_x {14.0f};
		constexpr auto margin_x {16.0f};

		float max_w = 180.0f;
		for (auto i = 0; i < count; ++i) {
			const auto [nw, nh] = xdraw::measure_text (specs [i].name);
			max_w = std::max (max_w, nw + pad_x * 2.0f + 60.0f);
		}

		const float panel_h = header_h + 10.0f + static_cast<float> (count) * (row_h + row_spacing);
		const float target_base_y = static_cast<float> (screen_h) * 0.5f - panel_h * 0.5f;

		smoothed_base_y.set_target (target_base_y);
		smoothed_base_y.update ();

		const auto x = static_cast<float> (screen_w) - max_w - margin_x;
		const auto base_y = smoothed_base_y.value ();

		const auto spec_accent = settings::g_misc.m_spectators.color.value;
		const auto tint = [master_alpha] (const xdraw::color& c, float scale) {
			return c.alpha (static_cast<std::uint8_t> (std::clamp (c.a * master_alpha * scale, 0.0f, 255.0f)));
		};

		draw_list.rect_filled (x, base_y, max_w, panel_h,
			xdraw::color {14, 15, 19, static_cast<std::uint8_t> (150 * master_alpha)},
			xdraw::corner_radius {r});
		draw_list.rect (x, base_y, max_w, panel_h, tint (spec_accent, 0.34f),
			xdraw::corner_radius {r}, 1.0f);

		// Leading stripe, matching the hotkey badges and the watermark.
		draw_list.rect_filled (x + 4.0f, base_y + 6.0f, 3.0f, panel_h - 12.0f,
			tint (spec_accent, 1.0f), xdraw::corner_radius {1.5f});

		const auto [hw, hh] = xdraw::measure_text ("SPECTATORS");
		draw_list.text (x + pad_x, base_y + (header_h - hh) * 0.5f + 0.5f, "SPECTATORS",
			tint (spec_accent, 1.0f));

		draw_list.rect_filled (x + pad_x, base_y + header_h - 1.0f, max_w - pad_x * 2.0f, 1.0f,
			tint (spec_accent, 0.5f));

		float ry = base_y + header_h + 8.0f;
		for (auto i = 0; i < count; ++i) {
			const float row_x = x + 8.0f;
			const float row_w = max_w - 16.0f;
			const float row_y = ry;

			draw_list.rect_filled (row_x, row_y, row_w, row_h,
				xdraw::color {255, 255, 255, static_cast<std::uint8_t> (10.0f * master_alpha)},
				xdraw::corner_radius {row_h * 0.5f});
			draw_list.rect (row_x, row_y, row_w, row_h, tint (spec_accent, 0.22f),
				xdraw::corner_radius {row_h * 0.5f}, 1.0f);

			const auto [nw, nh] = xdraw::measure_text (specs [i].name);
			draw_list.text (row_x + pad_x, row_y + (row_h - nh) * 0.5f + 0.5f, specs [i].name,
				xdraw::color {k_text_bright.r, k_text_bright.g, k_text_bright.b, static_cast<std::uint8_t> (k_text_bright.a * master_alpha)});

			char ping_buf [16];
			std::snprintf (ping_buf, sizeof (ping_buf), "%dms", specs [i].ping);
			const auto [pw, ph] = xdraw::measure_text (ping_buf);
			draw_list.text (row_x + row_w - pad_x - pw, row_y + (row_h - ph) * 0.5f + 0.5f, ping_buf,
				xdraw::color {k_text_dim.r, k_text_dim.g, k_text_dim.b, static_cast<std::uint8_t> (k_text_dim.a * master_alpha)});

			ry += row_h + row_spacing;
		}
	}

	void notifications::add (const std::string& text, float duration) {
		this->add_hitlog (text, "", 0, duration);
	}

	void notifications::add_hitlog (const std::string& text, const std::string& subtext, int icon_type, float duration) {
		notification_entry entry {};
		entry.text = text;
		entry.subtext = subtext;
		entry.icon_type = icon_type;
		entry.duration = duration;
		entry.elapsed = 0.0f;
		m_notifications.push_back (entry);
	}

	void notifications::draw () {
		if (m_notifications.empty ())
			return;

		const auto [screen_w, screen_h] = xdraw::viewport_size ();
		auto& dl = xdraw::get (xdraw::layer::top);
		const auto dt = xdraw::delta_time ();

		constexpr auto notify_r {6.0f};
		constexpr auto text_pad {12.0f};
		constexpr auto margin_left {20.0f};
		constexpr auto margin_bottom {120.0f};

		auto y = static_cast<float>(screen_h) - margin_bottom;

		for (auto it = m_notifications.begin (); it != m_notifications.end (); ) {
			it->elapsed += dt;

			if (it->elapsed >= it->duration) {
				it = m_notifications.erase (it);
				continue;
			}

			const auto alpha = std::clamp (1.0f - (it->elapsed / it->duration), 0.0f, 1.0f);
			const auto fade_alpha = alpha * alpha;

			const auto [tw, th] = xdraw::measure_text (it->text.c_str ());
			const auto [stw, sth] = it->subtext.empty() ? std::pair{0.0f, 0.0f} : xdraw::measure_text (it->subtext.c_str ());

			const float icon_w = 22.0f;
			const float max_text_w = std::max(tw, stw);
			const float notify_w = icon_w + text_pad + max_text_w + text_pad * 2.0f;
			const float notify_h = it->subtext.empty() ? 28.0f : 44.0f;

			const auto x = margin_left;

			// Dark translucent background (#0A0D14)
			auto bg = xdraw::color {10, 13, 20, static_cast<std::uint8_t>(220.0f * fade_alpha)};
			dl.rect_filled (x, y - notify_h, notify_w, notify_h, bg, xdraw::corner_radius {notify_r});

			// Glowing thin cyan outline border
			auto border = xdraw::color {0, 190, 255, static_cast<std::uint8_t>(160.0f * fade_alpha)};
			dl.rect (x, y - notify_h, notify_w, notify_h, border, xdraw::corner_radius {notify_r}, 1.0f);

			// Icon on left
			const char* icon_str = "(+)";
			xdraw::color icon_col = xdraw::color{ 0, 190, 255, static_cast<std::uint8_t>(255.0f * fade_alpha) };
			if (it->icon_type == 1) { icon_str = "(o)"; icon_col = xdraw::color{ 240, 200, 60, static_cast<std::uint8_t>(255.0f * fade_alpha) }; }
			else if (it->icon_type == 2) { icon_str = "(!)"; icon_col = xdraw::color{ 230, 80, 80, static_cast<std::uint8_t>(255.0f * fade_alpha) }; }

			dl.text (x + text_pad, y - notify_h + 6.0f, icon_str, icon_col);

			// Main text
			auto text_col = xdraw::color {255, 255, 255, static_cast<std::uint8_t>(255.0f * fade_alpha)};
			dl.text (x + text_pad + icon_w, y - notify_h + 6.0f, it->text.c_str (), text_col);

			// Subline text (if mismatch/miss)
			if (!it->subtext.empty()) {
				auto sub_col = xdraw::color {160, 170, 190, static_cast<std::uint8_t>(200.0f * fade_alpha)};
				dl.text (x + text_pad + icon_w, y - notify_h + 24.0f, it->subtext.c_str (), sub_col);
			}

			y -= notify_h + 6.0f;
			++it;
		}
	}

} // namespace rendering
