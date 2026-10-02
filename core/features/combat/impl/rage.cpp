#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/threadpool/threadpool.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>
#include <utilities/perf.hpp>
#include <exploits.h>
namespace features::combat {

	constexpr int k_max_hitbox_bones = 28;   // max bone index referenced by a hitbox (record->bones is 128)
	// Development-only, the same guard the penetration logs in shared.cpp use.
	// Every one of these lines formats a dozen arguments, and they sit on paths
	// that run once per tick -- and in the scan they sit inside the trace loop
	// itself, writing to one shared log from every worker thread at once. Ship
	// should not be paying for that, and a profiling run in Development cannot
	// measure the scan honestly while it is.
#if defined( _DEBUG ) || defined( DEV )
	constexpr bool k_enable_shot_logging = true;
#else
	constexpr bool k_enable_shot_logging = false;
#endif

	namespace detail {
		inline auto tick_add( int t, float f, int dt, float df ) -> std::pair< int, float >
		{
			f += df;
			const auto carry = static_cast< int >( std::floor( f ) );
			f -= static_cast< float >( carry );
			return std::pair{ t + dt + carry, f };
		}
	}

	namespace scoring {
		constexpr float k_lethal_bonus = 100000.0f;
		constexpr float k_direct_bonus = 5000.0f;
		constexpr float k_center_bonus = 2000.0f;
		constexpr float k_damage_scale = 20.0f;
		constexpr float k_hitgroup_scale = 10.0f;
		constexpr float k_cheap_fov_penalty = 1.0f;
		constexpr float k_pass_hitchance = 1000000.0f;
		constexpr float k_lethal_score = 100000.0f;
		constexpr float k_hc_scale = 10000.0f;
		constexpr float k_damage_hc_scale = 100.0f;
		constexpr float k_damage_base = 5.0f;
		constexpr float k_pen_direct = 250.0f;
		constexpr float k_center_score = 50.0f;
		constexpr float k_hitgroup_score = 2.0f;
		constexpr float k_fov_score_penalty = 0.1f;
		constexpr float k_record_age_penalty = 15.0f;
		constexpr float k_extrapolated_bonus = 200.0f;

		// Вес "достижимости" точки в cheap_score -- геометрического суррогата
		// hitchance (см. select_best). 4000 при k_damage_scale 20 эквивалентно
		// 200 единиц урона: этого хватает, чтобы центр капсулы обошёл её край,
		// но не хватает, чтобы перебить летальность (100000) или хитгруппу
		// головы (200000).
		constexpr float k_reachability_scale = 4000.0f;

		// Точка головы, подтверждённая прощупом модели как открытая. Вес
		// выбран между reachability (4000, чисто геометрическое удобство)
		// и scanned_wallbang (40000, подтверждённый прострел): открытая
		// голова -- это сильнее геометрической близости к прицелу, но
		// слабее подтверждённого окна, потому что модель всё равно
		// ошибается на границах капсулы.
		constexpr float k_open_head_bonus = 18000.0f;

		// Скан карты подтвердил, что через эту стену в эту сторону реально
		// летит пуля (см. map_scan::has_wallbang). Shot через подтверждённое
		// окно надёжнее shot'а, который нашёл сам ragebot: скан проверял не
		// одну точку на хитбоксе, а всю линию до неё, и отбросил те места,
		// где за стеной сразу начинается вторая геометрия.
		//
		// 40000 -- выше любого real damage*100 (100 урона = 10000), но ниже
		// lethal (100000) и ниже pass_hitchance (1000000). То есть окно
		// перевешивает «просто урон», но проигрывает гарантированному
		// убийству и точке, прошедшей гейт.
		constexpr float k_scanned_wallbang_bonus = 40000.0f;

		// Hitgroup priority is worth 200000 to the head, which is right while the
		// shot has already cleared the hitchance gate. Below the gate it buried
		// every body point outright -- see the note in select_best -- so there it
		// is scaled down to a nudge of 200, on the same units as expected damage.
		constexpr float k_failed_gate_hitgroup_scale = 0.001f;

		// Вес "попаду ли я вообще" в ветке без прошедшего гейт.
		//
		// Урон в эти единицы не переводится ничем: hc -- вероятность, damage --
		// очки урона. Раньше выигрывала точка с наибольшим damage*hc, и это
		// ровно та ошибка, из-за которой бот молчал, глядя на 11%-эдж головы:
		// 27 урона * 0.11 = 2.97 против 24 урона * 0.55 = 13.2 -- грудь уже
		// выигрывала по произведению, но проигрывала по одному только damage
		// (27 > 24), а рядом стоял k_center_score.
		//
		// 2000 -- как k_center_bonus: этого хватает, чтобы точка с вдвое лучшей
		// хитчастью обошла вдвое более убойную, но не хватает, чтобы перебить
		// летальность (100000) и подтверждённое окно через стену (40000).
		// Голова сюда попадает только если у неё реально выше hc, а не по
		// hitgroup -- то есть решение принимает геометрия, а не ярлык.
		constexpr float k_failed_gate_hc_scale = 2000.0f;
	}

	void rage::on_create_move( systems::input::usercmd* cmd )
	{
		auto& ctx = g_shared.ctx( );
		const auto local = systems::g_local.get( );
		this->update_penetration_crosshair( local );

		// Одна проверка на тик вместо ветки в каждом run(): трассировка
		// печатает по пять строк на вызов и при включении роняет кадр до 1 fps.
		shared::penetration::debug_log = settings::g_combat.m_ragebot.debug_pen_trace.value;

		if ( !ctx.valid )
		{
			this->m_revolver_cock_ticks = 0;
			return;
		}

		this->m_should_stop = false;
		this->m_firing_this_tick = false;

		if ( !settings::g_combat.m_duckpeek.enabled.value )
		{
			this->m_release_duck_for_shot = false;
			this->m_duckpeek_reduck = false;
		}

		if ( this->m_zeus_fired )
		{
			this->m_zeus_fired = false;

			if ( settings::g_combat.m_zeusbot.drop_after && !systems::g_local.is_in_deathmatch( ) )
			{
				memory::call<void>(PATTERN (patterns::engine_client_cmd), addresses::globals::source2engine_to_client, 0, "drop", 0x7ffef001 );
			}

			return;
		}

		const auto is_knife = ctx.weapon_type == cstypes::weapon_type::knife;
		const auto is_taser = ctx.weapon_type == cstypes::weapon_type::taser;

		if ( !is_knife && !is_taser && ( ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg ) )
		{
			return;
		}

		auto aim_ctx = this->build_context( cmd, local );

		if ( is_knife )
		{
			if ( !g_shared.can_shoot( cmd, local.controller ) )
			{
				return;
			}

			this->run_knife( cmd, aim_ctx, local );
		}
		else if ( is_taser )
		{
			if ( !g_shared.can_shoot( cmd, local.controller ) )
			{
				return;
			}

			this->run_taser( cmd, aim_ctx, local );
		}
		else if ( ctx.item_def_idx == cstypes::item_definition_index::weapon_r8_revolver )
		{
			// Quick shot goes through the ordinary gun path: there is no hammer to
			// hold, so there is no cycle to manage -- fire_gun just sends attack2.
			if ( settings::g_combat.m_autos.revolver_quick.value )
			{
				this->m_revolver_cock_ticks = 0;

				// Primary attack must not reach the revolver in this mode, and it
				// has to be stripped every tick rather than only on the tick we
				// fire.
				//
				// Holding the fire key cocks the hammer, and a cocked revolver goes
				// off when the game decides the cock is done -- on a tick we never
				// solved a seed for, with angles we never corrected. The spread
				// correction is computed, verified and sent, and then a different
				// shot entirely leaves the barrel: exactly the picture in the logs,
				// where the fixed point always succeeds and the bullet still lands
				// anywhere inside the full cone.
				//
				// The stale index matters just as much as the button. process_
				// doubletap clears it for the same reason when it takes a shot over.
				//
				// Only when the button is genuinely held. Announcing the change
				// unconditionally reads as "the trigger was released" on every
				// single command, and releasing the trigger is exactly what fires a
				// cocked revolver -- so the weapon spent every tick starting and
				// abandoning its cycle, jerked in the hands, and never fired at all.
				if ( cmd->buttons.value & cstypes::command_buttons::in_attack )
				{
					cmd->buttons.value &= ~cstypes::command_buttons::in_attack;
					cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
					cmd->buttons.value_scroll &= ~cstypes::command_buttons::in_attack;
					cmd->csgo_user_cmd.set_attack1_start_history_index( -1 );
				}

				if ( !g_shared.can_shoot( cmd, local.controller ) )
				{
					return;
				}

				this->run_gun( cmd, aim_ctx, local );
			}
			else
			{
				this->auto_revolver( cmd, aim_ctx, local );
			}
		}
		else
		{
			this->m_revolver_cock_ticks = 0;

			if ( !g_shared.can_shoot( cmd, local.controller ) )
			{
				return;
			}

			this->run_gun( cmd, aim_ctx, local );
		}
	}

	void rage::on_render( xdraw::draw_list& draw_list )
	{
		this->draw_penetration_crosshair( draw_list );

		const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );
		if ( !config.debug_multipoints.value )
		{
			return;
		}

		std::lock_guard lock( m_debug_mtx );

		for ( const auto& pt : m_debug_points )
		{
			const auto screen = systems::g_view.project( pt.position );
			if ( !systems::g_view.projection_valid( screen ) )
			{
				continue;
			}

			xdraw::color col{};
			switch ( pt.hitbox_index )
			{
			case 0:
				col = { 255, 80,  80  }; break; // head — red
			case 2: case 3:
				col = { 220, 220, 60  }; break; // stomach — yellow
			case 4: case 5: case 6:
				col = { 255, 160, 60  }; break; // chest — orange
			case 7: case 8: case 9: case 10: case 11: case 12:
				col = { 80,  160, 255 }; break; // legs — blue
			case 13: case 14: case 15: case 16: case 17: case 18:
				col = { 180, 80,  255 }; break; // arms — purple
			default:
				col = { 200, 200, 200 }; break;
			}

			const auto alpha  = pt.is_center ? std::uint8_t{ 255 } : std::uint8_t{ 160 };
			const auto radius = pt.is_center ? 3.5f : 2.0f;

			draw_list.circle_filled( screen.x, screen.y, radius, col.alpha( alpha ) );
		}
	}

	rage::aim_context rage::build_context( systems::input::usercmd* cmd, const systems::local::snapshot& local ) const
	{
		auto& ctx = g_shared.ctx( );
		const auto& prestate = systems::g_prediction.pre( );

		aim_context out{};
		out.velocity = prestate.velocity;
		out.spread = g_shared.get_spread( );
		out.predicted_inaccuracy = g_shared.get_inaccuracy( true );

		systems::g_prediction.simulate( cmd, local, [ & ]
			{
				g_shared.sh( ).snapshot( local.pawn, ctx.weapon_services );

				out.velocity = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
				out.spread = g_shared.get_spread( );
				out.predicted_inaccuracy = g_shared.get_inaccuracy( true );
			} );

		ctx.spread = out.spread;
		ctx.inaccuracy = out.predicted_inaccuracy;

		out.view_angles = systems::g_input.get_view_angles( );
		out.on_ground = ( prestate.flags & cstypes::entity_flags::on_ground ) != 0;
		out.is_scoped = ctx.is_scoped;
		out.weapon_max_speed = ctx.weapon_max_speed;
		out.accurate_threshold = ctx.weapon_max_speed * 0.34f;

		return out;
	}

	bool rage::process_doubletap( systems::input::usercmd* cmd, const systems::local::snapshot& local, bool charge_dt )
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
		if ( !config.doubletap.value )
		{
			return false;
		}

		const auto base_cmd = cmd->csgo_user_cmd.mutable_base( );
		if ( !base_cmd )
		{
			return false;
		}

		const auto client_tick = base_cmd->client_tick( );
		const auto next_primary = memory::read<int>( shared_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash ) );
		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );

		const auto can_attack = client_tick >= next_primary && tick_base >= g_shared.last_shoot_tick( ) + 2 && !memory::read<bool>( shared_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_bInReload"_hash ) ) && memory::read<int>( shared_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_iClip1"_hash ) ) > 0;

		const auto should_attack = can_attack && charge_dt;

		if ( should_attack )
		{
			if ( const auto subtick_moves = base_cmd->mutable_subtick_moves( ) )
			{
				const auto old_size = subtick_moves->m_current_size;
				const auto press = systems::g_input.acquire_subtick_step( subtick_moves );
				const auto release = systems::g_input.acquire_subtick_step( subtick_moves );
				if ( press && release )
				{
					press->set_button( cstypes::command_buttons::in_attack );
					press->set_pressed( true );
					press->set_when( 0.0f );
					press->set_analog_forward_delta( 0.0f );
					press->set_analog_left_delta( 0.0f );

					release->set_button( cstypes::command_buttons::in_attack );
					release->set_pressed( false );
					release->set_when( std::nextafter( 1.0f, 0.0f ) );
					release->set_analog_forward_delta( 0.0f );
					release->set_analog_left_delta( 0.0f );

					// The subtick release is the final state. Do not let the
					// base command turn this pulse into a held attack.
					cmd->buttons.value &= ~cstypes::command_buttons::in_attack;
					cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
					cmd->buttons.value_scroll &= ~cstypes::command_buttons::in_attack;
					cmd->csgo_user_cmd.set_attack1_start_history_index( -1 );
					return true;
				}

				// Do not leave a partial press in the command if allocation of
				// the matching release failed.
				subtick_moves->m_current_size = old_size;
			}
		}

		cmd->buttons.value &= ~cstypes::command_buttons::in_attack;
		cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
		cmd->buttons.value_scroll &= ~cstypes::command_buttons::in_attack;
		cmd->csgo_user_cmd.set_attack1_start_history_index( -1 );
		return false;
	}

	struct friction_result {
		math::vector3 velocity;
		int ticks;
		// Ground covered between now and the tick we reach accurate_threshold.
		// predict_stop used to extrapolate the eye at the full current velocity
		// for the whole stop duration, which overshoots badly: we are shedding
		// speed the entire time. Integrate it here instead of guessing.
		math::vector3 displacement;
	};

	friction_result simulate_friction_stop(
		const math::vector3& initial_velocity,
		float accurate_threshold,
		float weapon_max_speed,
		float max_move_speed,
		bool is_scoped,
		float sv_friction,
		float sv_stopspeed,
		float sv_accelerate,
		float surface_friction,
		int max_ticks = 15)
	{
		auto sim_vel = initial_velocity;
		sim_vel.z = 0.0f;
		int ticks_to_threshold = -1;
		int total_ticks = 0;
		math::vector3 displacement{};
		math::vector3 displacement_at_threshold{};

		for (auto i = 0; i < max_ticks; ++i) {
			const auto sim_speed = sim_vel.length_2d();
			if (sim_speed < 1.0f) {
				sim_vel = {};
				break;
			}

			if (ticks_to_threshold < 0 && sim_speed <= accurate_threshold) {
				ticks_to_threshold = i;
				displacement_at_threshold = displacement;
			}

			const auto entry_velocity = sim_vel;

			const auto control = std::max(sim_speed, sv_stopspeed);
			const auto drop = sv_friction * surface_friction * control * cstypes::tick_interval;
			auto new_speed = std::max(sim_speed - drop, 0.0f);
			auto accel = sv_accelerate;

			if (is_scoped) {
				const auto weapon_ratio = std::min(1.0f, weapon_max_speed / 250.0f);
				const auto scoped_max = std::max(250.0f, max_move_speed) * weapon_ratio * 0.52f;
				if (new_speed > scoped_max - 5.0f) {
					const auto t = 1.0f - std::max(0.0f, new_speed - (scoped_max - 5.0f)) / std::max(0.01f, 5.0f);
					accel *= std::clamp(t, 0.0f, 1.0f);
				}
			}

			const auto accel_speed = std::min(accel * weapon_max_speed * surface_friction * cstypes::tick_interval, new_speed);
			new_speed = std::max(new_speed - accel_speed, 0.0f);

			if (new_speed > 0.0f)
				sim_vel *= (new_speed / sim_speed);
			else
				sim_vel = {};

			// Average the tick's entry and exit velocity: the speed falls across
			// the tick, so neither end alone integrates the arc correctly.
			displacement += (entry_velocity + sim_vel) * 0.5f * cstypes::tick_interval;
			++total_ticks;

			if (new_speed <= 0.0f)
				break;
		}

		if (ticks_to_threshold < 0) {
			ticks_to_threshold = total_ticks;
			displacement_at_threshold = displacement;
		}

		return {sim_vel, ticks_to_threshold, displacement_at_threshold};
	}

	std::optional<rage::stop_prediction> rage::predict_stop( const aim_context& ctx, const math::vector3& current_eye, const systems::local::snapshot& local ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto& prestate = systems::g_prediction.pre( );
		const auto speed = prestate.networked_velocity.length_2d( );
		const auto will_stop = ctx.on_ground && ( speed > ctx.accurate_threshold || ( ctx.is_scoped && speed > 1.0f ) );

		if ( !will_stop )
		{
			return std::nullopt;
		}

		const auto sv_friction = CONVAR("sv_friction")->get<float>( );
		const auto sv_stopspeed = CONVAR("sv_stopspeed")->get<float>( );
		const auto sv_accelerate = CONVAR("sv_accelerate")->get<float>( );
		const auto surface_friction = prestate.surface_friction;

		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		const auto max_move_speed = movement_services ? memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) ) : 250.0f;

		const auto result = simulate_friction_stop(
			prestate.networked_velocity,
			ctx.accurate_threshold,
			shared_ctx.weapon_max_speed,
			max_move_speed,
			shared_ctx.is_scoped,
			sv_friction,
			sv_stopspeed,
			sv_accelerate,
			surface_friction
		);

		// result.displacement is the integrated path of the deceleration, not
		// velocity * time -- at 250 u/s over 8 ticks the old form placed the eye
		// roughly twice as far down range as we actually travel, so every shot
		// planned from the predicted stop was aimed from a spot we never reach.
		return stop_prediction
		{
			.eye =
			{
				current_eye.x + result.displacement.x,
				current_eye.y + result.displacement.y,
				current_eye.z
			},
			.inaccuracy = g_shared.get_inaccuracy_at_velocity( local.pawn, result.velocity )
		};
	}

	std::vector<rage::candidate> rage::gather_candidates( const systems::local::snapshot& local, float max_distance_sq ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto players = systems::g_entities.get_by_type( systems::entities::type::player );

		std::vector<candidate> out;
		out.reserve( players.size( ) );

		const_cast<rage*>( this )->m_extrapolated_records.clear( );
		const_cast<rage*>( this )->m_extrapolated_records.reserve( players.size( ) );

		for ( const auto& p : players )
		{
			if ( !p.ptr || p.ptr == local.controller )
			{
				continue;
			}

			if ( !memory::read<bool>( p.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
			{
				continue;
			}

			const auto pawn_handle = memory::read<std::uint32_t>( p.ptr + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			const auto pawn = systems::g_entities.lookup( pawn_handle );

			if ( !pawn || pawn == local.pawn )
			{
				continue;
			}

			const auto team = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
			if ( !local.is_this_other_team( team ) )
			{
				continue;
			}

			const auto health = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
			if ( health <= 0 )
			{
				continue;
			}

			if ( memory::read<bool>( pawn + SCHEMA( "C_CSPlayerPawn", "m_bGunGameImmunity"_hash ) ) )
			{
				continue;
			}

			auto records = g_shared.lc( ).get_valid_records( pawn );

			// Always consider the forward-extrapolated (server-time) position as a
			// candidate, not just as a fallback. For fast-moving targets the newest
			// networked record is already stale by delta_ticks, so the shot must aim
			// where the enemy will be when the server processes the command.
			auto extrap = g_shared.lc( ).extrapolate( pawn );
			if ( extrap.has_value( ) )
			{
				const_cast<rage*>( this )->m_extrapolated_records.push_back( std::move( *extrap ) );
				records.push_back( &const_cast<rage*>( this )->m_extrapolated_records.back( ) );
			}

			if ( records.empty( ) )
			{
				continue;
			}

			if ( max_distance_sq > 0.0f )
			{
				const auto& origin = systems::g_prediction.pre( ).origin;
				const auto delta_front = records.front( )->origin - origin;
				auto closest_sq = delta_front.x * delta_front.x + delta_front.y * delta_front.y + delta_front.z * delta_front.z;

				if ( records.size( ) > 1 )
				{
					const auto delta_back = records.back( )->origin - origin;
					const auto back_sq = delta_back.x * delta_back.x + delta_back.y * delta_back.y + delta_back.z * delta_back.z;
					closest_sq = std::min( closest_sq, back_sq );
				}

				if ( closest_sq > max_distance_sq )
				{
					continue;
				}
			}

			candidate c{};
			c.pawn = pawn;
			c.health = health;
			c.armor = memory::read<int>( pawn + SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_hash ) );

			// Цель в воздухе. m_fFlags & FL_ONGROUND -- то же поле, по которому
			// движок решает, применять ли гравитацию; читаем его у цели, а не
			// у себя, потому что baim_air спрашивает именно про цель.
			{
				constexpr auto fl_onground{ 1 << 0 };
				const auto flags = memory::read<std::uint32_t>( pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_hash ) );
				c.on_air = ( flags & fl_onground ) == 0;
			}

			const auto pick_record_indices = [ &records ]( std::array<int, k_max_scan_records>& out_indices ) -> int
				{
					const auto count = records.size( );
					if ( count == 0 )
					{
						return 0;
					}

					auto picked{ 0 };
					const auto add_index = [ & ]( int idx )
						{
							if ( picked >= k_max_scan_records )
							{
								return;
							}

							for ( auto i = 0; i < picked; ++i )
							{
								if ( out_indices[ i ] == idx )
								{
									return;
								}
							}

							out_indices[ picked++ ] = idx;
						};

					add_index( 0 );

					if ( count > 1 )
					{
						add_index( static_cast< int >( count - 1 ) );
					}

					return picked;
				};

			std::array<int, k_max_scan_records> record_indices{};
			const auto picked_count = pick_record_indices( record_indices );

			for ( auto i = 0; i < picked_count; ++i )
			{
				c.records[ i ] = records[ static_cast< std::size_t >( record_indices[ i ] ) ];
			}

			c.record_count = picked_count;

			if ( shared_ctx.weapon_type >= cstypes::weapon_type::pistol && shared_ctx.weapon_type <= cstypes::weapon_type::lmg )
			{
				const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
				c.min_damage = this->get_min_damage( config, health, config.min_damage_override.value );
			}

			out.push_back( c );
		}

		return out;
	}

	void rage::run_gun( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local, bool allow_fire )
	{
		if ( !settings::g_combat.m_ragebot.master_enabled( ) )
		{
			return;
		}

		auto& shared_ctx = g_shared.ctx( );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );

		// general.auto_fire. Раньше чекбокс "Auto fire" во вкладке General не
		// читался нигде: снятая галочка не мешала ragebot стрелять. Здесь он
		// мастер-гейт над выстрелом: снят -- наведение остаётся, атака нет.
		if ( !settings::g_combat.m_ragebot.general.auto_fire.value )
		{
			allow_fire = false;
		}

		// auto_scope. Раньше эта настройка не читалась нигде: скоперный ragebot
		// просто отказывался стрелять без скопа (см. should_stop_movement, где
		// sniper && !is_scoped даёт false), а заставлял ли игрок скопиться --
		// не проверялось. Из-за этого вкладка General и пер-оружийный auto_scope
		// были мёртвыми чекбоксами.
		//
		// Здесь мы сами жмём вторую атаку, пока снайперка не в скопе, и
		// возвращаем управление: выстрел в тот же тик ушёл бы не в цель, потому
		// что зум меняет fov/скорость и требует отдельного тика на применение.
		// general.auto_scope -- мастер-гейт поверх пер-оружийного.
		if ( shared_ctx.weapon_type == cstypes::weapon_type::sniper && !shared_ctx.is_scoped
			&& settings::g_combat.m_ragebot.auto_scope_effective( config ) )
		{
			this->request_scope( cmd );
			return;
		}

		const auto finish_doubletap = [ & ]( bool charge_dt )
		{
			if ( config.doubletap.value && shared_ctx.item_def_idx != cstypes::item_definition_index::weapon_r8_revolver )
			{
				return this->process_doubletap( cmd, local, charge_dt );
			}

			return false;
		};

		// Real charge-then-release doubletap (g_exploits): holds the shot back for
		// k_required_ticks while charging, then lets it through and shifts the fired
		// command's input-history tick backward in post_fire so the server processes
		// it as if the weapon had already come off cooldown. Runs every tick
		// (independent of target visibility) so the charge timer is real-time.
		// A disabled/ineligible weapon short-circuits to "always ready" so behavior
		// is unchanged when doubletap is off.
		const auto dt_enabled = config.doubletap.value && shared_ctx.item_def_idx != cstypes::item_definition_index::weapon_r8_revolver;
		const auto dt_ready = !dt_enabled || g_exploits.pre_think( cmd, local );

		auto candidates = this->gather_candidates( local );

		{
			std::lock_guard lock( m_debug_mtx );
			m_debug_points.clear( );
		}

		if ( candidates.empty( ) )
		{
			finish_doubletap( false );
			return;
		}

		// Prioritize the target we shot last tick: its position changes smoothly, so
		// reusing the previous aim context keeps selection stable between ticks.
		if ( this->m_last_target_pawn )
		{
			for ( auto i = 1u; i < candidates.size( ); ++i )
			{
				if ( candidates[ i ].pawn == this->m_last_target_pawn )
				{
					std::swap( candidates[ 0 ], candidates[ i ] );
					break;
				}
			}
		}

		auto eye_candidates = g_shared.sh( ).get_candidates( );
		{
			// The server fires from the interpolated shoot position, not the raw eye
			// (ring-buffer candidate or live muzzle fallback) -- those can be up to a
			// full lerp of movement ahead of it during jump-shots, which shifts the
			// aim ray off the hitbox at range while still landing fine up close
			// (small target-angle error at melee range). Always resolve entry 0 from
			// the interpolated position so scanning matches what the server tests.
			const auto interpolated_eye = local.pawn ? g_shared.get_interpolated_shoot_position( local.pawn ) : math::vector3{};

			if ( eye_candidates.count > 0 )
			{
				eye_candidates.entries[ 0 ].position = interpolated_eye;
			}
			else if ( interpolated_eye.length_sqr( ) > 1.0f )
			{
				eye_candidates.entries[ 0 ].position = interpolated_eye;
				eye_candidates.entries[ 0 ].is_uninterpolated = true;
				eye_candidates.count = 1;
			}
		}

		const auto scan_from_eye_candidates = [ & ]( const math::vector3& eye_offset, float inaccuracy )
		{
			// One probe per invocation: this is called several times a tick, and
			// the only way to know what that costs is to count it. See perf.hpp.
			perf::scope scan_probe{ perf::id::rage_scan };

			std::vector<scan_hit> hits_out;

			for ( auto i = 0; i < eye_candidates.count; ++i )
			{
				const auto eye = eye_candidates.entries[ i ].position + eye_offset;
				auto hits = this->scan_players( eye, inaccuracy, ctx, candidates, local );
				auto found_direct{ false };

				for ( auto& hit : hits )
				{
					auto source_eye = eye_candidates.entries[ i ];
					source_eye.position = eye;
					hit.source_eye = source_eye;
					found_direct = found_direct || !hit.penetrated;
					hits_out.push_back( std::move( hit ) );
				}

				if ( found_direct )
				{
					break;
				}
			}

			return hits_out;
		};

		if ( config.no_spread.value )
		{
			shared_ctx.inaccuracy = g_shared.get_inaccuracy( false );
			auto all_hits = scan_from_eye_candidates( {}, shared_ctx.inaccuracy );

			if ( all_hits.empty( ) )
			{
				finish_doubletap( false );
				return;
			}

			const auto best = this->select_best( ctx, all_hits, shared_ctx.inaccuracy, local );
			if ( !best.valid )
			{
				finish_doubletap( false );
				return;
			}

			if ( !allow_fire )
			{
				return;
			}

			if ( !dt_ready )
			{
				finish_doubletap( false );
				return;
			}

			const auto subtick_attack = finish_doubletap( true );
			this->fire_gun( cmd, best, false, best.hit.source_eye.position, local, subtick_attack );

			if ( dt_enabled && this->m_firing_this_tick )
			{
				g_exploits.post_fire( cmd, local );
			}

			return;
		}

		const auto primary_eye = eye_candidates.entries[ 0 ].position;
		const auto& prestate = systems::g_prediction.pre( );

		// Current-shot selection is always based on current engine shoot-history.
		auto current_hits = scan_from_eye_candidates( {}, ctx.predicted_inaccuracy );
		auto best = this->select_best( ctx, current_hits, ctx.predicted_inaccuracy, local );

		const auto needed_hc = config.hitchance_override.value ? static_cast< float >( config.hitchance_override_value ) / 100.0f : static_cast< float >( settings::g_combat.m_ragebot.hit_chance_effective( config ) ) / 100.0f;
		const auto duckpeek_active = settings::g_combat.m_duckpeek.enabled.value && ctx.on_ground;
		const auto is_ducked = ( prestate.flags & cstypes::entity_flags::ducking ) != 0;

		auto standing_inaccuracy = duckpeek_active ? this->get_standing_inaccuracy( local, ctx ) : ctx.predicted_inaccuracy;
		auto standing_hc = best.valid
			? ( duckpeek_active ? this->evaluate_hitchance( best.hit, ctx, standing_inaccuracy ) : best.hitchance )
			: 0.0f;

		// Whether autostop actually intends to halt for this target. Used so we only
		// withhold a shot while moving when a stop is genuinely planned (close /
		// approaching targets do not need one and must still be allowed to fire).
		const auto intend_to_stop = this->should_stop_for_target( ctx, best );

		auto max_acc = g_shared.is_max_accuracy( standing_inaccuracy );
		// Whether we are still fast enough that a planned stop hasn't landed yet.
		// This only guards the predictive re-evaluation below (Improvement #1),
		// which fires using a *hypothetical* future stopped inaccuracy. The base
		// `accurate` check below already uses ctx.predicted_inaccuracy -- the
		// engine's own honest simulation for this exact tick/state (moving, jump
		// apex, whatever it is) -- so it needs no extra gate: as soon as the real,
		// already-achieved hitchance clears the bar, fire. Requiring max_acc here
		// instead would mean waiting for a full stop / literal jump apex on every
		// shot, which is both needlessly slow on the ground and makes the bot
		// nearly never fire in the air (is_max_accuracy is only true on the exact
		// apex tick).
		const auto moving = prestate.networked_velocity.length_2d( ) > ctx.accurate_threshold;
		const auto auto_stop = settings::g_combat.m_ragebot.auto_stop_effective( config );
		// Reverted back to generic (2026-08-31): the ground-only version was tried
		// per explicit request and confirmed to reproduce exactly the predicted
		// failure -- firing while actively strafing missed because the shot used
		// predict_air_stop's *predicted* post-counter-strafe accuracy before the
		// real counter-strafe (now working correctly in airstrafe.cpp) had
		// actually brought horizontal velocity down. Generic `moving` makes the
		// bot wait the real ~2-4 ticks for the counter-strafe to land, then fire
		// on genuinely-achieved accuracy -- which is also just what "stop in the
		// air for a couple ticks, instant headshot, keep flying" requires.
		const auto wait_for_stop = auto_stop && moving && intend_to_stop;

		// Порог урона для РЕШЕНИЯ О ВЫСТРЕЛЕ.
		//
		// В скане стоит мягкий пол (min_damage * 0.65), и это правильно: точка
		// чуть ниже порога должна дожить до скоринга, где её сравнят с
		// остальными по ожидаемой ценности. Но мягкий пол -- инструмент
		// СРАВНЕНИЯ, а не разрешение стрелять. Здесь порог жёсткий.
		//
		// Раньше гейта не было вовсе, и точка с 4 урона в живот на цель со
		// 100 hp стреляла, если у неё высокая хитчасть: хитчасть отвечает на
		// "долетит ли пуля", а не на "стоит ли её пускать". По логу это
		// выглядело как промахи -- выстрел уходил, урона не наносил, цель
		// продолжала жить.
		const auto required_damage = this->get_min_damage(
			config, best.hit.health, config.min_damage_override.value );

		auto accurate = best.valid && standing_hc >= needed_hc && best.hit.damage >= required_damage;
		max_acc = g_shared.is_max_accuracy( standing_inaccuracy );
		auto force = best.valid && config.force_shot.value && max_acc;
		auto shot_viable = accurate || force;

		if ( best.valid )
		{
			const auto& weapon_vdata = g_shared.ctx( ).weapon_vdata;
			if ( weapon_vdata )
			{
				const auto full_vel = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) );
				auto real_vel = full_vel;
				real_vel.z = 0.0f;
				const auto real_speed_2d = real_vel.length_2d( );

				const auto inacc_stand = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyStand"_hash ) );
				const auto inacc_floor = std::max( inacc_stand, 0.004f );
				const auto real_inacc = g_shared.get_inaccuracy_at_velocity( local.pawn, real_vel );

				const auto too_inaccurate = real_inacc > inacc_floor * 2.0f + 0.003f;
				// 0.20f sat well under the max_speed * 0.34f accurate threshold the
				// rest of the aimbot uses, so shots were refused at speeds that fire
				// perfectly well. Ground only: in the air airborne_unshootable is the
				// check that matters, and this one just kept us from ever firing.
				const auto on_ground_now = ( prestate.flags & cstypes::entity_flags::on_ground ) != 0;
				const auto too_fast = on_ground_now && ctx.weapon_max_speed > 0.0f && real_speed_2d > ctx.weapon_max_speed * 0.34f;

				// Честный опрос физики движка CS2 для апекса прыжка:
				const auto airborne_unshootable = ( prestate.flags & cstypes::entity_flags::on_ground ) == 0
					&& [ & ]( ) -> bool
					{
						const auto inac_jump_apex = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_hash ) );
						const auto accuracy_penalty = memory::read<float>( g_shared.ctx( ).weapon + SCHEMA( "C_CSWeaponBase", "m_fAccuracyPenalty"_hash ) );
						const auto min_air_inaccuracy = accuracy_penalty + inac_jump_apex;
						const auto real_air_inaccuracy = g_shared.get_inaccuracy_at_velocity( local.pawn, full_vel );

						constexpr auto air_tolerance{ 0.003f };
						return real_air_inaccuracy > min_air_inaccuracy + air_tolerance;
					}( );

				// These three are guards for force_shot, which fires without ever
				// consulting hitchance. A shot that already cleared needed_hc was
				// measured at ctx.predicted_inaccuracy -- the real cone, jump
				// penalty included -- so vetoing it here re-decides a question
				// hitchance already answered, and keeps us silent for the whole
				// time the landing penalty takes to decay. That is the pause after
				// touching down where the aimbot sees the target and says nothing.
				if ( !accurate && ( too_inaccurate || too_fast || airborne_unshootable ) )
				{
					shot_viable = false;
				}
			}
		}

		float stop_inaccuracy = ctx.predicted_inaccuracy;
		math::vector3 stop_offset{};
		const auto autostop_enabled = settings::g_combat.m_ragebot.auto_stop_effective( config );

		// Kept alive for the re-selection below. That block used to run its own
		// scan_from_eye_candidates with byte-identical arguments -- same eye
		// offset, same inaccuracy, same tick, and nothing written to the game in
		// between -- so the most expensive part of the tick was paid twice for
		// information that was already sitting right here.
		std::vector<scan_hit> planned_hits;
		rage::target planned{};
		auto planned_scanned{ false };

		if ( autostop_enabled && !shot_viable && this->should_stop_movement( ctx ) )
		{
			const auto stop = this->predict_stop( ctx, primary_eye, local );
			if ( stop )
			{
				stop_inaccuracy = stop->inaccuracy;
				stop_offset = stop->eye - primary_eye;
				planned_hits = scan_from_eye_candidates( stop_offset, stop_inaccuracy );
				planned = this->select_best( ctx, planned_hits, stop_inaccuracy, local );
				planned_scanned = true;
				this->m_should_stop = planned.valid;
			}
			else
			{
				this->m_should_stop = best.valid;
			}
		}

		// Improvement #1: if autostop will bring us to a (near) stop, the real
		// inaccuracy at the moment of firing is the stopped inaccuracy, not the
		// current moving one. Re-select with that lower inaccuracy so the fire
		// decision uses a realistic hitchance and can pick a higher-value point.
		// Gated on !wait_for_stop: never fire on the same tick a stop is only just
		// being planned — the character is still moving with the real (wide) cone,
		// so committing here misses. Wait until the stop actually completes.
		if ( this->m_should_stop && stop_inaccuracy < ctx.predicted_inaccuracy - 0.0001f )
		{
			// m_should_stop can only be true here if the autostop block above ran
			// this tick and produced a stop, so planned_hits is already the answer
			// for exactly this eye offset and inaccuracy. The guard stays only so a
			// future edit that decouples the two cannot silently read an empty list.
			if ( !planned_scanned )
			{
				planned_hits = scan_from_eye_candidates( stop_offset, stop_inaccuracy );
				planned = this->select_best( ctx, planned_hits, stop_inaccuracy, local );
				planned_scanned = true;
			}

			if ( planned.valid && !wait_for_stop )
			{
				current_hits = std::move( planned_hits );
				best = planned;
				standing_inaccuracy = stop_inaccuracy;
				standing_hc = best.hitchance;
				// Порог урона пересчитывается: best -- уже другая точка (из
				// planned_hits), и её цель может иметь другое здоровье.
				accurate = best.valid && standing_hc >= needed_hc
					&& best.hit.damage >= this->get_min_damage(
						config, best.hit.health, config.min_damage_override.value );
				max_acc = g_shared.is_max_accuracy( standing_inaccuracy );
				force = best.valid && ( ctx.on_ground ? ( config.force_shot.value && max_acc ) : ( config.force_shot_air.value && max_acc ) );
				shot_viable = accurate || force;
			}
		}

		// Clutch: a lethal server-time (extrapolated) target must be committed
		// immediately, bypassing the normal hitchance gate and the autostop-wait.
		// The server rewinds the enemy to command time, so the extrapolated pose is
		// exactly where the bullet is tested — delaying a tick lets the enemy's own
		// shot land first (e.g. a 10 HP AWP clutch vs a peeking rusher).
		if ( best.is_lethal( ) && best.hit.record && best.hit.record->extrapolated )
		{
			accurate = best.valid;
			shot_viable = accurate || force;
		}

		// Improvement #6: if standing we cannot reach the needed hitchance, try a
		// crouched eye position — it may open a cleaner line through a wallbang or
		// expose a different hitbox face. Commit only if it is meaningfully better.
		// Hold the crouch across ticks. The press below only starts the movement;
		// the shot itself is taken later, by the ordinary path, once the eye has
		// actually arrived.
		if ( this->m_duck_for_shot_ticks > 0 )
		{
			--this->m_duck_for_shot_ticks;
		}

		this->m_should_duck_for_shot = this->m_duck_for_shot_ticks > 0;

		// auto stop duck: если торможение уже запрошено, приседаем вместе с ним.
		// Присед уменьшает выталкиваемый силуэт и опускает глаза, из-за чего
		// часть стены перестаёт перекрывать цель -- то есть это не украшение,
		// а второй способ добрать точность там, где торможения не хватило.
		//
		// Ставим флаг, а не жмём в дуке здесь же: нажатие применяется ниже,
		// вне fire-блока, по той же причине, что и m_should_duck_for_shot --
		// тик, который начинает присед, по определению не тик выстрела.
		if ( config.autostop_duck.value && this->m_should_stop && ctx.on_ground
			&& !( prestate.flags & cstypes::entity_flags::ducking )
			&& this->m_duck_for_shot_ticks <= 0 )
		{
			this->m_duck_for_shot_ticks = 16;
			this->m_should_duck_for_shot = true;
		}

		if ( !accurate && !force && ctx.on_ground && !( prestate.flags & cstypes::entity_flags::ducking ) )
		{
			constexpr auto duck_eye_delta = math::vector3{ 0.0f, 0.0f, -18.0f };

			auto ducked_hits = scan_from_eye_candidates( duck_eye_delta, ctx.predicted_inaccuracy );
			const auto ducked_best = this->select_best( ctx, ducked_hits, ctx.predicted_inaccuracy, local );

			if ( ducked_best.valid )
			{
				const auto ducked_hc = this->evaluate_hitchance( ducked_best.hit, ctx, ctx.predicted_inaccuracy );
				if ( ducked_hc > standing_hc + 0.05f )
				{
					// Duck now, shoot later. This used to adopt the crouched eye and
					// fire on the same command, but pressing +duck does not drop the
					// eye 18 units that tick -- CS2 ramps m_flDuckAmount over about a
					// fifth of a second. The bullet left from the standing eye while
					// the angle had been solved for the crouched one, and the server
					// logged it as a shoot position mismatch: 57 of 127 misses in the
					// last session, the single largest cause.
					//
					// So only start the movement here. Once the pose settles the
					// normal scan sees the same opening from an eye that is really
					// there, and takes the shot with an angle that matches it.
					this->m_duck_for_shot_ticks = 16;
					this->m_should_duck_for_shot = true;

					accurate = false;
					force = false;
					shot_viable = false;
				}
			}
		}

		if ( !best.valid )
		{
			if constexpr ( k_enable_shot_logging )
			{
				if ( !ctx.on_ground )
				{
					logging::console::print( xs( "[rage-air-debug] no valid target this tick (on_ground={})" ), ctx.on_ground );
				}
			}
			finish_doubletap( false );
			return;
		}

		if ( duckpeek_active && allow_fire )
		{
			if ( shot_viable )
			{
				this->m_release_duck_for_shot = true;
			}
			else if ( !this->m_duckpeek_reduck )
			{
				this->m_release_duck_for_shot = false;
			}
		}

		// Outside the fire block on purpose: the tick that starts the crouch is by
		// definition not the tick that fires.
		if ( this->m_should_duck_for_shot )
		{
			cmd->buttons.value |= cstypes::command_buttons::in_duck;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_duck;
			cmd->buttons.value_scroll |= cstypes::command_buttons::in_duck;
		}

		auto ready_to_fire = shot_viable;
		if ( duckpeek_active )
		{
			if ( is_ducked )
			{
				ready_to_fire = false;
			}
			else
			{
				ready_to_fire = ready_to_fire && this->m_release_duck_for_shot;
			}
		}

		// calculate_hitchance is pure geometry: a ray against one hitbox capsule,
		// with no world trace anywhere in it. A centre ray that clears the corner
		// of cover while the rest of the cone eats it therefore scores a perfect
		// hitchance, and the shot goes into the wall. Confirm the cone can
		// actually reach before spending the bullet.
		//
		// Once, on the chosen target, on the tick we are about to fire -- doing it
		// per candidate during the scan would multiply the scan's trace count by
		// the sample count.
		if ( ready_to_fire && config.cone_check.value )
		{
			const auto cone_factor = this->validate_shot_cone( best, ctx, standing_inaccuracy, local );

			const auto accept = std::clamp( static_cast< float >( config.cone_accept.value ) / 100.0f, 0.0f, 1.0f );

			// ВНИМАНИЕ: ветвление восстановлено по описанию из лога проекта
			// (2026-09-19, "причина C"), точного оригинала нет. В accurate
			// ветке порогом был нужный hitchance, помноженный на долю конуса;
			// в остальных -- accept из cone_accept.
			if ( accurate )
			{
				if ( standing_hc * cone_factor < needed_hc )
				{
					ready_to_fire = false;
				}
			}
			else if ( cone_factor < accept )
			{
				ready_to_fire = false;
			}
		}

		// -- Ожидание точности -------------------------------------------------
		//
		// Разброс оружия -- это конус, который на цель проецируется кругом
		// радиуса tan(spread) * distance. Если этот круг больше самой цели,
		// выстрел -- лотерея: половина пуль уйдёт мимо, и никакой hitchance
		// этого не покажет, потому что hitchance считает разброс отдельно от
		// геометрии попадания.
		//
		// Сравниваем УГЛОВЫЕ размеры: угловой радиус цели против угловой
		// ширины конуса. Раньше сравнивались линейные величины (радиус
		// капсулы), и на дальней дистанции это блокировало выстрел почти
		// всегда: капсула головы ~3 юнита на 30 метрах имеет угловой размер
		// порядка 0.1°, то есть любой реальный разброс оказывался "шире
		// цели" и бот молчал.
		//
		// Только для земли: в воздухе разброс и так максимальный, и ожидание
		// превратилось бы в полное молчание. Снайперки пропускаем -- у них
		// первый выстрел точен, и ожидание здесь только вредит.
		//
		// no_spread -- тоже пропускаем. Смысл настройки: разброс уже
		// компенсирован подобранным углом, и ждать "стабилизации" значило бы
		// держать выстрел, у которого проблема решена. Именно эта пара
		// (компенсация есть, но ожидание её игнорирует) давала молчание при
		// включённом по умолчанию no_spread.
		if ( ready_to_fire && config.spread_wait.value && !force && ctx.on_ground && shot_viable
			&& !config.no_spread.value
			&& ctx.spread > 0.0f
			&& g_shared.ctx( ).weapon_type != cstypes::weapon_type::sniper )
		{
			const auto distance = ( best.hit.position - best.hit.source_eye.position ).length( );
			if ( distance > 1.0f )
			{
				// Угловой радиус цели: сколько градусов занимает капсула с
				// точки выстрела. Именно с этим и должен сравниваться конус.
				const auto target_radius = std::max( best.hit.hitbox.radius, 1.0f );
				const auto target_angle = std::atanf( target_radius / distance );

				// tolerance -- во сколько раз конус может быть шире цели.
				// 1.0 означает "не шире"; меньше -- строже.
				const auto tolerance = std::max( config.spread_tolerance.value, 0.1f );
				if ( ctx.spread > target_angle * tolerance )
				{
					ready_to_fire = false;
				}
			}
		}

		// -- Терпение ---------------------------------------------------------
		//
		// Бот не обязан стрелять в первый же тик, когда выстрел стал возможен.
		// Если настройка включена и текущая цель не летальна, а лимит
		// удержания не исчерпан -- ждём: следующий тик даст новую точку
		// (цель сдвинется, капсула откроется, конус сузится), и выстрел
		// получится лучше.
		if ( ready_to_fire && config.patience.value && !force )
		{
			const auto lethal = best.hit.damage >= static_cast< float >( config.lethal_priority.value );
			const auto hold_limit = std::max( config.max_hold_ticks.value, 0 );

			// Считаем тики удержания на одной и той же цели: смена цели
			// сбрасывает счётчик, иначе бот однажды застревает в ожидании
			// навсегда, переключаясь между двумя врагами.
			if ( this->m_hold_target != best.hit.pawn )
			{
				this->m_hold_target = best.hit.pawn;
				this->m_hold_ticks = 0;
			}

			if ( !lethal && this->m_hold_ticks < hold_limit )
			{
				++this->m_hold_ticks;
				ready_to_fire = false;
			}
			else
			{
				this->m_hold_ticks = 0;
			}
		}
		else if ( ready_to_fire )
		{
			this->m_hold_ticks = 0;
		}

		if ( ready_to_fire && allow_fire && dt_ready )
		{
			const auto subtick_attack = finish_doubletap( true );

			this->fire_gun( cmd, best, !accurate && force, best.hit.source_eye.position, local, subtick_attack );

			// The shot is committed, so the stop has done its job. airstrafe runs
			// later in this same create_move and reads should_stop(); leaving the
			// flag set makes it brake for a shot that already went out.
			this->m_should_stop = false;

			// fire_gun already committed the attack bits/history entries above;
			// post_fire only rewrites their recorded tick so the server sees this
			// shot as landing on the tick the weapon actually came off cooldown.
			if ( dt_enabled && this->m_firing_this_tick )
			{
				g_exploits.post_fire( cmd, local );
			}

			if ( duckpeek_active )
			{
				this->m_duckpeek_reduck = true;
				this->m_release_duck_for_shot = false;
			}
		}
		else
		{
			finish_doubletap( false );
		}
	}

	void rage::run_taser( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local )
	{
		if ( !settings::g_combat.m_zeusbot.enabled )
		{
			return;
		}

		auto candidates = this->gather_candidates( local );
		if ( candidates.empty( ) )
		{
			return;
		}

		auto eye_candidates = g_shared.sh( ).get_candidates( );
		{
			// See run_gun: always resolve from the interpolated shoot position, not
			// the raw eye, so scanning matches what the server actually tests.
			const auto interpolated_eye = local.pawn ? g_shared.get_interpolated_shoot_position( local.pawn ) : math::vector3{};

			if ( eye_candidates.count > 0 )
			{
				eye_candidates.entries[ 0 ].position = interpolated_eye;
			}
			else if ( interpolated_eye.length_sqr( ) > 1.0f )
			{
				eye_candidates.entries[ 0 ].position = interpolated_eye;
				eye_candidates.entries[ 0 ].is_uninterpolated = true;
				eye_candidates.count = 1;
			}
		}

		std::vector<scan_hit> all_hits;

		for ( auto i = 0; i < eye_candidates.count; ++i )
		{
			auto hits = this->scan_taser( eye_candidates.entries[ i ].position, ctx, candidates, local );

			for ( auto& h : hits )
			{
				h.source_eye = eye_candidates.entries[ i ];
				all_hits.push_back( std::move( h ) );
			}
		}

		if ( all_hits.empty( ) )
		{
			return;
		}

		target best{};

		for ( const auto& h : all_hits )
		{
			if ( !best.valid || h.score > best.score )
			{
				best.hit = h;
				best.hitchance = 1.0f;
				best.score = h.score;
				best.valid = true;
			}
		}

		if ( best.valid )
		{
			this->m_zeus_fired = true;
			this->fire_melee( cmd, best, local );
		}
	}

	void rage::run_knife( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local )
	{
		if ( !settings::g_combat.m_knifebot.enabled )
		{
			return;
		}

		const auto info = this->get_knife_info( local );
		if ( !info.can_slash && !info.can_stab )
		{
			return;
		}

		constexpr auto max_knife_dist_sq = 150.0f * 150.0f;
		auto candidates = this->gather_candidates( local, max_knife_dist_sq );
		if ( candidates.empty( ) )
		{
			return;
		}

		auto eye_candidates = g_shared.sh( ).get_candidates( );
		{
			// See run_gun: always resolve from the interpolated shoot position, not
			// the raw eye, so scanning matches what the server actually tests.
			const auto interpolated_eye = local.pawn ? g_shared.get_interpolated_shoot_position( local.pawn ) : math::vector3{};

			if ( eye_candidates.count > 0 )
			{
				eye_candidates.entries[ 0 ].position = interpolated_eye;
			}
			else if ( interpolated_eye.length_sqr( ) > 1.0f )
			{
				eye_candidates.entries[ 0 ].position = interpolated_eye;
				eye_candidates.entries[ 0 ].is_uninterpolated = true;
				eye_candidates.count = 1;
			}
		}

		std::vector<scan_hit> all_hits;

		for ( auto i = 0; i < eye_candidates.count; ++i )
		{
			auto hits = this->scan_knife( eye_candidates.entries[ i ].position, ctx, info, candidates, local );

			for ( auto& h : hits )
			{
				h.source_eye = eye_candidates.entries[ i ];
				all_hits.push_back( std::move( h ) );
			}
		}

		if ( all_hits.empty( ) )
		{
			return;
		}

		target best{};
		target best_backstab{};

		for ( const auto& h : all_hits )
		{
			auto& dest = h.is_backstab ? best_backstab : best;

			if ( !dest.valid || h.score > dest.score )
			{
				dest.hit = h;
				dest.hitchance = 1.0f;
				dest.score = h.score;
				dest.valid = true;
			}
		}

		auto& chosen = best_backstab.valid ? best_backstab : best;
		if ( !chosen.valid )
		{
			return;
		}

		this->m_knife_attack = static_cast< std::uint8_t >( chosen.hit.attack_type );
		this->fire_melee( cmd, chosen, local );
	}

	void rage::auto_revolver( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local )
	{
		if ( !settings::g_combat.m_ragebot.master_enabled( ) )
		{
			this->m_revolver_cock_ticks = 0;
			return;
		}

		if ( !g_shared.can_shoot( cmd, local.controller ) )
		{
			this->m_revolver_cock_ticks = 0;
			return;
		}

		if ( !settings::g_combat.m_autos.revolver.value )
		{
			this->m_revolver_cock_ticks = 0;
			return;
		}

		constexpr auto cock_ticks{ 13 };
		if ( this->m_revolver_cock_ticks >= cock_ticks )
		{
			// End the held cycle. Target selection adds attack back on this
			// command only when the revolver should actually fire.
			cmd->buttons.value &= ~cstypes::command_buttons::in_attack;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
			cmd->buttons.value_scroll &= ~cstypes::command_buttons::in_attack;
			cmd->csgo_user_cmd.set_attack1_start_history_index( -1 );
			this->m_revolver_cock_ticks = 0;

			this->run_gun( cmd, ctx, local );
			return;
		}

		// Keep target and hitchance planning active throughout the cock cycle.
		// Autostop consumes this command's decision on the following command.
		this->run_gun( cmd, ctx, local, false );

		cmd->buttons.value |= cstypes::command_buttons::in_attack;
		cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
		cmd->buttons.value_scroll |= cstypes::command_buttons::in_attack;

		const auto history_index = cmd->csgo_user_cmd.input_history_size( ) - 1;
		if ( history_index >= 0 )
		{
			cmd->csgo_user_cmd.set_attack1_start_history_index( history_index );
		}

		++this->m_revolver_cock_ticks;
	}

	std::vector<rage::scan_hit> rage::scan_players( const math::vector3& eye, float inaccuracy, const aim_context& ctx, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const
	{
		std::vector<std::vector<scan_hit>> per_candidate( candidates.size( ) );

		const auto min_batch = candidates.size( ) < 4 ? static_cast< int >( candidates.size( ) ) : 1;
		threadpool::parallel_for( 0, static_cast< int >( candidates.size( ) ), [ & ]( int begin, int end )
			{
				for ( auto ci = begin; ci < end; ++ci )
				{
					auto& cand = candidates[ ci ];
					auto& candidate_hits = per_candidate[ ci ];
					candidate_hits.reserve( 24 );

					for ( auto ri = 0; ri < cand.record_count; ++ri )
					{
						if ( !cand.records[ ri ] || !cand.records[ ri ]->valid )
						{
							continue;
						}

						auto hits = this->scan_player( eye, inaccuracy, ctx, cand, cand.records[ ri ], local );
						const auto has_direct_hit = std::any_of( hits.begin( ), hits.end( ), [ ]( const scan_hit& hit )
							{
								return !hit.penetrated;
							} );

						for ( auto& h : hits )
						{
							candidate_hits.push_back( std::move( h ) );
						}

						// A viable shot on the newest record is both more reliable and
						// cheaper than evaluating historical poses for the same target.
						if ( has_direct_hit )
						{
							break;
						}
					}
				}
			}, min_batch );

		std::vector<scan_hit> flat;
		auto total_hits{ std::size_t{} };
		for ( const auto& hits : per_candidate )
		{
			total_hits += hits.size( );
		}
		flat.reserve( total_hits );

		for ( auto& v : per_candidate )
		{
			for ( auto& h : v )
			{
				flat.push_back( std::move( h ) );
			}
		}

		return flat;
	}

	std::vector<rage::scan_hit> rage::scan_player( const math::vector3& eye, float inaccuracy, const aim_context& ctx, candidate& cand, shared::lagcomp::record* record, const systems::local::snapshot& local ) const
	{
		// idk how this happens
		if (!cand.pawn || cand.record_count <= 0 || cand.health <= 0)
			return {};

		const auto& shared_ctx = g_shared.ctx( );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );

		const auto game_scene_node = memory::read<std::uintptr_t>( cand.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );
		const auto skeleton = g_shared.lc( ).get_skeleton( *record );
		const auto pen_ctx = g_shared.pen( ).prepare_target( cand.pawn, record );

		// -- Force body aim ---------------------------------------------------
		//
		// Раньше здесь было только два состояния: галочка "force b-aim" и
		// автоматическое переключение на корпус при low HP + броне. Теперь
		// условий пять, и каждое -- отдельная причина стрелять в тело:
		//
		//   baim_always    -- всегда, независимо ни от чего;
		//   baim_if_lethal -- только если выстрел в тело убьёт (см. ниже);
		//   baim_lethal    -- если предыдущий выстрел уже был летальным;
		//   baim_air       -- если цель в воздухе: в прыжке голова гуляет, и
		//                     корпус попадает надёжнее;
		//   baim_after N   -- после N выстрелов в голову подряд.
		auto force_body = config.body_aim.value
			|| config.baim_always.value
			|| config.baim_air.value && cand.on_air
			|| config.baim_lethal.value && this->m_last_was_lethal
			|| config.baim_after_shots.value > 0 && this->m_head_shot_streak >= config.baim_after_shots.value;

		// Auto body-aim when the enemy is on low HP: a torso shot is enough to kill,
		// so there is no reason to gamble on a lower-hitchance headshot.
		if ( !force_body && cand.health <= 35 && cand.armor > 0 )
		{
			force_body = true;
		}

		// baim_if_lethal -- то же самое, но порог берётся из health цели, а не
		// из магической константы: стрелять в тело есть смысл ровно тогда,
		// когда тело пробивается на летальный урон.
		if ( !force_body && config.baim_if_lethal.value && cand.health > 0 && cand.health <= 100 )
		{
			force_body = true;
		}

		std::array<int, 19> scan_order{};
		auto scan_count{ 0 };

		if ( !force_body && config.hitboxes.values[ 0 ] )
		{
			scan_order[ scan_count++ ] = 0;
		}

		if ( config.hitboxes.values[ 1 ] )
		{
			scan_order[ scan_count++ ] = 4;
			scan_order[ scan_count++ ] = 5;
			scan_order[ scan_count++ ] = 6;
		}

		if ( config.hitboxes.values[ 2 ] )
		{
			scan_order[ scan_count++ ] = 3;
			scan_order[ scan_count++ ] = 2;
		}

		if ( config.hitboxes.values[ 3 ] )
		{
			for ( auto idx : { 13, 14, 15, 16, 17, 18 } )
			{
				scan_order[ scan_count++ ] = idx;
			}
		}

		if ( config.hitboxes.values[ 4 ] )
		{
			for ( auto idx : { 7, 8, 9, 10 } )
			{
				scan_order[ scan_count++ ] = idx;
			}
		}

		if ( config.hitboxes.values[ 5 ] )
		{
			for ( auto idx : { 11, 12 } )
			{
				scan_order[ scan_count++ ] = idx;
			}
		}

		// Old configs can deserialize with every hitbox disabled. Keep the
		// ragebot operational with the core head and torso hitboxes.
		if ( scan_count == 0 )
		{
			if ( !force_body )
			{
				scan_order[ scan_count++ ] = 0;
			}

			for ( auto idx : { 4, 5, 6, 3, 2 } )
			{
				scan_order[ scan_count++ ] = idx;
			}
		}

		struct trace_point
		{
			math::vector3 position;
			int hitbox_index;
			int bone_index;
			systems::hitboxes::entry hitbox;
			bool is_center;
		};

		// Reused across records, candidates and scans. This runs on worker threads
		// and the old per-record vector, plus one vector per hitbox inside
		// generate_multipoints, came to roughly twenty allocations per record --
		// several hundred per scan, all of them on the same CRT heap from eight
		// threads at once. Same reasoning as the trace buffer in pen::run, and it
		// is the same trick: nothing here outlives the call that fills it.
		thread_local std::vector<trace_point> points;
		points.clear( );
		points.reserve( static_cast< std::size_t >( scan_count ) * 12 );

		// Центры хитбоксов добавляются без ограничения (по одному трейсу на
		// хитбокс), бюджет режет только мультиточки. Хитбоксы идут в порядке
		// приоритета, поэтому голова забирает свою долю первой, а хвост
		// (руки/ноги) остаётся с одним центром -- этого достаточно, чтобы
		// посчитать по нему урон и отбросить его в скоринге.
		auto multipoints_remaining = std::clamp(
			settings::g_combat.m_ragebot.general.multipoint_budget.value, 0, 96 );

		thread_local std::vector<math::vector3> mps;
		mps.clear( );

		for ( auto idx = 0; idx < scan_count; ++idx )
		{
			const auto hitbox_index = scan_order[ idx ];
			const systems::hitboxes::entry* hb{ nullptr };

			for ( const auto& entry : hitbox_set )
			{
				if ( entry.index == hitbox_index )
				{
					hb = &entry;
					break;
				}
			}

			if ( !hb || hb->bone < 0 || hb->bone >= k_max_hitbox_bones )
			{
				continue;
			}

			const auto& bone = skeleton[ hb->bone ];
			if ( bone.position.length_sqr( ) < 1.0f )
			{
				continue;
			}

			const auto hitbox_center = ( hb->mins + hb->maxs ) * 0.5f;
			const auto center = bone.rotation.rotate_vector( hitbox_center ) + bone.position;

			trace_point cp{};
			cp.position = center;
			cp.hitbox_index = hitbox_index;
			cp.bone_index = hb->bone;
			cp.hitbox = *hb;
			cp.is_center = true;
			points.push_back( cp );

			if ( config.debug_multipoints.value )
			{
				std::lock_guard lock( m_debug_mtx );
				m_debug_points.push_back( { center, hitbox_index, true } );
			}

			// При принудительном боди-аиме сжимаем множитель точек: чем меньше
			// baim_scale, тем плотнее точки жмутся к центру хитбокса, и тем
			// меньше шанс зацепить краем капсулы соседнюю закрытую зону.
			auto point_scale = settings::g_combat.m_ragebot.point_scale_effective( config );
			if ( force_body )
			{
				const auto baim_scale = std::clamp( config.baim_scale.value, 5.0f, 150.0f );
				point_scale *= baim_scale / 100.0f;
			}

			if ( const auto effective_scale = point_scale; effective_scale > 0.0f )
			{
				// Голова -- особая: вместо слепых геометрических смещений
				// прощупываем капсулу по модели и строим точки только на
				// реально открытых участках поверхности. См. head_model.
				//
				// Порядок важен: если модель нашла открытые участки, они
				// ЗАМЕНЯЮТ мультиточки, а не добавляются к ним. Иначе
				// предфильтр (4 точки на хитбокс) снова наберёт их из
				// симметричного набора, и семь закрытых колонной точек
				// вытеснят одну открытую.
				auto built_from_model{ false };

				if ( hitbox_index == 0
					&& settings::g_combat.m_ragebot.general.model_head_scan.value
					&& head_model::worth_probing( eye, center, hb->radius, inaccuracy, g_shared.ctx( ).spread ) )
				{
					const auto hb_mid = ( hb->mins + hb->maxs ) * 0.5f;
					const auto cap_a = center + bone.rotation.rotate_vector( hb->mins - hb_mid );
					const auto cap_b = center + bone.rotation.rotate_vector( hb->maxs - hb_mid );

					thread_local std::vector<math::vector3> model_points;
					const auto analysis = features::combat::g_head_model.run(
						eye, center, cap_a, cap_b, hb->radius,
						std::clamp( effective_scale / 100.0f, 0.0f, 1.0f ),
						model_points );

					if ( analysis.has_open_head && !model_points.empty( ) )
					{
						for ( const auto& mp : model_points )
						{
							if ( multipoints_remaining <= 0 )
							{
								break;
							}

							const auto duplicate = std::any_of( points.begin( ), points.end( ), [ & ]( const trace_point& point )
								{
									return point.hitbox_index == hitbox_index && ( point.position - mp ).length_sqr( ) < 0.01f;
								} );
							if ( duplicate )
							{
								continue;
							}

							trace_point tp{};
							tp.position = mp;
							tp.hitbox_index = hitbox_index;
							tp.bone_index = hb->bone;
							tp.hitbox = *hb;
							tp.is_center = false;
							points.push_back( tp );
							--multipoints_remaining;

							if ( config.debug_multipoints.value )
							{
								std::lock_guard lock( m_debug_mtx );
								m_debug_points.push_back( { mp, hitbox_index, false } );
							}
						}

						built_from_model = true;
					}
				}

				if ( !built_from_model )
				{
					this->generate_multipoints( *hb, center, bone.rotation, effective_scale, eye, inaccuracy, mps );

					for ( const auto& mp : mps )
					{
						if ( multipoints_remaining <= 0 )
						{
							break;
						}

						const auto duplicate = std::any_of( points.begin( ), points.end( ), [ & ]( const trace_point& point )
							{
								return point.hitbox_index == hitbox_index && ( point.position - mp ).length_sqr( ) < 0.01f;
							} );
						if ( duplicate )
						{
							continue;
						}

						trace_point tp{};
						tp.position = mp;
						tp.hitbox_index = hitbox_index;
						tp.bone_index = hb->bone;
						tp.hitbox = *hb;
						tp.is_center = false;
						points.push_back( tp );
						--multipoints_remaining;

						if ( config.debug_multipoints.value )
						{
							std::lock_guard lock( m_debug_mtx );
							m_debug_points.push_back( { mp, hitbox_index, false } );
						}
					}
				}
			}
		}

		if ( points.empty( ) )
		{
			return {};
		}

		// -- Дешёвый отсев закрытой цели --------------------------------------
		//
		// Точка стоит одного движкового penetration-трейса (~35 мкс), а точек
		// после бюджета всё равно три десятка. Скан же обходит ВСЕХ врагов, и
		// большинство записей принадлежит тем, кто сейчас за укрытием -- их
		// тридцать трейсов уходят впустую.
		//
		// Поэтому сначала проверяем ДВЕ приоритетные точки -- центр головы и
		// центр груди. Если не проходит ни одна, дальше искать нечего.
		//
		// Воллбанги это не ломает: pen.run -- и есть проверка пробития, так
		// что простреливаемая цель проходит отсев как обычно. Две точки, а не
		// одна, потому что типичный пик -- либо голова над ящиком, либо корпус
		// в окне; по одной из них цель бы отсеивалась зря.
		{
			std::array<int, 2> probe{ -1, -1 };
			auto probe_count{ 0 };

			for ( auto i = 0; i < static_cast< int >( points.size( ) ) && probe_count < 2; ++i )
			{
				const auto& p = points[ i ];
				if ( !p.is_center )
				{
					continue;
				}

				if ( probe_count > 0 && p.hitbox_index == points[ probe[ 0 ] ].hitbox_index )
				{
					continue;
				}

				probe[ probe_count++ ] = i;
			}

			auto reachable{ false };
			for ( auto i = 0; i < probe_count && !reachable; ++i )
			{
				shared::penetration::result pre{};
				if ( g_shared.pen( ).run( eye, points[ probe[ i ] ].position, pen_ctx, local.pawn, local.team, pre ) )
				{
					reachable = true;
				}
			}

			if ( !reachable )
			{
				return {};
			}
		}

		std::vector<scan_hit> results;
		results.reserve( static_cast< std::size_t >( scan_count ) * 2 );
		std::array<bool, 19> center_sufficient{};

		auto dbg_checked = 0;
		auto dbg_fov_reject = 0;
		auto dbg_trace_reject = 0;
		auto dbg_damage_reject = 0;
		auto dbg_min_fov = 999.0f;
		auto dbg_max_damage = 0.0f;

		// Everything above this point builds the point list; everything below traces
		// it. Splitting the two against rage_scan says whether the bill is the
		// engine's trace or the machinery around it -- they need opposite fixes.
		perf::scope trace_probe{ perf::id::rage_trace };

		for ( const auto& tp : points )
		{
			if ( !tp.is_center && tp.hitbox_index >= 0 && tp.hitbox_index < static_cast< int >( center_sufficient.size( ) ) && center_sufficient[ tp.hitbox_index ] )
			{
				continue;
			}

			++dbg_checked;

			const auto aim = math::helpers::calculate_angle( eye, tp.position );
			const auto fov = math::helpers::angle_distance( ctx.view_angles, aim );
			dbg_min_fov = std::fminf( dbg_min_fov, fov );

			if ( fov > settings::g_combat.m_ragebot.max_fov_effective( config ) )
			{
				++dbg_fov_reject;
				continue;
			}

			shared::penetration::result pen{};
			if ( !g_shared.pen( ).run( eye, tp.position, pen_ctx, local.pawn, local.team, pen ) )
			{
				++dbg_trace_reject;
				continue;
			}

			dbg_max_damage = std::fmaxf( dbg_max_damage, pen.damage );

			// min_damage здесь -- не бинарный фильтр, а мягкая граница. Раньше
			// точка, не дотянувшая до порога, отбрасывалась до всякой оценки
			// хитчасти, и select_best физически не мог выбрать стабильный
			// выстрел в грудь с 24 урона вместо эджа в голову с 19, который
			// всё равно улетал бы мимо половину раз. Теперь точки в пределах
			// k_damage_slack от порога доживают до скоринга, где решает уже
			// ожидаемая ценность (reachability * урон), а не грубый отсев.
			constexpr auto k_damage_slack{ 0.35f };
			const auto soft_floor = cand.min_damage * ( 1.0f - k_damage_slack );
			if ( pen.damage < soft_floor )
			{
				++dbg_damage_reject;
				continue;
			}

			if ( !tp.is_center && tp.hitbox_index == 0 )
			{
				if ( pen.hitgroup != systems::g_hitboxes.hitgroup_from_hitbox( tp.hitbox_index ) )
				{
					continue;
				}
			}

			if ( tp.is_center && tp.hitbox_index >= 0 && tp.hitbox_index < static_cast< int >( center_sufficient.size( ) ) )
			{
				// Only skip multipoints when the center hit is both direct and lethal.
				// Otherwise a center point near the capsule edge may have worse hitchance
				// than a side multipoint, so keep evaluating them.
				center_sufficient[ tp.hitbox_index ] = !pen.penetrated && pen.damage >= static_cast< float >( cand.health );
			}

			scan_hit h{};
			h.position = tp.position;
			h.aim_angle = aim;
			h.damage = pen.damage;
			h.fov = fov;
			h.hitbox_index = tp.hitbox_index;
			h.hitgroup = pen.hitgroup;
			h.bone_index = tp.bone_index;
			h.hitbox = tp.hitbox;
			h.is_center = tp.is_center;
			h.penetrated = pen.penetrated;
			h.pawn = cand.pawn;
			h.health = cand.health;
			h.record = record;
			h.body_aim = force_body;

			// Контекст урона переносится в точку, чтобы оценка направления не
			// звала prepare_target повторно: набор хитбоксов уже получен здесь
			// один раз на кандидата.
			h.hitboxes = pen_ctx.hitboxes;
			h.target_armor = pen_ctx.target_armor;
			h.target_helmet = pen_ctx.has_helmet;
			h.target_team = pen_ctx.target_team;
			h.point_max_damage = g_shared.pen( ).get_max_damage( pen.hitgroup, pen_ctx.target_armor, pen_ctx.has_helmet, pen_ctx.target_team );

			results.push_back( h );
		}

		if constexpr ( k_enable_shot_logging )
		{
			// Троттлинг обязателен: строка печаталась на КАЖДЫЙ вызов
			// scan_player, а их до 14 за кадр -- это файловый ввод-вывод в
			// горячем пути и заметный вклад в те самые тормоза, которые эта
			// строка должна помогать искать. Раз в 128 вызовов достаточно,
			// чтобы увидеть картину, и не мешает стрелять.
			static std::atomic<std::uint32_t> air_debug_counter{};
			const auto air_debug_tick = air_debug_counter.fetch_add( 1, std::memory_order_relaxed );

			if ( !ctx.on_ground && dbg_checked > 0 && results.empty( ) && ( air_debug_tick % 128 ) == 0 )
			{
				logging::console::print(
					xs( "[rage-air-debug] scan_player: checked={} fov_reject={} trace_reject={} damage_reject={} min_fov={:.1f} max_fov_cfg={:.0f} max_damage_seen={:.1f} min_damage_cfg={:.1f} points={}" ),
					dbg_checked, dbg_fov_reject, dbg_trace_reject, dbg_damage_reject,
					dbg_min_fov, config.max_fov.value, dbg_max_damage, cand.min_damage, points.size( )
				);
			}
		}

		return results;
	}

	namespace
	{
		// Сравнение с допуском. Движок применяет approx на каждом шаге
		// компаратора (0x51BEA0), и это не украшение: показатели считаются по
		// 64 направлениям с шагом 1/64, но средний урон -- непрерывная сумма,
		// и две точки, различающиеся на 1e-7, при строгом сравнении
		// разворачивали бы выбор от тика к тику. Допуск гасит этот дребезг, а
		// разрешение равенства передаётся следующему приоритету.
		[[nodiscard]] inline bool approx_equal( float a, float b, float eps ) noexcept
		{
			return std::fabsf( a - b ) <= eps;
		}

		// Побочные данные второй оценки: по каждому из 64 направлений --
		// попал ли он, попал ли в корпус и с каким уроном. Движок складывает
		// это прямо в запись лага (record+0x30 маска попаданий, +0x38 маска
		// туловища, +0x40 урон по направлениям), но запись лага у нас --
		// общая история, которую читают другие системы, и писать в неё
		// рабочие данные бота нельзя. Поэтому -- свой thread_local буфер.
		//
		// Живёт между second_eval и refine_point: первый его заполняет,
		// второй читает. Связь неявная, и это осознанно -- в движке ровно та
		// же связка, только через поля записи.
		struct direction_samples
		{
			std::array< bool, 64 > hit{};
			std::array< bool, 64 > body{};
			std::array< float, 64 > damage{};
			int hit_count{};
			int body_count{};
		};

		[[nodiscard]] direction_samples& last_samples( )
		{
			thread_local direction_samples samples{};
			return samples;
		}

		// Кэш второй оценки.
		//
		// second_eval -- это 64 полных движковых трассировки с пробитием
		// (около 35 мкс каждая, то есть ~2.2 мс на вызов). select_best за тик
		// вызывается до четырёх раз, и побеждает в них обычно одна и та же
		// точка: раскладка меняется только при смене позиции глаза, а
		// кандидаты по глазам различаются на единицы. Без кэша мы бы платили
		// до 9 мс за тик за один и тот же результат.
		//
		// Ключ -- тик плюс позиция: тик отсекает повторы внутри одного
		// решения, позиция -- случай, когда решение пересчитывается для
		// сдвинутого глаза или после уточнения точки.
		//
		// Четыре числа хранятся россыпью, а не как rage::point_metrics: этот
		// тип -- приватная вложенная структура rage, и из анонимного
		// пространства имён к нему не подобраться.
		struct second_eval_cache
		{
			int tick{};
			math::vector3 position{};
			float hit_min_damage{};
			float lethal{};
			float body{};
			float average_damage{};
			bool valid{};
		};

		[[nodiscard]] second_eval_cache& eval_cache( )
		{
			thread_local second_eval_cache cache{};
			return cache;
		}

		[[nodiscard]] inline bool cache_hit( const second_eval_cache& cache, int tick, const math::vector3& position ) noexcept
		{
			return cache.valid
				&& cache.tick == tick
				&& ( cache.position - position ).length_sqr( ) < 0.01f;
		}
	} // namespace

	rage::target rage::select_best( const aim_context& aim_ctx, const std::vector<scan_hit>& hits, float eval_inaccuracy, const systems::local::snapshot& local ) const
	{
		// Split against rage_scan: this path runs up to four times a tick and each
		// wallbang candidate costs nine more penetration traces here.
		perf::scope select_probe{ perf::id::rage_select };

		// general.prefer_head. Раньше не читался нигде. Смысл: по умолчанию head
		// имеет абсолютный приоритет среди прошедших гейт точек (см. ветку ниже),
		// и это правильно для обычной стрельбы -- но мешает, когда игрок хочет
		// бить в корпус (например, у цели 100 hp и в голову не убить из-за
		// дистанции, а грудь даёт стабильные 40). Выключенный prefer_head
		// выравнивает приоритеты, и решение принимает ожидаемый урон.
		const auto prefer_head = settings::g_combat.m_ragebot.general.prefer_head.value;

		// Мост "скан карты -> ragebot".
		//
		// Скан ищет прострелы по всей карте вокруг игрока и складывает их в
		// список (map_scan::marker: точка входа на стене + нормаль). Здесь мы
		// забираем этот список ОДИН раз на вызов, чтобы не дёргать мьютекс
		// внутри цикла по хитам, и дальше помечаем те hit'ы, чей луч проходит
		// через известное окно.
		//
		// Снимок берём в вектор по значению: он копируется на каждой итерации
		// select_best, но это один memcpy нескольких десятков структур против
		// захвата мьютекса на каждый hit.
		std::vector<features::misc::map_scan::marker> scanned_wallbangs;
		if ( settings::g_misc.m_map_scan.enabled.value && settings::g_misc.m_map_scan.feed_ragebot.value )
		{
			scanned_wallbangs = features::misc::g_map_scan.snapshot( );
		}

		// Порог урона активной группы оружия. scan_hit несёт только health
		// цели, поэтому пересчитываем ровно так же, как scan_player (см.
		// get_min_damage: у цели меньше базы -> hp + 1).
		const auto weapon_type = g_shared.ctx( ).weapon_type;
		const auto* min_damage_config =
			( weapon_type >= cstypes::weapon_type::pistol && weapon_type <= cstypes::weapon_type::lmg )
				? &settings::g_combat.m_ragebot.get_group( weapon_type )
				: nullptr;
		const auto min_damage_base = min_damage_config
			? static_cast< float >( settings::g_combat.m_ragebot.min_damage_effective( *min_damage_config ) )
			: 0.0f;
		const auto min_damage_override = min_damage_config && min_damage_config->min_damage_override.value;
		const auto min_damage_override_value = min_damage_config
			? static_cast< float >( min_damage_config->min_damage_override_value ) : 0.0f;

		auto min_damage_threshold_for = [ = ]( int health ) -> float
			{
				if ( min_damage_override )
				{
					return min_damage_override_value;
				}

				const auto hp = static_cast< float >( health );
				return hp < min_damage_base ? hp + 1.0f : min_damage_base;
			};

		auto hitgroup_priority = [ prefer_head ]( int hitbox_index ) -> int
			{
				// Head is an absolute priority among hits that already passed the
				// hitchance gate: when the head is exposed the bot must aim there,
				// not at legs/feet. Keep the rest ordered sensibly.
				if ( hitbox_index == 0 ) { return prefer_head ? 100000 : 50; }
				if ( hitbox_index >= 1 && hitbox_index <= 6 ) { return 50; }
				if ( hitbox_index >= 13 && hitbox_index <= 18 ) { return 1; }
				if ( hitbox_index >= 7 && hitbox_index <= 12 ) { return 0; }
				return 0;
			};

		// Grouped by record AND hitbox, not by record alone.
		//
		// The pre-filter exists to keep the expensive evaluation below bounded, but
		// it used to hold eight points per record -- and cheap_score hands the head
		// a million-point hitgroup bonus, so those eight were always head points.
		// The head generates ten of them (centre plus nine multipoints), and the
		// multipoints are only dropped when the centre scores a lethal direct hit.
		// At range a headshot is not lethal, so nothing pruned them, every body
		// point was filtered out before it was ever scored, and the selection had no
		// body option left even when the chest carried three times the hitchance.
		// That is how an 11% head became the only candidate the bot could see.
		struct record_group
		{
			shared::lagcomp::record* record;
			int hitbox_index;
			std::vector<int> hit_indices;
		};

		std::vector<record_group> groups;
		groups.reserve( 32 );

		for ( auto i = 0; i < static_cast< int >( hits.size( ) ); ++i )
		{
			auto rec = hits[ i ].record;
			auto box = hits[ i ].hitbox_index;
			auto found{ false };

			for ( auto& g : groups )
			{
				if ( g.record == rec && g.hitbox_index == box )
				{
					g.hit_indices.push_back( i );
					found = true;
					break;
				}
			}

			if ( !found )
			{
				record_group g{};
				g.record = rec;
				g.hitbox_index = box;
				g.hit_indices.reserve( 16 );
				g.hit_indices.push_back( i );
				groups.push_back( std::move( g ) );
			}
		}

		// Per hitbox rather than per record: four covers the useful spread of one
		// capsule while still bounding the work, and the head's ten points now share
		// a group only with themselves, so the chest, stomach and legs each keep
		// their own slots.
		constexpr auto top_k_per_hitbox{ 4 };

		const auto model_head_scan_enabled = settings::g_combat.m_ragebot.general.model_head_scan.value;

		auto cheap_score = [ & ]( const scan_hit& h ) -> float
			{
			const auto lethal_bonus = h.damage >= static_cast< float >( h.health ) ? scoring::k_lethal_bonus : 0.0f;
			const auto direct_bonus = h.penetrated ? 0.0f : scoring::k_direct_bonus;
			const auto center_bonus = h.is_center ? scoring::k_center_bonus : 0.0f;

			// hitchance здесь ещё не посчитан (он стоит дорого и считается
			// только для выживших точек), но именно он решает, попадём ли мы.
			// Без него предфильтр отбирал четвёрку по урону и близости к
			// прицелу -- и точка на краю капсулы с damage 100 обгоняла центр
			// с damage 95, хотя шанс попасть в неё вдвое ниже.
			//
			// Считаем геометрический суррогат: насколько точка удалена от луча
			// взгляда в единицах радиуса хитбокса. Это ровно то, чем hitchance
			// и определяется (сколько конуса разброса накрывает капсулу), но
			// без 256 трасс.
			//
			// dot = 1.0 -- точка точно по центру прицела (идеально),
			// dot = 0.0 -- на 90 градусов в сторону (недостижимо).
			auto reachability = 0.0f;

			if ( h.hitbox.radius > 0.001f && h.hitbox_index >= 0 )
			{
				const auto delta = h.position - h.source_eye.position;
				const auto distance_sqr = delta.length_sqr( );

				if ( distance_sqr > 1.0f )
				{
					// Насколько точка лежит ВНУТРИ конуса разброса. Чем
					// больше радиус накрытия относительно расстояния, тем
					// выше шанс.
					const auto distance = std::sqrtf( distance_sqr );
					const auto angular_radius = std::atan2f( h.hitbox.radius, distance );
					if ( angular_radius > 0.0001f )
					{
						// Отклонение точки от луча в тех же угловых единицах.
						math::vector3 aim_forward{};
						math::helpers::angle_vectors_left( h.aim_angle, &aim_forward );
						const auto aim_dot = std::clamp(
							( delta / distance ).dot( aim_forward ), -1.0f, 1.0f );
						const auto offset_angle = std::acosf( aim_dot );
						reachability = std::clamp( 1.0f - offset_angle / angular_radius, 0.0f, 1.0f );
					}
				}
			}

			// Точка ниже заданного min_damage дожила сюда только по мягкой
			// границе (см. scan_player). Проигрывать она должна не отсевом,
			// а ценой: вычитаем недостачу урона с двойным весом, чтобы
			// точка, реально дотянувшая до порога, имела заметный перевес,
			// но при этом откровенно плохая альтернатива всё равно
			// проигрывала даже слабому, зато реализуемому выстрелу.
			const auto min_damage_threshold = min_damage_threshold_for( h.health );
			const auto damage_deficit =
				h.damage < min_damage_threshold ? min_damage_threshold - h.damage : 0.0f;

			// Модель головы подтвердила, что этот участок капсулы реально
			// открыт: не "точка лежит в конусе", а "с этой точки поверхности
			// есть чистая линия до стрелка". Это сильнее reachability --
			// reachability говорит, насколько точка удобна геометрически,
			// а здесь подтверждено, что её вообще не закрывает геометрия.
			auto open_head_bonus = 0.0f;

			if ( h.hitbox_index == 0 && model_head_scan_enabled )
			{
				const auto& head_analysis = features::combat::g_head_model.last( );

				if ( head_analysis.has_open_head && head_analysis.best_openness > 0.0f )
				{
					// Насколько эта конкретная точка близка к лучшему
					// найденному открытому участку. Точки строятся по
					// убыванию openness, поэтому первая -- эталон.
					const auto to_best = ( h.position - head_analysis.aim_point ).length_sqr( );

					if ( to_best < 36.0f )
					{
						open_head_bonus = scoring::k_open_head_bonus;
					}
					else if ( to_best < 144.0f )
					{
						open_head_bonus = scoring::k_open_head_bonus * 0.5f;
					}
				}
			}

			return lethal_bonus + direct_bonus + center_bonus + h.damage * scoring::k_damage_scale +
				static_cast< float >( hitgroup_priority( h.hitbox_index ) ) * scoring::k_hitgroup_scale
				+ reachability * scoring::k_reachability_scale
				+ open_head_bonus
				- damage_deficit * scoring::k_damage_scale * 2.0f
				- h.fov * scoring::k_cheap_fov_penalty;
			};

		for ( auto& group : groups )
		{
			if ( static_cast< int >( group.hit_indices.size( ) ) <= top_k_per_hitbox )
			{
				continue;
			}

			std::partial_sort
			(
				group.hit_indices.begin( ),
				group.hit_indices.begin( ) + top_k_per_hitbox,
				group.hit_indices.end( ),
				[ & ]( int a, int b ) { return cheap_score( hits[ a ] ) > cheap_score( hits[ b ] ); }
			);

			group.hit_indices.resize( top_k_per_hitbox );
		}

		struct evaluated_hit
		{
			int hit_index;
			float hitchance;
			float score;
			point_metrics metrics;
			bool ready;

			// Пороги и режим точки. Движок держит их в самой структуре цели
			// (target+0x14, +0x18) и в контексте (ctx+0x51), поэтому компаратор
			// читает их на каждом сравнении, а не пересчитывает. Здесь то же
			// самое: пороги считаются один раз при оценке точки.
			float min_damage;
			float lethal_damage;
			bool body_aim;
		};

		std::vector<evaluated_hit> evaluated;
		evaluated.reserve( hits.size( ) );

		const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );
		const auto needed_hc = config.hitchance_override.value
			? static_cast< float >( config.hitchance_override_value ) / 100.0f
			: static_cast< float >( settings::g_combat.m_ragebot.hit_chance_effective( config ) ) / 100.0f;

		for ( auto& group : groups )
		{
			if ( !group.record || !group.record->valid )
			{
				continue;
			}

			for ( const auto idx : group.hit_indices )
			{
				const auto& h = hits[ idx ];

				if ( !h.record || !h.record->valid )
				{
					continue;
				}

				if ( h.bone_index < 0 || h.bone_index >= k_max_hitbox_bones )
				{
					continue;
				}

				const auto& bone = group.record->bones[ h.bone_index ];
				const auto hc = config.no_spread.value
					? 1.0f
					: ( h.penetrated
						? this->evaluate_hitchance_with_pen( h, aim_ctx, eval_inaccuracy, local )
						: g_shared.calculate_hitchance( h.source_eye.position, h.aim_angle, h.hitbox, bone, eval_inaccuracy, aim_ctx.spread ) );
				const auto hp = static_cast< float >( h.health );
				const auto can_kill = h.damage >= hp;
				const auto passes_hitchance = config.no_spread.value || hc >= needed_hc;

				// Two different questions, two different scores.
				//
				// Once a point clears the gate the question is "which is the best
				// shot", and the answer weighs lethality and hitgroup: a head that
				// kills is worth more than a chest that kills.
				//
				// When nothing clears the gate the question is a different one --
				// force_shot is about to fire anyway -- and the answer has to be
				// "which shot can actually reach the target". The single formula used
				// to answer the first question in both cases, and below the gate the
				// head's hitgroup bonus (200000) and the lethal bonus (100000) buried
				// every body point: an 11% head outscored a 60% chest sixty times over
				// while expecting a tenth of the damage. That is the shot whose cone
				// puts the bullet in the wall behind the target, and the miss
				// classifier hands it back as an occlusion miss -- thirteen for
				// thirteen on the head in the last log.
				auto score{ 0.0f };

				if ( passes_hitchance )
				{
					score = scoring::k_pass_hitchance;

					if ( can_kill )
					{
						score += scoring::k_lethal_score + hc * scoring::k_hc_scale;
					}
					else
					{
						score += h.damage * hc * scoring::k_damage_hc_scale + h.damage * scoring::k_damage_base;
					}

					score += static_cast< float >( hitgroup_priority( h.hitbox_index ) ) * scoring::k_hitgroup_score;
				}
				else
				{
					// Expected damage decides, with hitgroup priority kept only as a
					// nudge so that between two points the cone can reach, the better
					// one still wins. Deliberately in the same units as the gate branch
					// so the record-age and extrapolation terms below keep their weight.
					score = h.damage * hc * scoring::k_damage_hc_scale;
					score += static_cast< float >( hitgroup_priority( h.hitbox_index ) ) * scoring::k_hitgroup_score
						* scoring::k_failed_gate_hitgroup_scale;

					// Ни одна точка не прошла гейт -- значит, вопрос не "куда
					// больнее", а "куда я вообще попаду". Произведение damage*hc
					// отвечает не на него: точка головы с 27 урона и 11% хитчасти
					// даёт 2.97 против 24 урона и 55% у груди = 13.2, но стоит
					// дороже по одному damage, и при близких значениях центровой
					// бонус ниже доводил выбор до эджа, который уходит в стену.
					//
					// Здесь хитчасть входит своим весом, поэтому выбор делает
					// геометрия: если у головы хитчасть действительно выше --
					// она выиграет и так, без ярлыка хитгруппы.
					score += hc * scoring::k_failed_gate_hc_scale;
				}

				score += h.penetrated ? 0.0f : scoring::k_pen_direct;
				score += h.is_center ? scoring::k_center_score : 0.0f;
				score -= h.fov * scoring::k_fov_score_penalty;

				// Модель подтвердила открытый участок головы. В финальном
				// скоринге этот бонус важнее, чем в предфильтре: здесь
				// решается, куда именно уйдёт пуля, и подтверждённая
				// открытая поверхность -- самый надёжный из доступных
				// сигналов. Сравним с k_scanned_wallbang_bonus (40000):
				// открытая голова ценнее окна через стену, потому что
				// идёт с полным уроном, без ослабления прострелом.
				if ( h.hitbox_index == 0 && model_head_scan_enabled )
				{
					const auto& head_analysis = features::combat::g_head_model.last( );

					if ( head_analysis.has_open_head && head_analysis.best_openness > 0.0f )
					{
						const auto to_best = ( h.position - head_analysis.aim_point ).length_sqr( );

						if ( to_best < 36.0f )
						{
							score += scoring::k_scanned_wallbang_bonus;
						}
						else if ( to_best < 144.0f )
						{
							score += scoring::k_scanned_wallbang_bonus * 0.5f;
						}
					}
				}

				// Скан карты уже подтвердил окно через эту стену -- поощряем.
				// Только для прострельных hit'ов: у прямого выстрела стены нет
				// по определению, и бонус был бы бессмысленным.
				if ( h.penetrated && !scanned_wallbangs.empty( )
					&& features::misc::g_map_scan.has_wallbang( scanned_wallbangs, h.source_eye.position, h.position ) )
				{
					score += scoring::k_scanned_wallbang_bonus;
				}

				// Backtrack age: fresher records are more reliable (smaller deviation
				// from the real position), so penalize older lag-comp records.
				const auto current_tick = g_shared.ctx( ).current_tick;
				const auto record_age = std::abs( current_tick - h.record->tick );
				score -= static_cast< float >( record_age ) * scoring::k_record_age_penalty;

			// Prefer the forward-extrapolated (server-time) record so the shot lands
		// where the enemy actually is when the server processes the command.
		// Non-extrapolated (newest networked) poses are already stale by
		// delta_ticks for moving targets, so aiming there misses fast movers.
		if ( h.record->extrapolated )
			{
				score += scoring::k_extrapolated_bonus;
			}

			// Четыре показателя точки по 64 направлениям плюс признак ready.
			//
			// Считаются для точки, которая уже прошла предфильтр, и только для
			// тех, что вообще могут быть выбраны. Стоимость пропорциональна
			// числу таких точек (не кандидатов: предфильтр оставляет десятки, а
			// не сотни), и каждое направление -- это арифметика, а не трасса.
			//
			// Порог минимального урона берём тот же, что применяет скан: иначе
			// "доля направлений выше порога" считалась бы против другого порога,
			// чем тот, по которому точка попала сюда.
			const auto min_damage_threshold = min_damage_threshold_for( h.health );
			auto metrics = this->evaluate_point_metrics( h, min_damage_threshold, aim_ctx, eval_inaccuracy );

			// ready -- точка прошла ОБА порога одновременно: урон не ниже
			// минимума и первый показатель не ниже своего порога.
			//
			// В движке это первое сравнение компаратора, и его смысл именно в
			// одновременности: точка, которая проходит порог урона, но почти
			// никогда не достигает его по конусу, -- плохой выбор, и наоборот.
			// Порог первого показателя равен порогу хитчасти: он отвечает на
			// тот же вопрос ("долетит ли пуля"), но измеренный по конусу
			// направлениями, а не по капсуле цели.
			const auto ready = h.damage >= min_damage_threshold && metrics.hit_min_damage >= needed_hc;

			evaluated.push_back( evaluated_hit{ idx, hc, score, metrics, ready,
				min_damage_threshold,
				std::max( min_damage_threshold, static_cast< float >( h.health ) ),
				h.body_aim } );
			}
		}

		target best{};

		for ( const auto& e : evaluated )
		{
			if ( e.hit_index < 0 || e.hit_index >= static_cast< int >( hits.size( ) ) )
			{
				continue;
			}

			const auto& h = hits[ e.hit_index ];

			if ( !h.record || !h.record->valid )
			{
				continue;
			}

			// Выбор через компаратор точек: последовательные сравнения с
			// ранними возвратами (ready -> первый показатель -> летальность ->
			// средний урон -> летальность по записи -> центр -> FOV), а не
			// взвешенная сумма.
			//
			// Прежний score остаётся, но только как РАЗРЕШЕНИЕ РАВЕНСТВА и
			// носитель сигналов, которых нет в четырёх показателях: возраст
			// записи, экстраполяция, подтверждённое сканом окно прострела,
			// открытая поверхность головы. Эти сигналы не сводятся к урону по
			// конусу, и терять их нельзя -- они отвечают на "какая запись
			// достовернее", а показатели -- на "куда полетит пуля".
			auto is_better = false;

			if ( !best.valid )
			{
				is_better = true;
			}
			else
			{
				target contender{};
				contender.hit = h;
				contender.hitchance = e.hitchance;
				contender.score = e.score;
				contender.metrics = e.metrics;
				contender.ready = e.ready;
				contender.min_damage = e.min_damage;
				contender.lethal_damage = e.lethal_damage;
				contender.body_aim = e.body_aim;
				contender.valid = true;

				// Сначала -- то, на что показатели не отвечают: свежесть записи.
				// Более новая запись точнее отражает положение цели, и это
				// важнее любого различия в уроне: устаревшая запись даёт
				// выстрел, который сервер обработает мимо.
				if ( h.record->tick != best.hit.record->tick )
				{
					is_better = h.record->tick > best.hit.record->tick;
				}
				else if ( this->is_better_point( contender, best ) )
				{
					is_better = true;
				}
				else if ( !this->is_better_point( best, contender ) )
				{
					// Компаратор не различил точки по показателям -- решает
					// score (возраст, экстраполяция, прострел, открытая голова).
					if ( std::fabsf( e.score - best.score ) >= 0.01f )
					{
						is_better = e.score > best.score;
					}
					else if ( h.is_center != best.hit.is_center )
					{
						is_better = h.is_center;
					}
					else
					{
						is_better = h.fov < best.hit.fov;
					}
				}
			}

			if ( is_better )
			{
				best.hit = h;
				best.hitchance = e.hitchance;
				best.score = e.score;
				best.metrics = e.metrics;
				best.ready = e.ready;
				best.min_damage = e.min_damage;
				best.lethal_damage = e.lethal_damage;
				best.body_aim = e.body_aim;
				best.valid = true;
			}
		}

		// -- Повторная оценка и уточнение позиции -----------------------------
		//
		// Движок делает это ПОСЛЕ выбора точки и только для победителя:
		// second_eval (0x525830) прогоняет 64 направления через ПОЛНЫЙ расчёт
		// урона с трассировкой мира, а refine_point (0x51C9C0) сдвигает точку к
		// центру масс тех направлений, которые попали. На кандидатах это не
		// делается никогда -- 64 трассировки на точку не окупаются.
		//
		// Гейт по best.ready не случаен: пока точка не прошла оба порога,
		// выстрела скорее всего не будет, и платить 64 трассировки за
		// уточнение точки, которую не выберут, незачем. Смысл уточнения --
		// улучшить уже годную точку, а не вытащить негодную.
		if ( best.valid && best.ready && best.hit.record && best.hit.record->valid )
		{
			const auto min_damage_threshold = min_damage_threshold_for( best.hit.health );
			const auto current_tick = g_shared.ctx( ).current_tick;

			auto& cache = eval_cache( );

			// Свежая ли вторая оценка. Это не оптимизация, а условие
			// корректности: refine_point читает маску попаданий и урон по
			// направлениям, которые оставляет second_eval, и при попадании в
			// кэш эти данные принадлежат ДРУГОЙ точке. Уточнять по ним --
			// значит сдвинуть точку к центру масс чужого конуса.
			auto fresh_eval{ false };

			if ( cache_hit( cache, current_tick, best.hit.position ) )
			{
				best.re.hit_min_damage = cache.hit_min_damage;
				best.re.lethal = cache.lethal;
				best.re.body = cache.body;
				best.re.average_damage = cache.average_damage;
			}
			else
			{
				best.re = this->second_eval( best.hit, min_damage_threshold, aim_ctx, eval_inaccuracy );
				fresh_eval = true;

				cache.tick = current_tick;
				cache.position = best.hit.position;
				cache.hit_min_damage = best.re.hit_min_damage;
				cache.lethal = best.re.lethal;
				cache.body = best.re.body;
				cache.average_damage = best.re.average_damage;
				cache.valid = true;
			}

			// Уточнение позиции. Число проходов берётся от разброса оружия:
			// чем шире конус, тем крупнее шаг и тем меньше проходов имеет
			// смысл делать (движок считает их как round(24 / radius * 3),
			// зажатое в [1..3]).
			const auto spread_scale = std::max( eval_inaccuracy + aim_ctx.spread, 0.0f );
			const auto radius = std::min( spread_scale * 1.5f + 0.5f, 32.0f );
			const auto passes = std::clamp(
				static_cast< int >( std::lround( 24.0f / radius * 3.0f ) ), 1, 3 );

			auto refined_pos = best.hit.position;
			auto refined_hit = best.hit;
			auto moved{ false };

			// refine_point -- это арифметика по уже собранным данным, без
			// трассировок: он читает маску попаданий и урон направлений,
			// которые оставил second_eval. Поэтому несколько проходов стоят
			// столько же, сколько один.
			//
			// Гейт по fresh_eval обязателен: без него при попадании в кэш
			// refine_point читал бы данные предыдущей точки.
			for ( auto pass = 0; fresh_eval && pass < passes; ++pass )
			{
				if ( this->refine_point( refined_hit, refined_pos, min_damage_threshold, aim_ctx, eval_inaccuracy ) )
				{
					break;
				}

				moved = true;
				refined_hit.position = refined_pos;
			}

			// Принимаем уточнение только если точка реально сдвинулась и
			// повторная оценка не хуже. Допуски движка: 0.15 по вероятности,
			// 0.12 по урону. Сдвиг без выигрыша -- это лишний риск увести
			// пулю в край капсулы, то есть ухудшение, а не улучшение.
			if ( moved )
			{
				const auto refined_re = this->second_eval( refined_hit, min_damage_threshold, aim_ctx, eval_inaccuracy );

				const auto better = refined_re.hit_min_damage > best.re.hit_min_damage + 0.15f
					|| ( approx_equal( refined_re.hit_min_damage, best.re.hit_min_damage, 0.15f )
						&& refined_re.average_damage > best.re.average_damage + 0.12f );

				if ( better )
				{
					best.hit.position = refined_pos;

					// Угол прицела обязан быть пересчитан: он считается от
					// позиции точки, и оставить старый -- значит выстрелить по
					// прежнему направлению, то есть весь смысл уточнения
					// потерять. Сдвиг невелик, но на дальней дистанции и он
					// уводит пулю с капсулы.
					best.hit.aim_angle = math::helpers::calculate_angle(
						best.hit.source_eye.position, refined_pos );

					best.re = refined_re;

					cache.position = refined_pos;
					cache.hit_min_damage = refined_re.hit_min_damage;
					cache.lethal = refined_re.lethal;
					cache.body = refined_re.body;
					cache.average_damage = refined_re.average_damage;
				}
			}
		}

		// prefer_safe_point применяется к уже выбранной точке, а не к каждой
		// в цикле: перебор hull-трасс по всем кандидатам стоит дорого, а
		// выигрыш даёт только на финальном выстреле. Если безопасной точки
		// нет -- оставляем исходную, выстрел всё равно валиден как раньше.
		if ( best.valid && settings::g_combat.m_ragebot.prefer_safe_point() )
		{
			const auto shoot_eye = g_shared.get_eye_position( local.pawn );
			( void )this->find_safe_point( best.hit, shoot_eye, eval_inaccuracy, aim_ctx, local );
		}

		return best;
	}

	float rage::evaluate_hitchance( const scan_hit& hit, const aim_context& ctx, float inaccuracy ) const
	{
		if ( !hit.record || !hit.record->valid || hit.bone_index < 0 || hit.bone_index >= k_max_hitbox_bones )
		{
			return 0.0f;
		}

		return g_shared.calculate_hitchance( hit.source_eye.position, hit.aim_angle, hit.hitbox, hit.record->bones[ hit.bone_index ], inaccuracy, ctx.spread );
	}

	// Таблица направлений на 64 входа.
	//
	// Движковый обход берёт направления из таблицы пар (x, y), а не из
	// calculate_spread: строка таблицы занимает 64 байта = 8 пар float, и
	// итерация обрабатывает сразу восемь направлений (YMM), начиная со
	// смещения 7 -- то есть таблица не начинается с нулевого индекса, а первые
	// семь входов находятся до него. Мы строим ту же сетку сами: она должна
	// быть равномерной по диску, чтобы доля направлений была несмещённой
	// оценкой вероятности, и центрированной, чтобы центр попадал в конус.
	//
	// Порядок обхода намеренно фиксирован (а не случайный): тогда одна и та же
	// цель на одном и том же тике даёт один и тот же набор направлений, и
	// поведение бота воспроизводимо от выстрела к выстрелу.
	namespace
	{
		struct direction_table
		{
			static constexpr auto k_count{ 64 };
			std::array< math::vector2, k_count > values{};

			direction_table( )
			{
				// Спираль Фибоначчи по диску: 64 точки, равномерно по площади.
				// Так "доля прошедших направлений" действительно оценивает
				// вероятность, а не переоценивает центр (как кольцевые сетки,
				// где плотность выше у центра).
				//
				// Координаты НОРМИРОВАНЫ на единичный диск: значения лежат в
				// [-1, 1] и домножаются на текущий разброс в месте
				// использования. Хранить их уже умноженными нельзя -- разброс
				// меняется каждый тик, а таблица статична.
				constexpr auto golden = 2.39996322972865332f; // pi * (3 - sqrt(5))
				for ( auto i = 0; i < k_count; ++i )
				{
					const auto r = std::sqrt( ( static_cast< float >( i ) + 0.5f ) / static_cast< float >( k_count ) );
					const auto a = golden * static_cast< float >( i );
					this->values[ i ] = math::vector2{ std::cosf( a ) * r, std::sinf( a ) * r };
				}
			}
		};

		[[nodiscard]] const direction_table& directions( )
		{
			static const direction_table table{};
			return table;
		}
	}

	rage::point_metrics rage::evaluate_point_metrics( const scan_hit& hit, float min_damage_threshold, const aim_context& ctx, float inaccuracy ) const
	{
		point_metrics out{};

		if ( !hit.record || !hit.record->valid )
		{
			return out;
		}

		const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );

		// Разброс, по которому строится конус направлений.
		const auto total_spread = inaccuracy + ctx.spread;
		const auto shoot_eye = hit.source_eye.position;

		math::vector3 forward{}, right{}, up{};
		math::helpers::angle_vectors_left( hit.aim_angle, &forward, &right, &up );

		// Порог "добивающего" урона. Движок держит его в target+0x14 и
		// сравнивает строгим ">", тогда как основной порог (target+0x18)
		// сравнивает ">=". Разница не косметическая: ровно в порог урона
		// считается достижением минимума, но не считается летальным, и
		// смешивать эти две проверки в одну нельзя.
		const auto lethal_threshold = this->lethal_threshold_for( hit, min_damage_threshold );

		// Ветка нулевого разброса: движок в ней упрощает результат и
		// переключает второй показатель на ">=". У нас это случай, когда общий
		// разброс ниже измеримого -- тогда все 64 направления сходятся в одну
		// точку, и разница между показателями исчезает.
		const auto degenerate_cone = total_spread < 0.0001f;

		// Затухание по дистанции НЕ применяется здесь повторно.
		//
		// hit.damage -- это уже готовый урон скана: движок посчитал его вместе
		// с падением по дистанции и множителем хитгруппы ещё на этапе
		// сканирования точки. Если умножить его на затухание ещё раз, урон
		// окажется ослаблен дважды.
		//
		// Ошибка была бы незаметной при ручном просмотре, потому что множитель
		// ОДИН для всех 64 направлений и потому не меняет порядок точек между
		// собой. Но показатели сравниваются с порогами (>= target+0x18,
		// > target+0x14), а не только друг с другом: равномерно уменьшенный
		// урон переводит часть направлений из "проходит порог" в "не проходит",
		// и первый показатель падает у ВСЕХ точек сразу. Порог забирается
		// ровно у тех выстрелов, которые были на границе, -- то есть бот
		// начинает отказываться от стрельбы там, где должен стрелять.

		auto sum_damage{ 0.0f };
		auto count_min{ 0 };
		auto count_lethal{ 0 };
		auto count_body{ 0 };

		// Флаг "считать только летальные контакты телом".
		//
		// В движке это поле [[target+8]+0x50]: когда оно выставлено, третий
		// показатель не набирается вообще и остаётся нулём. Смысл -- не
		// поощрять точку, которая попадает в корпус, но не убивает: если
		// настройка требует летального исхода, "попаду в грудь" не является
		// полезной информацией и не должно влиять на выбор. У нас это
		// prefer_head вместе с требованием летальности.
		const auto body_requires_lethal = settings::g_combat.m_ragebot.general.prefer_head.value && config.min_damage_override.value;

		// Контекст урона берётся из САМОЙ точки: набор хитбоксов, броня, шлем
		// и команда цели уже посчитаны в scan_player (там prepare_target
		// вызывается один раз на кандидата). Повторный вызов здесь означал бы
		// полный query хитбоксов на каждую оцениваемую точку -- десятки
		// движковых чтений в цикле, который крутится до четырёх раз за тик.
		//
		// Урон направления считается по группе, в которую направление попало,
		// а множитель группы зависит от брони и шлема цели. Без этого
		// контекста get_max_damage вернул бы урон без учёта брони, и
		// "летальные направления" считались бы по завышенному урону.
		const auto& table = directions( );

		for ( const auto& dir : table.values )
		{
			// Направление внутри конуса. dir -- это пара (left, up) на
			// единичном диске; умножаем на полный разброс, получая угловое
			// отклонение в радианах -- ровно в той же параметризации, в
			// которой движок отклоняет пулю:
			//   forward + left * (dir.x * spread) + up * (dir.y * spread)
			// Таблица даёт 64 точки, равномерные по ДИСКУ, а не по углу,
			// поэтому доля прошедших направлений -- несмещённая оценка
			// вероятности, а не переоценка центра.
			//
			// Вырожденный конус (no_spread, разброс ~0) сходится в одну точку:
			// все 64 направления совпадают с центральным лучом, и показатели
			// становятся либо нулём, либо единицей -- что и делает ветку
			// degenerate_cone ниже корректной.
			const auto direction = forward + right * ( dir.x * total_spread ) + up * ( dir.y * total_spread );
			const auto angle = math::helpers::calculate_angle( shoot_eye, shoot_eye + direction );

			// Геометрия цели: направление обязано попасть в один из хитбоксов.
			// Промах даёт нулевой вклад во ВСЕ показатели, включая средний урон
			// -- именно поэтому средний урон ниже, чем урон "успешного"
			// направления, и его нельзя получить как damage * hit_ratio.
			//
			// В движке отсутствие пересечения возвращает -1 и выходит ДО
			// дальнейшей трассировки; здесь пересечение считается против самой
			// цели, поэтому проверка стоит первой и отсекает направление
			// целиком, а не вычитает его из одного счётчика.
			const auto direction_hitgroup = this->direction_hits_hitbox( shoot_eye, angle, hit, hit.hitboxes );

			if ( direction_hitgroup < 0 )
			{
				continue;
			}

			// Предварительный урон ИМЕННО ЭТОГО направления: он считается по
			// той группе, в которую направление попало, а не по группе точки
			// скана.
			//
			// Это ключ ко всей схеме. Урон по группе -- единственный источник
			// различий между направлениями: разброс вокруг головы даёт
			// часть направлений в голову (высокий урон, летально) и часть в
			// грудь (низкий, не летально). Если бы все направления делили
			// урон точки, показатели были бы 0 или 1, и компаратору было бы
			// нечего сравнивать -- кроме среднего урона, который в этом случае
			// равен урону точки, умноженному на долю попаданий.
			//
			// range_scale -- доля, которую сохранил выстрел (затухание по
			// дистанции плюс потеря на простреле). Считаем её как отношение
			// урона точки к максимальному урону её группы, а не заново: и
			// затухание, и потеря уже сидят внутри hit.damage, и повторный
			// расчёт разошёлся бы с движком.
			const auto range_scale = hit.point_max_damage > 0.0f
				? std::clamp( hit.damage / hit.point_max_damage, 0.0f, 1.0f )
				: 1.0f;

			auto direction_damage = g_shared.pen( ).get_max_damage( direction_hitgroup, hit.target_armor, hit.target_helmet, hit.target_team ) * range_scale;

			// Ножи и прочие оружия без множителя групп: get_max_damage вернёт
			// то же значение, что и для точки, а range_scale уйдёт в единицу.
			// Явный пол не нужен, но страховка от нуля -- нужна: нулевой урон
			// направил бы все три счётчика в ноль и сделал точку
			// неотличимой от промаха.
			if ( !( direction_damage > 0.0f ) )
			{
				direction_damage = hit.damage;
			}

			sum_damage += direction_damage;

			const auto passes_min = direction_damage >= min_damage_threshold;

			if ( passes_min )
			{
				++count_min;

				// Группы 2 (грудь) и 3 (живот) -- это попадание в корпус.
				// Показатель body отвечает на вопрос "попаду ли я в корпус",
				// поэтому он инкрементируется только внутри ветки минимального
				// урона: контакт, который не проходит порог, не является
				// полезным попаданием в туловище.
				//
				// При выставленном body_requires_lethal условие сужается до
				// летального: см. комментарий выше.
				if ( ( direction_hitgroup == 2 || direction_hitgroup == 3 )
					&& ( !body_requires_lethal || direction_damage > lethal_threshold ) )
				{
					++count_body;
				}
			}

			if ( degenerate_cone ? ( direction_damage >= lethal_threshold ) : ( direction_damage > lethal_threshold ) )
			{
				++count_lethal;
			}
		}

		// Знаменатель -- ВСЕГДА 64, а не число проверенных направлений: движок
		// может завершить проход рано и дооценить остаток, сохраняя знаменатель
		// полным. Если делить на фактическое число, раннее завершение
		// искусственно завысит показатели до единицы.
		out.hit_min_damage = static_cast< float >( count_min ) / point_metrics::k_directions;
		out.lethal = static_cast< float >( count_lethal ) / point_metrics::k_directions;
		out.body = static_cast< float >( count_body ) / point_metrics::k_directions;
		out.average_damage = sum_damage / point_metrics::k_directions;

		return out;
	}

	rage::point_metrics rage::second_eval( const scan_hit& hit, float min_damage_threshold, const aim_context& ctx, float inaccuracy ) const
	{
		point_metrics out{};

		auto& samples = last_samples( );
		samples = {};

		if ( !hit.record || !hit.record->valid )
		{
			return out;
		}

		const auto lethal_threshold = this->lethal_threshold_for( hit, min_damage_threshold );
		const auto total_spread = inaccuracy + ctx.spread;
		const auto shoot_eye = hit.source_eye.position;

		// Вырожденный конус: все 64 направления совпадают с центральным лучом.
		// Тогда результат считается по самой точке, без единой трассировки --
		// гонять 64 одинаковых луча было бы бессмысленно. Здесь второй
		// показатель сравнивается через ">=", как и в движке.
		if ( total_spread < 0.0001f )
		{
			out.hit_min_damage = 1.0f;
			out.lethal = hit.damage >= lethal_threshold ? 1.0f : 0.0f;
			out.body = ( hit.hitgroup == 2 || hit.hitgroup == 3 ) ? 1.0f : 0.0f;
			out.average_damage = hit.damage;

			samples.hit[ 0 ] = true;
			samples.damage[ 0 ] = hit.damage;
			samples.hit_count = 1;

			if ( out.body > 0.0f )
			{
				samples.body[ 0 ] = true;
				samples.body_count = 1;
			}

			return out;
		}

		math::vector3 forward{}, right{}, up{};
		math::helpers::angle_vectors_left( hit.aim_angle, &forward, &right, &up );

		const auto& table = directions( );
		const auto pen_ctx = g_shared.pen( ).prepare_target( hit.pawn, hit.record );
		const auto local = systems::g_local.get( );

		auto sum_damage{ 0.0f };
		auto count_min{ 0 };
		auto count_lethal{ 0 };

		for ( auto i = 0; i < 64; ++i )
		{
			const auto& dir = table.values[ static_cast< std::size_t >( i ) ];

			const auto direction = forward + right * ( dir.x * total_spread ) + up * ( dir.y * total_spread );

			// Направление, а не расстояние: pen.run сам масштабирует луч на
			// дальность оружия (trace_delta = direction * range), поэтому
			// конечная точка важна только своим направлением от глаза.
			shared::penetration::result pen{};
			if ( !g_shared.pen( ).run( shoot_eye, shoot_eye + direction, pen_ctx, local.pawn, local.team, pen ) )
			{
				continue;
			}

			samples.hit[ static_cast< std::size_t >( i ) ] = true;
			samples.damage[ static_cast< std::size_t >( i ) ] = pen.damage;
			++samples.hit_count;

			sum_damage += pen.damage;

			if ( pen.damage >= min_damage_threshold )
			{
				++count_min;

				if ( pen.hitgroup == 2 || pen.hitgroup == 3 )
				{
					samples.body[ static_cast< std::size_t >( i ) ] = true;
					++samples.body_count;
				}
			}

			// Второй показатель здесь сравнивается ">=", в отличие от
			// evaluate_point_metrics, где стоит строгое ">". Это не описка и
			// не косметика: ровно в порог урона считается летальным здесь и
			// не считается там, и смешивать две проверки нельзя.
			if ( pen.damage >= lethal_threshold )
			{
				++count_lethal;
			}
		}

		out.hit_min_damage = static_cast< float >( count_min ) / point_metrics::k_directions;
		out.lethal = static_cast< float >( count_lethal ) / point_metrics::k_directions;
		out.body = static_cast< float >( samples.body_count ) / point_metrics::k_directions;
		out.average_damage = sum_damage / point_metrics::k_directions;

		return out;
	}

	bool rage::refine_point( const scan_hit& hit, math::vector3& pos, float min_damage_threshold, const aim_context& ctx, float inaccuracy ) const
	{
		const auto& samples = last_samples( );

		if ( samples.hit_count <= 0 )
		{
			return true;
		}

		// Доля попаданий уже единица -- центр масс совпадает с текущей точкой,
		// сдвигать некуда. Движок в этом случае выходит сразу.
		if ( samples.hit_count >= 64 )
		{
			return true;
		}

		// Порог веса: lethal * (1 + 2.5 * max(lethal / min_damage - 1, 0)).
		//
		// Смысл -- не дать одному далёкому попаданию утянуть точку на себя.
		// Когда порог летальности близок к минимальному (цель убивается
		// штатным уроном), множитель равен единице и вес -- это просто урон.
		// Как только летальный порог заметно выше минимума (стреляем в корпус,
		// чтобы добить), вес обрезается, и точка остаётся там, где попаданий
		// больше, а не там, где случайно прошёл один сильный луч.
		const auto lethal_threshold = this->lethal_threshold_for( hit, min_damage_threshold );
		const auto ratio = min_damage_threshold > 0.0f ? lethal_threshold / min_damage_threshold : 1.0f;
		const auto cap = lethal_threshold * ( 1.0f + 2.5f * std::max( ratio - 1.0f, 0.0f ) );

		const auto shoot_eye = hit.source_eye.position;
		const auto total_spread = inaccuracy + ctx.spread;

		math::vector3 forward{}, right{}, up{};
		math::helpers::angle_vectors_left( hit.aim_angle, &forward, &right, &up );

		const auto& table = directions( );

		math::vector3 accum{};
		auto weight{ 0.0f };

		for ( auto i = 0; i < 64; ++i )
		{
			const auto slot = static_cast< std::size_t >( i );
			if ( !samples.hit[ slot ] )
			{
				continue;
			}

			const auto& dir = table.values[ slot ];
			const auto direction = forward + right * ( dir.x * total_spread ) + up * ( dir.y * total_spread );

			const auto w = std::min( samples.damage[ slot ], cap );
			accum = accum + direction * w;
			weight += w;
		}

		if ( !( weight > 0.0f ) )
		{
			return true;
		}

		const auto centroid = accum * ( 1.0f / weight );

		if ( centroid.length_sqr( ) < 1.0e-8f )
		{
			return true;
		}

		// Новая позиция -- на той же дистанции от глаза, что и старая, но по
		// направлению центра масс.
		//
		// В восстановлении стоит умножение на ctx->weapon_range, но это
		// расстояние до цели, а не дальность оружия: accum/weight -- уже
		// единичный вектор, и умножать его на 8192 означало бы увести точку за
		// карту. Физически осмысленно сохранить дистанцию, поэтому берём её из
		// текущей точки.
		const auto distance = ( pos - shoot_eye ).length( );
		if ( distance < 1.0f )
		{
			return true;
		}

		pos = shoot_eye + centroid.normalized( ) * distance;
		return false;
	}

	float rage::lethal_threshold_for( const scan_hit& hit, float min_damage ) const
	{
		// Порог летальности -- это отдельное поле (target+0x14), и его нельзя
		// выводить из min_damage.
		//
		// min_damage отвечает на "какой урон считается приемлемым" и может
		// быть опущен конфигом (min_damage_override) ниже здоровья цели --
		// это осознанный компромисс "стреляю в корпус, лишь бы попасть".
		// Летальность отвечает на другой вопрос: "этот выстрел УБЬЁТ". Если
		// подставить сюда заниженный конфигом min_damage, показатель lethal
		// начнёт считать летальными направления, которые цель не убивают, и
		// компаратор предпочтёт их -- при том что смысл показателя ровно
		// обратный.
		//
		// Поэтому порог -- здоровье цели: ровно столько урона закрывает её с
		// одного выстрела. max с min_damage нужен на случай здоровья <= 0
		// (мёртвая или испорченная цель): без него порог ушёл бы в ноль и
		// объявил летальным любой урон.
		return std::max( min_damage, static_cast< float >( hit.health ) );
	}

	int rage::direction_hits_hitbox( const math::vector3& eye, const math::vector3& angle, const scan_hit& hit, const systems::hitboxes::set& hitboxes ) const
	{
		if ( !hit.record || !hit.record->valid )
		{
			return -1;
		}

		math::vector3 forward{};
		math::helpers::angle_vectors_left( angle, &forward );

		// Луч достаёт за цель: точка прицела лежит на поверхности капсулы, и
		// луч должен пройти её насквозь, а не остановиться внутри.
		const auto reach = ( hit.position - eye ).length( ) + 64.0f;
		const auto ray_end = eye + forward * reach;
		const auto segment_b = ray_end - eye;
		const auto e = segment_b.dot( segment_b );

		// Луч нулевой длины -- направление выродилось.
		if ( e <= 1e-8f )
		{
			return -1;
		}

		// Направление проверяется против ВСЕХ хитбоксов цели, а не только
		// против того, который выбрал скан.
		//
		// Это и есть источник, из которого показатели берут свою
		// информативность. Если считать урон по одному хитбоксу, он одинаков
		// для всех 64 направлений, и все четыре показателя вырождаются в
		// 0 или 1 ("прошло / не прошло") -- различить точки становится нечем,
		// кроме среднего урона. Движок же оценивает каждое направление по
		// той группе, в которую именно оно попадает: одно направление летит
		// в голову, соседнее -- в грудь, ещё одно уходит мимо. Поэтому урон
		// по направлениям РАЗНЫЙ, и "доля летальных" -- настоящая вероятность,
		// а не повтор пороговой проверки.
		//
		// Берём лучшую (наибольшую) группу: направление, задевшее и голову,
		// и плечо, наносит урон по голове -- наложение хитбоксов не должно
		// занижать оценку.
		auto best_hitgroup = -1;
		auto best_rank = -1;

		for ( const auto& box : hitboxes )
		{
			// Границы по ДВУМ разным пределам: box.bone индексирует массив
			// костей записи (k_max_skeleton_bones), а не список хитбоксов.
			// Проверять его нужно именно против размера массива костей --
			// иначе значение из ремап-таблицы может выйти за границы bones.
			if ( box.bone < 0 || box.bone >= k_max_skeleton_bones )
			{
				continue;
			}

			const auto& bone = hit.record->bones[ box.bone ];
			const auto capsule_start = bone.rotation.rotate_vector( box.mins ) + bone.position;
			const auto capsule_end = bone.rotation.rotate_vector( box.maxs ) + bone.position;

			const auto radius = std::max( box.radius, 0.0f );
			const auto segment_a = capsule_end - capsule_start;
			const auto r = eye - capsule_start;
			const auto a = segment_a.dot( segment_a );
			const auto f = segment_b.dot( r );

			const auto c = segment_a.dot( r );
			const auto b = segment_a.dot( segment_b );
			const auto denom = a * e - b * b;

			// Параллельные отрезки: ближайшая точка не определена, берём начало
			// луча. Без этой ветки деление на ноль даёт NaN, он проходит все
			// проверки сравнением (NaN < x ложно), и направление ошибочно
			// засчитается попавшим.
			auto s = 0.0f;
			if ( std::fabsf( denom ) > 1e-8f )
			{
				s = std::clamp( ( b * f - c * e ) / denom, 0.0f, 1.0f );
			}

			auto t = ( b * s + f ) / e;
			t = std::clamp( t, 0.0f, 1.0f );

			// t == 0 -- пересечение за спиной: точка прицела позади глаза.
			// Формально направление лежит на прямой, но попадания нет.
			if ( t <= 0.0f )
			{
				continue;
			}

			const auto closest_capsule = capsule_start + segment_a * s;
			const auto closest_ray = eye + segment_b * t;
			const auto delta = closest_ray - closest_capsule;

			if ( delta.dot( delta ) > radius * radius )
			{
				continue;
			}

			const auto hitgroup = systems::g_hitboxes.hitgroup_from_hitbox( box.index );

			// Ранг группы: голова важнее живота, живот важнее груди, грудь
			// важнее конечностей. Совпадает со множителями scale_damage --
			// так "лучшая группа" и "больший урон" не расходятся.
			auto rank = 0;
			switch ( hitgroup )
			{
			case 1: rank = 4; break; // голова
			case 3: rank = 3; break; // живот
			case 2: rank = 2; break; // грудь
			case 4: case 5: case 8: rank = 1; break; // конечности
			case 6: case 7: rank = 0; break; // ноги
			default: rank = -1; break;
			}

			if ( rank > best_rank )
			{
				best_rank = rank;
				best_hitgroup = hitgroup;
			}
		}

		return best_hitgroup;
	}

	bool rage::is_better_point( const target& a, const target& b ) const
	{
		// Последовательные сравнения с ранними возвратами. Это существо
		// движкового компаратора (0x51BEA0): он НЕ считает взвешенную сумму и
		// не нормализует показатели. Каждое сравнение -- вопрос "да/нет", и
		// первое же "да" решает исход.
		//
		// Отдельно стоит сказать про ДОПУСКИ. Почти каждое сравнение ниже --
		// не строгое неравенство, а approx с порогом. Это не косметика: две
		// точки, различающиеся на 1e-7, при строгом сравнении разворачивали бы
		// выбор, и бот дёргался бы между ними каждый тик. Допуск делает выбор
		// устойчивым, а разрешение равенства отдаётся следующему приоритету.

		// Сравнение 1. Точка, прошедшая оба порога одновременно, побеждает
		// точку, которая не прошла. Если обе прошли -- идём дальше. Если ни
		// одна -- тоже идём: ready это приоритет, а не отсев.
		if ( a.ready != b.ready )
		{
			return a.ready;
		}

		// Сравнение 2. Предпочтение туловища.
		//
		// В движке включается тройкой ctx+0x51 (prefer_body) / ctx+0xA8 /
		// ctx+0xBC == 1, и -- что важнее -- только когда ОБЕ точки прошли
		// пороги. Смысл гейта: пока ни одна точка не готова, вопрос стоит
		// "куда я вообще попаду", и подсовывать туда предпочтение корпуса
		// нельзя -- оно бы увело выбор от головы ещё до того, как стало
		// понятно, что выстрел вообще возможен.
		//
		// "Туловищная" точка в движке -- та, у которой запись лага не помечена
		// промахом по конечностям (record+0x0C == 0). У нас тот же смысл несёт
		// хитгруппа: 2 (грудь) и 3 (живот) -- это корпус.
		if ( a.ready && b.ready && a.body_aim && b.body_aim )
		{
			const auto body_a = a.hit.hitgroup == 2 || a.hit.hitgroup == 3;
			const auto body_b = b.hit.hitgroup == 2 || b.hit.hitgroup == 3;

			if ( body_a != body_b )
			{
				return body_a;
			}
		}

		// Сравнение 3. Средний урон по конусу. Допуск 0.05.
		//
		// Стоит ВЫШЕ вероятности -- и это не описка. Средний урон отвечает
		// "сколько я в среднем выбью этим выстрелом", и точка головы с
		// высокой вероятностью, но низким средним проигрывает точке корпуса,
		// которая бьёт стабильно. Порядок снят с движка.
		if ( !approx_equal( a.metrics.average_damage, b.metrics.average_damage, 0.05f ) )
		{
			return a.metrics.average_damage > b.metrics.average_damage;
		}

		// Сравнение 4. Вероятность достичь минимального урона. Допуск 0.15 --
		// самый широкий из всех: показатель дискретный (шаг 1/64 = 0.0156), и
		// узкий допуск заставлял бы бота различать точки, отличающиеся на одно
		// направление из шестидесяти четырёх.
		if ( !approx_equal( a.metrics.hit_min_damage, b.metrics.hit_min_damage, 0.15f ) )
		{
			return a.metrics.hit_min_damage > b.metrics.hit_min_damage;
		}

		// Сравнение 5. При выключенном разбросе (no_spread) конус сжимается в
		// точку, и "куда я попаду" перестаёт быть вопросом вероятности: пуля
		// уйдёт ровно по направлению выстрела. Тогда решает близость точки к
		// текущему взгляду -- меньше доворота, меньше заметно снаружи.
		//
		// В движке это ветка ctx->mode == 2; у нас её роль играет no_spread,
		// потому что именно он делает конус вырожденным.
		if ( settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type ).no_spread.value )
		{
			if ( !approx_equal( a.hit.fov, b.hit.fov, 1.0e-3f ) )
			{
				return a.hit.fov < b.hit.fov;
			}
		}

		// Сравнение 6. Число выстрелов до убийства: round(lethal / damage),
		// зажатое в [1..6]. Меньше -- лучше.
		//
		// Считается только когда обе вероятности выше 0.6 и пороги урона
		// совпали. При низкой вероятности "за сколько выстрелов" -- фантазия:
		// попадания может не случиться вообще, и планировать серию бессмысленно.
		if ( a.metrics.hit_min_damage > 0.6f && b.metrics.hit_min_damage > 0.6f
			&& approx_equal( a.min_damage, b.min_damage, 0.0f )
			&& a.hit.damage > 0.0f && b.hit.damage > 0.0f )
		{
			const auto shots = [ ]( float lethal, float damage ) -> int
				{
					const auto n = static_cast< int >( std::lround( lethal / damage ) );
					return std::clamp( n, 1, 6 );
				};

			const auto shots_a = shots( a.lethal_damage, a.hit.damage );
			const auto shots_b = shots( b.lethal_damage, b.hit.damage );

			if ( shots_a != shots_b )
			{
				return shots_a < shots_b;
			}
		}

		// Сравнение 7. Вероятность летального урона. Допуск 0.10, и сравнение
		// вообще не проводится, пока хотя бы одна из вероятностей не выше 0.4:
		// при 0.05 против 0.02 разница не значит ничего, а порядок точек
		// поменяла бы.
		if ( !approx_equal( a.metrics.lethal, b.metrics.lethal, 0.10f )
			&& ( a.metrics.lethal > 0.4f || b.metrics.lethal > 0.4f ) )
		{
			return a.metrics.lethal > b.metrics.lethal;
		}

		// Сравнение 8. Средний урон повторной оценки. Допуск 2.0 -- самый
		// широкий: это уже не оценка по конусу, а результат полного расчёта
		// на 64 направления, и он шумнее.
		if ( !approx_equal( a.re.average_damage, b.re.average_damage, 2.0f ) )
		{
			return a.re.average_damage > b.re.average_damage;
		}

		// Сравнение 9. Свежесть записи лага. Больше тик -- свежее, а свежая
		// запись точнее отражает положение цели.
		const auto tick_a = a.hit.record ? a.hit.record->tick : 0;
		const auto tick_b = b.hit.record ? b.hit.record->tick : 0;

		return tick_a > tick_b;
	}

	float rage::evaluate_hitchance_with_pen( const scan_hit& hit, const aim_context& ctx, float inaccuracy, const systems::local::snapshot& local ) const
	{
		const auto base_hc = this->evaluate_hitchance( hit, ctx, inaccuracy );

		if ( !hit.penetrated )
		{
			return base_hc; // Direct shots are not weakened by penetration.
		}

		// Пользователь выключил проверку прострела -- верим геометрии центрального
		// луча. Это дешевле (никаких pen-трасс на кандидата) и стреляет чаще,
		// но прострелы через толстую геометрию будут проходить как обычные.
		const auto& pen_config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );
		if ( !pen_config.wall_check.value )
		{
			return base_hc;
		}

		// For wallbang targets, confirm that the spread cone also penetrates, not
		// just the exact center ray. Sample points inside the hitchance cone and
		// keep the fraction that still deals damage through the obstacle.
		//
		// The ring is trig on purpose: seeding these from calculate_spread is a
		// truer picture of the cone but costs an engine call per sample, on top
		// of the pen trace each one already runs, and this is called per
		// penetrated candidate across the whole scan. The centre sample is the
		// part worth keeping -- it is the single most likely bullet path, and a
		// ring alone never tested it.
		const auto ring_samples = std::clamp( pen_config.cone_samples.value, 0, 32 );
		if ( ring_samples <= 0 )
		{
			return base_hc;
		}

		const auto sample_count = ring_samples + 1;
		auto passing = 0;

		const auto pen_ctx = g_shared.pen( ).prepare_target( hit.pawn, hit.record );

		const auto distance = ( hit.position - hit.source_eye.position ).length( );

		// Тот же дефект, что чинился в validate_shot_cone: при узком конусе
		// (а no_spread сводит его почти в ноль) радиус кольца схлопывался, все
		// семплы шли в одну точку, и pen_factor мог быть только 1 или 0.
		// Нижняя граница -- полрадиуса хитбокса: кольцо должно охватывать
		// силуэт, иначе "доля прошедших" ничего не измеряет.
		const auto raw_spread_radius = std::tanf( inaccuracy + ctx.spread ) * distance;
		const auto spread_radius = std::max( raw_spread_radius, std::max( hit.hitbox.radius * 0.5f, 2.0f ) );

		// Порог урона для прострела -- из настроек: точка, которая "проходит" с
		// уроном 1, значит, что пуля задела цель на излёте, и считать её
		// успехом значит объявить прострел осмысленным там, где он бесполезен.
		const auto min_damage = std::max( 1.0f, static_cast< float >( pen_config.wall_penetration_min.value ) );

		math::vector3 right{}, up{};
		math::helpers::angle_vectors_left( hit.aim_angle, nullptr, &right, &up );

		for ( auto i = 0; i < sample_count; ++i )
		{
			auto offset_x = 0.0f;
			auto offset_y = 0.0f;

			if ( i < ring_samples )
			{
				const auto angle = static_cast< float >( i ) / static_cast< float >( ring_samples ) * 6.2831853f;
				offset_x = std::cosf( angle ) * spread_radius * 0.5f;
				offset_y = std::sinf( angle ) * spread_radius * 0.5f;
			}

			const auto sample_target = hit.position + right * offset_x + up * offset_y;

			shared::penetration::result pen{};
			if ( g_shared.pen( ).run( hit.source_eye.position, sample_target, pen_ctx, local.pawn, local.team, pen ) )
			{
				if ( pen.damage >= min_damage )
				{
					++passing;
				}
			}
		}

		const auto pen_factor = static_cast< float >( passing ) / static_cast< float >( sample_count );
		return base_hc * pen_factor;
	}

	float rage::validate_shot_cone( const target& tgt, const aim_context& ctx, float inaccuracy, const systems::local::snapshot& local ) const
	{
		if ( !tgt.valid || !tgt.hit.record )
		{
			return 1.0f;
		}

		// Penetrated candidates already had their cone sampled against the world
		// in evaluate_hitchance_with_pen, and their hitchance carries that factor
		// already -- applying it twice would square it.
		if ( tgt.hit.penetrated )
		{
			return 1.0f;
		}

		const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );

		const auto ring_samples = std::clamp( config.cone_samples.value, 0, 32 );
		if ( ring_samples <= 0 )
		{
			return 1.0f;
		}

		const auto sample_count = ring_samples + 1;

		const auto pen_ctx = g_shared.pen( ).prepare_target( tgt.hit.pawn, tgt.hit.record );

		const auto distance = ( tgt.hit.position - tgt.hit.source_eye.position ).length( );

		math::vector3 right{}, up{};
		math::helpers::angle_vectors_left( tgt.hit.aim_angle, nullptr, &right, &up );

		// Радиус, в котором разбрасываются семплы. Без нижней границы при
		// no_spread (там inaccuracy и spread близки к нулю) он схлопывался в
		// ноль: все 9 семплов били в одну точку, и cone_factor принимал
		// только два значения -- 1.0 или 0.0. Никакой "доли конуса" такая
		// проверка не измеряла, зато исправно отменяла выстрел, когда эта
		// единственная точка оказывалась непробиваемой кромкой капсулы.
		//
		// Полрадиуса хитбокса (минимум 2 юнита) -- нижняя граница, при которой
		// кольцо реально охватывает силуэт и порог в процентах обретает
		// смысл. Считается от tgt.hit.hitbox.radius, а не от головы: радиус
		// приходит вместе с хитбоксом и верен для любого из них.
		const auto raw_spread_radius = std::tanf( inaccuracy + ctx.spread ) * distance;
		const auto spread_radius = std::max( raw_spread_radius, std::max( tgt.hit.hitbox.radius * 0.5f, 2.0f ) );

		// Порог урона для пройденной точки -- из настроек, а не 1.0f. Точка,
		// которая "проходит" с уроном 1, -- это точка, где пуля задевает цель
		// на излёте и не значит ничего.
		const auto min_damage = std::max( 1.0f, static_cast< float >( config.wall_penetration_min.value ) );

		auto passing = 0;

		for ( auto i = 0; i < sample_count; ++i )
		{
			auto offset_x = 0.0f;
			auto offset_y = 0.0f;

			// The last sample is the centre -- the single most likely bullet path,
			// and the one a ring alone never tests.
			if ( i < ring_samples )
			{
				const auto angle = static_cast< float >( i ) / static_cast< float >( ring_samples ) * 6.2831853f;
				offset_x = std::cosf( angle ) * spread_radius * 0.5f;
				offset_y = std::sinf( angle ) * spread_radius * 0.5f;
			}

			const auto sample_target = tgt.hit.position + right * offset_x + up * offset_y;

			shared::penetration::result pen{};
			if ( g_shared.pen( ).run( tgt.hit.source_eye.position, sample_target, pen_ctx, local.pawn, local.team, pen )
				&& pen.damage >= min_damage )
			{
				++passing;
			}
		}

		return static_cast< float >( passing ) / static_cast< float >( sample_count );
	}

	// prefer_safe_point.
	//
	// Проверяем не "пройдёт ли пуля", а "пройдёт ли коробка игрока". Разница
	// принципиальная: пуля стартует из глаз и имеет нулевой радиус, поэтому
	// точка на хитбоксе может быть полностью прострелена, пока ствол/тело
	// стоят вплотную к стене или высунуты в окно под углом, где сервер
	// отклонит выстрел коллизией игрока. Такие выстрелы пропадают молча.
	//
	// Трассируем hull от глаз к точке, потом от точки до самой цели и берём
	// первую позицию, где оба прохода дают полную длину без контакта.
	// Кандидаты -- центр плюс лёгкое смещение в стороны, чтобы не потерять
	// цель на углу, когда центр перекрыт, а половина хитбокса -- нет.
	bool rage::find_safe_point( scan_hit& hit, const math::vector3& shoot_eye, float inaccuracy, const aim_context& ctx, const systems::local::snapshot& local ) const
	{
		constexpr auto k_hull_mins = math::vector3{ -9.0f, -9.0f, 0.0f };
		constexpr auto k_hull_maxs = math::vector3{ 9.0f, 9.0f, 72.0f };
		constexpr auto k_open_fraction = 0.985f;

		if ( !hit.record )
		{
			return false;
		}

		const auto distance = ( hit.position - shoot_eye ).length( );
		if ( distance < 1.0f )
		{
			return false;
		}

		// Направление и боковые векторы считаем от текущей точки: смещения
		// должны идти по поверхности хитбокса, а не по экрану.
		const auto dir = ( hit.position - shoot_eye ).normalized( );
		math::vector3 right{}, up{};
		math::helpers::angle_vectors_left( math::helpers::calculate_angle( shoot_eye, hit.position ), nullptr, &right, &up );

		// Опорные оси берём из хитбокса, а не из констант: у головы и у
		// корпуса разный масштаб, и смещение в 6 юнитов для головы -- это
		// уже промах, а для груди -- ничего.
		const auto extent_x = std::clamp( hit.hitbox.maxs.x - hit.hitbox.mins.x, 4.0f, 36.0f ) * 0.5f;
		const auto extent_y = std::clamp( hit.hitbox.maxs.y - hit.hitbox.mins.y, 4.0f, 36.0f ) * 0.5f;
		const auto extent_z = std::clamp( hit.hitbox.maxs.z - hit.hitbox.mins.z, 4.0f, 36.0f ) * 0.5f;

		const auto pen_ctx = g_shared.pen( ).prepare_target( hit.pawn, hit.record );

		// Spread-конус учитываем тем же радиусом, что и validate_shot_cone:
		// "безопасная" точка обязана оставаться безопасной и при разбросе.
		//
		// Нижняя граница обязательна по той же причине, что и там: при
		// no_spread слагаемое tan(...) обнуляется, и член spread_radius * 0.5f
		// в смещениях ниже перестаёт что-либо добавлять -- точка считается
		// "безопасной" по чистому лучу, без запаса на конус. Границей берём
		// полрадиуса хитбокса: ровно то, насколько точка может уехать по
		// капсуле при реальном выстреле.
		const auto raw_spread_radius = std::tanf( inaccuracy + ctx.spread ) * distance;
		const auto spread_radius = std::max( raw_spread_radius, std::max( hit.hitbox.radius * 0.5f, 2.0f ) );

		struct offset
		{
			float right;
			float up;
		};

		// Центр первым -- это самый частый и самый дешёвый случай.
		constexpr offset offsets[]{
			{  0.00f,  0.00f },
			{  0.00f,  0.50f },
			{  0.00f, -0.50f },
			{  0.55f,  0.00f },
			{ -0.55f,  0.00f },
			{  0.40f,  0.40f },
			{ -0.40f,  0.40f },
			{  0.40f, -0.40f },
			{ -0.40f, -0.40f },
		};

		const auto filter = systems::g_tracing.make_filter_cached( local.pawn, 0x1c3003, 4 );

		for ( const auto& o : offsets )
		{
			const auto point = hit.position
				+ right * ( o.right * extent_x + ( o.right > 0.0f ? spread_radius * 0.5f : -spread_radius * 0.5f ) )
				+ up * ( o.up * extent_z );

			// 1. Ствол: коробка игрока должна пройти от глаз к точке целиком.
			const auto hull = systems::g_tracing.trace_hull_fast( shoot_eye, point, k_hull_mins, k_hull_maxs, filter );
			if ( hull.fraction < k_open_fraction )
			{
				continue;
			}

			// 2. Прицел: коробка должна стоять на точке, а не упираться в стену
			//    за целью. Проверяем коридор "точка -> цель + запас", чтобы
			//    отсечь позиции вплотную к углу.
			const auto behind = point + dir * 8.0f;
			const auto corridor = systems::g_tracing.trace_hull_fast( point, behind, k_hull_mins, k_hull_maxs, filter );
			if ( corridor.fraction < k_open_fraction )
			{
				continue;
			}

			// 3. Сам выстрел всё ещё должен наносить урон -- безопасная точка
			//    не имеет смысла, если через неё пуля уходит в ноль.
			shared::penetration::result pen{};
			if ( !g_shared.pen( ).run( shoot_eye, point, pen_ctx, local.pawn, local.team, pen ) || pen.damage < 1.0f )
			{
				continue;
			}

			hit.position = point;
			hit.aim_angle = math::helpers::calculate_angle( shoot_eye, point );
			hit.penetrated = pen.penetrated;
			hit.damage = std::min( hit.damage, pen.damage );
			// Точка гарантированно открыта -- центр-флаг снимаем, иначе
			// визуализация показывает её как "центр хитбокса".
			hit.is_center = false;
			return true;
		}

		return false;
	}

	float rage::get_standing_inaccuracy( const systems::local::snapshot& local, const aim_context& ctx ) const
	{
		const auto& prestate = systems::g_prediction.pre( );
		auto velocity = prestate.networked_velocity;
		velocity.z = 0.0f;

		const auto speed = velocity.length_2d( );
		if ( speed > ctx.accurate_threshold )
		{
			return g_shared.get_inaccuracy_at_velocity( local.pawn, velocity );
		}

		const auto& shared_ctx = g_shared.ctx( );
		if ( !shared_ctx.weapon_vdata )
		{
			return ctx.predicted_inaccuracy;
		}

		const auto inaccuracy_stand = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyStand"_hash ) );
		return std::max( inaccuracy_stand, g_shared.get_inaccuracy_at_velocity( local.pawn, velocity ) );
	}

	std::vector<rage::scan_hit> rage::scan_taser( const math::vector3& eye, const aim_context& ctx, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		std::vector<scan_hit> results;

		for ( auto& cand : candidates )
		{
			for ( auto ri = 0; ri < cand.record_count; ++ri )
			{
				auto* record = cand.records[ ri ];
				if ( !record || !record->valid )
				{
					continue;
				}

				const auto game_scene_node = memory::read<std::uintptr_t>( cand.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
				if ( !game_scene_node )
				{
					continue;
				}

				const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );
				if ( hitbox_set.count <= 0 )
				{
					continue;
				}

				record->apply( );
				const auto skeleton = g_shared.lc( ).get_skeleton( *record );

				for ( auto i = 0; i < hitbox_set.count; ++i )
				{
					const auto& hb = hitbox_set.entries[ i ];

					if ( hb.bone < 0 || hb.bone >= k_max_hitbox_bones )
					{
						continue;
					}

					const auto& bone = skeleton[ hb.bone ];
					if ( bone.position.length_sqr( ) < 1.0f )
					{
						continue;
					}

					const auto center = bone.rotation.rotate_vector( ( hb.mins + hb.maxs ) * 0.5f ) + bone.position;
					const auto aim = math::helpers::calculate_angle( eye, center );
					const auto fov = math::helpers::angle_distance( ctx.view_angles, aim );

					if ( fov > settings::g_combat.m_zeusbot.max_fov_effective( ) )
					{
						continue;
					}

					math::vector3 forward{};
					math::helpers::angle_vectors_left( aim, &forward );

					const auto trace = this->trace_taser_hit( eye, forward, shared_ctx.range * 0.85f, cand.pawn, local.pawn );
					if ( trace.hit_entity != cand.pawn )
					{
						continue;
					}

					const auto dist = ( center - eye ).length( );
					const auto range_fraction = dist / shared_ctx.range;

					scan_hit h{};
					h.position = center;
					h.aim_angle = aim;
					h.damage = 500.0f;
					h.score = ( 10000.0f - dist ) * ( range_fraction > 0.92f ? 0.8f : 1.0f );
					h.fov = fov;
					h.hitbox_index = hb.index;
					h.hitgroup = systems::g_hitboxes.hitgroup_from_hitbox( hb.index );
					h.bone_index = hb.bone;
					h.hitbox = hb;
					h.is_center = true;
					h.pawn = cand.pawn;
					h.health = cand.health;
					h.record = record;

					results.push_back( h );
				}

				record->restore( );
			}
		}

		return results;
	}

	rage::knife_info rage::get_knife_info( const systems::local::snapshot& local ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );
		const auto next_primary = memory::read<int>( shared_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash ) );
		const auto next_secondary = memory::read<int>( shared_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextSecondaryAttackTick"_hash ) );
		const auto last_shot_time = memory::read<float>( shared_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_fLastShotTime"_hash ) );
		const auto cur_time = static_cast< float >( tick_base ) * cstypes::tick_interval;

		return knife_info
		{
			.can_slash = tick_base >= next_primary,
			.can_stab = tick_base >= next_secondary,
			.charged = ( cur_time - last_shot_time ) > 0.4f,
			.armor_ratio = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flArmorRatio"_hash ) )
		};
	}

	std::vector<rage::scan_hit> rage::scan_knife( const math::vector3& eye, const aim_context& ctx, const knife_info& info, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const
	{
		constexpr auto stab_range{ 50.0f };
		constexpr auto slash_range{ 66.0f };

		std::vector<scan_hit> results;

		for ( auto& cand : candidates )
		{
			const auto eye_angles = memory::read<math::vector3>( cand.pawn + SCHEMA( "C_CSPlayerPawn", "m_angEyeAngles"_hash ) );
			const auto hp = static_cast< float >( cand.health );

			const auto frontal_slash_dmg = this->get_knife_damage( info.charged ? 40.0f : 25.0f, cand.armor, info.armor_ratio );
			const auto frontal_stab_dmg = this->get_knife_damage( 65.0f, cand.armor, info.armor_ratio );
			const auto frontal_can_kill = ( info.can_slash && frontal_slash_dmg >= hp ) || ( info.can_stab && frontal_stab_dmg >= hp );

			for ( auto ri = 0; ri < cand.record_count; ++ri )
			{
				auto* record = cand.records[ ri ];
				if ( !record || !record->valid )
				{
					continue;
				}

				const auto game_scene_node = memory::read<std::uintptr_t>( cand.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
				if ( !game_scene_node )
				{
					continue;
				}

				const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );
				if ( hitbox_set.count <= 0 )
				{
					continue;
				}

				record->apply( );
				const auto skeleton = g_shared.lc( ).get_skeleton( *record );

				auto backstab{ false };
				{
					const auto delta = record->origin - systems::g_prediction.pre( ).origin;
					const auto dist_2d = std::sqrtf( delta.x * delta.x + delta.y * delta.y );

					if ( dist_2d > 0.001f )
					{
						const auto dir_x = delta.x / dist_2d;
						const auto dir_y = delta.y / dist_2d;

						math::vector3 body_forward{};
						math::helpers::angle_vectors_left( record->rotation, &body_forward );

						math::vector3 eye_forward{};
						math::helpers::angle_vectors_left( eye_angles, &eye_forward );

						backstab = ( dir_x * body_forward.x + dir_y * body_forward.y ) > 0.475f ||
							( dir_x * eye_forward.x + dir_y * eye_forward.y ) > 0.475f;
					}
				}

				const auto wait_for_backstab = backstab && !frontal_can_kill;

				for ( auto i = 0; i < hitbox_set.count; ++i )
				{
					const auto& hb = hitbox_set.entries[ i ];

					if ( hb.bone < 0 || hb.bone >= k_max_hitbox_bones )
					{
						continue;
					}

					const auto& bone = skeleton[ hb.bone ];
					if ( bone.position.length_sqr( ) < 1.0f )
					{
						continue;
					}

					const auto center = bone.rotation.rotate_vector( ( hb.mins + hb.maxs ) * 0.5f ) + bone.position;
					const auto dist = ( center - eye ).length( );
					const auto max_reach = info.can_slash ? slash_range : stab_range;

					if ( dist > max_reach )
					{
						continue;
					}

					const auto aim = math::helpers::calculate_angle( eye, center );
					const auto fov = math::helpers::angle_distance( ctx.view_angles, aim );

					if ( fov > settings::g_combat.m_knifebot.max_fov_effective( ) )
					{
						continue;
					}

					math::vector3 forward{};
					math::helpers::angle_vectors_left( aim, &forward );

					for ( const auto try_stab : { true, false } )
					{
						if ( try_stab && !info.can_stab )
						{
							continue;
						}

						if ( !try_stab && !info.can_slash )
						{
							continue;
						}

						const auto reach = try_stab ? stab_range : slash_range;
						if ( dist > reach )
						{
							continue;
						}

						const auto raw_dmg = try_stab ? ( backstab ? 180.0f : 65.0f ) : ( backstab ? 90.0f : ( info.charged ? 40.0f : 25.0f ) );
						const auto damage = this->get_knife_damage( raw_dmg, cand.armor, info.armor_ratio );
						const auto can_kill = damage >= hp;

						if ( wait_for_backstab && !can_kill )
						{
							continue;
						}

						const auto trace = this->trace_knife_hit( eye, forward, reach, cand.pawn, local.pawn );
						if ( trace.hit_entity != cand.pawn )
						{
							continue;
						}

						const auto reach_margin = 1.0f - ( dist / reach );

						scan_hit h{};
						h.position = center;
						h.aim_angle = aim;
						h.damage = damage;
						h.score = can_kill ? ( 10000.0f + damage * reach_margin ) : ( damage * 100.0f * reach_margin );
						h.fov = fov;
						h.hitbox_index = hb.index;
						h.hitgroup = systems::g_hitboxes.hitgroup_from_hitbox( hb.index );
						h.bone_index = hb.bone;
						h.hitbox = hb;
						h.is_center = true;
						h.is_backstab = backstab;
						h.attack_type = try_stab ? 1 : 0;
						h.pawn = cand.pawn;
						h.health = cand.health;
						h.record = record;

						results.push_back( h );
						break;
					}
				}

				record->restore( );
			}
		}

		return results;
	}

void rage::fire_gun( systems::input::usercmd* cmd, const target& tgt, bool was_forced, const math::vector3& shoot_eye, const systems::local::snapshot& local, bool subtick_attack )
{
	if ( !tgt.hit.record || !tgt.hit.record->valid )
	{
		return;
	}

	// The shot is committed here -- the aim is solved for a specific bullet, the
	// history entry is about to be stamped with those angles, and the attack bit
	// goes out. Tell the movement layer, so a strafe does not turn the walk out
	// from under a shot that is already on its way to the server.
	//
	// This used to be set at the top of the function and cleared right below the
	// record check, i.e. cleared exactly when the shot became real. The strafers
	// therefore never saw it: every tick was a strafe tick, including the tick
	// the bullet left. That is a large part of why a moving shot missed.
	this->m_firing_this_tick = true;

	const auto base = cmd->csgo_user_cmd.mutable_base( );
	const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );

	// Cache the target we committed to so next tick's scan can prioritize it.
	this->m_last_target_pawn = tgt.hit.pawn;
	this->m_last_target_hitbox = tgt.hit.hitbox_index;

	// История выстрелов для force body aim: сколько раз подряд шли в голову
	// и был ли предыдущий выстрел летальным. Считается здесь, потому что это
	// единственное место, где известно, куда именно ушла пуля.
	this->m_last_was_lethal = tgt.is_lethal( );
	if ( tgt.hit.hitgroup == 1 )
	{
		++this->m_head_shot_streak;
	}
	else
	{
		this->m_head_shot_streak = 0;
	}

	const auto& shared_ctx = g_shared.ctx( );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
		const auto aim_punch = g_shared.get_aim_punch( local.pawn );
		auto aim_angle = config.no_spread.value ? math::helpers::calculate_angle( shoot_eye, tgt.hit.position ) : tgt.hit.aim_angle;
		const auto uncorrected_aim = aim_angle;

		if ( config.no_spread.value )
		{
			// The seed has to be solved for the same tick the command is stamped
			// to, which is the interpolated backtrack tick whenever the shot is
			// aimed at a rewound record -- not our own client tick. The input
			// history entries below carry that stamp, and the server derives the
			// seed from them.
			auto stamp_tick = tick_base;
			auto stamp_frac{ 0.0f };

			if ( !tgt.hit.source_eye.is_uninterpolated )
			{
				std::tie( stamp_tick, stamp_frac ) = detail::tick_add( tgt.hit.source_eye.player_tick, tgt.hit.source_eye.player_frac, tgt.hit.source_eye.lerp_ticks_int, tgt.hit.source_eye.lerp_ticks_frac );
			}

			// Solve for the angle that goes on the wire, not for the one we aim at.
			//
			// The seed is a function of the view angles the command carries, and
			// what the command carries is aim_angle minus the accumulated punch --
			// the subtraction happens further down, and it used to happen after the
			// correction had already been verified against the un-subtracted angle.
			// While the punch is near zero the two are the same angle and the shot
			// lands; once it has built up they are different angles with different
			// seeds, and the compensation cancels a spread the weapon was never
			// going to use.
			//
			// That is exactly the shape the log has: no spread misses at all below a
			// five degree cone, and no compensation whatsoever above twelve -- the
			// cone is wide precisely when the punch has accumulated.
			const auto seed_base = math::vector3{
				aim_angle.x - aim_punch.x,
				aim_angle.y - aim_punch.y,
				0.0f
			};

			// The tick was ruled out by measurement before this: all four candidates
			// -- backtrack stamp, tick_base, tick_base + 1 and client_tick -- gave
			// the same deviation-to-cone ratio and the same hit rate inside noise.
			// The fire mode passed to calculate_spread was what mattered, and it is
			// decided there now.
			const auto corrected = g_shared.find_spread_correction( seed_base, stamp_tick );

			// Вето на выстрел при неудаче поиска здесь БЫЛО и это была ошибка.
			//
			// Раньше: нет решения -- return, выстрел не уходит вообще. Смысл
			// задумки понятен (стрелять без компенсации значит отдать пулю
			// разбросу), но на практике это превращало no_spread в главный
			// тормоз всего ragebot: компенсация ищется перебором неподвижной
			// точки seed'а, и на широких конусах -- движущийся автомат, отдача,
			// дробовик, большая дистанция -- её попросту может не быть. Бот
			// видел цель, целился, был готов стрелять и молчал, а со стороны
			// это выглядит как полностью нерабочий ragebot.
			//
			// Правильное решение: неудача поиска -- это не запрет стрелять, а
			// отсутствие бонуса. Пуля уйдёт по обычному разбросу, часть урона
			// потеряется, но выстрел состоится. Отсутствие компенсации никогда
			// не хуже отказа от выстрела: пропущенный выстрел -- гарантированный
			// ноль урона, а выстрел с разбросом иногда попадает.
			if ( !( corrected.x == 0.0f && corrected.y == 0.0f && corrected.z == 0.0f ) )
			{
				// Put it back into aim space, so the punch subtraction below
				// reproduces `corrected` byte for byte and the wire carries the
				// angle that was actually verified.
				aim_angle = math::vector3{
					corrected.x + aim_punch.x,
					corrected.y + aim_punch.y,
					corrected.z
				};
			}
			else if ( config.no_spread_strict.value )
			{
				// Строгий режим -- opt-in. Раньше это было поведением по
				// умолчанию, и оно делало no_spread главным тормозом ragebot:
				// без решения выстрел не уходил вообще. Теперь это осознанный
				// выбор пользователя -- "стрелять только с компенсацией".
				{
					static std::atomic< std::uint32_t > strict_misses{};
					const auto count = strict_misses.fetch_add( 1, std::memory_order_relaxed ) + 1;

					if ( count == 1 || ( count % 100 ) == 0 )
					{
						logging::console::print(
							xs( "[no spread] strict: no correction found {} times; shot withheld (inacc {:.4f}, spread {:.4f})" ),
							count, shared_ctx.inaccuracy, shared_ctx.spread );
					}
				}

				return;
			}
			else
			{
				// Компенсации нет -- стреляем как есть, по некомпенсированному
				// углу. aim_angle уже равен ему, roll оставляем нулевым.
				//
				// Считаем это событие, чтобы по логу было видно, насколько
				// часто no_spread не справляется на текущем оружии: если
				// постоянно -- дело в самом оружии или в дистанции, а не в
				// отсутствии решения в принципе.
				static std::atomic< std::uint32_t > misses{};
				const auto count = misses.fetch_add( 1, std::memory_order_relaxed ) + 1;

				if ( count == 1 || ( count % 200 ) == 0 )
				{
					logging::console::print(
						xs( "[no spread] no correction found {} times; firing uncompensated (inacc {:.4f}, spread {:.4f})" ),
						count, shared_ctx.inaccuracy, shared_ctx.spread );
				}
			}
		}

		g_shared.last_shoot_tick( ) = tick_base;

		if constexpr (k_enable_shot_logging)
		{
			const auto hitgroup_name = systems::g_hitboxes.hitgroup_to_name( tgt.hit.hitgroup );
			const auto bt_delta = g_shared.ctx( ).current_tick - tgt.hit.record->tick;
			logging::console::print(
				xs( "[rage] shot target hp {} for {:.0f} in {} (hc {:.0f}%, bt {}t{})" ),
				tgt.hit.health,
				tgt.hit.damage,
				hitgroup_name,
				tgt.hitchance * 100.0f,
				bt_delta,
				was_forced ? xs( ", forced" ) : ""
			);
		}

		features::misc::g_impacts.on_boom( tgt.hit.pawn, tgt.hit.hitgroup, tgt.hit.damage, tgt.hitchance, shared_ctx.inaccuracy, shared_ctx.spread, aim_angle, shoot_eye, tgt.hit.record->tick, g_shared.lc( ).get_skeleton( *tgt.hit.record ), was_forced );
		features::esp::player::g_chams.os ().push (tgt.hit.pawn);
		const auto record_time = cstypes::tick_fraction::from_value( tgt.hit.record->simulation_time / cstypes::tick_interval );
		const auto history_size = cmd->csgo_user_cmd.input_history_size( );
		for ( auto i = 0; i < history_size; ++i )
		{
			const auto entry = cmd->csgo_user_cmd.mutable_input_history( i );
			if ( !entry )
			{
				continue;
			}

			if ( const auto angles = entry->mutable_view_angles( ) )
			{
				angles->set_x( aim_angle.x - aim_punch.x );
				angles->set_y( aim_angle.y - aim_punch.y );

				if ( config.no_spread.value )
				{
					angles->set_z( aim_angle.z );
				}
			}

			this->stamp_input_entry( entry, record_time, tgt.hit.source_eye );
		}

		const auto quick_revolver = shared_ctx.item_def_idx == cstypes::item_definition_index::weapon_r8_revolver
			&& settings::g_combat.m_autos.revolver_quick.value;

		if ( !subtick_attack )
		{

			// Hand the cone we solved for to the calibrator, so the engine hook can
			// say how far off it was once this shot actually goes out.
			g_shared.note_pending_shot( shared_ctx.inaccuracy, quick_revolver, tick_base );

			// The hook has stayed silent across three sessions, and silence has too
			// many explanations: never fired, fired and agreed, or never asked
			// because the flag below was false. Say what this side saw, so the next
			// log rules out at least this half.
			if ( shared_ctx.item_def_idx == cstypes::item_definition_index::weapon_r8_revolver )
			{
				static std::atomic<std::uint32_t> revolver_shots{};
				const auto n = revolver_shots.fetch_add( 1, std::memory_order_relaxed ) + 1;

				if ( n <= 5 || ( n % 25 ) == 0 )
				{
					const auto mode_offset = SCHEMA( "C_CSWeaponBase", "m_weaponMode"_hash );
					const auto live_mode = mode_offset > 0
						? memory::read<int>( shared_ctx.weapon + mode_offset )
						: -1;

					// pitch_fix is deg( atan( |spread| ) ) -- the exact size of the
					// deviation no_spread believes it cancelled. Hold it against the
					// dev= figure in the miss line: equal magnitudes mean the amount
					// was right and only the direction was lost, which is a roll
					// problem; different magnitudes mean the cone itself is wrong.
					char line[ 256 ]{};
					_snprintf_s( line, sizeof( line ), _TRUNCATE,
						"[revolver] fire #%u: quick=%d live_mode=%d inacc=%.5f spread=%.5f pitch_fix=%.2fdeg roll=%.2fdeg punch=%.2f,%.2f",
						n, quick_revolver ? 1 : 0, live_mode,
						shared_ctx.inaccuracy, shared_ctx.spread,
						aim_angle.x - uncorrected_aim.x, aim_angle.z,
						aim_punch.x, aim_punch.y );
					diag::step( line );
				}
			}

			const auto attack_button = quick_revolver
				? cstypes::command_buttons::in_second_attack
				: cstypes::command_buttons::in_attack;

			cmd->buttons.value |= attack_button;
			cmd->buttons.value_changed |= attack_button;
			cmd->buttons.value_scroll |= attack_button;

			if ( history_size > 0 )
			{
				// The server reads the shot out of the history entry this index
				// points at, and it looks at a different field per button. Naming
				// the wrong one leaves the shot with no angles attached.
				if ( quick_revolver )
				{
					cmd->csgo_user_cmd.set_attack2_start_history_index( history_size - 1 );

					// Never leave the primary index pointing at an entry as well --
					// two live indices let the server pick the shot we did not send.
					cmd->csgo_user_cmd.set_attack1_start_history_index( -1 );
				}
				else
				{
					cmd->csgo_user_cmd.set_attack1_start_history_index( history_size - 1 );
				}
			}

			// Wire state after every mutation, not before. Roll being present in the
			// entry the index names is the one thing that cannot be inferred from
			// this side of the correction.
			if ( quick_revolver )
			{
				static std::atomic<std::uint32_t> wire{};
				const auto w = wire.fetch_add( 1, std::memory_order_relaxed ) + 1;

				if ( w <= 5 || ( w % 25 ) == 0 )
				{
					const auto idx = cmd->csgo_user_cmd.attack2_start_history_index( );
					const auto named = ( idx >= 0 && idx < history_size )
						? cmd->csgo_user_cmd.mutable_input_history( idx )
						: nullptr;
					const auto named_angles = named ? named->mutable_view_angles( ) : nullptr;

					char line[ 240 ]{};
					_snprintf_s( line, sizeof( line ), _TRUNCATE,
						"[revolver] wire #%u: hist=%d a2=%d a1=%d entry_roll=%.2f held_attack1=%d",
						w, history_size, idx,
						cmd->csgo_user_cmd.attack1_start_history_index( ),
						named_angles ? named_angles->z( ) : -999.0f,
						( cmd->buttons.value & cstypes::command_buttons::in_attack ) ? 1 : 0 );
					diag::step( line );
				}
			}
		}

		math::vector3 forward{};
		{
			if ( const auto angles = base->viewangles( ) )
			{
				math::helpers::angle_vectors_left( { angles->x( ), angles->y( ), angles->z( ) }, &forward );
			}
		}

		// Roll normally rides in the input history entries and nowhere else, because
		// that is where the server reads an attack1 shot from -- and leaving the
		// command angles flat keeps the view upright for everything else.
		//
		// The revolver's quick shot is judged from attack2, and every sign points
		// at the server taking those angles from the command rather than from the
		// history entry: the compensation is computed, the fixed point verifies,
		// and the bullet still lands with the full width of the cone in a random
		// direction. That is precisely what losing the roll looks like -- the pitch
		// half of the correction is applied while the half that aims it is not.
		const auto carry_roll_in_command = config.no_spread.value && quick_revolver;
		const auto punched_aim = math::vector3{
			aim_angle.x - aim_punch.x,
			aim_angle.y - aim_punch.y,
			carry_roll_in_command ? aim_angle.z : 0.0f
		};
		const auto facing_away = forward.dot( ( tgt.hit.record->origin - systems::g_prediction.pre( ).networked_origin ).normalized( ) ) < 0.707107f;

		auto command_aim = punched_aim;
		if ( !subtick_attack && facing_away && settings::g_combat.m_antiaim.hide_shots.value )
		{
			command_aim.x = 179.9f;
			command_aim.y = std::remainderf( punched_aim.y + 180.0f, 360.0f );
		}

		if ( const auto angles = base->mutable_viewangles( ) )
		{
			angles->set_x( command_aim.x );
			angles->set_y( command_aim.y );

			if ( carry_roll_in_command )
			{
				angles->set_z( command_aim.z );
			}
		}

		// general.silent_aim -- мастер-гейт над пер-оружийным silent. Пока он был
		// мёртвым, чекбокс "Silent aim" во вкладке General ничего не делал:
		// снятие галочки не возвращало видимое наведение.
		if ( !settings::g_combat.m_ragebot.silent_effective( config ) )
		{
			systems::g_input.set_view_angles( punched_aim );
		}
	}

	void rage::request_scope( systems::input::usercmd* cmd )
	{
		if ( !cmd )
		{
			return;
		}

		const auto base_cmd = cmd->csgo_user_cmd.mutable_base( );
		if ( !base_cmd )
		{
			return;
		}

		// Тот же паттерн, что и у выстрела: пара subtick-шагов press/release,
		// чтобы кнопка была именно нажата и отпущена внутри тика, а не осталась
		// висеть. Базовой команде нажатие не оставляем -- иначе зум схлопнется
		// обратно следующим же тиком, как только мы перестанем его держать.
		if ( const auto subtick_moves = base_cmd->mutable_subtick_moves( ) )
		{
			const auto old_size = subtick_moves->m_current_size;
			const auto press = systems::g_input.acquire_subtick_step( subtick_moves );
			const auto release = systems::g_input.acquire_subtick_step( subtick_moves );

			if ( press && release )
			{
				constexpr auto scope_button = cstypes::command_buttons::in_second_attack;

				press->set_button( scope_button );
				press->set_pressed( true );
				press->set_when( 0.0f );
				press->set_analog_forward_delta( 0.0f );
				press->set_analog_left_delta( 0.0f );

				release->set_button( scope_button );
				release->set_pressed( false );
				release->set_when( std::nextafter( 1.0f, 0.0f ) );
				release->set_analog_forward_delta( 0.0f );
				release->set_analog_left_delta( 0.0f );

				cmd->buttons.value &= ~scope_button;
				cmd->buttons.value_changed |= scope_button;
				cmd->buttons.value_scroll &= ~scope_button;
				return;
			}

			// Не оставлять половину нажатия, если второй шаг не выделился.
			subtick_moves->m_current_size = old_size;
		}

		// Subtick-шаги недоступны -- деградируем к удержанию кнопки в базовой
		// команде. Хуже (зум может не успеть примениться за один тик), но
		// лучше, чем не скопиться вообще.
		cmd->buttons.value |= cstypes::command_buttons::in_second_attack;
		cmd->buttons.value_changed |= cstypes::command_buttons::in_second_attack;
	}

	void rage::fire_melee( systems::input::usercmd* cmd, const target& tgt, const systems::local::snapshot& local )
	{
		if ( !tgt.hit.record || !tgt.hit.record->valid )
		{
			return;
		}

		this->m_firing_this_tick = true;

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );

		g_shared.last_shoot_tick( ) = tick_base;

		const auto record_time = cstypes::tick_fraction::from_value( tgt.hit.record->simulation_time / cstypes::tick_interval );
		const auto history_index = cmd->csgo_user_cmd.input_history_size( ) - 1;
		const auto entry = history_index >= 0 ? cmd->csgo_user_cmd.mutable_input_history( history_index ) : nullptr;

		if ( entry )
		{
			if ( const auto angles = entry->mutable_view_angles( ) )
			{
				angles->set_x( tgt.hit.aim_angle.x );
				angles->set_y( tgt.hit.aim_angle.y );
			}

			this->stamp_input_entry( entry, record_time, tgt.hit.source_eye );

		}

		const auto is_secondary = tgt.hit.attack_type == 1;
		const auto attack_button = is_secondary
			? cstypes::command_buttons::in_second_attack
			: cstypes::command_buttons::in_attack;

		cmd->buttons.value |= attack_button;
		cmd->buttons.value_changed |= attack_button;
		cmd->buttons.value_scroll |= attack_button;

		if ( history_index >= 0 )
		{
			if ( is_secondary )
			{
				cmd->csgo_user_cmd.set_attack2_start_history_index( history_index );
			}
			else
			{
				cmd->csgo_user_cmd.set_attack1_start_history_index( history_index );
			}
		}

		if ( const auto angles = base->mutable_viewangles( ) )
		{
			angles->set_x( tgt.hit.aim_angle.x );
			angles->set_y( tgt.hit.aim_angle.y );
		}
	}

	template <typename Entry>
	void rage::stamp_input_entry(
		Entry* entry,
		const cstypes::tick_fraction& record_time,
		const shared::shoot_history::eye_candidate& source_eye ) const
	{
		entry->set_render_tick_count( record_time.tick + 1 );
		entry->set_render_tick_fraction( 0.0f );

		if ( !source_eye.is_uninterpolated )
		{
			const auto [stamp_tick, stamp_frac] = detail::tick_add(
				source_eye.player_tick, source_eye.player_frac,
				source_eye.lerp_ticks_int, source_eye.lerp_ticks_frac );

			entry->set_player_tick_count( stamp_tick );
			entry->set_player_tick_fraction( stamp_frac );
		}

		if ( entry->has_sv_interp0( ) )
		{
			const auto interp = entry->mutable_sv_interp0( );
			interp->set_src_tick( -1 );
			interp->set_dst_tick( -1 );
			interp->set_frac( 0.0f );
		}

		if ( entry->has_sv_interp1( ) )
		{
			const auto interp = entry->mutable_sv_interp1( );
			interp->set_src_tick( -1 );
			interp->set_dst_tick( -1 );
			interp->set_frac( 0.0f );
		}

		if ( entry->has_cl_interp( ) )
		{
			const auto interp = entry->mutable_cl_interp( );
			interp->set_frac( 0.0f );
		}
	}

	void rage::generate_multipoints( const systems::hitboxes::entry& hitbox, const math::vector3& center, const math::quaternion& bone_rot, float pointscale, const math::vector3& shoot_pos, float inaccuracy, std::vector<math::vector3>& out ) const
	{
		out.clear( );

		auto scale = std::clamp( pointscale / 100.0f, 0.0f, 1.0f );
		if ( scale <= 0.01f )
		{
			return;
		}

		const auto hb_mid   = ( hitbox.mins + hitbox.maxs ) * 0.5f;
		const auto capsule_a = center + bone_rot.rotate_vector( hitbox.mins - hb_mid );
		const auto capsule_b = center + bone_rot.rotate_vector( hitbox.maxs - hb_mid );

		// Keep points inside the part of the hitbox reachable by the full spread cone.
		const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );
		if ( config.dynamic_pointscale.value && hitbox.radius > 0.001f )
		{
			const auto cone = std::max( inaccuracy + g_shared.ctx( ).spread, 0.0f );
			const auto cone_radius = std::tanf( cone ) * ( center - shoot_pos ).length( );
			const auto automatic_scale = std::clamp( 0.9f - cone_radius / hitbox.radius, 0.0f, 1.0f );
			scale = std::min( scale, automatic_scale );

			if ( scale <= 0.01f )
			{
				return;
			}
		}

		// Build a view-relative frame so points rotate correctly above and below us.
		const auto shoot_dir = ( center - shoot_pos ).normalized( );
		const auto ang       = math::helpers::vector_to_angle( shoot_dir );

		math::vector3 left{}, up{};
		math::helpers::angle_vectors_left( ang, nullptr, &left, &up );

		// angle_vectors_left returns left, so negate it for right.
		const auto right = math::vector3{ -left.x, -left.y, -left.z };

		// Trace from outside through the center to find the real capsule surface.
		// This is accurate around rounded end caps, where radius offsets are not.
		const auto surface_point = [ & ]( const math::vector3& direction ) -> math::vector3
		{
			const auto dir = direction.normalized( );

			if ( hitbox.radius > 0.001f )
			{
				const auto reach = ( capsule_b - capsule_a ).length( ) + hitbox.radius * 2.0f + 1.0f;
				const auto origin = center + dir * reach;
				const auto delta = dir * ( reach * -2.0f );
				auto fraction{ 1.0f };

				if ( g_shared.ray_vs_capsule( origin, delta, capsule_a, capsule_b, hitbox.radius, fraction ) )
				{
					return origin + delta * fraction;
				}
			}
			else
			{
				// Intersect box hitboxes in bone space using their directional support.
				auto inverse = bone_rot;
				inverse.x = -inverse.x;
				inverse.y = -inverse.y;
				inverse.z = -inverse.z;
				const auto local_dir = inverse.rotate_vector( dir );
				const auto extents = ( hitbox.maxs - hitbox.mins ) * 0.5f;
				auto distance = 8192.0f;

				if ( std::fabs( local_dir.x ) > 1.0e-6f ) distance = std::min( distance, std::fabs( extents.x / local_dir.x ) );
				if ( std::fabs( local_dir.y ) > 1.0e-6f ) distance = std::min( distance, std::fabs( extents.y / local_dir.y ) );
				if ( std::fabs( local_dir.z ) > 1.0e-6f ) distance = std::min( distance, std::fabs( extents.z / local_dir.z ) );

				if ( distance < 8192.0f )
				{
					return center + dir * distance;
				}
			}

			return center;
		};

		const auto scaled_surface = [ & ]( const math::vector3& direction )
		{
			const auto surface = surface_point( direction );
			return center + ( surface - center ) * scale;
		};

		// -- Кольца точек вокруг оси обзора -----------------------------------
		//
		// Раньше каждая ветка switch была расписана руками: четыре стороны на
		// голову, четыре на тело, две точки на ноги. Это давало ровно столько
		// углов, сколько вписал автор, и покрыть цель плотнее можно было только
		// дописав ветку. Теперь кольца строятся из настроек
		// (multipoint_rings x multipoint_points) -- одинаково для любого
		// хитбокса, и плотность покрытия задаёт пользователь.
		//
		// Точка берётся на поверхности капсулы по направлению (right/up).
		// Внутренние кольца смещены по фазе на полшага: без этого точки
		// соседних колец выстраиваются в спицы и оставляют сектора.
		const auto ring_points = [ & ]( int rings, int per_ring, std::vector<math::vector3>& dst )
		{
			const auto r = std::clamp( rings, 1, 4 );
			const auto n = std::clamp( per_ring, 3, 12 );

			for ( auto ring = 0; ring < r; ++ring )
			{
				const auto phase = static_cast< float >( ring ) * 0.5f * ( 6.2831853f / static_cast< float >( n ) );

				for ( auto i = 0; i < n; ++i )
				{
					const auto a = phase + ( 6.2831853f / static_cast< float >( n ) ) * static_cast< float >( i );
					const auto dir = ( right * std::cos( a ) + up * std::sin( a ) ).normalized( );
					dst.push_back( scaled_surface( dir ) );
				}
			}
		};

		switch ( hitbox.index )
		{
		case 0: // head
		{
			// Базовая капсула головы. Когда general.model_head_scan включён,
			// эти точки заменяются на прощупанные открытые участки (head_model);
			// здесь -- покрытие на случай, когда прощуп выключен.
			out.reserve( 12 );

			ring_points( 1, 4, out );

			// Четыре диагонали: кольцо не покрывает углы силуэта, а именно туда
			// попадает узкий конус вдали.
			out.push_back( scaled_surface( ( right + up ).normalized( ) ) );
			out.push_back( scaled_surface( ( -right + up ).normalized( ) ) );
			out.push_back( scaled_surface( ( right + -up ).normalized( ) ) );
			out.push_back( scaled_surface( ( -right + -up ).normalized( ) ) );

			// shoot_dir points from us to the head, so this lands on the near face
			// of the head capsule (toward the enemy's front when they face us, or
			// their back when they face away) — the side most likely to connect.
			out.push_back( scaled_surface( shoot_dir ) );

			// Плотное покрытие головы: вторая ручка, отдельная от тела --
			// по голове стреляют узким конусом, и лишние точки там окупаются.
			if ( config.head_pointscale.value )
			{
				const auto head_scale = std::clamp( config.head_pointscale_scale.value / 100.0f, 0.0f, 1.0f );
				if ( head_scale > 0.01f )
				{
					const auto saved = scale;
					scale = std::min( scale, head_scale );
					ring_points( std::clamp( config.multipoint_rings.value, 1, 4 ),
						std::clamp( config.multipoint_points.value, 3, 12 ), out );
					scale = saved;
				}
			}
			break;
		}

		case 2: case 3: // stomach / pelvis
		{
			out.reserve( 6 );
			ring_points( 1, 4, out );
			break;
		}

		case 4: case 5: case 6: // chest
		{
			out.reserve( 8 );
			ring_points( 1, 4, out );
			if ( hitbox.index == 6 )
			{
				out.push_back( scaled_surface( shoot_dir ) );
			}

			// Плотное покрытие корпуса: по телу почти всегда стреляют широким
			// конусом, и точность на краях там важнее числа точек.
			if ( config.body_pointscale.value )
			{
				const auto body_scale = std::clamp( config.body_pointscale_scale.value / 100.0f, 0.0f, 1.0f );
				if ( body_scale > 0.01f )
				{
					const auto saved = scale;
					scale = std::min( scale, body_scale );
					ring_points( std::clamp( config.multipoint_rings.value, 1, 4 ),
						std::clamp( config.multipoint_points.value, 3, 12 ), out );
					scale = saved;
				}
			}
			break;
		}

		case 7: case 8: case 9: case 10: case 11: case 12: // legs / feet
		{
			out.reserve( 2 );
			out.push_back( capsule_a );
			out.push_back( capsule_b );
			break;
		}

		case 13: case 14: case 15: case 16: case 17: case 18: // arms
		{
			out.reserve( 1 );
			out.push_back( capsule_b );
			break;
		}

		default:
		{
			out.reserve( 2 );
			out.push_back( scaled_surface( right ) );
			out.push_back( scaled_surface( -right ) );
			break;
		}
		}

		return;
	}

	bool rage::should_stop_for_target( const aim_context& ctx, const target& best_target ) const
	{
		if ( !this->should_stop_movement( ctx ) )
		{
			return false;
		}

		if ( !best_target.valid )
		{
			return true;
		}

		const auto& prestate = systems::g_prediction.pre( );
		const auto velocity = prestate.networked_velocity;
		const auto speed = velocity.length_2d( );

		if ( speed < 1.0f )
		{
			return false;
		}

		// Direction we are moving in (flattened to the ground plane).
		const auto move_dir = math::vector3{ velocity.x / speed, velocity.y / speed, 0.0f };

		// Direction from us to the target.
		const auto to_target = best_target.hit.position - prestate.origin;
		const auto dist_2d = std::sqrtf( to_target.x * to_target.x + to_target.y * to_target.y );

		if ( dist_2d < 1.0f )
		{
			return true; // Practically on top of the target, just stop.
		}

		const auto target_dir = math::vector3{ to_target.x / dist_2d, to_target.y / dist_2d, 0.0f };
		const auto dot = move_dir.x * target_dir.x + move_dir.y * target_dir.y;

		// Moving strongly sideways or away from the target: always stop.
		if ( dot < 0.3f )
		{
			return true;
		}

		// Moving toward a distant target: approaching improves hitchance on later
		// ticks, so there is no need to kill our momentum right now.
		if ( dot > 0.7f && dist_2d > 1500.0f )
		{
			return false;
		}

		return true;
	}

	bool rage::should_stop_movement( const aim_context& ctx ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto& prestate = systems::g_prediction.pre( );
		const auto velocity = prestate.networked_velocity;

		if ( shared_ctx.weapon_type == cstypes::weapon_type::sniper && !ctx.is_scoped )
		{
			return false;
		}

		// Per-weapon stop mode. This is the one place the decision is made, so
		// restricting it here covers every caller rather than each of them having
		// to remember the setting.
		const auto& stop_config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
		const auto stop_mode = stop_config.auto_stop_mode.value;

		if ( stop_mode == 2 && ctx.on_ground )
		{
			return false;
		}

		if ( stop_mode == 3 && !ctx.on_ground )
		{
			return false;
		}

		{
			const auto predicted_speed = std::sqrtf( ctx.velocity.x * ctx.velocity.x + ctx.velocity.y * ctx.velocity.y );

			// Порог, ниже которого останавливаться незачем. Раньше это была
			// точная граница точности оружия (accurate_threshold), и получалось,
			// что "auto stop" перестаёт тормозить ровно там, где разброс ещё
			// есть: стоя на месте с 30 юнитов/с конус уже шире идеального.
			// Теперь порог -- отдельная ручка (autostop_speed), а
			// accurate_threshold остаётся нижней границей: за ней торможение
			// не даёт ничего вообще.
			const auto floor_speed = std::max( ctx.accurate_threshold,
				stop_config.autostop_speed.value );

			if ( predicted_speed <= floor_speed )
			{
				return false;
			}
		}

		if ( ctx.on_ground )
		{
			const auto speed_2d = velocity.length_2d( );
			if ( speed_2d <= 0.1f )
			{
				return false;
			}

			if ( !shared_ctx.weapon_vdata )
			{
				return speed_2d > ctx.accurate_threshold;
			}

			// The comparison this replaces multiplied a speed in units per second
			// by a move-inaccuracy coefficient and held the result against a bare
			// standing cone. Those are not the same quantity: for a rifle the left
			// side lands around 25 and the right around 0.02, so it was true for
			// any movement at all. "auto" and "early" therefore did exactly the
			// same thing -- brake the instant we pass the accuracy threshold --
			// which is the mode behaving badly rather than the threshold being
			// wrong.
			//
			// Express it as a fraction instead, which is dimensionless and honest.
			// Movement adds no cone at all below 34% of max speed; that is what
			// accurate_threshold already encodes, and the guard above has cleared
			// it. Past that point the penalty grows with how far into the
			// remaining speed range we are.
			const auto max_speed = ctx.weapon_max_speed > 1.0f ? ctx.weapon_max_speed : 250.0f;
			const auto free_zone = ctx.accurate_threshold;
			const auto span = std::max( max_speed - free_zone, 1.0f );
			const auto move_ratio = std::clamp( ( speed_2d - free_zone ) / span, 0.0f, 1.0f );

			// early brakes as soon as we leave the free zone; auto keeps a real
			// margin, so walking and small adjustments no longer trigger a stop.
			const auto trigger = ( stop_mode == 1 ) ? 0.10f : 0.45f;
			return move_ratio > trigger;
		}

		if ( shared_ctx.weapon_type != cstypes::weapon_type::sniper )
		{
			return false;
		}

		if ( !shared_ctx.weapon_vdata )
		{
			return false;
		}

		// Still rising: too early to plan a stop.
		if ( velocity.z > 120.0f )
		{
			return false;
		}

		const auto sv_gravity = CONVAR ("sv_gravity")->get<float>( );
		const auto sv_friction = CONVAR ("sv_friction")->get<float>( );
		const auto sv_stopspeed = CONVAR ("sv_stopspeed")->get<float>( );

		const auto inac_jump_initial = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpInitial"_hash ) );
		const auto inac_jump_apex = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_hash ) );

		const auto shootable_threshold = inac_jump_apex + 0.001f;
		const auto air_inaccuracy = g_shared.get_air_inaccuracy( velocity.z, inac_jump_initial, inac_jump_apex );

		const auto speed_2d = velocity.length_2d( );
		const auto max_speed = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flMaxSpeed"_hash ) );
		const auto accurate_threshold = max_speed * 0.34f;

		// Glide: we are already inside the accuracy window, so asking movement to
		// brake here buys nothing and kills the strafe. This used to return true,
		// which is what made the aimbot stall in mid-air instead of shooting and
		// flying on.
		if ( air_inaccuracy <= shootable_threshold )
		{
			return false;
		}

		// "early" restores the pre-apex trigger: brake as soon as the cone is on
		// its way in rather than waiting for the last moment that still lands.
		if ( stop_mode == 1 )
		{
			const auto early_threshold = inac_jump_initial * 0.55f + inac_jump_apex * 0.45f;
			if ( air_inaccuracy <= early_threshold )
			{
				return true;
			}
		}

		// Horizontal speed is already fine, so there is nothing to brake off.
		if ( speed_2d <= accurate_threshold )
		{
			return false;
		}

		auto sim_vz = velocity.z;
		auto ticks_to_shootable{ 0 };

		for ( auto i = 1; i <= 32; ++i )
		{
			sim_vz -= sv_gravity * cstypes::tick_interval;

			if ( g_shared.get_air_inaccuracy( sim_vz, inac_jump_initial, inac_jump_apex ) <= shootable_threshold )
			{
				ticks_to_shootable = i;
				break;
			}
		}

		if ( ticks_to_shootable == 0 )
		{
			return false;
		}

		auto sim_speed = speed_2d;
		auto ticks_to_stop{ 32 };

		for ( auto i = 1; i <= 32; ++i )
		{
			const auto drop = std::fmaxf( sim_speed, sv_stopspeed ) * sv_friction * cstypes::tick_interval;
			sim_speed -= drop;

			if ( sim_speed <= accurate_threshold )
			{
				ticks_to_stop = i;
				break;
			}
		}

		// Late stop: brake at the last moment that still lands the shot at the apex.
		// The old +2 committed a tick earlier than needed.
		return ticks_to_shootable <= ticks_to_stop + 1;
	}

	float rage::get_min_damage( const settings::combat::ragebot::weapon_group& config, int target_health, bool override_active ) const
	{
		if ( override_active )
		{
			return static_cast< float >( config.min_damage_override_value );
		}

		// general.min_damage подставляется, когда у группы оружия min_damage
		// выставлен в 0 ("не задано"). Иначе пер-оружийное значение -- главное.
		const auto base = static_cast< float >( settings::g_combat.m_ragebot.min_damage_effective( config ) );

		const auto hp = static_cast< float >( target_health );
		if ( hp < base )
		{
			return hp + 1.0f;
		}

		return base;
	}

	float rage::get_knife_damage( float raw, int armor, float armor_ratio ) const
	{
		if ( armor <= 0 )
		{
			return raw;
		}

		const auto ratio = armor_ratio * 0.5f;
		auto damage_to_health = raw * ratio;
		const auto damage_to_armor = ( raw - damage_to_health ) * 0.5f;

		if ( damage_to_armor > static_cast< float >( armor ) )
		{
			damage_to_health = raw - static_cast< float >( armor ) * 2.0f;
		}

		return std::max( 0.0f, std::floorf( damage_to_health ) );
	}

	systems::tracing::result rage::trace_taser_hit( const math::vector3& origin, const math::vector3& forward, float range, std::uintptr_t target_pawn, std::uintptr_t local_pawn ) const
	{
		const auto end = origin + forward * range;
		const int filter_extras[ ]{ 0, 15 };

		for ( const auto extra : filter_extras )
		{
			const auto filter = extra == 0 ? systems::g_tracing.make_filter( local_pawn, 0x001c1003, 4 ) : systems::g_tracing.make_filter( local_pawn, 0x001c1003, 4, 15 );
			auto result = systems::g_tracing.trace( origin, end, filter );

			if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
			{
				return result;
			}

			for ( auto radius = 2.0f; radius <= 4.0f; radius += 2.0f )
			{
				const auto sweep_end = end - forward * radius;
				result = systems::g_tracing.trace_sphere( origin, sweep_end, radius, filter );

				if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
				{
					return result;
				}
			}
		}

		systems::tracing::result miss{};
		miss.fraction = 1.0f;
		miss.hit_entity = 0;
		return miss;
	}

	systems::tracing::result rage::trace_knife_hit( const math::vector3& origin, const math::vector3& forward, float reach, std::uintptr_t target_pawn, std::uintptr_t local_pawn ) const
	{
		const auto end = origin + forward * reach;
		const auto knife_filter = systems::g_tracing.make_filter( local_pawn, 0x0c3001, 4 );
		auto result = systems::g_tracing.trace( origin, end, knife_filter );

		if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
		{
			return result;
		}

		const auto weapon_filter = systems::g_tracing.make_filter( local_pawn, 0x0c3001, 4, 15 );
		result = systems::g_tracing.trace( origin, end, weapon_filter );

		if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
		{
			return result;
		}

		for ( auto radius = 14.0f; radius > 0.0f; radius -= 3.0f )
		{
			const auto sweep_end = end - forward * radius;
			result = systems::g_tracing.trace_sphere( origin, sweep_end, radius, weapon_filter );

			if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
			{
				return result;
			}
		}

		result.fraction = 1.0f;
		result.hit_entity = 0;
		return result;
	}

	void rage::update_penetration_crosshair( const systems::local::snapshot& local )
	{
		const auto& cfg = settings::g_combat.m_penetration_crosshair;
		const auto& ctx = g_shared.ctx( );

		if ( !cfg.enabled.value || !ctx.valid || !local.is_alive || local.team < 2
			|| !local.pawn || !ctx.weapon
			|| ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg )
		{
			this->m_penetration_crosshair_state.store( penetration_crosshair_state::unavailable, std::memory_order_relaxed );
			return;
		}

		// get_shoot_position() can be zero before prediction has populated the
		// weapon-services shoot history. The crosshair needs the current eye now.
		const auto eye_pos = g_shared.get_eye_position( local.pawn );
		auto view_angles = systems::g_input.get_view_angles( );
		const auto aim_punch = g_shared.get_aim_punch( local.pawn );
		view_angles.x += aim_punch.x;
		view_angles.y += aim_punch.y;

		math::vector3 forward{};
		math::helpers::angle_vectors_left( view_angles, &forward );

		auto pen_damage{ 0.0f };
		const auto can_pen = g_shared.pen( ).can( eye_pos, forward, pen_damage, local );
		this->m_penetration_crosshair_state.store(
			can_pen ? penetration_crosshair_state::penetrable : penetration_crosshair_state::blocked,
			std::memory_order_relaxed );
	}

	void rage::draw_penetration_crosshair( xdraw::draw_list& draw_list ) const
	{
		const auto& cfg = settings::g_combat.m_penetration_crosshair;
		if ( !cfg.enabled.value )
		{
			return;
		}

		const auto state = this->m_penetration_crosshair_state.load( std::memory_order_relaxed );
		const auto local = systems::g_local.get( );
		if ( state == penetration_crosshair_state::unavailable || !local.is_alive || systems::g_local.is_in_cinematic( ) )
		{
			return;
		}

		const auto can_pen = state == penetration_crosshair_state::penetrable;

		const auto& fill = can_pen ? cfg.can_penetrate_fill : cfg.blocked_fill;
		const auto& outline = can_pen ? cfg.can_penetrate_outline : cfg.blocked_outline;
		const auto [ screen_w, screen_h ] = xdraw::viewport_size( );
		const auto cx = std::floorf( static_cast< float >( screen_w ) * 0.5f );
		const auto cy = std::floorf( static_cast< float >( screen_h ) * 0.5f );
		constexpr auto half_size{ 3.0f };
		constexpr auto outline_size{ 1.0f };

		if ( cfg.glow )
		{
			auto& glow = xdraw::get_glow( );
			const auto glow_a = static_cast< std::uint8_t >( static_cast< float >( outline.value.a ) * cfg.glow_strength );
			const auto glow_col = xdraw::color{ outline.value.r, outline.value.g, outline.value.b, glow_a };

			glow.rect_filled( cx - half_size - outline_size, cy - half_size - outline_size,
				( half_size + outline_size ) * 2.0f, ( half_size + outline_size ) * 2.0f, glow_col );
		}

		draw_list.rect_filled( cx - half_size - outline_size, cy - half_size - outline_size,
			( half_size + outline_size ) * 2.0f, ( half_size + outline_size ) * 2.0f, outline );
		draw_list.rect_filled( cx - half_size, cy - half_size, half_size * 2.0f, half_size * 2.0f, fill );
	}

} // namespace features::combat
