#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

namespace features::movement {

	void airstrafe::on_create_move( systems::input::usercmd* cmd )
	{
		this->m_handled_this_tick = false;

		if ( features::movement::g_jumpbug.active_this_tick( ) )
		{
			return;
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}

		const auto current_buttons = cmd->buttons.value;
		const bool shift_held = current_buttons & static_cast< std::uintptr_t >( cstypes::command_buttons::in_sprint );

		const auto& prestate = systems::g_prediction.pre( );
		const bool in_air = !( prestate.flags & cstypes::entity_flags::on_ground );

		if ( shift_held && in_air )
		{
			const auto& vel = prestate.networked_velocity;
			float forward_move = 0.0f;
			float left_move = 0.0f;

			if ( vel.length_2d( ) > 10.0f )
			{
				const auto vel_yaw = std::atan2f( vel.y, vel.x )
					* ( 180.0f / std::numbers::pi_v<float> );

				const auto view_yaw = base->viewangles( )->y( );
				const auto relative_yaw = ( vel_yaw - view_yaw )
					* ( std::numbers::pi_v<float> / 180.0f );

				const auto fwd_x = std::cosf( relative_yaw );
				const auto fwd_y = std::sinf( relative_yaw );

				forward_move = std::clamp( -fwd_x, -1.0f, 1.0f );
				left_move = std::clamp( -fwd_y, -1.0f, 1.0f ); // negated
			}

			base->set_forwardmove( forward_move );
			base->set_leftmove( left_move );

			// write subtick entries so the engine respects the input precisely
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
			return;
		}

		const auto wants_stop = features::combat::g_rage.should_stop( )
			|| features::misc::g_projectile_trajectory.should_stop( );

		// split out of the old (!a && !b) || c condition so the precedence is
		// explicit rather than something to re-derive on every read.
		if ( features::combat::g_rage.is_firing_this_tick( ) )
		{
			return;
		}

		if ( !settings::g_movement.airstrafe.value && !wants_stop )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.pawn )
		{
			return;
		}

		const auto move_type = memory::read<std::uint8_t>( local.pawn + SCHEMA( "CBaseEntity", "m_nActualMoveType"_hash ) );
		if ( move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip )
		{
			return;
		}

		// test_strafer only runs under sv_quantize_movement_input 1, so on
		// quantize 0 this is already false. The !wants_stop guard matters anyway:
		// test_strafer never brakes, so handing it a stop tick would drop the
		// counter-strafe entirely.
		if ( !wants_stop
			&& settings::g_movement.m_test_strafer.enabled.value
			&& features::movement::g_test_strafer.handled_this_tick( ) )
		{
			return;
		}

		if ( !wants_stop && features::movement::g_valve_strafer.handled_this_tick( ) )
		{
			return;
		}

		if ( prestate.flags & cstypes::entity_flags::on_ground )
		{
			return;
		}

		if ( current_buttons & static_cast< std::uintptr_t >( cstypes::command_buttons::in_sprint ) )
		{
			return;
		}

		const auto subtick_moves = base->mutable_subtick_moves( );
		if ( !subtick_moves )
		{
			return;
		}

		if ( !wants_stop )
		{
			this->check_button( current_buttons, cstypes::command_buttons::in_moveleft );
			this->check_button( current_buttons, cstypes::command_buttons::in_moveright );
			this->check_button( current_buttons, cstypes::command_buttons::in_forward );
			this->check_button( current_buttons, cstypes::command_buttons::in_back );
			this->m_last_buttons = current_buttons;
		}

		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		if ( !movement_services )
		{
			return;
		}

		const auto sv_airaccelerate = CONVAR ("sv_airaccelerate")->get<float>( );
		const auto sv_air_max_wishspeed = CONVAR ("sv_air_max_wishspeed")->get<float>( );
		const auto sv_gravity = CONVAR ("sv_gravity")->get<float>( );
		const auto sv_staminarecoveryrate = CONVAR ("sv_staminarecoveryrate")->get<float>( );

		const auto view_angles = this->m_angles;
		const auto cmd_move_backup = math::vector3{ base->forwardmove( ), base->leftmove( ), 0.0f };
		const auto effective_maxspeed = memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) );

		constexpr int subtick_count = 32;
		constexpr float frame_time = cstypes::tick_interval / static_cast< float >( subtick_count );

		auto yaw_offset = 0.0f;

		if ( !wants_stop )
		{
			if ( this->m_last_pressed & cstypes::command_buttons::in_moveleft )
			{
				yaw_offset += 90.0f;
			}

			if ( this->m_last_pressed & cstypes::command_buttons::in_moveright )
			{
				yaw_offset -= 90.0f;
			}

			if ( this->m_last_pressed & cstypes::command_buttons::in_forward )
			{
				yaw_offset *= 0.5f;
			}
			else if ( this->m_last_pressed & cstypes::command_buttons::in_back )
			{
				yaw_offset = -yaw_offset * 0.5f + 180.0f;
			}
		}

		const auto has_direction_input = ( this->m_last_pressed & cstypes::command_buttons::in_moveleft ) || ( this->m_last_pressed & cstypes::command_buttons::in_moveright ) || ( this->m_last_pressed & cstypes::command_buttons::in_forward ) || ( this->m_last_pressed & cstypes::command_buttons::in_back );
		const auto effective_wants_stop = wants_stop || ( settings::g_movement.airstrafe_fully_directional.value && !has_direction_input );

		auto velocity = prestate.networked_velocity;
		auto last_impulses = prestate.last_movement_impulses;
		auto stamina = prestate.stamina;
		const auto surface_friction = prestate.surface_friction;

		// Seed the stop before the loop. last_impulses otherwise still carries the
		// previous tick's real WASD (usually forward), so the first iteration would
		// build its wish_dir from the held key and simulate one more subtick of
		// acceleration before rotate_to_stop ever ran. The digital movement bits go
		// with it: a physically-held key keeps its bit set in cmd->buttons.value no
		// matter what the analog values say, and would drive a full-speed wish
		// alongside the counter-strafe.
		if ( effective_wants_stop )
		{
			this->rotate_to_stop( base, velocity );
			last_impulses.x = base->forwardmove( );
			last_impulses.y = base->leftmove( );

			cmd->buttons.value &= ~static_cast< std::uintptr_t >(
				cstypes::command_buttons::in_forward
				| cstypes::command_buttons::in_back
				| cstypes::command_buttons::in_moveleft
				| cstypes::command_buttons::in_moveright );

			if ( velocity.length_2d( ) <= 10.0f )
			{
				return;
			}
		}

		if ( last_impulses.y < 0.0f )
		{
			this->m_side_switch = false;
		}
		else if ( last_impulses.y > 0.0f )
		{
			this->m_side_switch = true;
		}

		for ( auto i = 0; i < subtick_count; ++i )
		{
			// Restoring the held WASD unconditionally overwrote the braking values
			// every single iteration, so each subtick fought the previous one and
			// the stop came out as a stutter instead of a stop.
			if ( !effective_wants_stop )
			{
				base->set_forwardmove( cmd_move_backup.x );
				base->set_leftmove( cmd_move_backup.y );
			}

			auto speed_2d = std::sqrtf( velocity.x * velocity.x + velocity.y * velocity.y );

			if ( stamina > 0.0f )
			{
				const auto speed_scale = std::clamp( 1.0f - ( stamina / 100.0f ), 0.0f, 1.0f );
				speed_2d *= speed_scale * speed_scale;
				stamina = std::fmaxf( stamina - frame_time * sv_staminarecoveryrate, 0.0f );
			}

			// Half-step gravity either side of the acceleration, the way the engine
			// integrates it. The old pair took a full step down and then added half
			// of it back, which is neither half-step nor a full one.
			velocity.z -= sv_gravity * frame_time * 0.5f;

			if ( speed_2d > 0.0001f )
			{
				math::vector3 forward_dir{}, right_dir{};
				math::helpers::angle_vectors_2d( base->viewangles( )->y( ), forward_dir, right_dir );

				math::vector3 wish_dir
				{
					forward_dir.x * last_impulses.x * effective_maxspeed + right_dir.x * last_impulses.y * effective_maxspeed,
					forward_dir.y * last_impulses.x * effective_maxspeed + right_dir.y * last_impulses.y * effective_maxspeed,
					0.0f
				};

				auto wish_speed = std::sqrtf( wish_dir.x * wish_dir.x + wish_dir.y * wish_dir.y );
				if ( wish_speed > 0.0001f )
				{
					wish_dir.x /= wish_speed;
					wish_dir.y /= wish_speed;
					wish_dir.z = 0.0f;
				}

				wish_speed = std::fminf( wish_speed, effective_maxspeed );

				const auto capped_wish = std::fminf( wish_speed, sv_air_max_wishspeed );
				const auto current_speed = velocity.x * wish_dir.x + velocity.y * wish_dir.y;
				const auto add_speed = capped_wish - current_speed;

				if ( add_speed > 0.0f )
				{
					// AirAccelerate grants the whole accel_speed, not half of it.
					// Halving here made every simulated subtick gain half the speed
					// the server actually applies, so the 32-step curve ran behind
					// reality and the strafe angles below were solved for a speed we
					// never had -- that is the missing half of the boost.
					const auto accel_speed = sv_airaccelerate * effective_maxspeed * frame_time * surface_friction;
					const auto gain = std::fminf( accel_speed, add_speed );

					velocity.x += wish_dir.x * gain;
					velocity.y += wish_dir.y * gain;
				}
			}

			velocity.z -= sv_gravity * frame_time * 0.5f;
			speed_2d = std::sqrtf( velocity.x * velocity.x + velocity.y * velocity.y );

			// Start steering earlier than the old 10.0f gate, which left the first
			// part of every jump running on raw WASD before the ideal angle kicked in.
			const auto min_strafe_speed = std::max( settings::g_movement.airstrafe_min_speed.value, 0.1f );
			if ( speed_2d >= min_strafe_speed )
			{
				this->m_handled_this_tick = true;
				base->set_forwardmove( 0.0f );
				base->set_leftmove( 0.0f );

				if ( effective_wants_stop )
				{
					this->rotate_to_stop( base, velocity );
				}
				else
				{
					const auto velocity_angle = std::atan2f( velocity.y, velocity.x ) * ( 180.0f / std::numbers::pi_v<float> );
					const auto accel_speed = sv_airaccelerate * effective_maxspeed * frame_time * surface_friction;
					// Solve the angle against the same full accel_speed the gain above
					// applies; the floor covers the case where accel alone already
					// exceeds air_max_wishspeed.
					const auto optimal_floor = std::fmaxf( accel_speed * 0.5f, sv_air_max_wishspeed - accel_speed );
					const auto max_angle = std::clamp( settings::g_movement.airstrafe_max_angle.value, 5.0f, 80.0f );
					const auto attack_speed = std::max( settings::g_movement.airstrafe_attack_speed.value, 1.0f );
					const auto ideal_angle = std::clamp( std::atanf( optimal_floor / speed_2d ) * ( 180.0f / std::numbers::pi_v<float> ), 0.0f, max_angle );

					auto target_yaw = view_angles.y + yaw_offset;
					math::helpers::normalize_angle( target_yaw );

					auto velocity_delta = target_yaw - velocity_angle;
					math::helpers::normalize_angle( velocity_delta );

					// Порог агрессивного доворота -- настраиваемый: ниже него
					// стрейфер ведёт себя мягко и просто держит идеальный угол,
					// чтобы разгон на старте не дёргался.
					if ( ( std::fabsf( velocity_delta ) > 170.0f && speed_2d > attack_speed )
						|| ( velocity_delta > ideal_angle && speed_2d > attack_speed ) )
					{
						target_yaw = velocity_angle + ideal_angle;
						base->set_leftmove( -1.0f );
					}
					else if ( -ideal_angle <= velocity_delta || speed_2d <= attack_speed )
					{
						if ( this->m_side_switch )
						{
							target_yaw -= ideal_angle;
							base->set_leftmove( -1.0f );
						}
						else
						{
							target_yaw += ideal_angle;
							base->set_leftmove( 1.0f );
						}
					}
					else
					{
						target_yaw = velocity_angle - ideal_angle;
						base->set_leftmove( 1.0f );
					}

					math::helpers::normalize_angle( target_yaw );

					this->rotate_movement( base, target_yaw, base->viewangles( )->y( ) );
				}
			}

			const auto step = systems::g_input.acquire_subtick_step( subtick_moves );
			if ( !step )
			{
				continue;
			}

			step->set_button( 0 );
			step->set_pressed( false );
			step->set_when( static_cast< float >( i ) / static_cast< float >( subtick_count ) );
			step->set_analog_forward_delta( base->forwardmove( ) - last_impulses.x );
			step->set_analog_left_delta( base->leftmove( ) - last_impulses.y );

			last_impulses.x = base->forwardmove( );
			last_impulses.y = base->leftmove( );

			if ( !effective_wants_stop )
			{
				this->m_side_switch = !this->m_side_switch;
			}
		}
	}

	void airstrafe::store_angles( )
	{
		this->m_angles = systems::g_input.get_view_angles( );
	}

	void airstrafe::check_button( std::uintptr_t current_buttons, std::uintptr_t button )
	{
		constexpr auto moveleft = static_cast< std::uintptr_t >( cstypes::command_buttons::in_moveleft );
		constexpr auto moveright = static_cast< std::uintptr_t >( cstypes::command_buttons::in_moveright );
		constexpr auto forward = static_cast< std::uintptr_t >( cstypes::command_buttons::in_forward );
		constexpr auto back = static_cast< std::uintptr_t >( cstypes::command_buttons::in_back );

		if ( current_buttons & button && ( !( this->m_last_buttons & button ) || ( button & moveleft && !( this->m_last_pressed & moveright ) ) || ( button & moveright && !( this->m_last_pressed & moveleft ) ) || ( button & forward && !( this->m_last_pressed & back ) ) || ( button & back && !( this->m_last_pressed & forward ) ) ) )
		{
			if ( button & moveleft )
			{
				this->m_last_pressed &= ~moveright;
			}
			else if ( button & moveright )
			{
				this->m_last_pressed &= ~moveleft;
			}
			else if ( button & forward )
			{
				this->m_last_pressed &= ~back;
			}
			else if ( button & back )
			{
				this->m_last_pressed &= ~forward;
			}

			this->m_last_pressed |= button;
		}
		else if ( !( current_buttons & button ) )
		{
			this->m_last_pressed &= ~button;
		}
	}

	void airstrafe::rotate_movement( proto::base_usercmd_pb* base, float target_yaw, float view_yaw ) const
	{
		const auto forward_move = base->forwardmove( );
		const auto side_move = base->leftmove( );

		math::vector3 target_forward{}, target_right{};
		math::helpers::angle_vectors_2d( target_yaw, target_forward, target_right );

		math::vector3 view_forward{}, view_right{};
		math::helpers::angle_vectors_2d( view_yaw, view_forward, view_right );

		const auto tf = target_forward * forward_move;
		const auto tr = target_right * side_move;

		const auto corrected_forward = view_forward.dot( tf ) + view_forward.dot( tr );
		const auto corrected_side = view_right.dot( tf ) + view_right.dot( tr );

		base->set_forwardmove( std::clamp( -corrected_forward, -1.0f, 1.0f ) );
		base->set_leftmove( std::clamp( -corrected_side, -1.0f, 1.0f ) );
	}

	void airstrafe::rotate_to_stop( proto::base_usercmd_pb* base, const math::vector3& velocity ) const
	{
		const auto speed = velocity.length_2d( );

		if ( speed < 0.1f )
		{
			base->set_forwardmove( 0.0f );
			base->set_leftmove( 0.0f );
			return;
		}

		const auto wish_yaw = std::atan2f( velocity.y, velocity.x ) * ( 180.0f / std::numbers::pi_v<float> ) + 180.0f;

		{
			const auto& ctx = features::combat::g_shared.ctx( );
			const auto max_speed = ( ctx.valid && ctx.weapon_vdata ) ? memory::read<float>( ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flMaxSpeed"_hash ) ) : 250.0f;

			// speed / max_speed alone commands a hard counter-strafe right down to a
			// standstill, which reads as slamming the brakes. Ease off once we are
			// near accurate-fire speed so the last units bleed away instead.
			//
			// Порог тоже выведен из настройки: smoother == больше порог ==
			// раньше начинаем отпускать тормоз, поэтому сброс скорости выходит
			// плавным, а не ударом в ноль.
			const auto smooth = std::clamp( settings::g_movement.airstrafe_stop_smoothness.value, 0.0f, 1.0f );
			const auto accurate = max_speed * 0.34f;
			const auto threshold = accurate * ( 1.0f + smooth * 0.7f );

			auto brake = 1.0f;
			if ( speed <= threshold )
			{
				const auto floor = 0.15f - smooth * 0.13f;
				brake = std::clamp( speed / threshold, floor, 0.85f );
			}

			base->set_forwardmove( brake );
			base->set_leftmove( 0.0f );
		}

		const auto rotation = ( base->viewangles( )->y( ) - wish_yaw ) * ( std::numbers::pi_v<float> / 180.0f );
		const auto fwd = base->forwardmove( );
		const auto side = base->leftmove( );

		base->set_forwardmove( std::clamp( std::cosf( rotation ) * fwd - std::sinf( rotation ) * side, -1.0f, 1.0f ) );
		base->set_leftmove( std::clamp( ( std::sinf( rotation ) * fwd + std::cosf( rotation ) * side ) * -1.0f, -1.0f, 1.0f ) );
	}

} // namespace features::movement
