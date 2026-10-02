#include <pch/pch.hpp>
#include <core/settings.hpp>

#include "../../rendering.hpp"
#include "menu.widgets.hpp"
#include "ui.hpp"

namespace rendering {

	namespace detail {

		constexpr const char* hitbox_names[ ]{ "head", "chest", "stomach", "arms", "legs", "paws" };
		constexpr const char* pitch_items[ ]{ "none", "down", "up", "zero", "custom" };
		constexpr const char* yaw_items[ ]{ "fixed", "jitter", "spam", "random", "local view" };
		constexpr const char* spin_dirs[ ]{ "right", "left", "alternating" };
		constexpr const char* autostop_modes[ ]{ "auto", "early", "in air", "on ground" };

		// Имя группы оружия для заголовка карточки. Индекс совпадает с порядком
		// в settings::combat::ragebot::weapon_names, который задаёт и меню, и
		// config-категории ("ragebot - rifle").
		constexpr const char* weapon_group_names[ 6 ]{ "Pistols", "SMGs", "Rifles", "Shotguns", "Snipers", "LMGs" };

		[[nodiscard]] inline const char* weapon_group_name( int index )
		{
			return ( index >= 0 && index < 6 ) ? weapon_group_names[ index ] : "Weapon";
		}

		// Столбец карточек. Раньше обе колонки считались от m_body_w с
		// tokens::gap, и треть ширины уходила в никуда. Теперь ширина -- от
		// доступного места с учётом отступа между колонками.
		[[nodiscard]] inline float column_width( float body_w )
		{
			return ( body_w - theme::metric::space_md ) * 0.5f;
		}

	} // namespace detail

	void menu::draw_ragebot( float group_w ) const
	{
		( void )group_w;

		auto& s = settings::g_combat;
		auto& rb = s.m_ragebot;
		auto& aa = s.m_antiaim;
		auto& qp = s.m_quickpeek;
		auto& dp = s.m_duckpeek;
		auto& zb = s.m_zeusbot;
		auto& kb = s.m_knifebot;
		auto& autos = s.m_autos;
		auto& lg = s.m_lagcomp;

		if ( this->m_subtab == -1 )
		{
			this->draw_ragebot_general( 0.0f );
			return;
		}

		// Индекс группы -- это и есть subtab, но защищаемся от выхода за
		// границы: m_subtab приходит из таблицы шелла и переживает правки
		// списка оружия.
		if ( this->m_subtab < 0 || this->m_subtab >= static_cast< int >( settings::combat::ragebot::k_group_count ) )
		{
			this->draw_ragebot_general( 0.0f );
			return;
		}

		auto& wg = rb.groups[ this->m_subtab ];

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = this->m_body_x;
		const auto body_y = this->m_body_y;
		const auto content_w = this->m_body_w;
		const auto col_w = detail::column_width( content_w );
		const auto right_x = content_x + col_w + theme::metric::space_md;

		// ================ ЛЕВАЯ КОЛОНКА ================
		xui::layout::set_cursor( content_x - wx, body_y - wy );

		// ---- Карточка: Aim ----
		if ( xui::begin_child( "##rage_aim", col_w, 0.0f, false ) )
		{
			ui::card_begin( "Aim", col_w );

			ui::checkbox( "enabled", rb.enabled );
			ui::checkbox( "silent", wg.silent );
			ui::checkbox( "auto fire", s.m_ragebot.general.auto_fire );

			ui::slider_float( "max fov", wg.max_fov.value, 1.0f, 180.0f, "%.0f" );
			ui::slider_int( "hit chance", wg.hitchance.value, 0, 100, "%d%%" );
			ui::slider_int( "min damage", wg.min_damage.value, 1, 130, "%d" );
			ui::slider_float( "point scale", wg.pointscale.value, 0.0f, 100.0f, "%.0f%%" );

			ui::checkbox( "dynamic point scale", wg.dynamic_pointscale );

			ui::divider( );

			ui::checkbox( "hit chance override", wg.hitchance_override );
			if ( wg.hitchance_override.value )
				ui::slider_int( "override##hc", wg.hitchance_override_value.value, 0, 100, "%d%%" );

			ui::checkbox( "min damage override", wg.min_damage_override );
			if ( wg.min_damage_override.value )
				ui::slider_int( "override##md", wg.min_damage_override_value.value, 1, 130, "%d" );

			ui::card_end( );
			xui::end_child( );
		}

		// ---- Карточка: Accuracy ----
		if ( xui::begin_child( "##rage_accuracy", col_w, 0.0f, false ) )
		{
			ui::card_begin( "Accuracy", col_w );

			ui::checkbox( "no spread", wg.no_spread );
			if ( wg.no_spread.value )
			{
				ui::checkbox( "strict", wg.no_spread_strict );
				ui::slider_int( "solver iterations", wg.no_spread_iterations.value, 16, 512, "%d" );
			}

			ui::checkbox( "wait for accuracy", wg.spread_wait );
			if ( wg.spread_wait.value )
				ui::slider_float( "tolerance", wg.spread_tolerance.value, 0.1f, 4.0f, "%.1f" );

			ui::divider( );

			ui::checkbox( "auto stop", wg.auto_stop );
			if ( wg.auto_stop.value )
			{
				ui::combo( "mode##as", wg.auto_stop_mode.value, detail::autostop_modes, 4 );
				ui::slider_float( "stop speed", wg.autostop_speed.value, 0.0f, 250.0f, "%.0f" );
				ui::checkbox( "duck on stop", wg.autostop_duck );
			}

			ui::card_end( );
			xui::end_child( );
		}

		// ---- Карточка: Timing ----
		if ( xui::begin_child( "##rage_timing", col_w, 0.0f, false ) )
		{
			ui::card_begin( "Timing", col_w );

			ui::checkbox( "double tap", wg.doubletap );
			ui::checkbox( "force shot on ground", wg.force_shot );
			ui::checkbox( "force shot in air", wg.force_shot_air );

			ui::divider( );

			ui::checkbox( "patience", wg.patience );
			if ( wg.patience.value )
			{
				ui::slider_int( "lethal priority", wg.lethal_priority.value, 0, 130, "%d" );
				ui::slider_int( "max hold ticks", wg.max_hold_ticks.value, 0, 16, "%d" );
			}

			ui::checkbox( "extrapolation", lg.extrapolation );
			if ( lg.extrapolation.value )
				ui::slider_int( "extrapolate ticks", lg.max_extrapolate_ticks.value, 1, 10, "%d" );

			// The backtrack limit was read by get_valid_records the whole time and
			// had no control anywhere in the menu.
			ui::slider_int( "backtrack ticks", lg.max_backtrack_ticks.value, 1, 16, "%d" );

			ui::card_end( );
			xui::end_child( );
		}

		// ================ ПРАВАЯ КОЛОНКА ================
		xui::layout::set_cursor( right_x - wx, body_y - wy );

		// ---- Карточка: Hitboxes ----
		if ( xui::begin_child( "##rage_hitboxes", col_w, 0.0f, false ) )
		{
			ui::card_begin( "Hitboxes", col_w );

			xui::multicombo( "##hitboxes", wg.hitboxes.values, detail::hitbox_names, 6 );

			ui::divider( );

			ui::checkbox( "head multipoint", wg.head_pointscale );
			ui::checkbox( "body multipoint", wg.body_pointscale );
			ui::slider_float( "head scale", wg.head_pointscale_scale.value, 0.0f, 100.0f, "%.0f%%" );
			ui::slider_float( "body scale", wg.body_pointscale_scale.value, 0.0f, 100.0f, "%.0f%%" );
			ui::slider_int( "rings", wg.multipoint_rings.value, 1, 4, "%d" );
			ui::slider_int( "points per ring", wg.multipoint_points.value, 3, 12, "%d" );

			ui::divider( );

			ui::checkbox( "debug multipoints", wg.debug_multipoints );
			ui::checkbox( "debug pen trace", settings::g_combat.m_ragebot.debug_pen_trace );

			ui::card_end( );
			xui::end_child( );
		}

		// ---- Карточка: Force body aim ----
		if ( xui::begin_child( "##rage_baim", col_w, 0.0f, false ) )
		{
			ui::card_begin( "Force body aim", col_w );

			ui::checkbox( "always", wg.baim_always );
			ui::checkbox( "if lethal", wg.baim_if_lethal );
			ui::checkbox( "on lethal", wg.baim_lethal );
			ui::checkbox( "in air", wg.baim_air );
			ui::slider_int( "after shots", wg.baim_after_shots.value, 0, 10, "%d" );
			ui::slider_float( "spread", wg.baim_scale.value, 0.0f, 100.0f, "%.0f%%" );

			ui::divider( );

			ui::checkbox( "legacy force b-aim", wg.body_aim );

			ui::card_end( );
			xui::end_child( );
		}

		// ---- Карточка: Shot checks ----
		if ( xui::begin_child( "##rage_checks", col_w, 0.0f, false ) )
		{
			ui::card_begin( "Shot checks", col_w );

			ui::checkbox( "cone check", wg.cone_check );
			if ( wg.cone_check.value )
			{
				ui::slider_int( "samples", wg.cone_samples.value, 0, 32, "%d" );
				ui::slider_int( "accept", wg.cone_accept.value, 0, 100, "%d%%" );
			}

			ui::checkbox( "wall penetration check", wg.wall_check );
			if ( wg.wall_check.value )
				ui::slider_int( "min damage", wg.wall_penetration_min.value, 1, 100, "%d" );

			ui::card_end( );
			xui::end_child( );
		}

		// ---- Карточка: Anti Aim ----
		if ( xui::begin_child( "##rage_aa", col_w, 0.0f, false ) )
		{
			ui::card_begin( "Anti aim", col_w );

			ui::checkbox( "enabled##aa", aa.enabled );

			// pitch/yaw -- enum'ы, поэтому идём через xui::combo: у него есть
			// шаблонный overload для enum, а ui::combo принимает только int&.
			xui::combo( "pitch", aa.pitch.value, detail::pitch_items, 5 );
			if ( aa.pitch.value == settings::combat::antiaim::pitch_mode::custom )
				ui::slider_float( "pitch value", aa.pitch_custom.value, -89.0f, 89.0f, "%.1f" );

			xui::combo( "yaw mode", aa.yaw_mode.value, detail::yaw_items, 5 );

			if ( aa.yaw_mode.value == settings::combat::antiaim::yaw_mode::jitter )
				ui::slider_float( "jitter angle", aa.yaw_jitter.value, 1.0f, 180.0f, "%.1f" );

			if ( aa.yaw_mode.value == settings::combat::antiaim::yaw_mode::random )
				ui::slider_float( "random range", aa.yaw_random.value, 0.0f, 180.0f, "%.1f" );

			ui::checkbox( "spinbot", aa.spinbot );
			if ( aa.spinbot.value )
			{
				ui::slider_float( "speed##spin", aa.spinbot_speed.value, 0.5f, 50.0f, "%.1f" );
				xui::combo( "direction##spin", aa.spinbot_direction.value, detail::spin_dirs, 3 );
				if ( aa.spinbot_direction.value == settings::combat::antiaim::spin_direction::alternating )
					ui::slider_float( "switch every", aa.spinbot_switch_time.value, 0.2f, 6.0f, "%.1fs" );
			}

			ui::divider( );

			ui::checkbox( "compensate roll", aa.auto_yaw_adjust );
			ui::checkbox( "force left", aa.manual_left );
			ui::checkbox( "force right", aa.manual_right );
			ui::checkbox( "hide onshot", aa.hide_shots );
			ui::checkbox( "avoid backstab", aa.avoid_backstab );

			ui::checkbox( "direction indicator", aa.direction_indicator );
			if ( aa.direction_indicator.value )
			{
				xui::color_picker( "color##aai", aa.direction_indicator_color.value );
				ui::checkbox( "glow##aai", aa.direction_indicator_glow );
				if ( aa.direction_indicator_glow.value )
					ui::slider_float( "glow strength", aa.direction_indicator_glow_strength.value, 0.0f, 2.0f, "%.2f" );
			}

			ui::card_end( );
			xui::end_child( );
		}

		// ---- Карточка: Other bots ----
		if ( xui::begin_child( "##rage_other", col_w, 0.0f, false ) )
		{
			ui::card_begin( "Other bots", col_w );

			ui::checkbox( "auto revolver", autos.revolver );
			ui::checkbox( "quick shot (rmb)", autos.revolver_quick );
			ui::checkbox( "auto scope", autos.scope );

			ui::divider( );

			ui::checkbox( "zeusbot", zb.enabled );
			if ( zb.enabled.value )
			{
				ui::slider_float( "max fov##zb", zb.max_fov.value, 1.0f, 180.0f, "%.0f" );
				ui::checkbox( "drop after", zb.drop_after );
			}

			ui::checkbox( "knifebot", kb.enabled );
			if ( kb.enabled.value )
				ui::slider_float( "max fov##kb", kb.max_fov.value, 1.0f, 180.0f, "%.0f" );

			ui::divider( );

			ui::checkbox( "autopeek", qp.enabled );
			if ( qp.enabled.value )
			{
				xui::color_picker( "peek color##ap", qp.color.value );
				xui::color_picker( "retract color##ap", qp.retrack_color.value );
			}
			ui::checkbox( "duck peek", dp.enabled );

			ui::card_end( );
			xui::end_child( );
		}
	}

	void menu::draw_ragebot_general( float group_w ) const
	{
		( void )group_w;

		auto& s = settings::g_combat;
		auto& rb = s.m_ragebot;
		auto& gen = rb.general;

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = this->m_body_x;
		const auto body_y = this->m_body_y;
		const auto content_w = this->m_body_w;
		const auto col_w = detail::column_width( content_w );
		const auto right_x = content_x + col_w + theme::metric::space_md;

		xui::layout::set_cursor( content_x - wx, body_y - wy );

		// ---- Карточка: General ----
		if ( xui::begin_child( "##rage_general", col_w, 0.0f, false ) )
		{
			ui::card_begin( "General", col_w );

			// Вкладка General -- это мастер-гейты поверх пер-оружийных групп,
			// а не отдельный набор значений (см. settings::combat::ragebot).
			// Выключенный гейт снимает настройку у ВСЕХ групп оружия, поэтому
			// подписи говорят об этом прямо, а не "enable ragebot".
			ui::checkbox( "master enable", gen.enabled );
			ui::checkbox( "silent aim (all weapons)", gen.silent_aim );
			ui::checkbox( "auto fire (all weapons)", gen.auto_fire );
			ui::checkbox( "auto scope (all weapons)", gen.auto_scope );
			ui::checkbox( "auto stop (all weapons)", gen.auto_stop );

			ui::divider( );

			// Значения ниже подставляются в группу, у которой стоит 0 -- то
			// есть "не задано". Это дефолты, а не глобальные значения.
			ui::slider_int( "default hit chance", gen.hit_chance.value, 0, 100, "%d%%" );
			ui::slider_int( "default min damage", gen.min_damage.value, 0, 130, "%d" );
			ui::slider_int( "multipoint scale", gen.multipoint_scale.value, 0, 100, "%d%%" );
			ui::slider_int( "multipoint budget", gen.multipoint_budget.value, 0, 96, "%d pts" );

			ui::card_end( );
			xui::end_child( );
		}

		xui::layout::set_cursor( right_x - wx, body_y - wy );

		// ---- Карточка: Target selection ----
		if ( xui::begin_child( "##rage_target", col_w, 0.0f, false ) )
		{
			ui::card_begin( "Target selection", col_w );

			ui::checkbox( "prefer safe point", gen.prefer_safe_point );
			ui::checkbox( "prefer head", gen.prefer_head );
			ui::checkbox( "model head scan", gen.model_head_scan );

			ui::divider( );

			// Подсказка вместо галочки: эти настройки не переключаются, а
			// объясняют, что произойдёт при смене состояния выше.
			xui::text( "head scan probes the real capsule", theme::pal( ).text_muted );
			xui::text( "surface instead of fixed offsets.", theme::pal( ).text_muted );

			ui::card_end( );
			xui::end_child( );
		}
	}

} // namespace rendering
