#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

// Debug logging control (task 6.4)
#ifdef _DEBUG
	#define EXTRAP_DEBUG_LOG(...) logging::console::print( __VA_ARGS__ )
#else
	#define EXTRAP_DEBUG_LOG(...) ( ( void )0 )
#endif

	namespace {
		// Named constants for extrapolation (mirror ragebot values where relevant)
		constexpr float       k_pi                    = 3.14159265f;
		constexpr float       k_direction_decay      = 0.8f;
		constexpr float       k_max_velocity          = 3500.0f;
		constexpr std::uint64_t k_trace_mask_movement = 0x1c3003;
		constexpr int         k_collision_group_player = 4;
		constexpr float       k_standable_normal      = 0.7f;
		constexpr int         k_tick_state_vfunc_idx  = 23;
		constexpr std::size_t k_tick_state_server_tick_offset = 892;
	} // namespace

namespace features::combat {

	void shared::lagcomp::predict_movement( extrapolation_data& data, std::uintptr_t skip_entity ) const
	{
		if ( !addresses::globals::game_trace_manager )
		{
			return;
		}

		const auto sv_gravity = data.sv_gravity;
		const auto sv_friction = data.sv_friction;
		const auto sv_stopspeed = data.sv_stopspeed;

		if ( data.flags & cstypes::entity_flags::on_ground )
		{
			data.velocity.z = 0.0f;
			
			// Apply Source engine ground friction
			const auto speed_2d = std::sqrtf( data.velocity.x * data.velocity.x + data.velocity.y * data.velocity.y );
			if ( speed_2d > 0.0f )
			{
				const auto control = std::max( speed_2d, sv_stopspeed );
				const auto drop = control * sv_friction * data.surface_friction * cstypes::tick_interval;
				const auto new_speed = std::max( speed_2d - drop, 0.0f );
				
				if ( new_speed > 0.0f )
				{
					const auto scale = new_speed / speed_2d;
					data.velocity.x *= scale;
					data.velocity.y *= scale;
				}
				else
				{
					data.velocity.x = 0.0f;
					data.velocity.y = 0.0f;
				}
			}

			// FIX (E): apply ground acceleration to compensate friction so a running
			// target keeps a realistic speed instead of always decelerating.
			{
				const auto speed_after = std::sqrtf( data.velocity.x * data.velocity.x + data.velocity.y * data.velocity.y );
				if ( speed_after > 1.0f )
				{
					// Adaptive wish speed: preserve the target's current speed instead
					// of forcing 250 (wrong for scoped/slow movement).
					auto wish_speed = std::clamp( speed_after, 50.0f, 260.0f );
					if ( speed_after < wish_speed )
					{
						const auto sv_accelerate = CONVAR( "sv_accelerate" )->get<float>( );
						const auto add_speed = wish_speed - speed_after;
						auto accel_speed = sv_accelerate * wish_speed * data.surface_friction * cstypes::tick_interval;
						accel_speed = std::min( accel_speed, add_speed );
						const auto dir_x = data.velocity.x / speed_after;
						const auto dir_y = data.velocity.y / speed_after;
						data.velocity.x += dir_x * accel_speed;
						data.velocity.y += dir_y * accel_speed;
					}
				}
			}
		}
		else
		{
			data.velocity.z -= sv_gravity * cstypes::tick_interval;
		}

		const auto move_end = data.origin + data.velocity * cstypes::tick_interval;

		// Экстраполяция вызывается на КАЖДУЮ запись каждого кандидата -- то есть
		// десятки раз за тик на одну цель, и каждая итерация делает 2-6 hull-трасс
		// (bump-цикл + ground). Фильтр здесь постоянен по (пешка, маска, слой),
		// поэтому кэшируем его и идём по горячему пути трассы.
		thread_local std::uintptr_t extrap_filter_pawn{ 0 };
		thread_local systems::tracing::filter extrap_filter{};

		if ( extrap_filter_pawn != skip_entity || extrap_filter.vtable == 0 )
		{
			extrap_filter = systems::g_tracing.make_filter_cached( skip_entity, k_trace_mask_movement, k_collision_group_player );
			extrap_filter_pawn = skip_entity;
		}

		auto trace_result = systems::g_tracing.trace_hull_fast(
			data.origin, move_end,
			data.obb_mins, data.obb_maxs,
			extrap_filter
		);

		if ( trace_result.fraction != 1.0f )
		{
			EXTRAP_DEBUG_LOG( xs( "[extrap] predict_movement: wall hit (frac {:.2f}, normal {:.2f} {:.2f} {:.2f})\n" ),
				trace_result.fraction, trace_result.normal.x, trace_result.normal.y, trace_result.normal.z );

			// Correct Source-engine wall clip with remaining time (max 4 bumps)
			auto time_left = cstypes::tick_interval;

			for ( auto bump = 0; bump < 4; ++bump )
			{
				if ( time_left <= 0.0f )
					break;

				if ( trace_result.fraction > 0.0f )
					data.origin = trace_result.end_pos;

				if ( trace_result.fraction == 1.0f )
					break;

				time_left *= ( 1.0f - trace_result.fraction );

				// Clip velocity along the surface normal
				const auto dot = data.velocity.dot( trace_result.normal );
				if ( dot < 0.0f )
					data.velocity -= trace_result.normal * dot;

				const auto clip_end = data.origin + data.velocity * time_left;

				trace_result = systems::g_tracing.trace_hull_fast(
					data.origin, clip_end,
					data.obb_mins, data.obb_maxs,
					extrap_filter
				);
			}
		}

		// The final trace may start at a collision point. Its end position is the
		// clipped destination even when that follow-up trace completes fully.
		data.origin = trace_result.end_pos;

		const auto ground_end = math::vector3{ data.origin.x, data.origin.y, data.origin.z - 2.0f };
		const auto ground_trace = systems::g_tracing.trace_hull_fast(
			data.origin, ground_end,
			data.obb_mins, data.obb_maxs,
			extrap_filter
		);

		data.flags &= ~cstypes::entity_flags::on_ground;

		if ( ground_trace.fraction != 1.0f && ground_trace.normal.z > k_standable_normal )
		{
			data.flags |= cstypes::entity_flags::on_ground;
		}
	}

	std::optional<shared::lagcomp::record> shared::lagcomp::extrapolate( std::uintptr_t pawn )
	{
		if ( !settings::g_combat.m_lagcomp.extrapolation.value )
		{
			return std::nullopt;
		}

		// BUG 19: copy the records we need out of the lock instead of holding it
		// across the (potentially heavy) physics traces below.
		record latest_copy {};
		std::optional<record> prev_copy {};
		{
			std::shared_lock records_lock( this->m_records_mtx );

			auto it = this->m_records.find( pawn );
			if ( it == this->m_records.end( ) || it->second.empty( ) )
			{
				return std::nullopt;
			}

			latest_copy = it->second.front( );
			if ( it->second.size( ) > 1 && it->second[ 1 ].valid )
				prev_copy = it->second[ 1 ];
		}

		if ( !latest_copy.is_valid( ) )
		{
			return std::nullopt;
		}

		const auto net_client = addresses::globals::network_client_service;
		if ( !net_client )
		{
			return std::nullopt;
		}

		const auto tick_state = memory::call_vfunc<std::uintptr_t>( net_client, k_tick_state_vfunc_idx );
		if ( !tick_state )
		{
			return std::nullopt;
		}

		const auto server_tick = memory::read<int>( tick_state + k_tick_state_server_tick_offset );
		const auto delta_ticks = server_tick - latest_copy.tick;

		if ( delta_ticks <= 0 )
		{
			return std::nullopt;
		}

		// Slider = max physics steps we run (cost/safety), NOT the lag window.
		// The old gate keyed this to delta_ticks, so any real ping (delta_ticks > 3)
		// silently disabled server-time extrapolation. The real lag-comp window is
		// sv_maxunlag.
		const auto max_physics_steps = std::clamp(
			settings::g_combat.m_lagcomp.max_extrapolate_ticks.value, 1, 8 );

		float maxunlag = 0.2f; // safe fallback if the cvar is missing
		if ( auto* cvar = CONVAR( "sv_maxunlag" ) )
		{
			maxunlag = cvar->get<float>( );
		}
		const auto lag_budget = std::clamp(
			static_cast< int >( std::round( maxunlag / cstypes::tick_interval ) ),
			1, 64 );

		if ( delta_ticks > lag_budget )
		{
			return std::nullopt; // outside legitimate unlag window
		}

		// Extrapolate toward the server tick, but cap physics iterations.
		const auto ticks_to_extrapolate = std::min( delta_ticks, max_physics_steps );

		// Use record velocity from origin delta instead of client interpolated velocity
		math::vector3 velocity{};

		if ( prev_copy.has_value( ) )
		{
			const auto dt = latest_copy.simulation_time - prev_copy->simulation_time;

			// BUG 17: limit dt — a large choke makes the average velocity unreliable
			const auto max_reliable_dt = cstypes::tick_interval * 3.0f;
			if ( dt > 0.0f && dt <= max_reliable_dt )
			{
				velocity = ( latest_copy.origin - prev_copy->origin ) / dt;

				// Warn if discrepancy between record and client velocity is large
				const auto client_vel = memory::read<math::vector3>( pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) );
				const auto discrepancy = ( velocity - client_vel ).length( );
				if ( discrepancy > 500.0f )
				{
					EXTRAP_DEBUG_LOG( xs( "[extrap] {:x} | warn: velocity discrepancy {:.1f} u/s (record vs client)\\n" ), pawn, discrepancy );
				}
			}
		}

		// Fallback to client velocity if no previous record / unreliable dt
		if ( velocity.length_sqr( ) < 0.01f )
		{
			velocity = memory::read<math::vector3>( pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) );
		}

		const auto speed = std::sqrtf( velocity.x * velocity.x + velocity.y * velocity.y );

		if ( speed < 0.1f )
		{
			// Standing still is not a reason to have no server-time record. The
			// pose does not need advancing -- that is what standing still means --
			// but the timestamp does, because the rest of the aimbot treats an
			// extrapolated record as "this is where the target is at command time"
			// and gives it the clutch path that skips the autostop wait. Returning
			// nothing here left a stationary target permanently ineligible for
			// that, which is the one case where it is most obviously correct.
			auto stationary = latest_copy;
			stationary.extrapolated = true;
			stationary.simulation_time += static_cast< float >( ticks_to_extrapolate ) * cstypes::tick_interval;
			stationary.tick = latest_copy.tick + ticks_to_extrapolate;

			return stationary;
		}

		float direction = 0.0f;
		if ( velocity.x != 0.0f || velocity.y != 0.0f )
		{
			direction = std::atan2f( velocity.y, velocity.x ) * ( 180.0f / k_pi );
		}

		float direction_change = 0.0f;

		if ( prev_copy.has_value( ) )
		{
			const auto dt = latest_copy.simulation_time - prev_copy->simulation_time;
			if ( dt > 0.0f )
			{
				const auto origin_delta = latest_copy.origin - prev_copy->origin;
				float prev_dir = 0.0f;

				if ( origin_delta.x != 0.0f || origin_delta.y != 0.0f )
				{
					prev_dir = std::atan2f( origin_delta.y, origin_delta.x ) * ( 180.0f / k_pi );
				}

				auto angle_diff = direction - prev_dir;
				while ( angle_diff > 180.0f ) angle_diff -= 360.0f;
				while ( angle_diff < -180.0f ) angle_diff += 360.0f;

				// BUG 16: >90° disables extrapolation; 35-90° keeps linear movement
				if ( std::fabsf( angle_diff ) > 90.0f )
				{
					EXTRAP_DEBUG_LOG( xs( "[extrap] {:x} | skip: direction change too large ({:.1f} deg)\n" ), pawn, angle_diff );
					return std::nullopt;
				}

				if ( std::fabsf( angle_diff ) > 35.0f )
				{
					EXTRAP_DEBUG_LOG( xs( "[extrap] {:x} | warn: large direction change ({:.1f} deg), using linear\n" ), pawn, angle_diff );
					direction_change = 0.0f;
				}
				else
				{
					direction_change = ( angle_diff / dt ) * cstypes::tick_interval;
				}
			}
		}

		if ( std::fabsf( direction_change ) > 6.0f )
		{
			direction_change = 0.0f;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return std::nullopt;
		}

		const auto collision = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pCollision"_hash ) );
		math::vector3 obb_mins{}, obb_maxs{};

		if ( collision )
		{
			obb_mins = memory::read<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ) );
			obb_maxs = memory::read<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ) );
		}
		else
		{
			obb_mins = { -16.0f, -16.0f, 0.0f };
			obb_maxs = { 16.0f, 16.0f, 72.0f };
		}

		// A ducking target is shorter, but the flag is binary while the movement is
		// not: CS2 ramps m_flDuckAmount from 0 to 1 over about a fifth of a second,
		// and snapping the hull between 72 and 54 puts it in the wrong place for
		// every tick of that transition -- exactly the ticks a peeking target
		// spends crouching. This hull feeds the movement prediction traces, so a
		// wrong height here is a wrong predicted position rather than a wrong
		// hitbox: the bones come from the record either way.
		if ( ( latest_copy.flags & cstypes::entity_flags::ducking ) != 0 )
		{
			const auto duck_amount = std::clamp(
				memory::read<float>( pawn + SCHEMA( "C_CSPlayerPawn", "m_flDuckAmount"_hash ) ), 0.0f, 1.0f );

			obb_maxs.z = 72.0f - ( 72.0f - 54.0f ) * duck_amount;
		}

		// BUG 18: read flags from the record snapshot, not the live client state
		extrapolation_data data{};
		data.origin = latest_copy.origin;
		data.velocity = velocity;
		data.obb_mins = obb_mins;
		data.obb_maxs = obb_maxs;
		data.flags = latest_copy.flags;
		data.sim_time = latest_copy.simulation_time;
		data.direction = direction;

		// IMPR 21: cache convars once instead of querying them every tick
		data.sv_gravity   = CONVAR( "sv_gravity" )->get<float>( );
		data.sv_friction  = CONVAR( "sv_friction" )->get<float>( );
		data.sv_stopspeed = CONVAR( "sv_stopspeed" )->get<float>( );

		// BUG 15: surface friction from the player's entity
		data.surface_friction = memory::read<float>( pawn + SCHEMA( "C_BaseEntity", "m_flFriction"_hash ) );
		if ( data.surface_friction <= 0.0f )
			data.surface_friction = 1.0f;

		// CRITICAL 11: apply direction decay directly inside the movement loop and
		// accumulate the real yaw actually applied to the skeleton.
		float accumulated_yaw = 0.0f;
		for ( auto i = 0; i < ticks_to_extrapolate; ++i )
		{
			const auto tick_dir_change = direction_change * std::powf( k_direction_decay, static_cast< float >( i ) );
			data.direction += tick_dir_change;
			accumulated_yaw += tick_dir_change;

			while ( data.direction > 180.0f ) data.direction -= 360.0f;
			while ( data.direction < -180.0f ) data.direction += 360.0f;

			const auto rad = data.direction * ( k_pi / 180.0f );

			// BUG 14: clamp speed so lag spikes can't fling the extrapolation for km
			const auto current_speed = std::sqrtf( data.velocity.x * data.velocity.x + data.velocity.y * data.velocity.y );
			const auto clamped_speed = std::min( current_speed, k_max_velocity );
			data.velocity.x = std::cosf( rad ) * clamped_speed;
			data.velocity.y = std::sinf( rad ) * clamped_speed;

			data.sim_time += cstypes::tick_interval;

			this->predict_movement( data, pawn );
		}

		const auto origin_delta = data.origin - latest_copy.origin;

		if ( origin_delta.length_sqr( ) < 0.01f )
		{
			EXTRAP_DEBUG_LOG( xs( "[extrap] {:x} | skip: predicted origin unchanged after {} ticks\n" ), pawn, ticks_to_extrapolate );
			return std::nullopt;
		}

		record extrap_record = latest_copy;
		extrap_record.origin = data.origin;
		extrap_record.simulation_time = data.sim_time;
		extrap_record.tick = cstypes::time_to_ticks( data.sim_time );
		extrap_record.extrapolated = true;

		// Use the accumulated (decayed) yaw for bone transformation
		const bool needs_rotation = std::fabsf( accumulated_yaw ) > 0.5f;
		const auto yaw_rad = accumulated_yaw * ( k_pi / 180.0f );
		const auto cos_yaw = std::cosf( yaw_rad );
		const auto sin_yaw = std::sinf( yaw_rad );

		for ( auto i = 0; i < extrap_record.bone_count && i < 128; ++i )
		{
			// Transform bone position with rotation if needed
			if ( needs_rotation )
			{
				// Translate to origin-relative space, rotate, translate back
				const auto local_x = extrap_record.bones[ i ].position.x - latest_copy.origin.x;
				const auto local_y = extrap_record.bones[ i ].position.y - latest_copy.origin.y;

				const auto rotated_x = local_x * cos_yaw - local_y * sin_yaw;
				const auto rotated_y = local_x * sin_yaw + local_y * cos_yaw;

				extrap_record.bones[ i ].position.x = data.origin.x + rotated_x;
				extrap_record.bones[ i ].position.y = data.origin.y + rotated_y;
				extrap_record.bones[ i ].position.z += origin_delta.z;

				// CRITICAL 12: also rotate the bone orientation quaternion so hitbox
				// capsules point in the right direction.
				const auto half_yaw = yaw_rad * 0.5f;
				math::quaternion yaw_quat{};
				yaw_quat.x = 0.0f;
				yaw_quat.y = 0.0f;
				yaw_quat.z = std::sinf( half_yaw );
				yaw_quat.w = std::cosf( half_yaw );
				auto rotated_quat = yaw_quat * extrap_record.bones[ i ].rotation;

				// Normalize to avoid numerical drift accumulating over many ticks.
				const auto quat_len = std::sqrtf(
					rotated_quat.x * rotated_quat.x + rotated_quat.y * rotated_quat.y +
					rotated_quat.z * rotated_quat.z + rotated_quat.w * rotated_quat.w );
				if ( quat_len > 0.0001f )
				{
					rotated_quat.x /= quat_len;
					rotated_quat.y /= quat_len;
					rotated_quat.z /= quat_len;
					rotated_quat.w /= quat_len;
				}
				extrap_record.bones[ i ].rotation = rotated_quat;
			}
			else
			{
				// Small rotation - just linear offset
				extrap_record.bones[ i ].position.x += origin_delta.x;
				extrap_record.bones[ i ].position.y += origin_delta.y;
				extrap_record.bones[ i ].position.z += origin_delta.z;
			}
		}

		const auto dist = std::sqrtf( origin_delta.x * origin_delta.x + origin_delta.y * origin_delta.y + origin_delta.z * origin_delta.z );
		EXTRAP_DEBUG_LOG(
			xs( "[extrap] {:x} | ok: {} ticks | delta {:.2f} u | origin ({:.1f}, {:.1f}, {:.1f})\n" ),
			pawn, ticks_to_extrapolate, dist,
			data.origin.x, data.origin.y, data.origin.z
		);

		return extrap_record;
	}

	math::vector3 shared::lagcomp::predict_position_physics(
		std::uintptr_t pawn,
		const math::vector3& start_origin,
		const math::vector3& velocity,
		std::uint32_t flags,
		const math::vector3& obb_mins,
		const math::vector3& obb_maxs,
		int ticks
	) const
	{
		if (ticks <= 0)
			return { 0.0f, 0.0f, 0.0f };
		
		// Setup physics simulation data
		extrapolation_data data{};
		data.origin = start_origin;
		data.velocity = velocity;
		data.flags = flags;
		data.obb_mins = obb_mins;
		data.obb_maxs = obb_maxs;
		data.sim_time = 0.0f;
		data.direction = 0.0f;

		// Cache convars so predict_movement's physics stays correct
		data.sv_gravity   = CONVAR( "sv_gravity" )->get<float>( );
		data.sv_friction  = CONVAR( "sv_friction" )->get<float>( );
		data.sv_stopspeed = CONVAR( "sv_stopspeed" )->get<float>( );
		data.surface_friction = memory::read<float>( pawn + SCHEMA( "C_BaseEntity", "m_flFriction"_hash ) );
		if ( data.surface_friction <= 0.0f )
			data.surface_friction = 1.0f;

		// Simulate N ticks with full physics (gravity, friction, collisions)
		for (int i = 0; i < ticks; ++i) {
			predict_movement(data, pawn);
		}
		
		// Return offset relative to start position
		return data.origin - start_origin;
	}

} // namespace features::combat
