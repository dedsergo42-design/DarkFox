#include <pch/pch.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

#include "../../rendering.hpp"
#include "menu.widgets.hpp"
#include "theme.hpp"
#include "ui.hpp"

namespace rendering {

	void menu::draw_world (float group_w) const {
		auto& w = settings::g_world;

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = this->m_body_x;
		const auto body_y = this->m_body_y;
		const auto content_w = this->m_body_w;
		const auto col_w = (content_w - theme::metric::space_md) * 0.5f;
		const auto right_x = content_x + col_w + theme::metric::space_md;

		const auto subtab = this->m_subtab;

		xui::layout::set_cursor (content_x - wx, body_y - wy);

		if (subtab == 0) {
			auto& item = settings::g_esp.m_item;
			auto& proj = settings::g_esp.m_projectile;
			auto& other = settings::g_esp.m_other;

			constexpr const char* display_types [] {"text", "icon", "text + icon"};
			constexpr const char* cham_material_names [] {
				"liquid", "metallic", "matte", "flat", "bloom", "outlines", "glow", "electric", "distortion", "hologram", "pearl",
				"liquid (iz)", "matte (iz)", "flat (iz)", "bloom (iz)", "outlines (iz)", "glow (iz)", "distortion (iz)", "hologram (iz)"
			};
			constexpr auto cham_material_count = static_cast<int>(settings::esp::cham_ids::count);

			auto draw_chams_layer = [&] (const char* label, const char* popup_id, settings::esp::chams_layer& layer) {
				xui::checkbox (label, layer.enabled);
				if (xui::begin_popup (popup_id, 220.0f)) {
					xui::combo ("material", layer.material.value, cham_material_names, cham_material_count);
					xui::color_picker ("color", layer.color);
					xui::end_popup ();
				}
			};

			xui::layout::set_cursor (content_x - wx, body_y - wy);

			if (ui::card_begin ("Items", col_w)) {
				static int item_group {};
				xui::combo ("group##item_sel", item_group, settings::esp::item::k_group_names, settings::esp::item::k_group_count);

				xui::layout::separator ();

				xui::checkbox ("item esp", item.m_overlay.group_toggle (item_group));
				if (xui::begin_popup ("##ie_grp_cfg", 220.0f)) {
					auto& g = item.m_overlay.groups [item_group];

					char id_d [32] {}, id_m [32] {}, id_t [32] {}, id_i [32] {};
					std::snprintf (id_d, sizeof (id_d), "display##ie%d", item_group);
					std::snprintf (id_m, sizeof (id_m), "max dist##ie%d", item_group);
					std::snprintf (id_t, sizeof (id_t), "text color##ie%d", item_group);
					std::snprintf (id_i, sizeof (id_i), "icon color##ie%d", item_group);

					xui::combo (id_d, g.display.value, display_types, 3);
					xui::slider_float (id_m, g.max_distance, 1.0f, 200.0f, "%.0fm");
					xui::color_picker (id_t, g.text_color);
					xui::color_picker (id_i, g.icon_color);
					xui::end_popup ();
				}

				xui::checkbox ("item chams", item.m_chams.group_toggle (item_group));
				if (xui::begin_popup ("##ic_grp_cfg", 220.0f)) {
					auto& g = item.m_chams.groups [item_group];

					char id_p [48] {}, id_pp [48] {}, id_s [48] {}, id_sp [48] {};
					std::snprintf (id_p, sizeof (id_p), "primary##ic%d", item_group);
					std::snprintf (id_pp, sizeof (id_pp), "##ic_p%d", item_group);
					std::snprintf (id_s, sizeof (id_s), "secondary##ic%d", item_group);
					std::snprintf (id_sp, sizeof (id_sp), "##ic_s%d", item_group);

					draw_chams_layer (id_p, id_pp, g.primary);
					draw_chams_layer (id_s, id_sp, g.secondary);
					xui::end_popup ();
				}

				xui::checkbox ("item glow", item.m_glow.group_toggle (item_group));
				if (xui::begin_popup ("##ig_grp_cfg", 220.0f)) {
					char id [32] {};
					std::snprintf (id, sizeof (id), "color##ig%d", item_group);
					xui::color_picker (id, item.m_glow.groups [item_group].color);
					xui::end_popup ();
				}
			}
			ui::card_end ();

			xui::layout::set_cursor (right_x - wx, body_y - wy);

			if (ui::card_begin ("Projectiles", col_w)) {
				static auto proj_group {0};
				xui::combo ("group##proj_sel", proj_group, settings::esp::projectile::k_group_names, settings::esp::projectile::k_group_count);

				xui::layout::separator ();

				const auto is_inferno = (proj_group == 5);

				xui::checkbox (is_inferno ? "inferno esp" : "projectile esp", proj.m_overlay.group_toggle (proj_group));

				if (!is_inferno) {
					if (xui::begin_popup ("##pe_grp_cfg", 220.0f)) {
						auto& g = proj.m_overlay.groups [proj_group];

						char id_d [32] {}, id_m [32] {}, id_t [32] {}, id_i [32] {};
						std::snprintf (id_d, sizeof (id_d), "display##pe%d", proj_group);
						std::snprintf (id_m, sizeof (id_m), "max dist##pe%d", proj_group);
						std::snprintf (id_t, sizeof (id_t), "text color##pe%d", proj_group);
						std::snprintf (id_i, sizeof (id_i), "icon color##pe%d", proj_group);

						xui::combo (id_d, g.display.value, display_types, 3);
						xui::slider_float (id_m, g.max_distance, 1.0f, 200.0f, "%.0fm");
						xui::color_picker (id_t, g.text_color);
						xui::color_picker (id_i, g.icon_color);
						xui::end_popup ();
					}
				} else {
					if (xui::begin_popup ("##inferno_cfg", 220.0f)) {
						xui::color_picker ("fill color##inf", proj.m_overlay.m_infernos.fill_color);
						xui::color_picker ("outline color##inf", proj.m_overlay.m_infernos.outline_color);
						xui::slider_float ("outline thickness##inf", proj.m_overlay.m_infernos.outline_thickness, 0.5f, 5.0f, "%.1f");
						xui::checkbox ("glow##inf", proj.m_overlay.m_infernos.glow);
						xui::slider_float ("glow strength##inf", proj.m_overlay.m_infernos.glow_strength, 0.1f, 1.0f, "%.2f");
						xui::end_popup ();
					}
				}

				const auto indicator_id = proj_group == 0 ? 0 : proj_group == 3 ? 1 : proj_group == 5 ? 2 : -1;
				if (indicator_id >= 0) {
					xui::checkbox (is_inferno ? "indicator" : "landing indicator", proj.m_overlay.m_indicator.get_group (indicator_id).enabled);
					if (xui::begin_popup ("##ind_grp_cfg", 220.0f)) {
						auto& g = proj.m_overlay.m_indicator.get_group (indicator_id);

						char id_a [32] {}, id_i [32] {}, id_b [32] {}, id_g [32] {}, id_gs [32] {};
						std::snprintf (id_a, sizeof (id_a), "arc color##ind%d", indicator_id);
						std::snprintf (id_i, sizeof (id_i), "icon color##ind%d", indicator_id);
						std::snprintf (id_b, sizeof (id_b), "background##ind%d", indicator_id);
						std::snprintf (id_g, sizeof (id_g), "glow##ind%d", indicator_id);
						std::snprintf (id_gs, sizeof (id_gs), "glow strength##ind%d", indicator_id);

						xui::color_picker (id_a, g.arc_color);
						xui::color_picker (id_i, g.icon_color);
						xui::color_picker (id_b, g.background_color);
						xui::checkbox (id_g, g.glow);
						xui::slider_float (id_gs, g.glow_strength, 0.1f, 1.0f, "%.2f");
						xui::end_popup ();
					}
				}
			}
			ui::card_end ();

			if (ui::card_begin ("Other", col_w)) {
				xui::checkbox ("bomb timer", other.bomb_timer);
				xui::checkbox ("spectator list", other.spectator_list);
				if (xui::begin_popup ("##spectators_popup", 220.0f)) {
					xui::color_picker ("color##spec", settings::g_misc.m_spectators.color);
					xui::end_popup ();
				}

				xui::checkbox ("penetration wall", w.m_penetration_wall.enabled);
				if (xui::begin_popup ("##penwall_popup", 250.0f)) {
					xui::color_picker ("color##penwall", w.m_penetration_wall.color);
					xui::slider_float ("area##penwall", w.m_penetration_wall.fov, 4.0f, 60.0f, "%.0f°");
					xui::slider_int ("density##penwall", w.m_penetration_wall.density, 3, 15, "%d");
					xui::slider_float ("min damage##penwall", w.m_penetration_wall.min_damage, 1.0f, 100.0f, "%.0f");
					xui::checkbox ("scale by damage##penwall", w.m_penetration_wall.scale_by_damage);
					xui::end_popup ();
				}
			}
			ui::card_end ();
		}

		if (subtab == 1) {
			auto& scene = w.m_scene;

			if (ui::card_begin ("Scene", col_w)) {
				xui::checkbox ("skybox material", scene.skybox.custom_skybox);
				if (xui::begin_popup ("##skybox_popup", 220.0f)) {
					const auto& skyboxes = features::world::g_scene.get_skyboxes ();
					if (!skyboxes.empty ()) {
						std::vector<const char*> names;
						names.reserve (skyboxes.size ());
						for (const auto& skybox : skyboxes) {
							names.push_back (skybox.display_name.c_str ());
						}

						scene.skybox.selected_skybox.value = std::clamp (
							scene.skybox.selected_skybox.value, 0,
							static_cast<int>(skyboxes.size ()) - 1);
						xui::combo (
							"skybox", scene.skybox.selected_skybox.value,
							names.data (), static_cast<int>(names.size ()));
					}
					xui::end_popup ();
				}

				xui::checkbox ("skybox color", scene.skybox.custom_color);
				if (xui::begin_popup ("##skycolor_popup", 220.0f)) {
					xui::color_picker ("sky color", scene.skybox.skybox_color);
					xui::color_picker ("cloud color", scene.skybox.cloud_color);
					xui::color_picker ("sun color", scene.skybox.sun_color);
					xui::end_popup ();
				}

				xui::checkbox ("world color", scene.world_setting);
				if (xui::begin_popup ("##worldcolor_popup", 220.0f)) {
					xui::color_picker ("color##world", scene.world_color);
					xui::end_popup ();
				}

				xui::checkbox ("lighting", scene.lighting);
				if (xui::begin_popup ("##lighting_popup", 220.0f)) {
					xui::slider_float ("intensity##light", scene.lighting_intensity, 0.0f, 2.0f, "%.2f");
					xui::color_picker ("color##light", scene.lighting_color);
					xui::end_popup ();
				}

				xui::checkbox ("bloom", scene.bloom);
				if (xui::begin_popup ("##bloom_popup", 220.0f)) {
					xui::slider_float ("value##bloom", scene.bloom_value, 0.0f, 2.0f, "%.2f");
					xui::end_popup ();
				}

				xui::checkbox ("gamma", scene.gamma);
				if (xui::begin_popup ("##gamma_popup", 220.0f)) {
					xui::slider_float ("value##gamma", scene.gamma_value, 0.5f, 5.0f, "%.1f");
					xui::end_popup ();
				}

				xui::checkbox ("depth of field", scene.dof);
				if (xui::begin_popup ("##dof_popup", 260.0f)) {
					xui::slider_float ("near blurry##dof", scene.dof_near_blurry, 0.0f, 50.0f, "%.1f");
					xui::slider_float ("near crisp##dof", scene.dof_near_crisp, 0.0f, 50.0f, "%.1f");
					xui::slider_float ("far crisp##dof", scene.dof_far_crisp, 100.0f, 2000.0f, "%.0f");
					xui::slider_float ("far blurry##dof", scene.dof_far_blurry, 100.0f, 3000.0f, "%.0f");
					xui::end_popup ();
				}
			}
			ui::card_end ();
		}

		if (subtab == 2) {
			auto& weather = w.m_weather;

			if (ui::card_begin ("Weather", col_w)) {
				xui::checkbox ("weather", weather.enabled);
				if (xui::begin_popup ("##weather_popup", 220.0f)) {
					constexpr const char* weather_types [] {"snow", "rain", "stars"};
					xui::combo ("type", weather.type.value, weather_types, 3);

					xui::color_picker ("color##weather", weather.color);
					xui::end_popup ();
				}

				xui::checkbox ("fog", weather.fog_enabled);
				if (xui::begin_popup ("##fog_popup", 220.0f)) {
					xui::slider_float ("density##fog", weather.fog_density, 0.0f, 1.0f, "%.2f");
					xui::slider_float ("anisotropy", weather.fog_anisotropy, 0.0f, 1.0f, "%.2f");
					xui::slider_float ("draw distance", weather.fog_draw_distance, 500.0f, 20000.0f, "%.0f");
					xui::color_picker ("color##fog", weather.fog_color);
					xui::end_popup ();
				}

				xui::checkbox ("wetness", weather.wetness);
				if (xui::begin_popup ("##wetness_popup", 220.0f)) {
					xui::slider_float ("density##wet", weather.wetness_density, 0.0f, 5.0f, "%.1f");
					xui::slider_float ("speed##wet", weather.wetness_speed, 0.0f, 3.0f, "%.1f");
					xui::end_popup ();
				}

				xui::checkbox ("wind", weather.wind);
				if (xui::begin_popup ("##wind_popup", 220.0f)) {
					xui::slider_float ("strength##wind", weather.wind_strength, 0.0f, 5.0f, "%.1f");
					xui::slider_float ("direction##wind", weather.wind_direction, 0.0f, 360.0f, "%.0f");
					xui::slider_float ("turbulence##wind", weather.wind_turbulence, 0.0f, 5.0f, "%.1f");
					xui::end_popup ();
				}
			}
			ui::card_end ();
		}
	}

} // namespace rendering