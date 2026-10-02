#pragma once

#include <utilities/math/math.hpp>
#include <external/config.hpp>

namespace settings {

	struct combat
	{
		struct general_settings
		{
			xui::setting enabled          { true,  {}, "general enabled",          "ragebot" };
			xui::setting silent_aim       { true,  {}, "general silent aim",       "ragebot" };
			xui::setting auto_fire        { true,  {}, "general auto fire",        "ragebot" };
			xui::setting auto_scope       { true,  {}, "general auto scope",       "ragebot" };
			xui::setting auto_stop        { true,  {}, "general auto stop",        "ragebot" };
			config::val<int> hit_chance   { 55, "ragebot", "general hit chance"   };
			config::val<int> min_damage   { 24, "ragebot", "general min damage"   };
			config::val<int> multipoint_scale { 75, "ragebot", "general multipoint scale" };
			// Бюджет мультиточек на одну запись цели. Центры хитбоксов
			// добавляются всегда (по одному трейсу на хитбокс), бюджет режет
			// только мультиточки, идущие в порядке приоритета -- голова первой.
			//
			// Зачем: скан тратит по одному движковому penetration-трейсу на
			// точку. Центр + мультиточки на каждый из ~7 хитбоксов давали ~84
			// точки, при двух записях и ~5 кандидатах это ~1100 трейсов на
			// кадр (~35 мкс каждый) -- 38 мс только на трейсы, 11 fps.
			config::val<int> multipoint_budget { 24, "ragebot", "general multipoint budget" };
			xui::setting prefer_safe_point{ true,  {}, "general prefer safe",      "ragebot" };
			xui::setting prefer_head     { false, {}, "general prefer head",      "ragebot" };
			// Прощуп модели головы: ищем открытые участки капсулы вместо
			// слепых геометрических мультиточек. См. head_model.
			xui::setting model_head_scan { true,  {}, "general model head scan",  "ragebot" };
		};

		struct ragebot
		{
			static constexpr auto k_group_count{ 6u };

			xui::setting enabled{ true, {}, "enabled", "ragebot" };

			// Печатает каждую трассу автопробития в DarkFox_init.log. Включённый
			// -- это три Win32-вызова, vformat и WriteFile на каждый pen::run,
			// то есть сотни тысяч записей в секунду с восьми воркеров. Измерено
			// на живой игре: 354k строк [pen] за окно 5 с и кадр 700-790 мс при
			// 1 fps. Глобально (не per-weapon): флаг один на процесс.
			xui::setting debug_pen_trace{ false, {}, "debug penetration trace", "ragebot" };

			struct weapon_group
			{
				xui::setting silent{ true, {}, "silent", "ragebot" };
				// 0 auto, 1 early, 2 in air, 3 on ground -- see rage::should_stop_movement.
				xui::setting auto_stop{ true, {}, "auto stop", "ragebot" };
				config::val<int> auto_stop_mode{ 0, "ragebot", "auto stop mode" };
				xui::setting no_spread{ true, {}, "no spread", "ragebot" };
				xui::setting doubletap{ false, {}, "doubletap", "ragebot" };
				xui::setting body_aim{ false, {}, "force b-aim", "ragebot" };
				xui::setting force_shot_air{ false, {}, "force shot in air", "ragebot" };
				xui::setting force_shot{ false, {}, "force shot on ground", "ragebot" };
				xui::setting auto_scope{ true, {}, "auto scope", "ragebot" };

				config::val<float> max_fov{ 180.0f };

				config::val<int> hitchance{ 80 };

				// 101 = "стрелять только если расчёт гарантирует убийство одним
				// выстрелом". Это самый пассивный режим из возможных: ragebot
				// молчит, пока не увидит летальный урон, и выглядит нерабочим,
				// даже когда всё исправно. 20 -- рабочий компромисс: стреляет
				// по урону от 20 и выше, летальные всегда приоритетнее (см.
				// scoring::k_lethal_bonus в select_best).
				config::val<int> min_damage{ 20 };

				config::val<int> min_damage_override_value{ 11 };
				xui::setting min_damage_override{ false, {}, "min damage override", "ragebot" };

				config::val<int> hitchance_override_value{ 75 };
				xui::setting hitchance_override{ false, {}, "hit chance override", "ragebot" };

			config::val<float> pointscale{ 85.0f };


			xui::setting dynamic_pointscale{ true, {}, "dynamic point scale", "ragebot" };
			xui::setting debug_multipoints{ false, {}, "debug multipoints", "ragebot" };

			config::bools<6> hitboxes{ { true, true, true, true, true, true } };

			// -- Мультипоинты --------------------------------------------------
			// Мультипоинт -- это смещение точки прицеливания от центра хитбокса
			// к его краю. Чем больше значений, тем больше шансов, что хотя бы
			// одно попадёт в открытую часть модели, но и тем дороже скан.
			xui::setting head_pointscale{ true, {}, "head multipoint", "ragebot" };
			xui::setting body_pointscale{ true, {}, "body multipoint", "ragebot" };
			config::val<float> head_pointscale_scale{ 100.0f, "ragebot", "head multipoint scale" };
			config::val<float> body_pointscale_scale{ 70.0f, "ragebot", "body multipoint scale" };
			// Сколько колец точек строить вокруг центра. Больше -- плотнее
			// покрытие и дороже; 1 кольцо из 4 точек уже даёт четыре разных
			// угла атаки на один хитбокс.
			config::val<int> multipoint_rings{ 2, "ragebot", "multipoint rings" };
			config::val<int> multipoint_points{ 6, "ragebot", "multipoint points per ring" };

			// -- Проверка выстрела -------------------------------------------
			// Cone check -- сколько точек вокруг намеченной должно пройти
			// autowall, чтобы выстрел считался осмысленным. 0 = выключено.
			xui::setting cone_check{ true, {}, "shot cone check", "ragebot" };
			config::val<int> cone_samples{ 8, "ragebot", "shot cone samples" };
			// Минимальная доля прошедших точек (в процентах). Ниже -- стреляет
			// чаще и рискует промахом, выше -- стреляет реже и точнее.
			config::val<int> cone_accept{ 60, "ragebot", "shot cone accept %" };
			// Насколько близко к решающей линии должен быть стабилен мир, чтобы
			// выстрел не ушёл в стену из-за расхождения предикта и сервера.
			xui::setting wall_check{ true, {}, "wall penetration check", "ragebot" };
			config::val<int> wall_penetration_min{ 5, "ragebot", "wall min damage" };

			// -- Неточность и разброс ----------------------------------------
			// Если текущий разброс оружия уже съедает цель, бот ждёт стабилизации
			// вместо выстрела вслепую. Порог -- во сколько раз конус разброса
			// должен быть меньше углового размера цели.
			xui::setting spread_wait{ true, {}, "wait for accuracy", "ragebot" };
			config::val<float> spread_tolerance{ 1.0f, "ragebot", "accuracy tolerance" };
			xui::setting no_spread_strict{ false, {}, "no spread strict", "ragebot" };
			config::val<int> no_spread_iterations{ 96, "ragebot", "no spread iterations" };

			// -- Тайминг ------------------------------------------------------
			// Минимальный урон, при котором выстрел считается летальным и
			// бот не ждёт более выгодной точки. 0 = выключено (ждать оптимум).
			config::val<int> lethal_priority{ 1, "ragebot", "lethal priority" };
			// Сколько тиков бот готов ждать выгодного момента, прежде чем
			// выстрелить по тому, что есть. 0 = не ждать.
			config::val<int> max_hold_ticks{ 4, "ragebot", "max hold ticks" };
			// Терпение включено по умолчанию: бот держит выстрел до
			// max_hold_ticks тиков, если цель не летальна.
			xui::setting patience{ true, {}, "patience", "ragebot" };

			// -- Хитбоксы и точка попадания ----------------------------------
			xui::setting baim_always{ false, {}, "baim always", "ragebot" };
			xui::setting baim_lethal{ false, {}, "baim on lethal", "ragebot" };
			xui::setting baim_if_lethal{ false, {}, "baim if lethal", "ragebot" };
			xui::setting baim_air{ false, {}, "baim in air", "ragebot" };
			config::val<int> baim_after_shots{ 0, "ragebot", "baim after shots" };
			// Насколько широко разбрасывать точки по телу при force b-aim.
			config::val<float> baim_scale{ 50.0f, "ragebot", "baim scale" };

			// -- Авто-стоп ----------------------------------------------------
			// Скорость, до которой бот тормозит перед выстрелом. Меньше --
			// точнее выстрел и дольше выход на позицию.
			config::val<float> autostop_speed{ 30.0f, "ragebot", "auto stop speed" };
			xui::setting autostop_duck{ false, {}, "auto stop duck", "ragebot" };

			void init( std::string_view cat )
			{
				const auto s = std::string( cat );

				this->silent.category = s;
				this->no_spread.category = s;
				this->doubletap.category = s;
				this->body_aim.category = s;
				this->force_shot_air.category = s;
				this->force_shot.category = s;
				this->auto_scope.category = s;
				this->min_damage_override.category = s;
				this->hitchance_override.category = s;
				this->dynamic_pointscale.category = s;
				this->debug_multipoints.category = s;

				this->head_pointscale.category = s;
				this->body_pointscale.category = s;
				this->cone_check.category = s;
				this->wall_check.category = s;
				this->spread_wait.category = s;
				this->no_spread_strict.category = s;
				this->patience.category = s;
				this->baim_always.category = s;
				this->baim_lethal.category = s;
				this->baim_if_lethal.category = s;
				this->baim_air.category = s;
				this->autostop_duck.category = s;

				this->max_fov.reg( s, "max fov" );
				this->hitchance.reg( s, "hit chance" );
				this->min_damage.reg( s, "min damage" );
				this->min_damage_override_value.reg( s, "min damage override value" );
				this->hitchance_override_value.reg( s, "hit chance override value" );
				this->pointscale.reg( s, "point scale" );

				this->head_pointscale_scale.reg( s, "head multipoint scale" );
				this->body_pointscale_scale.reg( s, "body multipoint scale" );
				this->multipoint_rings.reg( s, "multipoint rings" );
				this->multipoint_points.reg( s, "multipoint points per ring" );
				this->cone_samples.reg( s, "shot cone samples" );
				this->cone_accept.reg( s, "shot cone accept %" );
				this->wall_penetration_min.reg( s, "wall min damage" );
				this->spread_tolerance.reg( s, "accuracy tolerance" );
				this->no_spread_iterations.reg( s, "no spread iterations" );
				this->lethal_priority.reg( s, "lethal priority" );
				this->max_hold_ticks.reg( s, "max hold ticks" );
				this->baim_after_shots.reg( s, "baim after shots" );
				this->baim_scale.reg( s, "baim scale" );
				this->autostop_speed.reg( s, "auto stop speed" );

				this->hitboxes.reg( s, "hitboxes" );
			}

				void set_default_binds( )
				{
					this->force_shot_air.bind = { .key = VK_XBUTTON1, .mode = xui::bind_mode::hold_on };
					this->min_damage_override.bind = { .key = VK_XBUTTON2, .mode = xui::bind_mode::hold_on };
					this->hitchance_override.bind = { .key = VK_SPACE, .mode = xui::bind_mode::hold_on };
				}
			};

			general_settings general{};

			std::array<weapon_group, k_group_count> groups{};

			ragebot( )
			{
				constexpr const char* weapon_names[ ]{ "pistol", "smg", "rifle", "shotgun", "sniper", "lmg" };

				for ( std::uint32_t i = 0; i < k_group_count; ++i )
				{
					this->groups[ i ].init( std::string( "ragebot - " ) + weapon_names[ i ] );
				}

				this->groups[ 0 ].set_default_binds( );
				this->groups[ 4 ].set_default_binds( );
			}

			weapon_group& get_group( std::uint32_t weapon_type )
			{
				const auto idx = weapon_type - cstypes::weapon_type::pistol;
				return this->groups[ idx < k_group_count ? idx : 2 ];
			}

			const weapon_group& get_group( std::uint32_t weapon_type ) const
			{
				const auto idx = weapon_type - cstypes::weapon_type::pistol;
				return this->groups[ idx < k_group_count ? idx : 2 ];
			}

			// Вкладка "General" -- это мастер-гейты поверх пер-оружийных групп,
			// а не отдельный набор значений. Раньше эти десять настроек рисовались
			// в меню, но ни одна логика их не читала: чекбоксы переключались,
			// и ровно ничего не происходило. Здесь general применяется как
			// "выключено" и как значения по умолчанию, а конкретная группа
			// оружия может их переопределить.
			//
			//   general.enabled == false        -> ragebot выключен для всего
			//   general.silent_aim == false     -> silent снимается у всех групп
			//   general.auto_fire == false      -> атака без авто-выстрела (только наведение)
			//   general.auto_scope == false     -> авто-скоп снимается у всех групп
			//   general.auto_stop == false      -> авто-стоп снимается у всех групп
			//   general.hit_chance / min_damage / multipoint_scale
			//                                   -> подставляются, когда у группы
			//                                      стоит 0 (то есть "не задано")
			//   general.prefer_safe_point       -> общий fallback для выбора точки
			[[nodiscard]] bool master_enabled( ) const
			{
				return this->general.enabled.value;
			}

			// prefer_safe_point -- тоже readonly-fallback, а не гейт: группа
			// per-weapon флага не имеет, поэтому general здесь единственный
			// источник. true = сначала целиться в точку без риска зацепить
			// геометрию, и только если её нет -- в обычную.
			[[nodiscard]] bool prefer_safe_point( ) const
			{
				return this->general.prefer_safe_point.value;
			}

			[[nodiscard]] bool silent_effective( const weapon_group& g ) const
			{
				return this->general.silent_aim.value && g.silent.value;
			}

			[[nodiscard]] bool auto_scope_effective( const weapon_group& g ) const
			{
				return this->general.auto_scope.value && g.auto_scope.value;
			}

			[[nodiscard]] bool auto_stop_effective( const weapon_group& g ) const
			{
				return this->general.auto_stop.value && g.auto_stop.value;
			}

			[[nodiscard]] int hit_chance_effective( const weapon_group& g ) const
			{
				return g.hitchance.value > 0 ? g.hitchance.value : this->general.hit_chance.value;
			}

			[[nodiscard]] int min_damage_effective( const weapon_group& g ) const
			{
				return g.min_damage.value > 0 ? g.min_damage.value : this->general.min_damage.value;
			}

			[[nodiscard]] float point_scale_effective( const weapon_group& g ) const
			{
				// Пер-оружийный pointscale -- основная ручка; general.multipoint_scale
				// множит её, если она задана, иначе задаёт сама.
				const auto base = g.pointscale.value > 0.0f ? g.pointscale.value : 100.0f;
				return base * ( static_cast< float >( this->general.multipoint_scale.value ) / 100.0f );
			}

			// max_fov -- единственная per-weapon ручка без effective-обёртки, и
			// это ломало ragebot молча: 0 здесь означает не "не задано", а
			// "цель должна быть ровно по центру прицела", поэтому фильтр
			// `fov > config.max_fov` (rage.cpp) отбрасывал ВСЕ точки и выстрела
			// не происходило никогда. Старые конфиги приезжают именно с 0.
			// Трактуем 0 и отрицательные как "не задано" -- как у соседних ручек.
			[[nodiscard]] float max_fov_effective( const weapon_group& g ) const
			{
				return g.max_fov.value > 0.0f ? g.max_fov.value : 180.0f;
			}
		} m_ragebot{};

		struct general_settings_legit
		{
			xui::setting enabled   { false, {}, "general enabled",   "legitbot" };
			config::val<int> fov   { 5,    "legitbot", "general fov" };
			config::val<int> smoothness { 40, "legitbot", "general smoothness" };
			xui::setting auto_fire { false, {}, "general auto fire", "legitbot" };
			config::val<int> min_damage { 20, "legitbot", "general min damage" };
		};

		struct legitbot
		{
			static constexpr auto k_group_count{ 6u };

			struct weapon_group
			{
				xui::setting aimbot{ false, { VK_XBUTTON2, xui::bind_mode::hold_on }, "aimbot", "legitbot" };
				config::val<float> fov{ 5.0f };
				config::val<int> smooth{ 5 };
				config::bools<5> hitboxes{ { true, false, false, false, false } };

				xui::setting rcs{ true, {}, "recoil control", "legitbot" };
				config::val<int> rcs_min{ 95 };
				config::val<int> rcs_max{ 105 };

				xui::setting standalone_rcs{ false, {}, "standalone rcs", "legitbot" };
				config::val<int> standalone_rcs_strength{ 100 };
				config::val<int> standalone_rcs_min{ 95 };
				config::val<int> standalone_rcs_max{ 105 };

				xui::setting triggerbot{ false, { VK_XBUTTON1, xui::bind_mode::hold_on }, "triggerbot", "legitbot" };
				config::val<int> trigger_delay{ 5 };
				config::val<int> trigger_hitchance{ 80 };
				xui::setting trigger_head_only{ false, {}, "trigger head only", "legitbot" };
				xui::setting give_me_your_seed{ false, {}, "trigger seed mode", "legitbot" };

				xui::setting autowall{ true, {}, "autowall", "legitbot" };
				config::val<int> min_damage{ 101 };

				xui::setting visualize_fov{ true, {}, "visualize fov", "legitbot" };
				config::col fov_color{ { 255, 255, 255, 150 } };

				// autostop — плавно останавливаемся при aim assist на цели
				enum class autostop_mode_t : std::uint8_t
				{
					standard,   // резко — cmd->forwardmove/sidemove = 0
					smooth,     // плавно — противодействующее движение (как в rage)
					predict     // с предсказанием — стопим только когда есть смысл
				};

				xui::setting autostop{ false, {}, "auto stop", "legitbot" };
				config::enm<autostop_mode_t> autostop_mode_value{ autostop_mode_t::smooth };
				config::val<int> autostop_min_speed{ 10 };

				void init( std::string_view cat )
				{
					const auto s = std::string( cat );

					this->aimbot.category = s;
					this->rcs.category = s;
					this->standalone_rcs.category = s;
					this->triggerbot.category = s;
					this->trigger_head_only.category = s;
					this->give_me_your_seed.category = s;
					this->autowall.category = s;
					this->visualize_fov.category = s;
					this->autostop.category = s;

					this->fov.reg( s, "fov" );
					this->smooth.reg( s, "smooth" );
					this->hitboxes.reg( s, "hitboxes" );
					this->rcs_min.reg( s, "rcs min" );
					this->rcs_max.reg( s, "rcs max" );
					this->standalone_rcs_strength.reg( s, "standalone rcs strength" );
					this->standalone_rcs_min.reg( s, "standalone rcs min" );
					this->standalone_rcs_max.reg( s, "standalone rcs max" );
					this->trigger_delay.reg( s, "trigger delay" );
					this->trigger_hitchance.reg( s, "trigger hitchance" );
					this->min_damage.reg( s, "min damage" );
					this->fov_color.reg( s, "fov color" );
					this->autostop_mode_value.reg( s, "autostop mode" );
					this->autostop_min_speed.reg( s, "autostop min speed" );
				}
			};

			xui::setting enabled{ false, {}, "enabled", "legitbot" };
			general_settings_legit general{};

			std::array<weapon_group, k_group_count> groups{};

			legitbot( )
			{
				constexpr const char* weapon_names[ ]{ "pistol", "smg", "rifle", "shotgun", "sniper", "lmg" };

				for ( auto i = 0u; i < k_group_count; ++i )
				{
					this->groups[ i ].init( std::string( "legitbot - " ) + weapon_names[ i ] );
				}
			}

			weapon_group& get_group( std::uint32_t weapon_type )
			{
				const auto idx = weapon_type - cstypes::weapon_type::pistol;
				return this->groups[ idx < k_group_count ? idx : 2 ];
			}

			const weapon_group& get_group( std::uint32_t weapon_type ) const
			{
				const auto idx = weapon_type - cstypes::weapon_type::pistol;
				return this->groups[ idx < k_group_count ? idx : 2 ];
			}
		} m_legitbot{};

		struct antiaim
		{
			enum class pitch_mode : std::uint8_t
			{
				none,
				down,
				up,
				zero,
				custom
			};

			enum class yaw_mode : std::uint8_t
			{
				fixed,
				jitter,
				spam,
				random,
				local_view
			};

			// Which way the spin runs. Kept separate from the speed so a negative
			// speed cannot silently mean "the other way" and confuse the reading.
			enum class spin_direction : std::uint8_t
			{
				right,
				left,
				alternating
			};

			xui::setting enabled{ true, {}, "anti aim", "anti aim" };
			config::enm<pitch_mode> pitch{ pitch_mode::down, "anti aim", "pitch" };
			config::val<float> pitch_custom{ -90.0f, "anti aim", "pitch custom value" };
			config::enm<yaw_mode> yaw_mode{ yaw_mode::fixed, "anti aim", "yaw mode" };
			config::val<float> yaw_jitter{ 45.0f, "anti aim", "yaw jitter" };
			config::val<float> yaw_spin_speed{ 8.0f, "anti aim", "yaw spin speed" };

			// Spinbot is its own switch rather than a yaw mode, so it layers on top
			// of whatever the yaw is already doing instead of replacing it.
			xui::setting spinbot{ false, {}, "spinbot", "anti aim" };
			config::val<float> spinbot_speed{ 8.0f, "anti aim", "spinbot speed" };
			config::enm<spin_direction> spinbot_direction{ spin_direction::right, "anti aim", "spinbot direction" };
			config::val<float> spinbot_switch_time{ 1.5f, "anti aim", "spinbot switch time" };
			config::val<float> yaw_random{ 0.0f, "anti aim", "yaw random range" };
			xui::setting auto_yaw_adjust{true, {}, "correct yaw to compensate for the models inherit sideways roll", "anti aim"};
			xui::setting manual_left{ false, { 'Z', xui::bind_mode::toggle }, "force left", "anti aim" };
			xui::setting manual_right{ false, { 'C', xui::bind_mode::toggle }, "force right", "anti aim" };
			xui::setting hide_shots{ true, {}, "hide onshot", "anti aim" };
			xui::setting avoid_backstab{ true, {}, "avoid backstab", "anti aim" };

			xui::setting direction_indicator{ true, {}, "direction indicator", "anti aim" };
			config::col direction_indicator_color{ { 173, 192, 255, 220 }, "anti aim", "direction indicator color" };
			xui::setting direction_indicator_glow{ true, {}, "direction indicator glow", "anti aim" };
			config::val<float> direction_indicator_glow_strength{ 0.55f, "anti aim", "direction indicator glow strength" };

			antiaim( )
			{
				this->manual_left.bind.excludes = &this->manual_right;
				this->manual_right.bind.excludes = &this->manual_left;
			}
		} m_antiaim{};

		struct quickpeek
		{
			xui::setting enabled{ false, { 'V', xui::bind_mode::hold_on }, "quick peek", "peek assistance" };
			config::col color{ { 173, 192, 255, 255 }, "peek assistance", "quick peek color" };
			config::col retrack_color{ { 255, 171, 234, 255 }, "peek assistance", "retracting color" };
		} m_quickpeek{};

		struct duckpeek
		{
			xui::setting enabled{ false, { VK_LMENU, xui::bind_mode::hold_on }, "duck peek", "peek assistance" };
		} m_duckpeek{};

		struct lagcomp_settings
		{
			config::val<int> max_backtrack_ticks{ 12, "ragebot", "max backtrack ticks" };
			xui::setting extrapolation{ true, {}, "extrapolation", "ragebot" };
			config::val<int> max_extrapolate_ticks{ 8, "ragebot", "max extrapolate ticks" };

		} m_lagcomp{};

		struct zeusbot
		{
			xui::setting enabled{ true, {}, "zeusbot", "other 'bots'" };
			xui::setting drop_after{ true, {}, "drop after", "zeusbot" };
			config::val<float> max_fov{ 180.0f, "zeusbot", "max fov" };

			// См. ragebot::max_fov_effective: 0 здесь означал бы "только точно
			// по центру прицела" и глушил бота молча.
			[[nodiscard]] float max_fov_effective( ) const
			{
				return this->max_fov.value > 0.0f ? this->max_fov.value : 180.0f;
			}
		} m_zeusbot{};

		struct autos
		{
			xui::setting revolver{ true, {}, "auto revolver", "autos" };
			// The R8's primary attack cocks the hammer and only fires ~13 ticks
			// later. Its secondary fires the instant the button goes down, at the
			// cost of a much wider cone -- which no_spread cancels anyway.
			xui::setting revolver_quick{ true, {}, "revolver quick shot", "autos" };
			xui::setting scope{ true, {}, "auto scope", "autos" };
		} m_autos{};

		// Resolver settings — управление резольвером для ragebot
		struct resolver
		{
			xui::setting enabled{ true, {}, "resolver", "resolver" };
			config::val<int> head_misses_trigger{ 3, "resolver", "head misses trigger" };
			config::val<float> body_aim_duration{ 1.5f, "resolver", "body aim duration" };
		} m_resolver{};

		struct knifebot
		{
			xui::setting enabled{ true, {}, "knifebot", "other 'bots'" };
			config::val<float> max_fov{ 180.0f, "knifebot", "max fov" };

			// См. ragebot::max_fov_effective.
			[[nodiscard]] float max_fov_effective( ) const
			{
				return this->max_fov.value > 0.0f ? this->max_fov.value : 180.0f;
			}
		} m_knifebot{};

		struct penetration_crosshair
		{
			xui::setting enabled{ false, {}, "penetration crosshair", "pen crosshair" };
			config::col can_penetrate_fill{ { 173, 192, 255, 120 }, "pen crosshair", "can penetrate fill" };
			config::col can_penetrate_outline{ { 173, 192, 255, 210 }, "pen crosshair", "can penetrate outline" };
			config::col blocked_fill{ { 252, 217, 240, 80 }, "pen crosshair", "blocked fill" };
			config::col blocked_outline{ { 252, 217, 240, 160 }, "pen crosshair", "blocked outline" };
			xui::setting glow{ true, {}, "glow", "pen crosshair" };
			config::val<float> glow_strength{ 1.0f, "pen crosshair", "glow strength" };
		} m_penetration_crosshair{};
	};

	struct esp
	{
		enum class cham_ids : std::uint8_t
		{
			liquid, metallic, matte, flat, bloom, outlines, glow, electric, distortion, hologram, pearl,
			liquid_ignorez, matte_ignorez, flat_ignorez, bloom_ignorez, outlines_ignorez, glow_ignorez, distortion_ignorez, hologram_ignorez,
			count
		};

		struct chams_layer
		{
			xui::setting enabled{ false, {}, "chams layer", "chams" };
			config::col color{ { 255, 255, 255, 255 } };
			config::enm<cham_ids> material{ cham_ids::matte };

			void init( std::string_view cat, std::string_view layer_name )
			{
				const auto s = std::string( cat );
				this->enabled.name = std::string( layer_name );
				this->enabled.category = s;
				this->color.reg( s, std::string( layer_name ) + " color" );
				this->material.reg( s, std::string( layer_name ) + " material" );
			}
		};

		struct chams_config
		{
			xui::setting enabled{ false, {}, "chams", "chams" };
			chams_layer primary{};
			chams_layer secondary{};
			chams_layer overlay{};
		};

		struct glow_target
		{
			xui::setting enabled{ false, {}, "glow", "glow" };
			config::col color{ { 173, 192, 255, 75 } };

			// Сила свечения. Раньше яркость была зашита в альфу цвета: чтобы
			// сделать свечение заметнее, приходилось править сам цвет, и
			// получалось, что "поярче" и "поменять оттенок" -- одна ручка.
			// Теперь альфа задаёт оттенок, а strength -- интенсивность.
			config::val<float> strength{ 1.0f };

			// Толщина ореола в пикселях по контуру модели. Больше -- свечение
			// заметнее и выходит дальше за силуэт.
			config::val<float> thickness{ 2.0f };

			void init( std::string_view cat, std::string_view color_name = "color" )
			{
				this->color.reg( cat, color_name );
				this->strength.reg( cat, std::string( color_name ) + " strength" );
				this->thickness.reg( cat, std::string( color_name ) + " thickness" );
			}
		};

		struct player
		{
			struct overlay
			{
				xui::setting enabled{ true, {}, "esp overlay", "esp" };

				struct box
				{
					enum class style_type : std::uint8_t { full, cornered };

					xui::setting enabled{};
					config::enm<style_type> style{ style_type::cornered };
					xui::setting fill{};
					xui::setting outline{};
					config::val<float> corner_length{ 10.0f };
					config::val<float> thickness{ 1.0f };
					xui::setting glow{};
					config::val<float> glow_strength{ 0.7f };
					config::val<int> glow_layers{ 3 };

					config::col visible_color{ { 173, 192, 255, 255 } };
					config::col occluded_color{ { 255, 171, 234, 255 } };

					box( ) = default;

					explicit box( const std::string& prefix )
						: enabled{ false, {}, "bounding box", prefix + " box" }
						, fill{ true, {}, "fill", prefix + " box" }
						, outline{ true, {}, "outline", prefix + " box" }
						, glow{ false, {}, "box glow", prefix + " box" }
					{
						const auto cat = prefix + " box";
						this->style.reg( cat, "style" );
						this->corner_length.reg( cat, "corner length" );
						this->thickness.reg( cat, "thickness" );
						this->glow_strength.reg( cat, "glow strength" );
						this->glow_layers.reg( cat, "glow layers" );
						this->visible_color.reg( cat, "visible color" );
						this->occluded_color.reg( cat, "occluded color" );
					}
				};

				struct skeleton
				{
					enum class mode : std::uint8_t { normal, backtrack };

					xui::setting enabled{};
					config::enm<mode> type{ mode::backtrack };
					config::val<float> thickness{ 1.5f };
					xui::setting glow{};
					config::val<float> glow_strength{ 0.7f };

					config::col visible_color{ { 173, 192, 255, 255 } };
					config::col occluded_color{ { 220, 225, 240, 255 } };

					skeleton( ) = default;

					explicit skeleton( const std::string& prefix )
						: enabled{ false, {}, "skeleton", prefix + " skeleton" }
						, glow{ false, {}, "skeleton glow", prefix + " skeleton" }
					{
						const auto cat = prefix + " skeleton";
						this->type.reg( cat, "mode" );
						this->thickness.reg( cat, "thickness" );
						this->glow_strength.reg( cat, "glow strength" );
						this->visible_color.reg( cat, "visible color" );
						this->occluded_color.reg( cat, "occluded color" );
					}
				};

				struct health_bar
				{
					enum class position_type : std::uint8_t { left, top, bottom };

					xui::setting enabled{};
					config::enm<position_type> position{ position_type::left };
					xui::setting outline_setting{};
					xui::setting gradient{};
					xui::setting show_value{};
					xui::setting glow{};

					config::col full_color{ { 173, 192, 255, 255 } };
					config::col low_color{ { 130, 145, 200, 255 } };
					config::col background_color{ { 0, 0, 0, 255 } };
					config::col outline_color{ { 0, 0, 0, 255 } };
					config::col text_color{ { 255, 255, 255, 255 } };
					config::col glow_color{ { 173, 192, 255, 255 } };
					config::val<float> glow_strength{ 0.55f };

					health_bar( ) = default;

					explicit health_bar( const std::string& prefix )
						: enabled{ true, {}, "health bar", prefix + " health" }
						, outline_setting{ true, {}, "outline", prefix + " health" }
						, gradient{ true, {}, "gradient", prefix + " health" }
						, show_value{ true, {}, "show value", prefix + " health" }
						, glow{ true, {}, "glow", prefix + " health" }
					{
						const auto cat = prefix + " health";
						this->position.reg( cat, "position" );
						this->full_color.reg( cat, "full color" );
						this->low_color.reg( cat, "low color" );
						this->background_color.reg( cat, "background" );
						this->outline_color.reg( cat, "outline color" );
						this->text_color.reg( cat, "text color" );
						this->glow_color.reg( cat, "glow color" );
						this->glow_strength.reg( cat, "glow strength" );
					}
				};

				struct ammo_bar
				{
					enum class position_type : std::uint8_t { left, top, bottom };

					xui::setting enabled{};
					config::enm<position_type> position{ position_type::bottom };
					xui::setting outline_setting{};
					xui::setting gradient{};
					xui::setting show_value{};
					xui::setting glow{};

					config::col full_color{ { 255, 171, 234, 255 } };
					config::col low_color{ { 255, 210, 244, 255 } };
					config::col background_color{ { 0, 0, 0, 255 } };
					config::col outline_color{ { 0, 0, 0, 255 } };
					config::col text_color{ { 255, 255, 255, 255 } };
					config::col glow_color{ { 173, 192, 255, 255 } };
					config::val<float> glow_strength{ 0.55f };

					ammo_bar( ) = default;

					explicit ammo_bar( const std::string& prefix )
						: enabled{ true, {}, "ammo bar", prefix + " ammo" }
						, outline_setting{ true, {}, "outline", prefix + " ammo" }
						, gradient{ true, {}, "gradient", prefix + " ammo" }
						, show_value{ false, {}, "show value", prefix + " ammo" }
						, glow{ true, {}, "glow", prefix + " ammo" }
					{
						const auto cat = prefix + " ammo";
						this->position.reg( cat, "position" );
						this->full_color.reg( cat, "full color" );
						this->low_color.reg( cat, "low color" );
						this->background_color.reg( cat, "background" );
						this->outline_color.reg( cat, "outline color" );
						this->text_color.reg( cat, "text color" );
						this->glow_color.reg( cat, "glow color" );
						this->glow_strength.reg( cat, "glow strength" );
					}
				};

				struct info_flags
				{
					enum flag : std::uint8_t
					{
						money = 0, armor, kit, scoped, defusing, flashed, ping, distance, count
					};

					xui::setting enabled{};
					config::bools<count> flags{ { false, false, false, true, true, true, true, false } };

					config::col money_color{ { 160, 210, 140, 255 } };
					config::col armor_color{ { 220, 225, 240, 255 } };
					config::col kit_color{ { 173, 192, 255, 255 } };
					config::col scoped_color{ { 220, 225, 240, 255 } };
					config::col defusing_color{ { 173, 192, 255, 255 } };
					config::col flashed_color{ { 240, 230, 170, 255 } };
					config::col distance_color{ { 185, 190, 205, 255 } };

					info_flags( ) = default;

					explicit info_flags( const std::string& prefix ) : enabled{ true, {}, "info flags", prefix + " flags" }
					{
						const auto cat = prefix + " flags";
						this->flags.reg( cat, "flags" );
						this->money_color.reg( cat, "money color" );
						this->armor_color.reg( cat, "armor color" );
						this->kit_color.reg( cat, "kit color" );
						this->scoped_color.reg( cat, "scoped color" );
						this->defusing_color.reg( cat, "defusing color" );
						this->flashed_color.reg( cat, "flashed color" );
						this->distance_color.reg( cat, "distance color" );
					}

					[[nodiscard]] bool has( flag f ) const { return this->flags[ f ]; }
				};

				struct name
				{
					xui::setting enabled{};
					config::col color{ { 255, 255, 255, 225 } };

					name( ) = default;

					explicit name( const std::string& prefix ) : enabled{ true, {}, "name", prefix + " name" }
					{
						this->color.reg( prefix + " name", "color" );
					}
				};

				struct weapon
				{
					enum class display_type : std::uint8_t { text, icon, text_and_icon };

					xui::setting enabled{};
					config::enm<display_type> display{ display_type::text_and_icon };

					config::col text_color{ { 255, 255, 255, 225 } };
					config::col icon_color{ { 255, 255, 255, 225 } };

					weapon( ) = default;

					explicit weapon( const std::string& prefix ) : enabled{ true, {}, "weapon", prefix + " weapon" }
					{
						const auto cat = prefix + " weapon";
						this->display.reg( cat, "display" );
						this->text_color.reg( cat, "text color" );
						this->icon_color.reg( cat, "icon color" );
					}
				};

				struct oof_arrow
				{
					xui::setting enabled{};
					xui::setting glow{};
					config::val<float> width{ 20.0f };
					config::val<float> height{ 15.0f };
					config::val<float> radius_x{ 200.0f };
					config::val<float> radius_y{ 200.0f };
					config::val<float> glow_strength{ 1.0f };

					config::col visible_color{ { 255, 171, 234, 255 } };
					config::col occluded_color{ { 173, 192, 255, 255 } };

					oof_arrow( ) = default;

					explicit oof_arrow( const std::string& prefix ) : enabled{ true, {}, "oof arrow", prefix + " oof" }, glow{ true, {}, "glow", prefix + " oof" }
					{
						const auto cat = prefix + " oof";
						this->width.reg( cat, "width" );
						this->height.reg( cat, "height" );
						this->radius_x.reg( cat, "radius x" );
						this->radius_y.reg( cat, "radius y" );
						this->glow_strength.reg( cat, "glow strength" );
						this->visible_color.reg( cat, "visible color" );
						this->occluded_color.reg( cat, "occluded color" );
					}
				};

				box m_box{};
				skeleton m_skeleton{};
				health_bar m_health_bar{};
				ammo_bar m_ammo_bar{};
				info_flags m_info_flags{};
				name m_name{};
				weapon m_weapon{};
				oof_arrow m_oof_arrow{};

				overlay( ) = default;

				explicit overlay( const char* prefix, bool enabled_default = true )
					: overlay{ std::string{ prefix }, enabled_default }
				{
				}

				explicit overlay( const std::string& prefix, bool enabled_default = true )
					: enabled{ enabled_default, {}, "esp overlay", prefix }
					, m_box{ prefix }
					, m_skeleton{ prefix }
					, m_health_bar{ prefix }
					, m_ammo_bar{ prefix }
					, m_info_flags{ prefix }
					, m_name{ prefix }
					, m_weapon{ prefix }
					, m_oof_arrow{ prefix }
				{
				}
			};

			std::array<overlay, 2> m_overlay{ { overlay{ "esp enemy" }, overlay{ "esp team", false } } };

			struct chams
			{
				chams_config enemy
				{
					.enabled = { true, {}, "chams", "chams enemy" },
					.primary = {.enabled = { true, {}, "primary layer", "chams enemy" }, .color = { { 173, 192, 255, 150 }, "chams enemy", "primary color" }, .material = { cham_ids::flat, "chams enemy", "primary material" } },
					.secondary = {.enabled = { true, {}, "secondary layer", "chams enemy" }, .color = { { 255, 208, 243, 118 }, "chams enemy", "secondary color" }, .material = { cham_ids::flat_ignorez, "chams enemy", "secondary material" } }
				};
				chams_config enemy_ragdoll{ .enabled = { false, {}, "ragdoll chams", "chams enemy ragdoll" } };
				chams_config team{ .enabled = { false, {}, "chams", "chams team" } };
				chams_config team_ragdoll{ .enabled = { false, {}, "ragdoll chams", "chams team ragdoll" } };
				chams_config local
				{
					.enabled = { true, {}, "chams", "chams local" },
					.overlay = {.enabled = { true, {}, "overlay layer", "chams local" }, .color = { { 173, 192, 255, 175 }, "chams local", "overlay color" }, .material = { cham_ids::outlines, "chams local", "overlay material" } }
				};
				chams_config local_ragdoll{ .enabled = { false, {}, "ragdoll chams", "chams local ragdoll" } };

				chams_config backtrack
				{
					.enabled = { false, {}, "backtrack chams", "chams backtrack" },
					.primary = {.enabled = { false, {}, "primary layer", "chams backtrack" }, .color = { { 173, 192, 255, 25 }, "chams backtrack", "primary color" }, .material = { cham_ids::flat, "chams backtrack", "primary material" } },
					.secondary = {.enabled = { false, {}, "secondary layer", "chams backtrack" }, .color = { { 173, 192, 255, 255 }, "chams backtrack", "secondary color" }, .material = { cham_ids::outlines, "chams backtrack", "secondary material" } }
				};

				chams_config onshot
				{
					.enabled = { false, {}, "onshot chams",    "chams onshot" },
					.primary = {.enabled = { true,  {}, "primary layer",   "chams onshot" }, .color = { { 255, 100, 100, 200 }, "chams onshot", "primary color" }, .material = { cham_ids::flat, "chams onshot", "primary material" } },
					.secondary = {.enabled = { false, {}, "secondary layer", "chams onshot" }, .color = { { 255, 100, 100, 100 }, "chams onshot", "secondary color" }, .material = { cham_ids::flat_ignorez, "chams onshot", "secondary material" } },
					.overlay = {.enabled = { false, {}, "overlay layer",   "chams onshot" }, .color = { { 255, 100, 100, 255 }, "chams onshot", "overlay color" }, .material = { cham_ids::outlines, "chams onshot", "overlay material" } },
				};
				config::val<float> onshot_fade_time {0.8f, "chams onshot", "fade time"};
			} m_chams{};

			struct glow
			{
				glow_target enemy{ .enabled = { true, {}, "glow", "glow enemy" }, .color = { { 173, 192, 255, 40 }, "glow enemy", "color" } };
				glow_target enemy_ragdoll{ .enabled = { false, {}, "ragdoll glow", "glow enemy" }, .color = { { 173, 192, 255, 40 }, "glow enemy", "ragdoll color" } };
				glow_target team{ .enabled = { true, {}, "glow", "glow team" }, .color = { { 225, 225, 225, 40 }, "glow team", "color" } };
				glow_target team_ragdoll{ .enabled = { false, {}, "ragdoll glow", "glow team" }, .color = { { 173, 192, 255, 40 }, "glow team", "ragdoll color" } };
				glow_target local{ .enabled = { false, {}, "glow", "glow local" }, .color = { { 252, 217, 240, 50 }, "glow local", "color" } };
				glow_target local_ragdoll{ .enabled = { false, {}, "ragdoll glow", "glow local" }, .color = { { 173, 192, 255, 40 }, "glow local", "ragdoll color" } };
		} m_glow{};

	} m_player{};

		struct viewmodel
		{
			chams_config weapon
			{
				.enabled = { true, {}, "weapon chams", "viewmodel" },
				.overlay = {.enabled = { true, {}, "overlay layer", "viewmodel weapon" }, .color = { { 217, 173, 202, 175 }, "viewmodel weapon", "overlay color" }, .material = { cham_ids::glow, "viewmodel weapon", "overlay material" } }
			};
			chams_config arms
			{
				.enabled = { true, {}, "arms chams", "viewmodel" },
				.primary = {.enabled = { false, {}, "primary layer", "viewmodel arms" }, .color = { { 173, 192, 255, 255 }, "viewmodel arms", "primary color" }, .material = { cham_ids::outlines, "viewmodel arms", "primary material" } },
				.overlay = {.enabled = { true, {}, "overlay layer", "viewmodel arms" }, .color = { { 173, 192, 255, 255 }, "viewmodel arms", "overlay color" }, .material = { cham_ids::outlines, "viewmodel arms", "overlay material" } }
			};
		} m_viewmodel{};

		struct local_alpha
		{
			xui::setting enabled{ true, {}, "lower opacity", "chams local" };
			config::val<float> opacity{ 0.5f, "chams local", "opacity" };
			xui::setting only_scoped{ true, {}, "only when scoped", "chams local" };
		} m_local_alpha{};

		struct item
		{
			static constexpr auto k_group_count{ 6u };
			static constexpr const char* k_group_names[ ]{ "pistol", "smg", "rifle", "shotgun", "sniper", "utility" };

			struct overlay
			{
				struct group
				{
					enum class display_type : std::uint8_t { text, icon, text_and_icon };

					config::enm<display_type> display{ display_type::icon };
					config::val<float> max_distance{ 50.0f };
					config::col text_color{ { 255, 255, 255, 225 } };
					config::col icon_color{ { 255, 255, 255, 225 } };

					void init( std::string_view cat )
					{
						const auto s = std::string( cat );
						this->display.reg( s, "display" );
						this->max_distance.reg( s, "max distance" );
						this->text_color.reg( s, "text color" );
						this->icon_color.reg( s, "icon color" );
					}
				};

				xui::setting enabled{ true, {}, "item esp", "esp items" };
				xui::setting pistol{ false, {}, "pistol", "esp items" };
				xui::setting smg{ false, {}, "smg", "esp items" };
				xui::setting rifle{ false, {}, "rifle", "esp items" };
				xui::setting shotgun{ false, {}, "shotgun", "esp items" };
				xui::setting sniper{ true, {}, "sniper", "esp items" };
				xui::setting utility{ true, {}, "utility", "esp items" };

				std::array<group, k_group_count> groups{};

				overlay( )
				{
					for ( auto i = 0u; i < k_group_count; ++i )
					{
						this->groups[ i ].init( std::string( "esp items - " ) + k_group_names[ i ] );
					}

					this->groups[ 4 ].display = group::display_type::text_and_icon;
					this->groups[ 4 ].max_distance = 100.0f;
					this->groups[ 5 ].display = group::display_type::text_and_icon;
					this->groups[ 5 ].max_distance = 100.0f;
				}

				xui::setting& group_toggle( std::uint32_t id )
				{
					switch ( id )
					{
					case 0: return this->pistol;
					case 1: return this->smg;
					case 2: return this->rifle;
					case 3: return this->shotgun;
					case 4: return this->sniper;
					case 5: return this->utility;
					default: return this->pistol;
					}
				}

				[[nodiscard]] bool is_active( std::uint32_t group_id ) const
				{
					switch ( group_id )
					{
					case 0: return this->pistol.value;
					case 1: return this->smg.value;
					case 2: return this->rifle.value;
					case 3: return this->shotgun.value;
					case 4: return this->sniper.value;
					case 5: return this->utility.value;
					default: return false;
					}
				}

				group& get_group( std::uint32_t group_id )
				{
					return this->groups[ group_id < k_group_count ? group_id : 2 ];
				}

				const group& get_group( std::uint32_t group_id ) const
				{
					return this->groups[ group_id < k_group_count ? group_id : 2 ];
				}
			} m_overlay{};

			struct chams
			{
				xui::setting enabled{ true, {}, "item chams", "chams items" };
				xui::setting pistol{ false, {}, "pistol", "chams items" };
				xui::setting smg{ false, {}, "smg", "chams items" };
				xui::setting rifle{ false, {}, "rifle", "chams items" };
				xui::setting shotgun{ false, {}, "shotgun", "chams items" };
				xui::setting sniper{ true, {}, "sniper", "chams items" };
				xui::setting utility{ true, {}, "utility", "chams items" };

				std::array<chams_config, k_group_count> groups{};

				chams( )
				{
					for ( auto i = 0u; i < k_group_count; ++i )
					{
						const auto cat = std::string( "chams items - " ) + k_group_names[ i ];
						this->groups[ i ].primary.init( cat, "primary layer" );
						this->groups[ i ].secondary.init( cat, "secondary layer" );
					}

					this->groups[ 4 ].primary.enabled.value = true;
					this->groups[ 4 ].primary.color = { 173, 192, 255, 255 };
					this->groups[ 4 ].primary.material = cham_ids::flat;

					this->groups[ 5 ].primary.enabled.value = true;
					this->groups[ 5 ].primary.color = { 173, 192, 255, 255 };
					this->groups[ 5 ].primary.material = cham_ids::flat;
				}

				xui::setting& group_toggle( std::uint32_t id )
				{
					switch ( id )
					{
					case 0: return this->pistol;
					case 1: return this->smg;
					case 2: return this->rifle;
					case 3: return this->shotgun;
					case 4: return this->sniper;
					case 5: return this->utility;
					default: return this->pistol;
					}
				}

				[[nodiscard]] bool is_active( std::uint32_t group_id ) const
				{
					switch ( group_id )
					{
					case 0: return this->pistol.value;
					case 1: return this->smg.value;
					case 2: return this->rifle.value;
					case 3: return this->shotgun.value;
					case 4: return this->sniper.value;
					case 5: return this->utility.value;
					default: return false;
					}
				}

				chams_config& get_group( std::uint32_t group_id )
				{
					return this->groups[ group_id < k_group_count ? group_id : 2 ];
				}

				const chams_config& get_group( std::uint32_t group_id ) const
				{
					return this->groups[ group_id < k_group_count ? group_id : 2 ];
				}
			} m_chams{};

			struct glow
			{
				xui::setting enabled{ true, {}, "item glow", "glow items" };
				xui::setting pistol{ false, {}, "pistol", "glow items" };
				xui::setting smg{ false, {}, "smg", "glow items" };
				xui::setting rifle{ false, {}, "rifle", "glow items" };
				xui::setting shotgun{ false, {}, "shotgun", "glow items" };
				xui::setting sniper{ true, {}, "sniper", "glow items" };
				xui::setting utility{ true, {}, "utility", "glow items" };

				std::array<glow_target, k_group_count> groups{};

				glow( )
				{
					for ( auto i = 0u; i < k_group_count; ++i )
					{
						const auto cat = std::string( "glow items - " ) + k_group_names[ i ];
						this->groups[ i ].init( cat );
					}

					this->groups[ 4 ].color = { 173, 192, 255, 50 };
					this->groups[ 5 ].color = { 173, 192, 255, 50 };
				}

				xui::setting& group_toggle( std::uint32_t id )
				{
					switch ( id )
					{
					case 0: return this->pistol;
					case 1: return this->smg;
					case 2: return this->rifle;
					case 3: return this->shotgun;
					case 4: return this->sniper;
					case 5: return this->utility;
					default: return this->pistol;
					}
				}

				[[nodiscard]] bool is_active( std::uint32_t group_id ) const
				{
					switch ( group_id )
					{
					case 0: return this->pistol.value;
					case 1: return this->smg.value;
					case 2: return this->rifle.value;
					case 3: return this->shotgun.value;
					case 4: return this->sniper.value;
					case 5: return this->utility.value;
					default: return false;
					}
				}

				glow_target& get_group( std::uint32_t group_id )
				{
					return this->groups[ group_id < k_group_count ? group_id : 2 ];
				}

				const glow_target& get_group( std::uint32_t group_id ) const
				{
					return this->groups[ group_id < k_group_count ? group_id : 2 ];
				}
			} m_glow{};
		} m_item{};

		struct projectile
		{
			static constexpr auto k_group_count{ 6u };
			static constexpr const char* k_group_names[ ]{ "he grenade", "flashbang", "smoke", "molotov", "decoy", "inferno" };

			struct overlay
			{
				struct infernos
				{
					config::col fill_color{ { 173, 192, 255, 50 }, "esp inferno", "fill color" };
					config::col outline_color{ { 255, 171, 234, 150 }, "esp inferno", "outline color" };
					config::val<float> outline_thickness{ 1.5f, "esp inferno", "outline thickness" };
					xui::setting glow{ true, {}, "glow", "esp inferno" };
					config::val<float> glow_strength{ 0.55f, "esp inferno", "glow strength" };
				} m_infernos{};

				struct indicator
				{
					static constexpr auto k_group_count{ 3u };
					static constexpr const char* k_group_names[ ]{ "he grenade", "molotov", "inferno" };

					struct group
					{
						xui::setting enabled{ true, {}, "", "" };
						config::col arc_color{ { 173, 192, 255, 225 } };
						config::col icon_color{ { 255, 255, 255, 225 } };
						config::col background_color{ { 0, 0, 0, 175 } };
						xui::setting glow{ true, {}, "", "" };
						config::val<float> glow_strength{ 1.0f };

						void init( std::string_view cat )
						{
							const auto s = std::string( cat );
							this->enabled.name = std::string( cat );
							this->arc_color.reg( s, "arc color" );
							this->icon_color.reg( s, "icon color" );
							this->background_color.reg( s, "background color" );
							this->glow_strength.reg( s, "glow strength" );
						}
					};

					std::array<group, k_group_count> groups{};

					indicator( )
					{
						for ( auto i = 0u; i < k_group_count; ++i )
						{
							this->groups[ i ].init( std::string( "esp indicator - " ) + k_group_names[ i ] );
						}

						this->groups[ 2 ].arc_color = { 255, 171, 234, 225 };
					}

					group& get_group( std::uint32_t id )
					{
						return this->groups[ id < k_group_count ? id : 0 ];
					}

					const group& get_group( std::uint32_t id ) const
					{
						return this->groups[ id < k_group_count ? id : 0 ];
					}
				} m_indicator{};

				struct group
				{
					enum class display_type : std::uint8_t { text, icon, text_and_icon };

					config::enm<display_type> display{ display_type::text_and_icon };
					config::val<float> max_distance{ 100.0f };
					config::col text_color{ { 255, 255, 255, 225 } };
					config::col icon_color{ { 255, 255, 255, 225 } };

					void init( std::string_view cat )
					{
						const auto s = std::string( cat );
						this->display.reg( s, "display" );
						this->max_distance.reg( s, "max distance" );
						this->text_color.reg( s, "text color" );
						this->icon_color.reg( s, "icon color" );
					}
				};

				xui::setting enabled{ true, {}, "projectile esp", "esp projectiles" };
				xui::setting he_grenade{ true, {}, "he grenade", "esp projectiles" };
				xui::setting flashbang{ true, {}, "flashbang", "esp projectiles" };
				xui::setting smoke{ true, {}, "smoke", "esp projectiles" };
				xui::setting molotov{ true, {}, "molotov", "esp projectiles" };
				xui::setting decoy{ true, {}, "decoy", "esp projectiles" };
				xui::setting inferno{ true, {}, "inferno", "esp projectiles" };

				std::array<group, 5> groups{};

				overlay( )
				{
					for ( auto i = 0u; i < 5u; ++i )
					{
						this->groups[ i ].init( std::string( "esp projectiles - " ) + k_group_names[ i ] );
					}
				}

				xui::setting& group_toggle( std::uint32_t id )
				{
					switch ( id )
					{
					case 0: return this->he_grenade;
					case 1: return this->flashbang;
					case 2: return this->smoke;
					case 3: return this->molotov;
					case 4: return this->decoy;
					case 5: return this->inferno;
					default: return this->he_grenade;
					}
				}

				[[nodiscard]] bool is_active( std::uint32_t group_id ) const
				{
					switch ( group_id )
					{
					case 0: return this->he_grenade.value;
					case 1: return this->flashbang.value;
					case 2: return this->smoke.value;
					case 3: return this->molotov.value;
					case 4: return this->decoy.value;
					case 5: return this->inferno.value;
					default: return false;
					}
				}

				// Нужна для раннего выхода в on_render: без неё список сущностей
				// проектайлов обходился целиком даже при полностью выключенном ESP.
				[[nodiscard]] bool any_active( ) const
				{
					return this->he_grenade.value || this->flashbang.value || this->smoke.value
						|| this->molotov.value || this->decoy.value || this->inferno.value;
				}

				group& get_group( std::uint32_t group_id )
				{
					return this->groups[ group_id < 5 ? group_id : 0 ];
				}

				const group& get_group( std::uint32_t group_id ) const
				{
					return this->groups[ group_id < 5 ? group_id : 0 ];
				}
			} m_overlay{};

			struct tracers
			{

			} m_tracers{};
		} m_projectile{};

		struct other
		{
			xui::setting bomb_timer{ true, {}, "bomb timer", "other esp" };
			xui::setting spectator_list{ true, {}, "spectator list", "other esp" };
		} m_other{};
	};

	struct changer
	{
		struct applied_skin
		{
			int paint_kit_id{};
			float wear{ 0.01f };
			int seed{};
			bool stattrak{};
			std::int16_t override_def_index{ 0 };

			bool operator==( const applied_skin& ) const = default;
		};

		struct skin_map_field : config::custom_field
		{
			std::unordered_map<std::int16_t, applied_skin> data{};

			nlohmann::json serialize( ) const override
			{
				auto j = nlohmann::json::object( );
				for ( const auto& [def, s] : data )
				{
					j[ std::to_string( def ) ] = nlohmann::json
					{
						{"p", s.paint_kit_id},
						{"w", s.wear},
						{"s", s.seed},
						{"t", s.stattrak},
						{"d", s.override_def_index}
					};
				}

				return j;
			}

			void deserialize( const nlohmann::json& j ) override
			{
				data.clear( );

				if ( !j.is_object( ) )
				{
					return;
				}

				for ( auto it = j.begin( ); it != j.end( ); ++it )
				{
					try
					{
						const auto def = static_cast< std::int16_t >( std::stoi( it.key( ) ) );
						auto& s = data[ def ];
						s.paint_kit_id = it.value( ).value( "p", 0 );
						s.wear = it.value( ).value( "w", 0.01f );
						s.seed = it.value( ).value( "s", 0 );
						s.stattrak = it.value( ).value( "t", false );
						s.override_def_index = it.value( ).value( "d", static_cast< std::int16_t >( 0 ) );
					}
					catch ( ... ) {}
				}
			}
		};

		struct agent_selection_field : config::custom_field
		{
			std::int16_t ct_def{};
			std::int16_t t_def{};

			nlohmann::json serialize( ) const override
			{
				return nlohmann::json
				{
					{ "ct", ct_def },
					{ "t", t_def }
				};
			}

			void deserialize( const nlohmann::json& j ) override
			{
				if ( !j.is_object( ) )
				{
					return;
				}

				ct_def = j.value( "ct", static_cast< std::int16_t >( 0 ) );
				t_def = j.value( "t", static_cast< std::int16_t >( 0 ) );
			}
		};

		struct custom_agent_entry
		{
			std::string name{};
			std::string model_path{};
			int team{ 3 }; // 2 = T, 3 = CT

			bool operator==( const custom_agent_entry& ) const = default;
		};

		struct custom_agents_field : config::custom_field
		{
			std::vector<custom_agent_entry> entries{};
			int selected_ct{ 0 };
			int selected_t{ 0 };

			nlohmann::json serialize( ) const override
			{
				nlohmann::json j;
				j["selected_ct"] = selected_ct;
				j["selected_t"] = selected_t;
				j["entries"] = nlohmann::json::array( );
				
				for ( const auto& entry : entries )
				{
					j["entries"].push_back( {
						{ "name", entry.name },
						{ "model_path", entry.model_path },
						{ "team", entry.team }
					} );
				}
				
				return j;
			}

			void deserialize( const nlohmann::json& j ) override
			{
				if ( !j.is_object( ) )
				{
					return;
				}

				selected_ct = j.value( "selected_ct", 0 );
				selected_t = j.value( "selected_t", 0 );
				
				entries.clear( );
				if ( j.contains( "entries" ) && j["entries"].is_array( ) )
				{
					for ( const auto& item : j["entries"] )
					{
						custom_agent_entry entry;
						entry.name = item.value( "name", std::string( "Custom Agent" ) );
						entry.model_path = item.value( "model_path", std::string( ) );
						entry.team = item.value( "team", 3 );
						entries.push_back( entry );
					}
				}
			}
		};

		skin_map_field skins{};
		agent_selection_field agents{};
		custom_agents_field custom_agents{};

		xui::setting cross_weapon_skins{ false, {}, "cross-weapon skins", "changer" };

		changer( )
		{
			config::detail::register_field( { .key = config::detail::make_key( "changer", "applied skins" ), .type = config::field_type::custom, .ptr = &skins, .count = 1 } );
			config::detail::register_field( { .key = config::detail::make_key( "changer", "agents" ), .type = config::field_type::custom, .ptr = &agents, .count = 1 } );
			config::detail::register_field( { .key = config::detail::make_key( "changer", "custom agents" ), .type = config::field_type::custom, .ptr = &custom_agents, .count = 1 } );
		}
	};

	struct misc
	{
		struct scoreboard_weapons
		{
			xui::setting enabled{ false, {}, "scoreboard weapons", "misc" };
			config::col color{ { 255, 255, 0, 255 }, "misc", "scoreboard weapons color" };
		} m_scoreboard_weapons{};

		struct name_changer
		{
			xui::setting clantag{ false, {}, "clantag", "name changer" };
			xui::setting override_name{ false, {}, "override name", "name changer" };
			config::str name{ "Player", "name changer", "name" };
			config::str clantag_text{ "DarkFox cc", "name changer", "clantag text" };
		} m_name_changer{};

		struct projectile_trajectory
		{
			xui::setting enabled{ true, {}, "projectile trajectory", "trajectory" };
			xui::setting straight_throw{ true, {}, "straight throw", "trajectory" };

			config::col held_color{ { 173, 192, 255, 255 }, "trajectory", "held color" };
			config::col thrown_color{ { 220, 225, 240, 255 }, "trajectory", "thrown color" };
			config::col will_deal_damage_held_color{ { 252, 217, 240, 255 }, "trajectory", "will damage held color" };
			config::col will_deal_damage_thrown_color{ { 252, 217, 240, 255 }, "trajectory", "will damage thrown color" };

			xui::setting glow{ true, {}, "glow", "trajectory" };
			config::val<float> glow_strength{ 1.0f, "trajectory", "glow strength" };
		} m_projectile_trajectory{};

		struct impacts
		{
			enum class sound_type : int { shop_click, home_click, bell, killcard, bullet_casing, coin_pickup, item_drop, popcan, key_press, custom };
			enum class marker_type : int { classic, damage, both };
			enum class bullet_impact_type : int { overlay, sparks, both };

			xui::setting hit_log{ true, {}, "hit logs", "impacts" };
			config::val<float> hit_log_duration{ 3.5f, "impacts", "hit log duration" };
			xui::setting console_log{ true, {}, "console logs", "impacts" };
			xui::setting chat_log{ false, {}, "chat logs", "impacts" };

			xui::setting miss_log{ true, {}, "miss logs", "impacts" };
			config::val<float> miss_log_duration{ 4.5f, "impacts", "miss log duration" };

			xui::setting hit_sound{ true, {}, "hit sound", "impacts" };
			config::enm<sound_type> hit_sound_type{ sound_type::killcard, "impacts", "hit sound type" };
			config::val<float> hit_sound_volume{ 25.0f, "impacts", "hit sound volume" };
			config::str custom_hit_sound{ "hit.wav", "impacts", "custom hit sound" };

			xui::setting death_sound{ true, {}, "death sound", "impacts" };
			config::enm<sound_type> death_sound_type{ sound_type::bell, "impacts", "death sound type" };
			config::val<float> death_sound_volume{ 20.0f, "impacts", "death sound volume" };
			config::str custom_death_sound{ "kill.wav", "impacts", "custom death sound" };

			xui::setting hit_effect{ true, {}, "hit effect", "impacts" };
			config::col hit_effect_color{ { 173, 192, 255, 255 }, "impacts", "hit effect color" };
			config::val<float> hit_effect_duration{ 0.75f, "impacts", "hit effect duration" };
			config::val<float> hit_effect_strength{ 60.0f, "impacts", "hit effect strength" };

			xui::setting death_effect{ true, {}, "death effect", "impacts" };
			enum class death_effect_type : int
			{
				fade,
				killstars,
				sparks,
				explosion,
				halo,
				molotov_air,
				c4_explosion,
				blood,
				smoke
			};
			config::enm<death_effect_type> death_effect_style{ death_effect_type::fade, "impacts", "death effect style" };
			config::col death_effect_color{ { 173, 192, 255, 255 }, "impacts", "death effect color" };

			xui::setting bullet_impact_effect{ true, {}, "bullet impacts", "impacts" };
			config::enm<bullet_impact_type> bullet_impact_effect_type{ bullet_impact_type::overlay, "impacts", "bullet impact type" };
			config::col bullet_impact_effect_fill_color{ { 173, 192, 255, 85 }, "impacts", "bullet impact fill color" };
			config::col bullet_impact_effect_edge_color{ { 173, 192, 255, 255 }, "impacts", "bullet impact edge color" };
			config::col bullet_impact_effect_color_spark{ { 173, 192, 255, 255 }, "impacts", "bullet impact spark color" };
			config::val<float> bullet_impact_effect_duration{ 2.5f, "impacts", "bullet impact duration" };
			xui::setting bullet_impact_effect_glow{ true, {}, "glow", "bullet impacts" };
			config::val<float> bullet_impact_effect_glow_strength{ 1.0f, "bullet impacts", "glow strength" };

			xui::setting bullet_tracers{ false, {}, "bullet tracers", "impacts" };
			config::col bullet_tracer_color{ { 173, 192, 255, 255 }, "impacts", "bullet tracer color" };
			config::val<float> bullet_tracer_duration{ 0.5f, "impacts", "bullet tracer duration" };

			xui::setting hit_marker{ true, {}, "hit marker", "impacts" };
			config::enm<marker_type> hit_marker_type{ marker_type::classic, "impacts", "hit marker type" };
			config::val<float> hit_marker_duration{ 2.5f, "impacts", "hit marker duration" };
			config::col hit_marker_color{ { 255, 255, 255, 255 }, "impacts", "hit marker color" };
			xui::setting hit_marker_glow{ true, {}, "glow", "hit marker" };
			config::val<float> hit_marker_glow_strength{ 1.0f, "hit marker", "glow strength" };
		} m_impacts{};

		struct removals
		{
			xui::setting crosshair{ true, {}, "remove crosshair", "removals" };
			xui::setting scope{ true, {}, "remove scope", "removals" };
			xui::setting skybox_fog{ true, {}, "remove skybox fog", "removals" };
			xui::setting overhead{ true, {}, "remove overhead", "removals" };
			xui::setting legs{ true, {}, "remove legs", "removals" };
			xui::setting skybox_3d{ true, {}, "remove 3d skybox", "removals" };
			xui::setting recoil{ true, {}, "remove recoil", "removals" };
			xui::setting decals{ true, {}, "remove decals", "removals" };
			xui::setting smoke{ true, {}, "remove smoke", "removals" };
			config::val<float> flash_alpha{ 25.0f, "removals", "flash alpha" };
		} m_removals{};

		// Оптимизация рендера: сначала режем то, что вообще не влияет на
		// геймплей, потом уже внешний вид. Мастер-тумблер -- сверху, чтобы
		// можно было одним кликом вернуть ванильную картинку.
		struct optimization
		{
			xui::setting enabled{ true, {}, "optimization", "optimization" };

			// --- геометрия и материалы ---
			xui::setting remove_grass{ true, {}, "remove grass", "optimization - world" };
			xui::setting remove_foliage{ true, {}, "remove foliage & trees", "optimization - world" };
			xui::setting remove_ropes{ true, {}, "remove wires & ropes", "optimization - world" };
			xui::setting remove_decals{ false, {}, "remove decals", "optimization - world" };
			xui::setting remove_props{ false, {}, "remove small props", "optimization - world" };
			config::val<float> prop_min_scale{ 1.0f, "optimization - world", "prop scale threshold" };

			// --- освещение ---
			xui::setting flat_lighting{ true, {}, "flat lighting", "optimization - lighting" };
			config::val<float> light_intensity{ 0.8f, "optimization - lighting", "light intensity" };
			config::col light_color{ { 255, 255, 255, 255 }, "optimization - lighting", "light color" };

			// --- материалы ---
			xui::setting remove_normal_maps{ false, {}, "strip normal maps", "optimization - materials" };
			xui::setting remove_specular{ false, {}, "strip specular", "optimization - materials" };
			xui::setting remove_detail{ false, {}, "strip detail layer", "optimization - materials" };

			// Картонные текстуры. Отдельный режим, а не набор галочек выше:
			// он не «упрощает» родной материал, а заменяет альбедо на
			// однотонную заливку. Родной шейдер при этом сохраняется, поэтому
			// объект продолжает получать свет и писать глубину -- картинка
			// остаётся читаемой, но перестаёт быть текстурной.
			xui::setting cardboard{ false, {}, "cardboard world", "optimization - materials" };
			config::col cardboard_color{ { 196, 176, 140, 255 }, "optimization - materials", "cardboard color" };
			config::val<float> cardboard_roughness{ 1.0f, "optimization - materials", "cardboard roughness" };
			xui::setting cardboard_players{ false, {}, "cardboard players", "optimization - materials" };

			// --- тени ---
			// Тени -- это отдельный проход на каждый источник света плюс
			// shadow-атлас, который перерисовывается при любом движении
			// геометрии в кадре. Самая дорогая вещь в сцене после
			// пост-обработки, поэтому выключены по умолчанию.
			xui::setting remove_shadows{ true, {}, "remove shadows", "optimization - shadows" };

			// --- эффекты и частицы ---
			xui::setting remove_particles{ true, {}, "remove particles", "optimization - effects" };
			xui::setting remove_beams{ true, {}, "remove beams", "optimization - effects" };
			xui::setting remove_glow{ true, {}, "remove glow sprites", "optimization - effects" };
			xui::setting remove_rain_snow{ true, {}, "remove weather particles", "optimization - effects" };

			// --- пост-обработка ---
			xui::setting remove_bloom{ true, {}, "remove post-process bloom", "optimization - post" };
			xui::setting remove_dof{ true, {}, "remove depth of field", "optimization - post" };
			xui::setting remove_motion_blur{ true, {}, "remove motion blur", "optimization - post" };
			xui::setting remove_ssao{ true, {}, "remove ambient occlusion", "optimization - post" };

			// --- дистанция ---
			xui::setting limit_view_distance{ false, {}, "limit view distance", "optimization - distance" };
			config::val<float> view_distance{ 6000.0f, "optimization - distance", "view distance" };
			// Дальняя граница отдельно от лимита по расстоянию. Разница в том,
			// ЧТО именно вырезается: limit_view_distance бьёт по объектам
			// (пропа, модели, эффекты), а дальняя граница -- по геометрии
			// мира, то есть по полу, стенам и крышам, которые и рисуют
			// основную массу треугольников. Порог задаётся больше лимита
			// объектов, поэтому сначала отваливается мелочь, и только потом
			// режется сам уровень.
			xui::setting limit_far_plane{ false, {}, "clip far plane", "optimization - distance" };
			config::val<float> far_plane{ 8000.0f, "optimization - distance", "far plane" };

			// --- геометрия ---
			// Порог размера объекта: всё, что мельче, не рисуется. Считается
			// по тому же полю примитива, что и порог пропы, поэтому две эти
			// настройки нельзя включать с разными ожиданиями -- работает
			// максимум из двух порогов.
			xui::setting cull_small_geometry{ false, {}, "cull tiny geometry", "optimization - geometry" };
			config::val<float> min_screen_size{ 1.0f, "optimization - geometry", "min object size" };
			// Принудительный низкий LOD. В отличие от сдвига дистанции это не
			// ползунок, а бит на scene object: движок берёт самую грубую
			// модель, какая есть у объекта. Геометрия остаётся на месте --
			// меняется только её детализация, поэтому на попадание по хитбоксу
			// это не влияет.
			xui::setting force_low_lod{ false, {}, "force low detail models", "optimization - geometry" };

			// --- диагностика ---
			xui::setting show_stats{ true, {}, "show stats", "optimization - stats" };
		} m_optimization{};
		struct camera
		{
			xui::setting change_fov{ true, {}, "custom fov", "camera" };
			config::val<float> fov{ 115.0f, "camera", "fov" };

			xui::setting scoped_fov_override{ false, {}, "scoped fov override", "camera" };
			config::val<float> scoped_fov{ 40.0f, "camera", "scoped fov" };

			xui::setting thirdperson{ true, { VK_MBUTTON, xui::bind_mode::toggle }, "thirdperson", "camera" };
			config::val<float> thirdperson_distance{ 85.0f, "camera", "thirdperson distance" };
			config::val<float> thirdperson_hull_size{ 12.0f, "camera", "thirdperson hull size" };

			xui::setting change_aspect_ratio{ false, {}, "custom aspect ratio", "camera" };
			config::val<float> aspect_ratio{ 1.333f, "camera", "aspect ratio" };
		} m_camera{};

		struct viewmodel_adjust
		{
			xui::setting enabled{ false, {}, "viewmodel adjust", "viewmodel" };
			config::val<float> offset_x{ 0.0f, "viewmodel", "offset x" };
			config::val<float> offset_y{ 0.0f, "viewmodel", "offset y" };
			config::val<float> offset_z{ 0.0f, "viewmodel", "offset z" };
			config::val<float> fov{ 68.0f, "viewmodel", "viewmodel fov" };
		} m_viewmodel_adjust{};

		struct hud
		{
			struct crosshair
			{
				xui::setting enabled{ true, {}, "crosshair overlay", "crosshair" };
				config::val<float> size{ 1.0f, "crosshair", "size" };
				config::val<float> outline{ 1.0f, "crosshair", "outline" };
				config::col color{ { 173, 192, 255, 255 }, "crosshair", "color" };
				config::col outline_color{ { 15, 15, 25, 200 }, "crosshair", "outline color" };

				// Rotating ring drawn around the dot. Segments are drawn as arcs of
				// a circle, so "count" is how many gaps the ring is broken into.
				xui::setting spinner{ false, {}, "spinner", "crosshair" };
				config::val<float> spinner_radius{ 14.0f, "crosshair", "spinner radius" };
				config::val<float> spinner_thickness{ 2.0f, "crosshair", "spinner thickness" };
				config::val<float> spinner_speed{ 90.0f, "crosshair", "spinner speed" };
				config::val<float> spinner_arc{ 0.55f, "crosshair", "spinner arc" };
				config::val<int> spinner_segments{ 3, "crosshair", "spinner segments" };
				config::col spinner_color{ { 198, 128, 240, 255 }, "crosshair", "spinner color" };
				xui::setting spinner_gradient{ true, {}, "spinner gradient", "crosshair" };
			} m_crosshair{};

			struct scope
			{
				xui::setting enabled{ true, {}, "scope overlay", "scope overlay" };
				config::val<float> line_length{ 125.0f, "scope overlay", "line length" };
				config::val<float> gap{ 8.0f, "scope overlay", "gap" };
				config::val<float> thickness{ 0.5f, "scope overlay", "thickness" };
				config::val<float> anim_speed{ 10.0f, "scope overlay", "anim speed" };
				config::col color{ { 173, 192, 255, 255 }, "scope overlay", "color" };
				xui::setting fade_in{ true, {}, "fade in", "scope overlay" };

				xui::setting glow{ true, {}, "glow", "scope overlay" };
				config::val<float> glow_strength{ 1.0f, "scope overlay", "glow strength" };
			} m_scope{};

			struct hat
			{
				enum class hat_type : std::uint8_t { kasa, bucket };

				xui::setting enabled{ false, {}, "hat", "hat" };
				config::enm<hat_type> type{ hat_type::kasa, "hat", "type" };
				config::col color{ { 255, 171, 234, 160 }, "hat", "color" };
				config::col secondary_color{ { 173, 192, 255, 160 }, "hat", "secondary color" };
				xui::setting glow{ true, {}, "glow", "hat" };
				config::val<float> glow_strength{ 1.0f, "hat", "glow strength" };
			} m_hat{};

			struct velocity
			{
				xui::setting counter{ false, {}, "velocity counter", "velocity hud" };
				xui::setting chart{ false, {}, "velocity chart", "velocity hud" };
				config::col color{ { 173, 192, 255, 255 }, "velocity hud", "color" };
				config::val<float> bottom_offset{ 80.0f, "velocity hud", "bottom offset" };
				config::val<float> chart_width{ 200.0f, "velocity hud", "chart width" };
				config::val<float> chart_height{ 44.0f, "velocity hud", "chart height" };
			} m_velocity{};

			struct waifu {
				static constexpr int k_count = 3;

				xui::setting enabled {false, {}, "waifu", "hud"};
				config::val<int> selected {0, "hud", "waifu character"};
				config::val<float> size {180.0f, "hud", "waifu size"};
				config::val<float> opacity {1.0f, "hud", "waifu opacity"};
				config::val<float> offset_x {0.0f, "hud", "waifu offset x"};   
				config::val<float> offset_y {0.0f, "hud", "waifu offset y"};   
			} m_waifu {};

			struct keybinds_cfg
			{
				// list  - the stacked panel with a header and one row per bind.
				// pills - each bind as its own rounded badge, stacked loosely.
				enum class layout_style : std::uint8_t { list, pills };

				xui::setting enabled{ true, {}, "keybinds", "keybinds" };
				config::enm<layout_style> style{ layout_style::pills, "keybinds", "style" };

				// A bind carrying a number (min damage, hit chance) can show it as
				// text, as a ring filled to its share of the slider range, or both.
				xui::setting show_values{ true, {}, "show values", "keybinds" };
				xui::setting show_rings{ true, {}, "show rings", "keybinds" };

				// The only glow in the whole interface, and it sits on the outer
				// contour of the badge rather than behind the text.
				xui::setting outer_glow{ true, {}, "outer glow", "keybinds" };
				config::val<float> glow_strength{ 0.6f, "keybinds", "glow strength" };

				config::col color{ { 235, 238, 245, 255 }, "keybinds", "color" };

				// Screen position, as a fraction of the viewport so it survives a
				// resolution change. Dragged with the mouse while the menu is open.
				config::val<float> position_x{ 0.015f, "keybinds", "position x" };
				config::val<float> position_y{ 0.55f, "keybinds", "position y" };
			} m_keybinds{};
		} m_hud{};

		struct post_process
		{
			struct chromatic_aberration
			{
				xui::setting enabled{ false, {}, "chromatic aberration", "post process" };
				config::val<float> intensity{ 0.003f, "post process", "chromatic aberration intensity" };
			} m_chromatic_aberration{};
		} m_post_process{};

		struct dlight
		{
			xui::setting enabled{ false, {}, "dynamic light", "misc" };
			config::col color{ { 255, 255, 255, 255 }, "dlight", "color" };
			config::val<float> radius{ 300.0f, "dlight", "radius" };
			config::val<float> z_offset{ 2.0f, "dlight", "z offset" };
		} m_dlight{};

		// Полный скан карты: ищет прострелы и ставит на них метки.
		// Скан идёт порциями по кадрам (см. map_scan::step_scan), потому что
		// крутится на рендер-потоке, а трассы дорогие.
        struct map_scan
        {
            // Включено по умолчанию: фича сама себя ограничивает бюджетом
            // кадра, а без неё ragebot не знает про окна прострела. Выключить
            // можно, если нужен строго нулевой оверхед.
            xui::setting enabled{ true, {}, "map scan", "map scan" };
            xui::setting draw_entry{ true, {}, "entry point", "map scan" };
            xui::setting draw_line{ true, {}, "wallbang line", "map scan" };
            xui::setting show_damage{ true, {}, "damage", "map scan" };

            // Передавать найденные окна прострела ragebot'у: его shot через
            // подтверждённую стену получает приоритет в select_best. Без этого
            // скан рисует метки, но стрельба их не видит.
            xui::setting feed_ragebot{ true, {}, "feed ragebot", "map scan" };

			// Строка прогресса: фаза (сетка/веер), процент и число меток.
			// Скан идёт несколько секунд, и без индикатора не отличить его от
			// зависшего оверлея.
			xui::setting show_progress{ true, {}, "progress", "map scan" };

			// Хранить разметку прострелов на диск, по файлу на карту
			// (см. map_store). Файл обфусцирован: XTEA + XOR-гамма,
			// ключ производный от имени карты.
			//
			// Зачем: полный скан -- тысячи трасс, при каждом рестарте он
			// считается заново. Файл превращает разовую работу в
			// постоянную -- ragebot знает прострелы с первого тика.
			xui::setting persist{ true, {}, "save to file", "map scan" };

			// Читать файл карты при входе на уровень. Отдельно от persist,
			// чтобы можно было писать, но не грузить (или наоборот) --
			// например, при отладке самого сканера.
			xui::setting autoload{ true, {}, "load from file", "map scan" };

            config::col color{ { 255, 180, 80, 220 }, "map scan", "color" };
            config::val<float> radius{ 6.0f, "map scan", "marker radius" };
        } m_map_scan{};

		struct autobuy
		{
			xui::setting enabled{ true, {}, "auto buy", "autobuy" };
			config::val<int> primary_weapon{ 3, "autobuy", "primary weapon" };
			config::val<int> secondary_weapon{ 3, "autobuy", "secondary weapon" };
			xui::setting armor{ true, {}, "armor", "autobuy" };
			xui::setting defuser{ true, {}, "defuser", "autobuy" };
			xui::setting taser{ true, {}, "taser", "autobuy" };
			config::bools<5> grenades{ { true, true, true, false, false }, "autobuy", "grenades" };
		} m_autobuy{};

		xui::setting preserve_killfeed{ true, {}, "preserve killfeed", "misc" };
		xui::setting reveal_radar{ true, {}, "reveal radar", "misc" };
		xui::setting disable_game_logs{ true, {}, "disable game logs", "misc" };
		config::val<int> menu_key{ VK_DELETE, "misc", "menu key" };

		struct interface_settings
		{
			config::val<float> animation_speed{ 15.0f, "interface", "animation speed" };

			xui::setting menu_glow_enabled{ true, {}, "menu glow", "interface" };

			xui::setting watermark_enabled{ true, {}, "watermark", "interface" };
			xui::setting watermark_show_fps{ true, {}, "show fps", "interface" };
			xui::setting watermark_show_ping{ true, {}, "show ping", "interface" };
			xui::setting watermark_show_time{ true, {}, "show time", "interface" };
			xui::setting watermark_show_user{ true, {}, "show user", "interface" };
			xui::setting watermark_show_map{ true, {}, "show map", "interface" };
			xui::setting watermark_show_tick{ true, {}, "show tick", "interface" };
			xui::setting watermark_show_velocity{ true, {}, "show velocity", "interface" };
			config::val<int> watermark_position_x{ 20, "interface", "watermark position x" };
			config::val<int> watermark_position_y{ 20, "interface", "watermark position y" };

			xui::setting hud_blur{ true, {}, "hud blur", "interface" };

			config::val<bool> show_rage_profiler{ false, "interface.show_rage_profiler", "debug" };
		} m_interface{};

		struct watermark_cfg
		{
			xui::setting enabled  { true, {}, "watermark",       "watermark" };
			xui::setting show_fps { true, {}, "show fps",        "watermark" };
			xui::setting show_ping{ true, {}, "show ping",       "watermark" };
			xui::setting show_time{ true, {}, "show time",       "watermark" };
			xui::setting show_user{ true, {}, "show user",       "watermark" };
			xui::setting show_map { true, {}, "show map",        "watermark" };
			xui::setting show_tick{ true, {}, "show tick",       "watermark" };
			xui::setting show_velocity{ true, {}, "show velocity", "watermark" };
			xui::setting show_icon{ false, {}, "show icon", "watermark" };
			config::col color{ { 235, 238, 245, 255 }, "watermark", "color" };
		} m_watermark{};

		struct spectators_cfg
		{
			xui::setting enabled{ true, {}, "spectators list", "spectators" };
			config::col color{ { 235, 238, 245, 255 }, "spectators", "color" };
		} m_spectators{};

		struct widgets_cfg
		{
			enum class style : std::uint8_t { modern, classic, neo, glass };

			config::enm<style> widget_style{ style::modern, "widgets", "style" };

			struct glass_cfg
			{
				config::col text_color{ { 235, 238, 248, 255 }, "glass widget", "text color" };
				config::col icon_color{ { 173, 192, 255, 255 }, "glass widget", "icon color" };
				xui::setting per_stat_icon_colors{ false, {}, "per stat icon colors", "glass widget" };
				config::col logo_icon_color{ { 173, 192, 255, 255 }, "glass widget", "logo icon color" };
				config::col fps_icon_color{ { 173, 192, 255, 255 }, "glass widget", "fps icon color" };
				config::col ping_icon_color{ { 173, 192, 255, 255 }, "glass widget", "ping icon color" };
				config::col time_icon_color{ { 173, 192, 255, 255 }, "glass widget", "time icon color" };
				config::col vel_icon_color{ { 173, 192, 255, 255 }, "glass widget", "velocity icon color" };
				config::col warn_text_color{ { 255, 92, 92, 255 }, "glass widget", "warn text color" };
				config::col warn_icon_color{ { 255, 92, 92, 255 }, "glass widget", "warn icon color" };
				config::val<int> ping_warn_threshold{ 80, "glass widget", "ping warn threshold" };
				config::col bg_color{ { 12, 14, 20, 155 }, "glass widget", "background color" };
				config::col shadow_color{ { 0, 0, 0, 255 }, "glass widget", "shadow color" };
				config::col avatar_ring_color{ { 255, 255, 255, 40 }, "glass widget", "avatar ring color" };
				config::val<float> blur_strength{ 1.0f, "glass widget", "blur strength" };
				config::val<float> shadow_strength{ 1.4f, "glass widget", "shadow strength" };
				config::val<float> shadow_spread{ 1.2f, "glass widget", "shadow spread" };
				config::val<float> icon_size{ 15.0f, "glass widget", "icon size" };
				config::val<float> pill_height{ 32.0f, "glass widget", "pill height" };
				config::val<float> section_gap{ 16.0f, "glass widget", "section gap" };
				config::val<float> pad_x{ 14.0f, "glass widget", "padding x" };
				xui::setting show_avatar{ true, {}, "show avatar", "glass widget" };
			} m_glass{};
		} m_widgets{};
	};

	struct movement
	{
		xui::setting bhop{ true, {}, "bhop", "movement" };
		// Авто-bhop: прыгать без удержания пробела. Выключено -- прежнее
		// поведение, bhop только выправляет тайминг ручных прыжков.
		xui::setting bhop_auto{ false, {}, "bhop auto", "movement" };
		// Two presentations of the same analog strafer. The subtick budget is the
		// only real difference: without the quantizer the full 32-step curve is
		// what makes the strafe smooth, while sv_quantize_movement_input 1 rounds
		// everything past the first few steps away, so spending 32 there buys
		// nothing. Config keys stay as they were so existing configs keep working.
		xui::setting airstrafe{ true, {}, "airstrafe", "movement" };
		xui::setting valve_strafer{ false, {}, "valve strafer", "movement" };
		xui::setting airstrafe_fully_directional{ true, {}, "fully directional", "movement - airstrafe" };
		/// Ширина угла атаки, в пределах которого стрейфер доворачивает вектор
		/// желания. Меньше -- аккуратнее и медленнее, больше -- агрессивнее.
		config::val<float> airstrafe_max_angle{ 45.0f, "movement - airstrafe", "max strafe angle" };
		/// Порог скорости, ниже которого стрейфер не трогает ввод: на малой
		/// скорости идеальный угол становится шумным и ломает разгон.
		config::val<float> airstrafe_min_speed{ 5.0f, "movement - airstrafe", "min strafe speed" };
		/// Скорость, с которой начинается агрессивный доворот под идеальный угол.
		config::val<float> airstrafe_attack_speed{ 80.0f, "movement - airstrafe", "attack speed" };
		/// Мягкость контр-стрейфа при остановке под выстрел. Меньше -- резче
		/// тормозит (быстрее остановка, читается как удар в тормоз), больше --
		/// плавнее сбрасывает скорость.
		config::val<float> airstrafe_stop_smoothness{ 0.35f, "movement - airstrafe", "stop smoothness" };
		xui::setting jumpbug{ true, {}, "jumpbug", "movement" };
		xui::setting fastladder{ true, {}, "fastladder", "movement" };
		xui::setting edgejump{ false, { 'E', xui::bind_mode::hold_on}, "edgejump", "movement" };
		xui::setting edgestop{ false, { 'N', xui::bind_mode::hold_on}, "edgestop", "movement" };
		xui::setting edgebug{ false, {}, "edgebug", "movement" };
		/// 0..4 — matches jmp table order around \c loc_C80A3A in dump (mode dword selects case before the active path).
		config::val<int> edgebug_mode{ 1, "movement", "edgebug mode" };
		/// Analog of \c xmmword_E22CA4+0xC — extra subtick duck cycles (each cycle = press+release pair).
		config::val<int> edgebug_passes{ 1, "movement", "edgebug passes" };
		/// Adds jump up/down subticks like jumpbug after duck sequence (not in every dump path; optional).
		xui::setting edgebug_include_jump_steps{ false, {}, "edgebug jump steps", "movement" };
		xui::setting slowwalk{ false, { 'P', xui::bind_mode::hold_on}, "slowwalk", "movement" };
		config::val<float> slowwalk_speed{ 33.0f, "movement", "slowwalk speed" };

		struct test_strafer
		{
			xui::setting enabled{ false, {}, "test strafer", "movement" };
			config::val<int> max_subticks{ 4, "movement", "max subticks" };
			config::val<bool> adaptive_subticks{ true, "movement", "adaptive subticks" };
		} m_test_strafer{};

		struct velocity_debug
		{
			xui::setting enabled{ false, {}, "velocity debug", "movement" };
			xui::setting reset_on_land{ true, {}, "reset peak on land", "movement - velocity debug" };
		} m_velocity_debug{};
	};

	struct world
	{
		struct weather
		{
			enum class weather_type : std::uint8_t { snow, rain, stars };

			xui::setting enabled{ true, {}, "weather", "weather" };
			config::enm<weather_type> type{ weather_type::snow, "weather", "type" };
			config::col color{ { 117, 120, 142, 144 }, "weather", "color" };

			xui::setting fog_enabled{ true, {}, "fog", "weather" };
			config::val<float> fog_density{ 0.5f, "weather", "fog density" };
			config::val<float> fog_anisotropy{ 0.5f, "weather", "fog anisotropy" };
			config::val<float> fog_draw_distance{ 8000.0f, "weather", "fog draw distance" };
			config::col fog_color{ { 160, 175, 210, 255 }, "weather", "fog color" };

			xui::setting wetness{ false, {}, "wetness", "weather" };
			config::val<float> wetness_density{ 1.8f, "weather", "wetness density" };
			config::val<float> wetness_speed{ 0.8f, "weather", "wetness speed" };

			xui::setting wind{ true, {}, "wind", "weather" };
			config::val<float> wind_strength{ 3.0f, "weather", "wind strength" };
			config::val<float> wind_direction{ 0.0f, "weather", "wind direction" };
			config::val<float> wind_turbulence{ 1.0f, "weather", "wind turbulence" };
		} m_weather{};

		struct scene
		{
			struct skyboxing
			{
				xui::setting custom_skybox{ true, {}, "skybox material", "scene" };
				config::val<int> selected_skybox{ 0, "scene", "selected skybox" };

				xui::setting custom_color{ true, {}, "skybox color", "scene" };
				config::col skybox_color{ { 249, 103, 206, 255 }, "scene", "skybox color value" };
				config::col cloud_color{ { 173, 192, 255, 0 }, "scene", "cloud color" };
				config::col sun_color{ { 173, 192, 255, 0 }, "scene", "sun color" };
			};

			skyboxing skybox{};

			xui::setting lighting{ true, {}, "lighting", "scene" };
			config::col lighting_color{ { 173, 192, 255, 255 }, "scene", "lighting color" };
			config::val<float> lighting_intensity{ 0.85f, "scene", "lighting intensity" };
			config::vec3 lighting_rotation{ { -0.9f, 0.3f, 0.2f }, "scene", "lighting rotation" };

			xui::setting world_setting{ true, {}, "world color", "scene" };
			config::col world_color{ { 115, 125, 160, 255 }, "scene", "world color value" };

			xui::setting bloom{ true, {}, "bloom", "scene" };
			config::val<float> bloom_value{ 2.0f, "scene", "bloom value" };

			xui::setting gamma{ true, {}, "gamma", "scene" };
			config::val<float> gamma_value{ 2.2f, "scene", "gamma value" };

			xui::setting dof{ true, {}, "depth of field", "scene" };
			config::val<float> dof_near_blurry{ 0.0f, "scene", "dof near blurry" };
			config::val<float> dof_near_crisp{ 5.0f, "scene", "dof near crisp" };
			config::val<float> dof_far_crisp{ 600.0f, "scene", "dof far crisp" };
			config::val<float> dof_far_blurry{ 1400.0f, "scene", "dof far blurry" };

		} m_scene{};

		// Paints the part of a surface a bullet would actually get through.
		//
		// Off by default and sampled on a grid rather than derived from world
		// geometry: enumerating brushes to find penetrable ones is not something
		// that can be done per frame, but tracing a handful of rays through the
		// view and marking where they come out the far side answers the same
		// question for the wall you are actually looking at.
		struct penetration_wall
		{
			xui::setting enabled{ false, {}, "penetration wall", "world" };
			config::col color{ { 198, 128, 240, 90 }, "world", "penetration wall color" };
			config::val<float> fov{ 22.0f, "world", "penetration wall fov" };
			config::val<int> density{ 9, "world", "penetration wall density" };
			config::val<float> min_damage{ 10.0f, "world", "penetration wall min damage" };
			xui::setting scale_by_damage{ true, {}, "scale by damage", "world" };
		} m_penetration_wall{};
	};

	inline combat g_combat{};
	inline esp g_esp{};
	inline changer g_changer{};
	inline misc g_misc{};
	inline movement g_movement{};
	inline world g_world{};

	inline void finalize_binds( )
	{
		auto& aa = g_combat.m_antiaim;
		aa.manual_left.bind.excludes = &aa.manual_right;
		aa.manual_right.bind.excludes = &aa.manual_left;

		if ( aa.manual_left.value && aa.manual_right.value )
		{
			aa.manual_right.value = false;
			aa.manual_right.bind.active = false;
		}
	}

} // namespace settings
