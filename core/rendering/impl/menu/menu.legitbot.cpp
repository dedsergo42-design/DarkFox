#include <pch/pch.hpp>
#include <core/settings.hpp>

#include "../../rendering.hpp"
#include "menu.widgets.hpp"
#include "theme.hpp"
#include "ui.hpp"

namespace rendering {

	namespace detail {

		constexpr const char* hitbox_names_legit [] {"head", "chest", "stomach", "arms", "legs"};

	} // namespace detail

	void menu::draw_legitbot (float group_w) const {
		auto& s = settings::g_combat;
		auto& lb = s.m_legitbot;
				if ( this->m_subtab == -1 ) {
			this->draw_legitbot_general ( group_w );
			return;
		}

		auto& wg = lb.groups [this->m_subtab];

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = this->m_body_x;
		const auto body_y = this->m_body_y;
		const auto content_w = this->m_body_w;
		const auto col_w = (content_w - theme::metric::space_md) * 0.5f;
		const auto right_x = content_x + col_w + theme::metric::space_md;

		xui::layout::set_cursor (content_x - wx, body_y - wy);

		if (xui::begin_child ("##legitbot_master", lb.enabled.value ? col_w : content_w)) {
			xui::checkbox ("enabled", lb.enabled);
			xui::end_child ();
		}

		if (!lb.enabled.value) {
			return;
		}

		if (xui::begin_child ("##legitbot_aimbot", col_w)) {
			xui::checkbox ("aimbot", wg.aimbot);

			xui::slider_float ("fov", wg.fov, 0.5f, 30.0f, "%.1f°");
			xui::slider_int ("smooth", wg.smooth, 0, 100, "%d");
			xui::multicombo ("hitboxes", wg.hitboxes, detail::hitbox_names_legit, 5);

			xui::checkbox ("auto stop", wg.autostop);
			if (xui::begin_popup ("##autostop_popup", 240.0f)) {
				constexpr const char* autostop_modes [] {"standard", "smooth", "predict"};
				xui::combo ("mode##autostop", wg.autostop_mode_value.value, autostop_modes, 3);
				xui::slider_int ("min speed##autostop", wg.autostop_min_speed, 1, 100, "%d u/s");
				xui::end_popup ();
			}

			xui::checkbox ("visualize fov", wg.visualize_fov);

			if (xui::begin_popup ("##fov_color_popup", 220.0f)) {
				xui::color_picker ("color##fov", wg.fov_color);
				xui::end_popup ();
			}

			xui::end_child ();
		}

		if (xui::begin_child ("##legitbot_rcs", col_w)) {
			xui::checkbox ("recoil control", wg.rcs);
			if (xui::begin_popup ("##rcs_popup", 220.0f)) {
				xui::slider_int ("min##rcs", wg.rcs_min, 50, 150, "%d%%");
				xui::slider_int ("max##rcs", wg.rcs_max, 50, 150, "%d%%");
				xui::end_popup ();
			}

			xui::checkbox ("standalone rcs", wg.standalone_rcs);
			if (xui::begin_popup ("##srcs_popup", 220.0f)) {
				xui::slider_int ("strength##srcs", wg.standalone_rcs_strength, 0, 100, "%d%%");
				xui::slider_int ("min##srcs", wg.standalone_rcs_min, 50, 150, "%d%%");
				xui::slider_int ("max##srcs", wg.standalone_rcs_max, 50, 150, "%d%%");
				xui::end_popup ();
			}

			xui::end_child ();
		}

		xui::layout::set_cursor (right_x - wx, body_y - wy);

		if (xui::begin_child ("##legitbot_triggerbot", col_w)) {
			xui::checkbox ("triggerbot", wg.triggerbot);
			xui::slider_int ("delay", wg.trigger_delay, 0, 250, "%d ms");
			xui::slider_int ("hitchance", wg.trigger_hitchance, 0, 100, "%d%%");
			xui::checkbox ("head only", wg.trigger_head_only);
			xui::checkbox ("seeded", wg.give_me_your_seed);

			xui::end_child ();
		}

		if (xui::begin_child ("##legitbot_other", col_w)) {
			xui::checkbox ("autowall", wg.autowall);
			if (xui::begin_popup ("##aw_popup", 220.0f)) {
				xui::slider_int ("min damage##aw", wg.min_damage, 1, 125, "%d");
				xui::end_popup ();
			}

			xui::end_child ();
		}
	}


		void menu::draw_legitbot_general (float group_w) const {
			auto& s = settings::g_combat;
			auto& lb = s.m_legitbot;
			auto& gen = lb.general;

			const auto wx = this->m_x;
			const auto wy = this->m_y;
			const auto content_x = this->m_body_x;
			const auto body_y = this->m_body_y;
			const auto content_w = this->m_body_w;
			const auto col_w = (content_w - theme::metric::space_md) * 0.5f;

			xui::layout::set_cursor (content_x - wx, body_y - wy);

			if (xui::begin_child ("##legitbot_general", col_w)) {
				xui::checkbox ("Enable legitbot", gen.enabled);
				xui::slider_int ("FOV", gen.fov, 0, 30);
				xui::slider_int ("Smoothness", gen.smoothness, 0, 100);
				xui::checkbox ("Auto fire", gen.auto_fire);
				xui::slider_int ("Min damage", gen.min_damage, 0, 130);
				xui::end_child ();
			}
		}


} // namespace rendering