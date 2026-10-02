#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>
namespace features::combat {

	void misc::antiaim::on_create_move( systems::input::usercmd* cmd )
	{
		this->m_antiaim_active = false;

		if ( !settings::g_combat.m_antiaim.enabled.value )
		{
			return;
		}

		if ( systems::g_local.is_in_cinematic( ) || systems::g_local.is_in_time_freeze( ) )
		{
			return;
		}

		if ( settings::g_combat.m_antiaim.manual_left.value && settings::g_combat.m_antiaim.manual_right.value )
		{
			settings::g_combat.m_antiaim.manual_right.value = false;
			settings::g_combat.m_antiaim.manual_right.bind.active = false;
		}

		if ( settings::g_combat.m_antiaim.manual_left.value )
		{
			this->m_yaw_side = -1;
		}
		else if ( settings::g_combat.m_antiaim.manual_right.value )
		{
			this->m_yaw_side = 1;
		}
		else
		{
			this->m_yaw_side = 0;
		}

		if ( !cmd )
		{
			return;
		}

		const auto local = systems::g_local.get( );

		// The move_type read and is_near_ladder( ) below both go through
		// local.pawn, and nothing above this point established that we have one.
		if ( !local.pawn || !local.is_alive )
		{
			return;
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}
		const auto view_angles = systems::g_input.get_view_angles( );
		const auto& ctx = g_shared.ctx( );

		if ( cmd->buttons.value & cstypes::command_buttons::in_use )
		{
			return;
		}

		if ( ctx.weapon_type == cstypes::weapon_type::grenade )
		{
			if ( memory::read<float>( ctx.weapon + SCHEMA( "C_BaseCSGrenade", "m_fThrowTime"_hash ) ) > 0.0f )  // bail regardless of pin state
			{
				return;
			}
		}

		const auto move_type = memory::read<int>( local.pawn + SCHEMA( "C_BaseEntity", "m_nActualMoveType"_hash ) );
		if ( move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip )
		{
			return;
		}

		if ( this->is_near_ladder( local.pawn ) )
		{
			return;
		}

		this->m_old_angles = view_angles;
		this->m_antiaim_active = true;

		this->m_modified_angles = this->m_old_angles;
		this->m_modified_angles.x = this->get_pitch( this->m_old_angles.x );
		this->m_modified_angles.y = this->get_yaw( this->m_old_angles, local );

		math::helpers::normalize_angles( this->m_modified_angles );

		// mutable_viewangles( ) is not protobuf's allocating accessor -- it hands
		// back the stored field pointer through impl_ptr, which returns null when
		// the usercmd arrived without viewangles set. Writing through that put a
		// float at offset 0 of a null pointer: the 0xC0000005 write to 0x0.
		const auto angles = base->mutable_viewangles( );
		if ( !angles )
		{
			this->m_antiaim_active = false;
			return;
		}

		angles->set_x( this->m_modified_angles.x );
		angles->set_y( this->m_modified_angles.y );
		angles->set_z( this->m_modified_angles.z );

		this->m_should_correct = true;

		this->correct_movement( cmd );
	}

	void misc::antiaim::on_render( xdraw::draw_list& draw_list ) const
	{
		if ( !settings::g_combat.m_antiaim.enabled.value || !settings::g_combat.m_antiaim.direction_indicator.value )
		{
			return;
		}

		if ( !this->m_antiaim_active )
		{
			return;
		}

		if ( !systems::g_frame_data.valid( ) )
		{
			return;
		}

		const auto origin = systems::g_frame_data.origin( );
		const auto aa_yaw_rad = this->m_indicator_yaw * ( std::numbers::pi_v<float> / 180.0f );

		constexpr auto radius{ 28.0f };
		constexpr auto feet_offset{ -2.0f };
		constexpr auto arc_sweep_deg{ 60.0f };
		constexpr auto arc_segments{ 48 };
		constexpr auto max_thickness{ 3.0f };

		const auto& cfg = settings::g_combat.m_antiaim;
		const auto& color = cfg.direction_indicator_color;
		const auto base = math::vector3{ origin.x, origin.y, origin.z + feet_offset };

		const auto half_sweep = ( arc_sweep_deg * 0.5f ) * ( std::numbers::pi_v<float> / 180.0f );
		const auto start_angle = aa_yaw_rad - half_sweep;
		const auto end_angle = aa_yaw_rad + half_sweep;
		const auto angle_step = ( end_angle - start_angle ) / static_cast< float >( arc_segments );

		std::vector<math::vector2> pts;
		pts.reserve( arc_segments + 1 );

		for ( auto i = 0; i <= arc_segments; ++i )
		{
			const auto angle = start_angle + angle_step * static_cast< float >( i );

			const auto world_pt = math::vector3
			{
				base.x + std::cosf( angle ) * radius,
				base.y + std::sinf( angle ) * radius,
				base.z
			};

			const auto sp = systems::g_view.project( world_pt );

			if ( !systems::g_view.projection_valid( sp ) )
			{
				return;
			}

			pts.push_back( { sp.x, sp.y } );
		}

		if ( pts.size( ) < 2 )
		{
			return;
		}

		const auto total = static_cast< float >( pts.size( ) - 1 );

		const auto fade_at = [ ]( std::size_t idx, float total ) -> float
			{
				const auto frac = static_cast< float >( idx ) / total;
				const auto edge = 1.0f - std::fabsf( frac - 0.5f ) * 2.0f;
				return edge * edge * edge * ( edge * ( edge * 6.0f - 15.0f ) + 10.0f );
			};

		if ( cfg.direction_indicator_glow )
		{
			auto& glow = xdraw::get_glow( );

			for ( auto i = 0ull; i + 1 < pts.size( ); ++i )
			{
				const auto f0 = fade_at( i, total );
				const auto f1 = fade_at( i + 1, total );

				const auto ga0 = static_cast< std::uint8_t >( static_cast< float >( color.value.a ) * cfg.direction_indicator_glow_strength * std::fmaxf( f0, 0.05f ) );
				const auto ga1 = static_cast< std::uint8_t >( static_cast< float >( color.value.a ) * cfg.direction_indicator_glow_strength * std::fmaxf( f1, 0.05f ) );

				const auto thickness = ( max_thickness + 2.0f ) * ( ( f0 + f1 ) * 0.5f * 0.85f + 0.15f );

				const float seg[ ]{ pts[ i ].x, pts[ i ].y, pts[ i + 1 ].x, pts[ i + 1 ].y };
				const xdraw::color cols[ ]{ { color.value.r, color.value.g, color.value.b, ga0 }, { color.value.r, color.value.g, color.value.b, ga1 } };

				glow.polyline_gradient( seg, cols, false, thickness );
			}
		}

		for ( auto i = 0ull; i + 1 < pts.size( ); ++i )
		{
			const auto f0 = fade_at( i, total );
			const auto f1 = fade_at( i + 1, total );

			const auto a0 = static_cast< std::uint8_t >( color.value.a * std::fmaxf( f0, 0.05f ) );
			const auto a1 = static_cast< std::uint8_t >( color.value.a * std::fmaxf( f1, 0.05f ) );

			const auto thickness = max_thickness * ( ( f0 + f1 ) * 0.5f * 0.85f + 0.15f );

			const float seg[ ]{ pts[ i ].x, pts[ i ].y, pts[ i + 1 ].x, pts[ i + 1 ].y };
			const xdraw::color cols[ ]{ { color.value.r, color.value.g, color.value.b, a0 }, { color.value.r, color.value.g, color.value.b, a1 } };

			draw_list.polyline_gradient( seg, cols, false, thickness );
		}
	}

	float misc::antiaim::get_pitch( float view_pitch )
	{
		switch ( settings::g_combat.m_antiaim.pitch )
		{
		case settings::combat::antiaim::pitch_mode::down:
			return 89.0f;
		case settings::combat::antiaim::pitch_mode::up:
			return -89.0f;
		case settings::combat::antiaim::pitch_mode::zero:
			return 0.0f;
		case settings::combat::antiaim::pitch_mode::custom:
			// Custom pitch from setting
			return settings::g_combat.m_antiaim.pitch_custom.value;
		default:
			return view_pitch;
		}
	}

	// Accumulated spin, shared by every exit path in get_yaw. Spinbot is not a
	// yaw mode, so it has to be applied wherever the yaw is finally decided --
	// including the local-view branch, which returns early and would otherwise
	// never see it.
	float misc::antiaim::advance_spin( )
	{
		const auto& aa = settings::g_combat.m_antiaim;

		if ( !aa.spinbot.value )
		{
			return 0.0f;
		}

		const auto step = std::max( aa.spinbot_speed.value, 0.1f );

		switch ( aa.spinbot_direction.value )
		{
		case settings::combat::antiaim::spin_direction::left:
			this->m_spin_sign = -1.0f;
			break;

		case settings::combat::antiaim::spin_direction::alternating:
			// Wall-clock driven so the reversal keeps its rhythm whatever the
			// frame rate is doing.
			this->m_spin_switch_timer += xdraw::delta_time( );
			if ( this->m_spin_switch_timer >= std::max( aa.spinbot_switch_time.value, 0.1f ) )
			{
				this->m_spin_switch_timer = 0.0f;
				this->m_spin_sign = -this->m_spin_sign;
			}
			break;

		case settings::combat::antiaim::spin_direction::right:
		default:
			this->m_spin_sign = 1.0f;
			break;
		}

		this->m_spin_angle = std::remainderf( this->m_spin_angle + step * this->m_spin_sign, 360.0f );
		return this->m_spin_angle;
	}

	float misc::antiaim::get_yaw( const math::vector3& view_angles, const systems::local::snapshot& local )
	{
		auto base_yaw_offset{ 180.0f };

		// Advanced exactly once per call, before any branch can return.
		const auto spin = this->advance_spin( );

		const auto view_yaw = view_angles.y;
		auto base_yaw = view_yaw - base_yaw_offset;

		// Local view: always face the back toward where we look (spine follows the
		// crosshair), ignoring enemy-based target selection entirely.
		if ( settings::g_combat.m_antiaim.yaw_mode == settings::combat::antiaim::yaw_mode::local_view )
		{
			auto yaw = base_yaw + spin;
			if ( this->m_yaw_side == -1 )
				yaw -= 90.0f;
			else if ( this->m_yaw_side == 1 )
				yaw += 90.0f;
			if ( settings::g_combat.m_antiaim.auto_yaw_adjust.value )
				yaw += 33.0f;
			this->m_indicator_yaw = yaw;
			return yaw;
		}

		const auto local_game_scene_node = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		const auto local_origin = memory::read<math::vector3>( local_game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		const auto& players = systems::g_entities.get_by_type( systems::entities::type::player );
		const auto eye_pos = local_origin + memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );

		if ( settings::g_combat.m_antiaim.avoid_backstab.value )
		{
			constexpr auto backstab_range_sq = 350.0f * 350.0f;
			auto knife_dist = std::numeric_limits<float>::max( );
			auto knife_yaw{ 0.0f };
			auto knife_found{ false };

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

				const auto enemy_game_scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
				if ( !enemy_game_scene_node )
				{
					continue;
				}

				const auto enemy_origin = memory::read<math::vector3>( enemy_game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
				const auto dx = enemy_origin.x - local_origin.x;
				const auto dy = enemy_origin.y - local_origin.y;
				const auto dist_sq = dx * dx + dy * dy;

				if ( dist_sq > backstab_range_sq )
				{
					continue;
				}

				const auto weapon_services = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
				if ( !weapon_services )
				{
					continue;
				}

				const auto weapon_handle = memory::read<std::uint32_t>( weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_hash ) );
				if ( !weapon_handle )
				{
					continue;
				}

				const auto weapon = systems::g_entities.lookup( weapon_handle );
				if ( !weapon )
				{
					continue;
				}

				const auto weapon_vdata = memory::read<std::uintptr_t>( weapon + SCHEMA( "C_BaseEntity", "m_nSubclassID"_hash ) + 0x8 );
				if ( !weapon_vdata )
				{
					continue;
				}

				const auto weapon_type = memory::read<std::uint32_t>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_WeaponType"_hash ) );
				if ( weapon_type != cstypes::weapon_type::knife )
				{
					continue;
				}

				if ( dist_sq < knife_dist )
				{
					knife_dist = dist_sq;
					knife_yaw = std::atan2f( dy, dx ) * ( 180.0f / std::numbers::pi_v<float> );
					knife_found = true;
				}
			}

			if ( knife_found )
			{
				this->m_indicator_yaw = knife_yaw;
				return knife_yaw;
			}
		}

		const auto pick_target_yaw = [ & ]( ) -> std::optional<float>
			{
				auto best_yaw = base_yaw;
				auto best_threat_score = std::numeric_limits<float>::max( );

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

					if ( memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) ) <= 0 )
					{
						continue;
					}

					if ( memory::read<bool>( pawn + SCHEMA( "C_CSPlayerPawn", "m_bGunGameImmunity"_hash ) ) )
					{
						continue;
					}

					const auto enemy_game_scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
					if ( !enemy_game_scene_node )
					{
						continue;
					}

					const auto enemy_origin = memory::read<math::vector3>( enemy_game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
					const auto enemy_eye_pos = enemy_origin + memory::read<math::vector3>( pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );
					const auto angle_to_enemy = math::helpers::calculate_angle( eye_pos, enemy_eye_pos );
					const auto fov = math::helpers::angle_distance( view_angles, angle_to_enemy );
					const auto distance = eye_pos.distance( enemy_eye_pos );

					// Match Requiem's threat order: crosshair first, then proximity, whether
					// the enemy is looking at us, and finally whether they are visible.
					auto threat_score = fov * 4.0f + distance * 0.01f;

					math::vector3 enemy_forward{};
					const auto enemy_eye_angles = memory::read<math::vector3>( pawn + SCHEMA( "C_CSPlayerPawn", "m_angEyeAngles"_hash ) );
					math::helpers::angle_vectors_left( enemy_eye_angles, &enemy_forward );
					const auto direction_to_us = ( eye_pos - enemy_eye_pos ).normalized( );
					threat_score -= std::clamp( enemy_forward.dot( direction_to_us ), -1.0f, 1.0f ) * 25.0f;

					if ( systems::g_tracing.is_visible( eye_pos, enemy_eye_pos, pawn, local.pawn ) )
					{
						threat_score -= 15.0f;
					}

					if ( threat_score < best_threat_score )
					{
						best_threat_score = threat_score;
						best_yaw = angle_to_enemy.y - base_yaw_offset;
					}
				}

				return best_threat_score < std::numeric_limits<float>::max( ) ? std::optional<float>{ best_yaw } : std::nullopt;
			};

		if ( const auto target_yaw = pick_target_yaw( ) )
		{
			base_yaw = *target_yaw;
		}

		auto indicator = base_yaw;
		if ( this->m_yaw_side == -1 )
		{
			indicator -= 90.0f;
		}
		else if ( this->m_yaw_side == 1 )
		{
			indicator += 90.0f;
		}

		// Apply yaw diversity based on mode
		auto yaw_mode = settings::g_combat.m_antiaim.yaw_mode.value;
		auto yaw = base_yaw;

		switch ( yaw_mode )
		{
		case settings::combat::antiaim::yaw_mode::jitter:
		{
			const auto jitter = settings::g_combat.m_antiaim.yaw_jitter.value;
			// Randomize yaw within jitter range
			const auto random_offset = ( ( rand( ) % 1000 ) / 1000.0f ) * 2.0f - 1.0f; // -1 to 1
			yaw += random_offset * jitter;
			indicator = yaw;
			break;
		}
		case settings::combat::antiaim::yaw_mode::random:
		{
			// Random yaw with configurable range
			const auto random_range = settings::g_combat.m_antiaim.yaw_random.value;
			static float last_random_yaw = 0.0f;
			static auto last_change_tick = 0;
			
			const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
			const auto current_tick = global_vars ? memory::read<int>( global_vars + 0x44 ) : 0;
			
			// Change random yaw every few ticks
			if ( current_tick - last_change_tick > 5 )
			{
				last_change_tick = current_tick;
				const auto random_offset = ( ( rand( ) % 1000 ) / 1000.0f ) * 2.0f - 1.0f;
				last_random_yaw = random_offset * random_range;
			}
			
			yaw += last_random_yaw;
			indicator = yaw;
			break;
		}
		case settings::combat::antiaim::yaw_mode::spam:
		{
			// Switch between jitter and spin every few ticks
			static bool use_jitter = true;
			static auto last_switch_tick = 0;
			
			const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
			const auto current_tick = global_vars ? memory::read<int>( global_vars + 0x44 ) : 0;
			
			if ( current_tick - last_switch_tick > 10 )
			{
				last_switch_tick = current_tick;
				use_jitter = !use_jitter;
			}
			
			if ( use_jitter )
			{
				const auto jitter = settings::g_combat.m_antiaim.yaw_jitter.value;
				const auto random_offset = ( ( rand( ) % 1000 ) / 1000.0f ) * 2.0f - 1.0f;
				yaw += random_offset * jitter;
			}
			else
			{
				const auto spin_speed = settings::g_combat.m_antiaim.yaw_spin_speed.value;
				const auto speed_scale = 15.0f;
				yaw += spin_speed * speed_scale;
			}
			indicator = yaw;
			break;
		}
		case settings::combat::antiaim::yaw_mode::fixed:
		default:
			// Keep base yaw
			break;
		}

		if ( spin != 0.0f )
		{
			yaw += spin;
			indicator = yaw;
		}

		if ( this->m_yaw_side == -1 )
		{
			yaw -= 90.0f;
		}
		else if ( this->m_yaw_side == 1 )
		{
			yaw += 90.0f;
		}

		if (settings::g_combat.m_antiaim.auto_yaw_adjust.value)
			yaw += 33.0f; // thx saphy

		this->m_indicator_yaw = indicator;

		return yaw;
	}

	void misc::antiaim::correct_movement (systems::input::usercmd* cmd) {
		if (!this->m_should_correct) {
			return;
		}

		this->m_should_correct = false;

		const auto base = cmd->csgo_user_cmd.mutable_base ();
		if (!base) {
			return;
		}

		const auto forward_move = base->forwardmove ();
		const auto side_move = base->leftmove ();

		if (std::abs (forward_move) < 0.01f && std::abs (side_move) < 0.01f) {
			return;
		}

		// Build both bases from yaw alone. Taking the full angles and flattening
		// them afterwards looks equivalent but is not: the anti-aim pitch sits at
		// +-89 degrees, where the forward vector is almost entirely vertical and
		// its horizontal part is about 0.017 long. Normalising that back up
		// amplifies whatever float error it carries, and the result is a movement
		// direction that is off by a degree or two -- the slight, constant skew
		// while simply walking. Movement is horizontal regardless of where the
		// player is looking, so pitch has no business in this basis at all.
		const auto horizontal_basis = [] ( float yaw, math::vector3& fwd, math::vector3& left ) {
			math::vector3 f {}, r {};
			math::helpers::angle_vectors_2d (yaw, f, r);
			fwd = f;
			left = math::vector3 { -r.x, -r.y, 0.0f };
		};

		math::vector3 new_forward {}, new_left {};
		horizontal_basis (this->m_modified_angles.y, new_forward, new_left);

		math::vector3 old_forward {}, old_left {};
		horizontal_basis (this->m_old_angles.y, old_forward, old_left);

		const auto intent = old_forward * forward_move + old_left * -side_move;
		const auto intent_len = intent.length ();

		if (intent_len < 0.01f) {
			return;
		}

		const auto intent_dir = intent / intent_len;
		const auto corrected_forward = new_forward.dot (intent_dir) * intent_len;
		const auto corrected_side = -new_left.dot (intent_dir) * intent_len;

		// Analog movement in CS2 is a unit range. The +-450 that used to be here
		// is the CS:GO speed-unit range and clamps nothing, but it says the code
		// was written against the old input model.
		base->set_forwardmove (std::clamp (corrected_forward, -1.0f, 1.0f));
		base->set_leftmove (std::clamp (corrected_side, -1.0f, 1.0f));

		// The base analog pair is only half the input. The game has already
		// written its own subtick move steps for this tick, and their analog
		// deltas were produced against the player's real view angles -- rotating
		// the base while leaving those alone leaves the two halves disagreeing,
		// which is the constant slight drift while simply walking.
		if (const auto subtick_moves = base->mutable_subtick_moves ()) {
			for (auto i = 0; i < base->subtick_moves_size (); ++i) {
				const auto step = base->mutable_subtick_moves (i);
				if (!step) {
					continue;
				}

				const auto step_fwd = step->analog_forward_delta ();
				const auto step_left = step->analog_left_delta ();

				if (std::abs (step_fwd) < 0.0001f && std::abs (step_left) < 0.0001f) {
					continue;
				}

				const auto step_intent = old_forward * step_fwd + old_left * -step_left;

				// Deliberately NOT quantized, unlike the base pair above.
				//
				// These are increments, not positions. A key going down produces
				// deltas of a tenth or two per step, and a half-unit threshold
				// rounds nearly all of them to nothing -- the axis stops receiving
				// input at all. Applied to both axes it reads as movement that has
				// gone stiff, most obviously forward, where a held key is exactly
				// this kind of small repeated increment.
				//
				// The threshold belongs to the normalised pair the server rounds,
				// and does not transfer to deltas just because the same convar
				// governs both.
				step->set_analog_forward_delta (std::clamp (new_forward.dot (step_intent), -1.0f, 1.0f));
				step->set_analog_left_delta (std::clamp (-new_left.dot (step_intent), -1.0f, 1.0f));
			}
		}

		// На земле пересобираем кнопки движения по знаку скорректированного движения,
		// чтобы движок не конфликтовал с оригинальными WASD-кнопками.
		if (systems::g_prediction.pre ().flags & cstypes::entity_flags::on_ground) {
			auto buttons = cmd->buttons.value;
			buttons &= ~static_cast<std::uintptr_t>(cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back | cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright);

			if (base->forwardmove () > 0.0f) {
				buttons |= cstypes::command_buttons::in_forward;
			} else if (base->forwardmove () < 0.0f) {
				buttons |= cstypes::command_buttons::in_back;
			}

			if (base->leftmove () > 0.0f) {
				buttons |= cstypes::command_buttons::in_moveleft;
			} else if (base->leftmove () < 0.0f) {
				buttons |= cstypes::command_buttons::in_moveright;
			}

			cmd->buttons.value = buttons;
		}
	}

	bool misc::antiaim::is_near_ladder( std::uintptr_t local_pawn ) const
	{
		return false; // ill do this lader.
	}

	namespace {

		math::vector3 quickpeek_ground_snap( std::uintptr_t skip_pawn, const math::vector3& feet_pos )
		{
			const auto start = math::vector3{ feet_pos.x, feet_pos.y, feet_pos.z + 64.0f };
			const auto end = math::vector3{ feet_pos.x, feet_pos.y, feet_pos.z - 8192.0f };
			const auto tr = systems::g_tracing.trace( start, end, skip_pawn );

			if ( tr.fraction <= 0.0f || tr.fraction >= 0.997f )
			{
				return feet_pos;
			}

			auto out = tr.position;
			out.z += 1.0f;
			return out;
		}

	} // namespace

	void misc::duckpeek::on_create_move( systems::input::usercmd* cmd )
	{
		this->m_fake_stand_active = false;

		if ( !cmd || !settings::g_combat.m_duckpeek.enabled.value )
		{
			this->m_was_active = false;
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.is_alive || !local.pawn || systems::g_local.is_in_cinematic( ) || systems::g_local.is_in_time_freeze( ) )
		{
			this->m_was_active = false;
			return;
		}

		this->m_was_active = true;

		if ( g_rage.should_release_duck_for_shot( ) )
		{
			cmd->buttons.value &= ~cstypes::command_buttons::in_duck;
			this->m_fake_stand_active = true;
			return;
		}

		cmd->buttons.value |= cstypes::command_buttons::in_duck;

		if ( g_rage.duckpeek_wants_reduck( ) )
		{
			g_rage.clear_duckpeek_reduck( );
		}
	}

	void misc::duckpeek::on_override_view( std::uintptr_t view_setup )
	{
		(void)view_setup;

		if ( !settings::g_combat.m_duckpeek.enabled.value )
		{
			this->m_was_active = false;
			this->m_fake_stand_active = false;
		}
	}

	void misc::quickpeek::on_create_move( systems::input::usercmd* cmd )
	{
		if ( !settings::g_combat.m_quickpeek.enabled.value )
		{
			this->reset( );
			return;
		}

		const auto local = systems::g_local.get( );
		const auto& ctx = g_shared.ctx( );

		if ( ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg )
		{
			this->reset( );
			return;
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		constexpr auto movement_cancel_mask = static_cast< std::uintptr_t >( cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back | cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright );
		const auto curr_movement_bits = cmd->buttons.value & movement_cancel_mask;

		const auto game_scene_node = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );

		if ( this->m_saved_origin.length_sqr( ) < 0.001f )
		{
			this->m_saved_origin = quickpeek_ground_snap( local.pawn, origin );
			this->m_should_retrack = false;
			this->m_fired = false;
			this->m_active = true;
			this->create_particle( );
			this->m_prev_movement_bits = curr_movement_bits;
			return;
		}

		this->update_particle( );

		const auto distance = ( origin - this->m_saved_origin ).length_2d( );

		if ( this->m_should_retrack && ( curr_movement_bits & ~this->m_prev_movement_bits ) != 0 )
		{
			this->m_should_retrack = false;
		}

		if ( this->m_should_retrack && ( systems::g_prediction.pre( ).flags & cstypes::entity_flags::on_ground ) )
		{
			const auto velocity = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
			const auto speed = velocity.length_2d( );

			if ( distance < 5.0f && speed < 15.0f )
			{
				this->m_should_retrack = false;
				this->m_fired = false;
			}
			else if ( distance < speed * 0.1f && speed > 15.0f )
			{
				const auto vel_angle = math::helpers::vector_to_angle( velocity * -1.0f );
				const auto yaw_diff = math::helpers::deg_to_rad( base->viewangles( )->y( ) - vel_angle.y );

				base->set_forwardmove( std::cosf( yaw_diff ) );
				base->set_leftmove( -std::sinf( yaw_diff ) );

				auto buttons = cmd->buttons.value;
				buttons &= ~static_cast< std::uintptr_t >( cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back | cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright );

				if ( base->forwardmove( ) > 0.0f )
				{
					buttons |= cstypes::command_buttons::in_forward;
				}
				else if ( base->forwardmove( ) < 0.0f )
				{
					buttons |= cstypes::command_buttons::in_back;
				}

				if ( base->leftmove( ) > 0.0f )
				{
					buttons |= cstypes::command_buttons::in_moveleft;
				}
				else if ( base->leftmove( ) < 0.0f )
				{
					buttons |= cstypes::command_buttons::in_moveright;
				}

				cmd->buttons.value = buttons;
			}
			else
			{
				const auto diff = this->m_saved_origin - origin;
				const auto angle_to_pos = math::helpers::vector_to_angle( diff );
				const auto yaw_diff = math::helpers::deg_to_rad( base->viewangles( )->y( ) - angle_to_pos.y );

				base->set_forwardmove( std::cosf( yaw_diff ) );
				base->set_leftmove( -std::sinf( yaw_diff ) );

				auto buttons = cmd->buttons.value;
				buttons &= ~static_cast< std::uintptr_t >( cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back | cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright );

				if ( base->forwardmove( ) > 0.0f )
				{
					buttons |= cstypes::command_buttons::in_forward;
				}
				else if ( base->forwardmove( ) < 0.0f )
				{
					buttons |= cstypes::command_buttons::in_back;
				}

				if ( base->leftmove( ) > 0.0f )
				{
					buttons |= cstypes::command_buttons::in_moveleft;
				}
				else if ( base->leftmove( ) < 0.0f )
				{
					buttons |= cstypes::command_buttons::in_moveright;
				}

				cmd->buttons.value = buttons;
			}
		}

		// attack2 counts too: the revolver's quick shot is a real shot, and
		// autopeek has to retrack after it like any other.
		constexpr auto fired_bits = cstypes::command_buttons::in_attack | cstypes::command_buttons::in_second_attack;
		if ( ( cmd->buttons.value & fired_bits ) && !g_rage.is_cocking_revolver( ) )
		{
			this->m_should_retrack = true;
			this->m_fired = true;
		}

		this->m_prev_movement_bits = curr_movement_bits;
	}

	void misc::quickpeek::reset_if_needed( )
	{
		if ( !this->m_active )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.is_alive || !local.pawn )
		{
			this->reset( );
			return;
		}

		if ( !settings::g_combat.m_quickpeek.enabled.value )
		{
			this->reset( );
		}
	}

	void misc::quickpeek::create_particle( )
	{
		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		if ( !particle_manager )
		{
			return;
		}

		constexpr auto particle_path{ "particles/embedded/halo.vpcf" };

		if ( !this->m_particle_loaded )
		{
			struct buffer_string
			{
				std::uint32_t m_unknown1{};
				std::uint32_t m_unknown2{ 0xc00000c8 };

				union
				{
					std::uintptr_t m_str_ptr;
					std::uint8_t data[ 0xc8 ];
				};

				std::uintptr_t m_unknown3{ 0 };
				std::uintptr_t m_unknown4{ 0 };
			} buffer;

			memory::call<void>(PATTERN (patterns::init_particle_path_buffer), &buffer, particle_path );
			buffer.m_unknown4 = 'fcpv';
			memory::call<void>(PATTERN (patterns::resource_system_precache), addresses::globals::resource_system, &buffer, "" );

			this->m_particle_loaded = true;
		}

		auto effect_index{ invalid_effect_index };
		memory::call<int*>(PATTERN (patterns::particle_create_effect), particle_manager, &effect_index, particle_path, 8, 0ll, 0ll, 0ll, 0 );

		this->m_particle_effect = effect_index;

		if ( effect_index == invalid_effect_index )
		{
			return;
		}

		memory::call<bool>(PATTERN (patterns::particle_set_control_point), particle_manager, effect_index, 0, &this->m_saved_origin, 0 );
	}

	void misc::quickpeek::update_particle( )
	{
		if ( this->m_particle_effect == invalid_effect_index )
		{
			return;
		}

		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		if ( !particle_manager )
		{
			return;
		}

		const auto& cfg = settings::g_combat.m_quickpeek;
		const auto& col = this->m_should_retrack ? cfg.retrack_color : cfg.color;
		const auto color = math::vector3{ static_cast< float >( col.value.r ), static_cast< float >( col.value.g ), static_cast< float >( col.value.b ) };

		memory::call<bool>(PATTERN (patterns::particle_set_control_point), particle_manager, this->m_particle_effect, 1, &color, 0 );
		memory::call<bool>(PATTERN (patterns::particle_set_control_point), particle_manager, this->m_particle_effect, 0, &this->m_saved_origin, 0 );
	}

	void misc::quickpeek::release_particle( )
	{
		if ( this->m_particle_effect == invalid_effect_index )
		{
			return;
		}

		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		if ( particle_manager )
		{
			memory::call<void>(PATTERN (patterns::particle_destroy_effect), particle_manager, this->m_particle_effect, true, true );
		}

		this->m_particle_effect = invalid_effect_index;
	}

	void misc::quickpeek::reset( )
	{
		this->release_particle( );
		this->m_saved_origin = {};
		this->m_should_retrack = false;
		this->m_fired = false;
		this->m_active = false;
		this->m_prev_movement_bits = 0;
	}

	void misc::autostop::on_create_move( systems::input::usercmd* cmd )
	{
		if ( !settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type ).auto_stop.value )
		{
			return;
		}

		if ( !features::combat::g_rage.should_stop( ) )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		const auto base = cmd->csgo_user_cmd.mutable_base( );
		const auto& prestate = systems::g_prediction.pre( );
		const auto& ctx = g_shared.ctx( );

		if ( !( prestate.flags & cstypes::entity_flags::on_ground ) )
		{
			return;
		}

		auto velocity = prestate.networked_velocity;
		auto speed = velocity.length_2d( );

		if ( speed <= 1.0f )
		{
			return;
		}

		const auto sv_friction = CONVAR ("sv_friction")->get<float>( );
		const auto sv_stopspeed = CONVAR ("sv_stopspeed")->get<float>( );
		const auto surface_friction = prestate.surface_friction;

		const auto control = std::fmaxf( speed, sv_stopspeed );
		const auto drop = control * sv_friction * surface_friction * cstypes::tick_interval;
		const auto post_friction = std::fmaxf( speed - drop, 0.0f );

		if ( post_friction > 0.0f )
		{
			velocity *= ( post_friction / speed );
			speed = post_friction;
		}
		else
		{
			base->set_forwardmove( 0.0f );
			base->set_leftmove( 0.0f );
			return;
		}

		if ( speed < 2.0f )
		{
			base->set_forwardmove( 0.0f );
			base->set_leftmove( 0.0f );
			return;
		}

		auto accel = CONVAR ("sv_accelerate")->get<float>( );
		const auto accel_base = this->get_effective_accel_base( local.pawn, movement_services, prestate.flags, ctx.weapon_max_speed );

		if ( ctx.is_scoped )
		{
			const auto weapon_ratio = std::fminf( 1.0f, ctx.weapon_max_speed / 250.0f );
			const auto v20 = std::fmaxf( 250.0f, memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) ) ) * weapon_ratio;
			const auto scoped_max = v20 * 0.52f;

			if ( speed > scoped_max - 5.0f )
			{
				const auto t = 1.0f - std::fmaxf( 0.0f, speed - ( scoped_max - 5.0f ) ) / std::fmaxf( 0.01f, 5.0f );
				accel *= std::clamp( t, 0.0f, 1.0f );
			}
		}

		const auto wish_x = -velocity.x / speed;
		const auto wish_y = -velocity.y / speed;
		const auto accel_speed = std::fminf( accel * accel_base * surface_friction * cstypes::tick_interval, speed );

		velocity.x += wish_x * accel_speed;
		velocity.y += wish_y * accel_speed;

		// Full analog magnitude, not scaled by speed/max_speed: that taper made the
		// *sent* command weaker precisely as speed (and therefore the internal
		// velocity sim above) approached zero, while the internal sim itself always
		// used the full wish direction. The real server-side deceleration this
		// produces is then slower than what was simulated, so the last ~20-30 u/s
		// never quite converge -- it reads as the stop "floating" instead of
		// snapping. The speed<2.0f cutoff above already zeroes the command outright
		// once we're effectively stopped, so there's no need to taper the approach.
		const auto yaw_rad = base->viewangles( )->y( ) * ( std::numbers::pi_v<float> / 180.0f );
		const auto sy = std::sinf( yaw_rad );
		const auto cy = std::cosf( yaw_rad );

		const auto forward_move = std::clamp( wish_x * cy + wish_y * sy, -1.0f, 1.0f );
		const auto left_move = std::clamp( ( wish_x * sy - wish_y * cy ) * -1.0f, -1.0f, 1.0f );

		base->set_forwardmove( forward_move );
		base->set_leftmove( left_move );

		const auto subtick_moves = base->mutable_subtick_moves( );
		if ( subtick_moves )
		{
			const auto step = systems::g_input.acquire_subtick_step( subtick_moves );
			if ( step )
			{
				step->set_button( 0 );
				step->set_pressed( false );
				step->set_when( 0.0f );
				step->set_analog_forward_delta( forward_move - prestate.last_movement_impulses.x );
				step->set_analog_left_delta( left_move - prestate.last_movement_impulses.y );
			}
		}

		// Clear the previous forward/back/left/right bits before setting the new
		// ones. Without this, braking against held movement (e.g. releasing W into
		// a back-brake) OR'd in_back on top of the still-set in_forward, sending
		// the server a command with both pressed at once -- Source 2 reads that as
		// conflicting input each tick, which is exactly the micro-stutter/dither
		// this was meant to smooth out.
		cmd->buttons.value &= ~static_cast< std::uintptr_t >(
			cstypes::command_buttons::in_forward |
			cstypes::command_buttons::in_back |
			cstypes::command_buttons::in_moveleft |
			cstypes::command_buttons::in_moveright
		);

		if ( forward_move > 0.0f )
		{
			cmd->buttons.value |= cstypes::command_buttons::in_forward;
		}
		else if ( forward_move < 0.0f )
		{
			cmd->buttons.value |= cstypes::command_buttons::in_back;
		}

		if ( left_move > 0.0f )
		{
			cmd->buttons.value |= cstypes::command_buttons::in_moveleft;
		}
		else if ( left_move < 0.0f )
		{
			cmd->buttons.value |= cstypes::command_buttons::in_moveright;
		}
	}

	float misc::autostop::get_effective_accel_base( std::uintptr_t local_pawn, std::uintptr_t movement_services, std::uint32_t flags, float max_weapon_speed ) const
	{
		const auto max_speed_base = memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) );
		const auto is_ducked = ( flags & 4 ) != 0;
		const auto ducking_state = memory::read<bool>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_bDucking"_hash ) );
		const auto is_scoped = g_shared.ctx( ).is_scoped;
		const auto is_ducking = is_ducked || ducking_state;
		const auto v19 = std::fmaxf( 250.0f, max_speed_base );

		auto friction_scale{ 1.0f };

		if (CONVAR ("sv_accelerate_use_weapon_speed")->get<bool>( ) )
		{
			const auto weapon_ratio = std::fminf( 1.0f, max_weapon_speed / 250.0f );

			if ( !is_ducking && !is_scoped )
			{
				friction_scale = weapon_ratio;
			}
		}

		if ( is_ducking )
		{
			friction_scale = std::fminf( 0.34f, friction_scale );
		}

		auto accel_base = v19 * friction_scale;

		if ( is_scoped && !is_ducking )
		{
			accel_base *= 0.52f;
		}

		return accel_base;
	}

} // namespace features::combat
