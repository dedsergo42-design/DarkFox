#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

namespace features::combat {

	namespace detail {

		struct bullet_trace_record
		{
			float enter_fraction;
			float exit_fraction;
			float damage_applied;
			int team_at_contact;
			std::uint16_t enter_contact_ix;
			std::uint16_t exit_contact_ix;
			std::uint8_t can_penetrate;
			std::uint8_t pad[ 3 ];
		};

	} // namespace detail

	void shared::penetration::prepare( std::uintptr_t weapon_vdata, std::uintptr_t weapon )
	{
		if ( !weapon_vdata || !weapon )
		{
			return;
		}

		this->m_weapon_data = weapon_data
		{
			.damage = static_cast< float >( memory::read<int>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_nDamage"_hash ) ) ),
			.penetration = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flPenetration"_hash ) ),
			.range_modifier = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flRangeModifier"_hash ) ),
			.range = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flRange"_hash ) ),
			.armor_ratio = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flArmorRatio"_hash ) ),
			.headshot_multiplier = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flHeadshotMultiplier"_hash ) )
		};
	}

	shared::penetration::run_context shared::penetration::prepare_target( std::uintptr_t target_pawn, lagcomp::record* record ) const
	{
		run_context ctx{};
		ctx.target_pawn = target_pawn;
		ctx.record = record;
		if ( record && record->game_scene_node )
		{
			ctx.hitboxes = systems::g_hitboxes.query( record->game_scene_node );
		}

		ctx.target_armor = memory::read<int>( target_pawn + SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_hash ) );
		ctx.target_team = memory::read<int>( target_pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );

		if ( ctx.target_armor > 0 )
		{
			const auto services = memory::read<std::uintptr_t>( target_pawn + SCHEMA( "C_BasePlayerPawn", "m_pItemServices"_hash ) );
			if ( services )
			{
				ctx.has_helmet = memory::read<bool>( services + SCHEMA( "CCSPlayer_ItemServices", "m_bHasHelmet"_hash ) );
			}
		}

		// Taken from the per-tick context: see the note on context::scales.
		ctx.scales = g_shared.ctx( ).scales;

		ctx.armor_ratio = this->m_weapon_data.armor_ratio;
		ctx.headshot_multiplier = this->m_weapon_data.headshot_multiplier;

		return ctx;
	}

	bool shared::penetration::run( const math::vector3& start, const math::vector3& end, const run_context& ctx, std::uintptr_t local_pawn, int local_team, result& out ) const
	{
		if ( this->m_weapon_data.damage <= 0.0f )
		{
			return false;
		}

		const auto direction = ( end - start ).normalized( );
		const auto trace_delta = direction * this->m_weapon_data.range;

		// Фильтр зависит только от локальной пешки, маски и слоя -- все три
		// постоянны на протяжении тика. Раньше он собирался заново на каждую
		// точку (а точек в скане десятки, и pen::run по одной на точку), и
		// каждая сборка стоит игровой вызов trace_filter_init. Кэш внутри
		// tracing отдаёт готовую структуру по ключу.
		const auto filter = systems::g_tracing.make_filter_cached( local_pawn, 0x1c300b, 3, 15 );
		// Rage scanning calls this hundreds of times in a frame. Reuse the large
		// trace buffer per worker instead of allocating and freeing 7 KB per point.
		thread_local systems::tracing::trace_data trace_storage{};
		trace_storage = {};
		auto* trace = &trace_storage;
		trace->array_pointer = &trace->elements;
		trace->hit_array_pointer = &trace->hit_elements;

		g_shared.m_current_autowall_record = ctx.record;
		g_shared.m_autowalling = true;

		// The hitbox-transform hook supplies this thread's record directly.
		// Do not swap the live entity pose: Present may read it concurrently.
		systems::g_tracing.setup_trace( trace, start, trace_delta, filter, 4, true );

		g_shared.m_autowalling = false;
		g_shared.m_current_autowall_record = nullptr;

		const auto num_hits = trace->num_hits;
		const auto hit_array = reinterpret_cast< std::uintptr_t >( trace->hit_array_pointer );

		if ( num_hits <= 0 )
		{
			out = {};
			return false;
		}

		const auto surface_array = reinterpret_cast< std::uintptr_t >( trace->array_pointer );

		memory::call<void> (PATTERN (patterns::trace_bullet), trace, this->m_weapon_data.damage, this->m_weapon_data.penetration, this->m_weapon_data.range_modifier, 4, local_team, static_cast<std::uintptr_t>(0));
		#if defined( _DEBUG ) || defined( DEV )
	if ( penetration::debug_log )
	logging::console::print( xs( "[pen] run start | muzzle {:.1f} | pen {:.2f} | range_mod {:.3f} | team {} | mask 0x{:x} | num_hits {}" ),
		this->m_weapon_data.damage, this->m_weapon_data.penetration, this->m_weapon_data.range_modifier, local_team, 0x1c300b, num_hits );
#endif

		auto actual_hitbox{ -1 };
		auto closest_hitbox_fraction{ 1.0f };
		if ( ctx.record )
		{
			for ( const auto& hitbox : ctx.hitboxes )
			{
				if ( hitbox.bone < 0 || hitbox.bone >= ctx.record->bone_count )
				{
					continue;
				}

				const auto& bone = ctx.record->bones[ hitbox.bone ];
				auto fraction{ 1.0f };
				auto intersects{ false };

				if ( hitbox.radius > 0.001f )
				{
					const auto capsule_start = bone.rotation.rotate_vector( hitbox.mins ) + bone.position;
					const auto capsule_end = bone.rotation.rotate_vector( hitbox.maxs ) + bone.position;
					intersects = g_shared.ray_vs_capsule( start, trace_delta, capsule_start, capsule_end, hitbox.radius, fraction );
				}
				else
				{
					auto inverse = bone.rotation;
					inverse.x = -inverse.x;
					inverse.y = -inverse.y;
					inverse.z = -inverse.z;

					const auto local_origin = inverse.rotate_vector( start - bone.position );
					const auto local_delta = inverse.rotate_vector( trace_delta );
					auto entry{ 0.0f };
					auto exit{ 1.0f };

					const auto intersect_axis = [ & ]( float origin, float delta, float minimum, float maximum )
						{
							if ( std::fabsf( delta ) < 1.0e-8f )
							{
								return origin >= minimum && origin <= maximum;
							}

							auto first = ( minimum - origin ) / delta;
							auto second = ( maximum - origin ) / delta;
							if ( first > second ) std::swap( first, second );
							entry = std::max( entry, first );
							exit = std::min( exit, second );
							return entry <= exit;
						};

					intersects = intersect_axis( local_origin.x, local_delta.x, hitbox.mins.x, hitbox.maxs.x ) &&
						intersect_axis( local_origin.y, local_delta.y, hitbox.mins.y, hitbox.maxs.y ) &&
						intersect_axis( local_origin.z, local_delta.z, hitbox.mins.z, hitbox.maxs.z );
					fraction = entry;
				}

				// fraction is parametric along a trace_delta the length of the whole
				// weapon range, so the old > 0.01f guard threw away every hitbox
				// closer than ~1% of that -- roughly 80 units at an 8192 range.
				// Point blank and through-window shots resolved to actual_hitbox < 0
				// and came back as occlusion misses.
				if ( intersects && fraction < closest_hitbox_fraction )
				{
					closest_hitbox_fraction = fraction;
					actual_hitbox = hitbox.index;
				}
			}
		}

		auto penetrated{ false };

		for ( auto i = 0; i < num_hits; ++i )
		{
			auto hit = reinterpret_cast< detail::bullet_trace_record* >( hit_array + i * sizeof( detail::bullet_trace_record ) );
			const auto damage = *reinterpret_cast< float* >( reinterpret_cast< std::uintptr_t >( hit ) + 8 );
			#if defined( _DEBUG ) || defined( DEV )
	if ( penetration::debug_log )
	logging::console::print( xs( "[pen] hit[{}] dmg_applied {:.1f} | pen_bit {} | exit {:.3f} | enter_ix {}" ),
		i, damage, ( hit->can_penetrate & 1 ), hit->exit_fraction, hit->enter_contact_ix );
#endif

			if ( damage <= 0.0f )
			{
#if defined( _DEBUG ) || defined( DEV )
	if ( penetration::debug_log )
				logging::console::print( xs( "[pen] reject i={} : damage_applied<=0 (bullet stopped)" ), i );
#endif
				break;
			}

			if ( ( hit->can_penetrate & 1 ) != 0 )
			{
				penetrated = true;

				if ( *reinterpret_cast< float* >( reinterpret_cast< std::uintptr_t >( hit ) + 4 ) == 1.0f )
				{
#if defined( _DEBUG ) || defined( DEV )
	if ( penetration::debug_log )
					logging::console::print( xs( "[pen] reject i={} : exit_fraction==1 (did not exit wall)" ), i );
#endif
					break;
				}

				continue;
			}

			const auto trace_holder = surface_array + sizeof( systems::tracing::trace_array_element ) * ( hit->enter_contact_ix & 0x7fff );
			const auto hit_handle = memory::read<std::uint32_t>( trace_holder + 0x2c );
			const auto hit_entity = systems::g_entities.lookup( hit_handle );

			// The handle out of the contact element is not dependable. Last session it
			// named the SAME entity on 222364 of 222876 rejects while the scan was
			// working through five different targets, which no bullet can do, and each
			// of those rejects threw away a record the engine had already scored at 87
			// damage. Note that enter_contact_ix came back as 0x8000 on exactly the
			// record that carried the player hit and 0 on the wall records behind it,
			// so masking it with 0x7fff and reading that surface element points at the
			// wrong entry for the one record that matters.
			//
			// So the handle decides only when it agrees. The geometry does not lie: the
			// ray is intersected against the target's own hitboxes from the same
			// lag-comp record the server tests against, so the fraction at which the
			// bullet reaches the target is already known. A hit record entering there
			// IS the target -- those are the same point in space -- whoever the handle
			// claims it is. Eight units of slack, because enter_fraction and
			// closest_hitbox_fraction are both fractions of the same full-range ray.
			const auto fraction_tolerance = this->m_weapon_data.range > 1.0f
				? 8.0f / this->m_weapon_data.range
				: 0.01f;

			const auto enters_on_target = actual_hitbox >= 0
				&& std::fabsf( hit->enter_fraction - closest_hitbox_fraction ) < fraction_tolerance;

			if ( ( !hit_entity || hit_entity != ctx.target_pawn ) && !enters_on_target )
			{
#if defined( _DEBUG ) || defined( DEV )
	if ( penetration::debug_log )
				logging::console::print( xs( "[pen] reject i={} : entity {} != target {} (skip) | enter {:.4f} vs {:.4f} (tol {:.4f}) | local {}" ),
					i, hit_entity, ctx.target_pawn, hit->enter_fraction, closest_hitbox_fraction, fraction_tolerance, local_pawn );
#endif
				continue;
			}

#if defined( _DEBUG ) || defined( DEV )
			if ( penetration::debug_log && hit_entity != ctx.target_pawn )
			{
				logging::console::print( xs( "[pen] accept i={} by geometry | enter {:.4f} vs {:.4f} | handle {}" ),
					i, hit->enter_fraction, closest_hitbox_fraction, hit_entity );
			}
#endif

			if ( actual_hitbox < 0 )
			{
#if defined( _DEBUG ) || defined( DEV )
	if ( penetration::debug_log )
				logging::console::print( xs( "[pen] reject i={} : actual_hitbox<0 (no hitbox)" ), i );
#endif
				continue;
			}

			out.hitbox = actual_hitbox;
			out.hitgroup = systems::g_hitboxes.hitgroup_from_hitbox (actual_hitbox);
			out.penetrated = penetrated;
			out.damage = damage;

			this->scale_damage (out.hitgroup, ctx.target_armor, ctx.has_helmet, ctx.target_team, ctx.armor_ratio, ctx.headshot_multiplier, ctx.scales, out.damage);
			#if defined( _DEBUG ) || defined( DEV )
	if ( penetration::debug_log )
	logging::console::print( xs( "[pen] HIT i={} | dmg_applied {:.1f} -> out_damage {:.1f} | hitgroup {} | penetrated {} | ent {}" ),
		i, damage, out.damage, out.hitgroup, out.penetrated, hit_entity );
#endif

			return true;
		}

#if defined( _DEBUG ) || defined( DEV )
	if ( penetration::debug_log )
	logging::console::print( xs( "[pen] MISS: {} records, no valid target hit" ), num_hits );
#endif
		out = {};
		return false;
	}

	bool shared::penetration::can( const math::vector3& start, const math::vector3& direction, float& out_damage, const systems::local::snapshot& local ) const
	{
		out_damage = 0.0f;

		if ( this->m_weapon_data.damage <= 0.0f || this->m_weapon_data.penetration <= 0.0f )
		{
			return false;
		}

		const auto local_team = memory::read<int>( local.pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
		const auto trace_delta = direction * this->m_weapon_data.range;

		auto filter = systems::g_tracing.make_filter_cached( local.pawn, 0x1c300b, 3, 15 );
		thread_local systems::tracing::trace_data trace_storage{};
		trace_storage = {};
		auto* trace = &trace_storage;
		trace->array_pointer = &trace->elements;
		trace->hit_array_pointer = &trace->hit_elements;

		systems::g_tracing.setup_trace( trace, start, trace_delta, filter, 4, true );

		const auto num_hits = trace->num_hits;

		if ( num_hits <= 0 )
		{
			return false;
		}

		const auto hit_array = reinterpret_cast< std::uintptr_t >( trace->hit_array_pointer );

		memory::call<void> (PATTERN (patterns::trace_bullet), trace, this->m_weapon_data.damage, this->m_weapon_data.penetration, this->m_weapon_data.range_modifier, 4, local_team, static_cast<std::uintptr_t>(0));

		for ( auto i = 0; i < num_hits; ++i )
		{
			auto hit = reinterpret_cast< detail::bullet_trace_record* >( hit_array + i * sizeof( detail::bullet_trace_record ) );
			const auto damage = hit->damage_applied;

			if ( damage <= 0.0f )
			{
				break;
			}

			if ( ( hit->can_penetrate & 1 ) != 0 )
			{
				// Match run(): bit 0 marks a penetration record and an exit
				// fraction of 1 means the bullet did not make it through.
				if ( hit->exit_fraction == 1.0f )
				{
					break;
				}

				out_damage = damage;
				return true;
			}
		}

		return false;
	}

	float shared::penetration::get_max_damage( int hitgroup, int target_armor, bool has_helmet, int target_team ) const
	{
		if ( this->m_weapon_data.damage <= 0.0f )
		{
			return 0.0f;
		}

		const damage_scales scales
		{
			.ct_head = CONVAR ("mp_damage_scale_ct_head")->get<float> (),
			.t_head = CONVAR ("mp_damage_scale_t_head")->get<float> (),
			.ct_body = CONVAR ("mp_damage_scale_ct_body")->get<float> (),
			.t_body = CONVAR ("mp_damage_scale_t_body")->get<float> ()
		};

		auto damage = this->m_weapon_data.damage;
		this->scale_damage( hitgroup, target_armor, has_helmet, target_team, this->m_weapon_data.armor_ratio, this->m_weapon_data.headshot_multiplier, scales, damage );
		return damage;
	}

	void shared::penetration::scale_damage( int hitgroup, int armor, bool has_helmet, int team, float armor_ratio, float headshot_multiplier, const damage_scales& scales, float& damage ) const
	{
		const auto is_ct = ( team == 3 );
		const auto head_scale = is_ct ? scales.ct_head : scales.t_head;
		const auto body_scale = is_ct ? scales.ct_body : scales.t_body;

		switch ( hitgroup )
		{
		case 1:
			damage *= headshot_multiplier * head_scale;
			break;
		case 2:
		case 4:
		case 5:
		case 8:
			damage *= body_scale;
			break;
		case 3:
			damage *= 1.25f * body_scale;
			break;
		case 6:
		case 7:
			damage *= 0.75f * body_scale;
			break;
		default:
			break;
		}

		const auto is_head = ( hitgroup == 1 );
		const auto is_armored = ( hitgroup >= 1 && hitgroup <= 5 ) || ( hitgroup == 8 );

		if ( armor <= 0 || !is_armored || ( is_head && !has_helmet ) )
		{
			damage = std::floor( damage );
			return;
		}

		constexpr auto armor_bonus{ 0.5f };
		const auto armor_ratio_scaled = armor_ratio * 0.5f;

		auto damage_to_health = damage * armor_ratio_scaled;
		auto damage_to_armor = ( damage - damage_to_health ) * armor_bonus;

		if ( damage_to_armor > static_cast< float >( armor ) )
		{
			damage_to_health = damage - ( static_cast< float >( armor ) / armor_bonus );
		}

		damage = std::floor( damage_to_health );
	}

	bool shared::lagcomp::record::setup( std::uintptr_t pawn )
	{
		this->pawn = pawn;
		this->game_scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );

		if ( !this->game_scene_node )
		{
			return false;
		}

		this->bone_cache = memory::read<std::uintptr_t>( this->game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x80 );
		if ( !this->bone_cache )
		{
			return false;
		}

		this->bone_count = memory::read<int>( this->game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x8c );
		if ( this->bone_count <= 0 )
		{
			return false;
		}
		this->bone_count = std::min( this->bone_count, 128 );

		const auto abs_origin = memory::read<math::vector3>( this->game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		const auto abs_rotation = memory::read<math::vector3>( this->game_scene_node + SCHEMA( "CGameSceneNode", "m_angAbsRotation"_hash ) );
		if ( !std::isfinite( abs_origin.x ) || !std::isfinite( abs_origin.y ) || !std::isfinite( abs_origin.z ) )
		{
			return false;
		}

		// Network origin is encoded. Records and bones must stay in the same
		// evaluated world-space coordinate system.
		this->origin = abs_origin;
		this->rotation = abs_rotation;

		this->simulation_time = memory::read<float>( pawn + SCHEMA( "C_BaseEntity", "m_flSimulationTime"_hash ) );
		this->flags = memory::read<std::uint32_t>( pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_hash ) );

		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		if ( !global_vars )
		{
			return false;
		}

		const auto backup_current_time = memory::read<float>( global_vars + 0x30 );
		const auto backup_tick_count = memory::read<int>( global_vars + 0x44 );
		memory::write<float>( global_vars + 0x30, this->simulation_time );
		memory::write<int>( global_vars + 0x44, cstypes::time_to_ticks( this->simulation_time ) );

		memory::call<void>(PATTERN (patterns::game_scene_node_set_mesh_group), this->game_scene_node, 0xfffff );
		memory::call<void>(PATTERN (patterns::game_scene_node_set_skeleton), this->game_scene_node, 0x100 );

		memory::write<int>( global_vars + 0x44, backup_tick_count );
		memory::write<float>( global_vars + 0x30, backup_current_time );

		this->bone_cache = memory::read<std::uintptr_t>( this->game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x80 );
		if ( !this->bone_cache )
		{
			return false;
		}

		std::memcpy( this->bones, reinterpret_cast< void* >( this->bone_cache ), sizeof( systems::bones::data ) * this->bone_count );

		this->tick = cstypes::time_to_ticks( this->simulation_time );
		this->valid = true;

		return true;
	}

	bool shared::lagcomp::record::is_valid( ) const
	{
		if ( !this->valid )
		{
			return false;
		}

		const auto local_pawn = systems::g_local.get( ).pawn;
		const auto net_channel = memory::call<std::uintptr_t>(PATTERN (patterns::get_net_channel), 0, 0 );
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );

		if ( !local_pawn || !net_channel || !global_vars )
		{
			return false;
		}

		const auto max_unlag = [ ]
			{
				const auto server_limit = CONVAR ("sv_maxunlag")->get<float>( );
				const auto player_limit = CONVAR ("sv_maxunlag_player")->get<float>( );
				return player_limit > 0.0f ? std::min( server_limit, player_limit ) : server_limit;
			}( );

		const auto current_time = memory::read<float>( global_vars + 0x30 );
		const auto latency = memory::call_vfunc<float>( net_channel, 10, 0 );

		if ( !std::isfinite( max_unlag ) || !std::isfinite( current_time ) ||
			!std::isfinite( latency ) )
		{
			return false;
		}

		// This value is the effective ping for the selected flow in this build;
		// combining both flows double-counts latency and can erase the window.
		const auto budget = max_unlag - std::max( latency, 0.0f );

		return budget > 0.0f && this->simulation_time >= current_time - budget;
	}

	void shared::lagcomp::record::apply( )
	{
		if ( !this->valid || this->is_applied || !this->game_scene_node )
		{
			return;
		}

		this->bone_cache = memory::read<std::uintptr_t>( this->game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x80 );
		if ( !this->bone_cache )
		{
			return;
		}

		const auto size = sizeof( systems::bones::data ) * this->bone_count;
		std::memcpy( this->bones_backup, reinterpret_cast< void* >( this->bone_cache ), size );
		std::memcpy( reinterpret_cast< void* >( this->bone_cache ), this->bones, size );

		this->is_applied = true;
	}

	void shared::lagcomp::record::restore( )
	{
		if ( !this->valid || !this->is_applied || !this->bone_cache )
		{
			return;
		}

		const auto size = sizeof( systems::bones::data ) * this->bone_count;
		std::memcpy( reinterpret_cast< void* >( this->bone_cache ), this->bones_backup, size );

		this->is_applied = false;
	}

	void shared::lagcomp::run( )
	{
		std::unique_lock records_lock( this->m_records_mtx );

		const auto local = systems::g_local.get( );
		if ( !local.is_alive )
		{
			this->m_records.clear( );
			return;
		}

		std::unordered_set<std::uintptr_t> active{};

		for ( const auto& p : systems::g_entities.get_by_type( systems::entities::type::player ) )
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

			active.insert( pawn );
		}

		std::erase_if( this->m_records, [ & ]( const auto& pair ) { return !active.contains( pair.first ); } );

		struct pending_record
		{
			std::uintptr_t pawn{};
			int simulation_tick{};
		};

		std::vector<pending_record> pending;
		pending.reserve( active.size( ) );

		for ( const auto& pawn : active )
		{
			const auto health = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
			if ( health <= 0 )
			{
				this->m_records.erase( pawn );
				continue;
			}

			auto& records = this->m_records[ pawn ];
			const auto simulation_time = memory::read<float>( pawn + SCHEMA( "C_BaseEntity", "m_flSimulationTime"_hash ) );
			const auto simulation_tick = cstypes::time_to_ticks( simulation_time );

			if ( records.empty( ) || simulation_tick > records.front( ).tick )
			{
				pending.push_back( { pawn, simulation_tick } );
			}

			while ( !records.empty( ) && !records.back( ).is_valid( ) )
			{
				records.pop_back( );
			}
		}

		if ( pending.empty( ) )
		{
			return;
		}

		for ( auto& p : pending )
		{
			record rec{};

			if ( rec.setup( p.pawn ) )
			{
				this->m_records[ p.pawn ].emplace_front( std::move( rec ) );
			}
		}

		for ( auto& [pawn, records] : this->m_records )
		{
			for ( auto& rec : records )
			{
				rec.was_valid = rec.is_valid( );
			}
		}

		// Прямой индикатор состояния целей: рейджбот стреляет ровно тогда, когда
		// здесь есть игроки с записями. Пустой список -- самая частая причина
		// «не стреляет», и по логу это видно сразу, а не выясняется гаданием.
		// Кэш сущностей наполняется хуками add_entity/remove_entity: если их
		// паттерны сломались после обновления игры, список останется пустым.
		{
			static int throttle{};
			if ( ( ++throttle % 512 ) == 0 )
			{
				std::size_t with_records{};
				for ( const auto& [pawn, records] : this->m_records )
				{
					if ( !records.empty( ) )
					{
						++with_records;
					}
				}

				char line[ 160 ]{};
				_snprintf_s( line, sizeof( line ), _TRUNCATE,
					"[lagcomp] players in cache %zu, with records %zu, live records %zu",
					systems::g_entities.get_by_type( systems::entities::type::player ).size( ),
					with_records, this->m_records.size( ) );
				diag::step( line );
			}
		}
	}

	shared::lagcomp::record* shared::lagcomp::get_oldest_valid( std::uintptr_t pawn )
	{
		std::shared_lock records_lock( this->m_records_mtx );

		auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) || it->second.empty( ) )
		{
			return nullptr;
		}

		for ( auto rit = it->second.rbegin( ); rit != it->second.rend( ); ++rit )
		{
			if ( rit->is_valid( ) )
			{
				return &( *rit );
			}
		}

		return nullptr;
	}

	shared::lagcomp::record* shared::lagcomp::get_oldest_was_valid( std::uintptr_t pawn )
	{
		std::shared_lock records_lock( this->m_records_mtx );

		auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) || it->second.empty( ) )
		{
			return nullptr;
		}

		for ( auto rit = it->second.rbegin( ); rit != it->second.rend( ); ++rit )
		{
			if ( rit->was_valid )
			{
				return &( *rit );
			}
		}

		return nullptr;
	}

	std::optional<shared::lagcomp::visual_record> shared::lagcomp::get_oldest_was_valid_visual( std::uintptr_t pawn ) const
	{
		std::shared_lock records_lock( this->m_records_mtx );

		const auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) )
		{
			return std::nullopt;
		}

		for ( auto rit = it->second.rbegin( ); rit != it->second.rend( ); ++rit )
		{
			if ( !rit->was_valid )
			{
				continue;
			}

			visual_record out{};
			out.origin = rit->origin;
			for ( auto i = 0; i < 27; ++i )
			{
				out.bones[ i ] = rit->bones[ i ];
			}

			return out;
		}

		return std::nullopt;
	}

	std::vector<shared::lagcomp::record*> shared::lagcomp::get_valid_records( std::uintptr_t pawn )
	{
		std::shared_lock records_lock( this->m_records_mtx );

		std::vector<record*> result;

		auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) )
		{
			return result;
		}

		result.reserve( it->second.size( ) );

		for ( auto& rec : it->second )
		{
			if ( rec.is_valid( ) )
			{
				result.push_back( &rec );
			}
		}

		if ( result.empty( ) )
		{
			return result;
		}

		const auto max_ticks = std::clamp( settings::g_combat.m_lagcomp.max_backtrack_ticks.value, 1, static_cast< int >( rage::k_max_lagcomp_records ) );
		const auto newest_tick = result.front( )->tick;

		// Smart Backtrack: remember the oldest valid record. When an enemy peeks
		// and then ducks back behind cover, the freshest tick is already hidden,
		// but an older tick still shows the exposed head. We re-include that
		// "full backtrack" tick as a fallback so a lethal headshot is not lost.
		record* oldest_valid = result.front( );
		for ( auto* rec : result )
		{
			if ( rec->tick < oldest_valid->tick )
			{
				oldest_valid = rec;
			}
		}

		result.erase(
			std::remove_if( result.begin( ), result.end( ), [ newest_tick, max_ticks ]( const record* rec )
				{
					return ( newest_tick - rec->tick ) > max_ticks;
				} ),
			result.end( )
		);

		// max_backtrack_ticks is the user's "do not scan older than this" limit,
		// not the server's. The server window is sv_maxunlag minus latency, and
		// record::is_valid( ) is exactly that test -- it compares simulation_time
		// against current_time minus the budget -- so a record that passes it is
		// one the server will still accept. Re-check it here rather than trusting
		// the filter above, so this stays true if that pre-filter ever changes.
		if ( oldest_valid != nullptr && oldest_valid->is_valid( ) )
		{
			const bool still_present = std::any_of( result.begin( ), result.end( ),
				[ oldest_valid ]( const record* rec ) { return rec == oldest_valid; } );
			if ( !still_present )
			{
				result.push_back( oldest_valid );
			}
		}

		return result;
	}

	std::array<systems::bones::data, 27> shared::lagcomp::get_skeleton( const record& record ) const
	{
		std::array<systems::bones::data, 27> skeleton;

		if ( record.valid )
		{
			for ( auto i = 0; i < 27; ++i )
			{
				skeleton[ i ] = record.bones[ i ];
			}
		}

		return skeleton;
	}

	void shared::shoot_history::snapshot( std::uintptr_t local_pawn, std::uintptr_t weapon_services )
	{
		this->m_count = 0;

		if ( !weapon_services )
		{
			return;
		}

		{
			const auto net_client = addresses::globals::network_client_service;
			if ( !net_client )
			{
				return;
			}

			const auto tick_state = memory::call_vfunc<std::uintptr_t>( net_client, 23 );
			if ( !tick_state )
			{
				return;
			}

			this->m_server_tick = memory::read<int>( tick_state + 892 );
		}

		{
			const auto idx_raw = memory::read<int>( addresses::globals::frame_input_ring_idx );
			const auto idx = static_cast< unsigned >( idx_raw ) % 10u;
			const auto slot = addresses::globals::frame_input_ring_base + 40ull * idx;

			this->m_client_tick = memory::read<int>( slot + 0x0c );
			this->m_client_tick_frac = memory::read<float>( slot + 0x10 );
		}

		{
			const auto lerp_seconds = memory::call<float>(PATTERN (patterns::get_interp_amount), local_pawn );
			const auto lerp_ticks_f = lerp_seconds * 64.0f;
			const auto rounded = std::round( lerp_ticks_f );

			if ( std::fabs( lerp_ticks_f - rounded ) < 1e-4f )
			{
				this->m_lerp_ticks_int = static_cast< int >( rounded );
				this->m_lerp_ticks_frac = 0.0f;
			}
			else
			{
				this->m_lerp_ticks_int = static_cast< int >( std::floor( lerp_ticks_f ) );
				this->m_lerp_ticks_frac = lerp_ticks_f - static_cast< float >( this->m_lerp_ticks_int );
			}
		}

		const auto tail = memory::read<int>( weapon_services + 872 );
		const auto count = memory::read<int>( weapon_services + 876 );

		if ( count <= 0 || count > 32 || tail < 0 || tail >= 32 )
		{
			return;
		}

		for ( auto i = 0; i < count; ++i )
		{
			const auto idx = ( tail + i ) % 32;
			const auto off = weapon_services + 232 + 20ull * idx;

			auto& e = this->m_entries[ i ];
			e.tick = memory::read<int>( off + 0x00 );
			e.fraction = memory::read<float>( off + 0x04 );
			e.position.x = memory::read<float>( off + 0x08 );
			e.position.y = memory::read<float>( off + 0x0C );
			e.position.z = memory::read<float>( off + 0x10 );
		}

		this->m_count = count;
	}

	shared::shoot_history::eye_candidates shared::shoot_history::get_candidates( ) const
	{
		eye_candidates out{};

		if ( this->m_count < 1 )
		{
			return out;
		}

		constexpr auto ring_slot{ 0.03125f };

		const auto newest_valid_tick = this->m_client_tick - this->m_lerp_ticks_int;
		const auto oldest_valid_tick = this->m_client_tick - this->m_lerp_ticks_int - 1;

		auto first_valid{ -1 };
		auto last_valid{ -1 };

		for ( auto i = 0; i < this->m_count; ++i )
		{
			const auto t = this->m_entries[ i ].tick;
			if ( t > newest_valid_tick || t < oldest_valid_tick )
			{
				continue;
			}

			if ( first_valid == -1 )
			{
				first_valid = i;
			}

			last_valid = i;
		}

		if ( last_valid == -1 )
		{
			return out;
		}

		const auto& newest = this->m_entries[ last_valid ];
		out.entries[ 0 ].position = newest.position;
		out.entries[ 0 ].player_tick = newest.tick;
		out.entries[ 0 ].player_frac = newest.fraction + ring_slot;
		out.entries[ 0 ].lerp_ticks_int = this->m_lerp_ticks_int;
		out.entries[ 0 ].lerp_ticks_frac = this->m_lerp_ticks_frac;
		out.count = 1;

		if ( first_valid != last_valid )
		{
			const auto& oldest = this->m_entries[ first_valid ];
			const auto  delta = oldest.position - newest.position;

			if ( delta.x * delta.x + delta.y * delta.y + delta.z * delta.z >= 4.0f )
			{
				out.entries[ 1 ].position = oldest.position;
				out.entries[ 1 ].player_tick = oldest.tick;
				out.entries[ 1 ].player_frac = oldest.fraction + ring_slot;
				out.entries[ 1 ].lerp_ticks_int = this->m_lerp_ticks_int;
				out.entries[ 1 ].lerp_ticks_frac = this->m_lerp_ticks_frac;
				out.count = 2;
			}
		}

		return out;
	}

	void shared::update( )
	{
		this->m_ctx = {};

		const auto local = systems::g_local.get( );
		if ( !local.pawn )
		{
			return;
		}

		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );

		if ( !global_vars || !movement_services )
		{
			return;
		}

		this->m_ctx.current_tick = memory::read<int>( global_vars + 0x44 );
		this->m_ctx.current_time = memory::read<float>( global_vars + 0x30 );
		this->m_ctx.is_scoped = memory::read<bool>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsScoped"_hash ) );
		this->m_ctx.ticks_since_land = this->m_ctx.current_tick - memory::read<int>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_ModernJump"_hash ) + SCHEMA( "CCSPlayerModernJump", "m_nLastLandedTick"_hash ) );
		this->m_ctx.weapon_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );

		if ( !this->m_ctx.weapon_services )
		{
			return;
		}

		const auto weapon_handle = memory::read<std::uint32_t>( this->m_ctx.weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_hash ) );
		if ( !weapon_handle )
		{
			return;
		}

		this->m_ctx.weapon = systems::g_entities.lookup( weapon_handle );
		if ( !this->m_ctx.weapon )
		{
			return;
		}

		this->m_ctx.weapon_vdata = memory::read<std::uintptr_t>( this->m_ctx.weapon + SCHEMA( "C_BaseEntity", "m_nSubclassID"_hash ) + 0x8 );
		if ( !this->m_ctx.weapon_vdata )
		{
			return;
		}

		this->m_ctx.range = memory::read<float>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flRange"_hash ) );
		this->m_ctx.weapon_type = memory::read<std::uint32_t>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_WeaponType"_hash ) );
		this->m_ctx.item_def_idx = memory::read<std::uint16_t>( this->m_ctx.weapon + SCHEMA( "C_EconEntity", "m_AttributeManager"_hash ) + SCHEMA( "C_AttributeContainer", "m_Item"_hash ) + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ) );
		this->m_ctx.num_bullets = memory::read<int>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_nNumBullets"_hash ) );
		this->m_ctx.recoil_index = memory::read<float>( this->m_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_flRecoilIndex"_hash ) );
		this->m_ctx.weapon_max_speed = memory::read<float>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flMaxSpeed"_hash ) );
		this->m_ctx.is_jump_scouting = ( systems::g_prediction.pre( ).flags & cstypes::entity_flags::on_ground ) == 0 && this->m_ctx.item_def_idx == cstypes::item_definition_index::weapon_ssg_08 && this->m_ctx.is_scoped;
		{
			// CONVAR hashes its name at compile time, so each of these has to be
			// written out rather than looked up through a helper.
			const auto floor_scale = [ ]( float value )
				{
					// Zero means "this server did not set it", not "no damage".
					// Multiplying by it silently disables the aimbot.
					return value > 0.001f ? value : 1.0f;
				};

			auto* ct_head = CONVAR( "mp_damage_scale_ct_head" );
			auto* t_head = CONVAR( "mp_damage_scale_t_head" );
			auto* ct_body = CONVAR( "mp_damage_scale_ct_body" );
			auto* t_body = CONVAR( "mp_damage_scale_t_body" );

			this->m_ctx.scales =
			{
				.ct_head = ct_head ? floor_scale( ct_head->get<float>( ) ) : 1.0f,
				.t_head = t_head ? floor_scale( t_head->get<float>( ) ) : 1.0f,
				.ct_body = ct_body ? floor_scale( ct_body->get<float>( ) ) : 1.0f,
				.t_body = t_body ? floor_scale( t_body->get<float>( ) ) : 1.0f
			};
		}

		this->m_ctx.valid = true;

		this->m_pen.prepare( this->m_ctx.weapon_vdata, this->m_ctx.weapon );
	}

	void shared::invalidate_if_needed( )
	{
		const auto local = systems::g_local.get( );
		if ( !local.is_alive || !local.pawn || !local.controller )
		{
			this->m_ctx = {};
			this->m_last_shoot_tick = 0;
		}
	}

	std::uint32_t shared::get_spread_seed( const math::vector3& angles, int tick ) const
	{
		return memory::call<std::uint32_t>(PATTERN (patterns::get_tick_view_angles), nullptr, &angles, tick );
	}

	math::vector2 shared::calculate_spread( int seed, float accuracy, float spread, float recoil_index, int item_def_idx, int num_bullets ) const
	{
		math::vector2 out{};

		// Third argument is the fire mode, and it was a literal zero here for as
		// long as this function has existed. Primary fire never noticed. The
		// revolver's quick shot did: the angles reached the server correctly, the
		// seed was solved and verified, and the spread that came back belonged to
		// the primary distribution -- so the compensation cancelled a deviation the
		// weapon was never going to have, and the shot kept the full width of the
		// real cone in a random direction.
		//
		// Measured over a rotation of both values on live shots: mode 0 landed 31%
		// of quick shots, mode 1 landed 78%.
		//
		// Read from the context rather than passed in, because scanning runs on
		// pool threads and anything thread-local set on the main thread would not
		// reach them.
		const auto fire_mode = this->quick_revolver_active( ) ? 1 : 0;

		memory::call<void>(PATTERN (patterns::weapon_calculate_spread), static_cast< std::int16_t >( item_def_idx ), num_bullets, fire_mode, static_cast< std::uint32_t >( seed + 1 ), accuracy, spread, recoil_index, &out.x, &out.y );

		return out;
	}

	math::vector3 shared::get_aim_punch( std::uintptr_t local_pawn ) const
	{
		math::vector3 out{};

		if ( !local_pawn )
		{
			return out;
		}

		// Накопленная отдача прицела читается из схемы, а не вызовом игровой
		// функции.
		//
		// Раньше здесь был вызов по сигнатуре get_aim_punch, и после обновления
		// игры сигнатура перестала находиться. Последствие было не "punch не
		// компенсируется", а хуже: memory::call на неразрешённом адресе молча
		// возвращает, out остаётся нулём, и выстрел уходит по НЕкомпенсированному
		// углу. Прицел уезжает вверх ровно на накопленную отдачу, и чем длиннее
		// очередь, тем больше промах -- а hitchance считается по конусу без
		// учёта этого сноса, поэтому бот уверен в попадании и всё равно мажет.
		//
		// Схема даёт те же числа без единой сигнатуры: полный aim punch -- это
		// сумма предсказуемой и непредсказуемой составляющих, обе лежат прямо
		// в сервисе. Читаем их напрямую, и обновление игры больше не может
		// сломать компенсацию отдачи: смещения приходят из дампа, а его Fox
		// обновляет вместе с игрой.
		const auto services = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_CSPlayerPawn", "m_pAimPunchServices"_hash ) );

		if ( services )
		{
			const auto predictable = memory::read<math::vector3>( services + SCHEMA( "CCSPlayer_AimPunchServices", "m_predictableBaseAngle"_hash ) );
			const auto unpredictable = memory::read<math::vector3>( services + SCHEMA( "CCSPlayer_AimPunchServices", "m_unpredictableBaseAngle"_hash ) );

			// Обе составляющие -- углы (QAngle), и они складываются покомпонентно.
			// Z не трогаем: aim punch в CS2 плоский, третья компонента не
			// участвует в сносе прицела и её чтение только внесло бы шум.
			out.x = predictable.x + unpredictable.x;
			out.y = predictable.y + unpredictable.y;
			out.z = 0.0f;

			// Защита от мусора: после неудачного чтения (битый указатель,
			// выгруженная сущность) в поле может оказаться что угодно. Снос
			// больше нескольких градусов -- это уже не punch, а испорченная
			// память, и вычитать его из прицела значит гарантированно промахнуться.
			constexpr auto k_max_punch = 45.0f;
			if ( !std::isfinite( out.x ) || !std::isfinite( out.y )
				|| std::fabsf( out.x ) > k_max_punch || std::fabsf( out.y ) > k_max_punch )
			{
				return math::vector3{};
			}

			return out;
		}

		// Сервис ещё не создан (первый тик после респауна). Ноль здесь корректен:
		// отдачи в этот момент тоже нет.
		return out;
	}

	float shared::calculate_hitchance( const math::vector3& shoot_position, const math::vector3& aim_angle, const systems::hitboxes::entry& hitbox, const systems::bones::data& bone, float inaccuracy, float spread, int samples ) const
	{
		const auto total = spread + inaccuracy;


		if ( total < 0.0001f )
		{
			return 1.0f;
		}

		if ( samples <= 0 )
		{
			return 0.0f;
		}

		const auto capsule_start = bone.rotation.rotate_vector( hitbox.mins ) + bone.position;
		const auto capsule_end = bone.rotation.rotate_vector( hitbox.maxs ) + bone.position;
		const auto is_capsule = hitbox.radius > 0.001f;
		auto inverse_rotation = bone.rotation;
		inverse_rotation.x = -inverse_rotation.x;
		inverse_rotation.y = -inverse_rotation.y;
		inverse_rotation.z = -inverse_rotation.z;
		const auto box_ray_origin = inverse_rotation.rotate_vector( shoot_position - bone.position );

		const auto ray_vs_box = [ & ]( const math::vector3& ray_direction )
		{
			const auto direction = inverse_rotation.rotate_vector( ray_direction );
			auto entry{ 0.0f };
			auto exit{ 1.0f };

			const auto intersect_axis = [ & ]( float origin, float delta, float minimum, float maximum )
			{
				if ( std::fabs( delta ) < 1.0e-8f )
				{
					return origin >= minimum && origin <= maximum;
				}

				auto first = ( minimum - origin ) / delta;
				auto second = ( maximum - origin ) / delta;
				if ( first > second )
				{
					std::swap( first, second );
				}

				entry = std::max( entry, first );
				exit = std::min( exit, second );
				return entry <= exit;
			};

			return intersect_axis( box_ray_origin.x, direction.x, hitbox.mins.x, hitbox.maxs.x ) &&
				intersect_axis( box_ray_origin.y, direction.y, hitbox.mins.y, hitbox.maxs.y ) &&
				intersect_axis( box_ray_origin.z, direction.z, hitbox.mins.z, hitbox.maxs.z );
		};

		math::vector3 forward{}, left{}, up{};
		math::helpers::angle_vectors_left( aim_angle, &forward, &left, &up );

		// Every candidate in a scan uses the same weapon state. The engine spread
		// function is much more expensive than the capsule test, so calculate each
		// deterministic seed once and reuse it for all candidate points.
		struct spread_cache
		{
			float inaccuracy{};
			float spread{};
			float recoil_index{};
			int item_def_idx{};
			int num_bullets{};
			int count{};
			bool initialized{};
			std::array<math::vector2, 256> values{};
		};

		// Four slots, not one.
		//
		// A single slot was right when a tick used one inaccuracy, but the scan now
		// asks about several within the same tick -- the predicted cone, the cone
		// after an autostop, the standing cone, and the crouched one. Each switch
		// evicted the other, so a tick could refill 256 engine calls three or four
		// times over for values it had already computed a moment earlier.
		//
		// The keys stay exact. Rounding them to share a slot would be cheaper still
		// and would quietly answer with the spread of a cone the weapon does not
		// have, which is the kind of shortcut that costs a session to find.
		// Восемь слотов, не четыре.
		//
		// Четыре стояли ровно по числу конусов, о которых спрашивает один тик, то
		// есть кэш работал на пределе: любой пятый запрос (например, конус без
		// пересчёта штрафа при no_spread) выбивал слот, и следующий тик заново
		// считал 256 движковых значений. Восемь дают запас, а память здесь
		// копеечная -- 2 КБ на слот в thread_local.
		constexpr auto k_cache_slots = 8;
		thread_local std::array<spread_cache, k_cache_slots> caches{};
		thread_local int next_slot{};

		spread_cache* found = nullptr;
		for ( auto& candidate : caches )
		{
			if ( candidate.initialized && candidate.inaccuracy == inaccuracy && candidate.spread == spread &&
				candidate.recoil_index == this->m_ctx.recoil_index && candidate.item_def_idx == this->m_ctx.item_def_idx &&
				candidate.num_bullets == this->m_ctx.num_bullets )
			{
				found = &candidate;
				break;
			}
		}

		if ( !found )
		{
			found = &caches[ next_slot ];
			next_slot = ( next_slot + 1 ) % k_cache_slots;

			found->inaccuracy = inaccuracy;
			found->spread = spread;
			found->recoil_index = this->m_ctx.recoil_index;
			found->item_def_idx = this->m_ctx.item_def_idx;
			found->num_bullets = this->m_ctx.num_bullets;
			found->count = 0;
			found->initialized = true;
		}

		auto& cache = *found;

		const auto cached_samples = std::min( samples, static_cast< int >( cache.values.size( ) ) );

		auto hits{ 0 };
		auto misses{ 0 };

		// Early exit thresholds - РµСЃР»Рё СЂРµР·СѓР»СЊС‚Р°С‚ РћР§Р•Р’РР”Р•Рќ, РЅРµ СЃС‡РёС‚Р°РµРј РІСЃС‘
		// РџСЂРѕРІРµСЂСЏРµРј РєР°Р¶РґС‹Рµ 16 samples РЅР°С‡РёРЅР°СЏ СЃ 32
		constexpr auto early_check_interval = 16;
		constexpr auto min_samples_before_early_exit = 32;
		constexpr auto certain_hit_ratio = 0.95f;    // running ratio above this = stop counting
		constexpr auto certain_miss_ratio = 0.15f;   // running ratio below this = stop counting

		for ( auto i = 0; i < samples; ++i )
		{
			// Разброс считаем ЛЕНИВО, по ходу сэмплирования, а не все 256 заранее.
			//
			// Раньше значения заполнялись до цикла, и ранний выход ниже экономил
			// только тесты луча -- движковые вызовы calculate_spread всё равно
			// выполнялись все 256. А вызов движка на порядки дороже теста
			// капсулы, и ранний выход как раз срабатывает на постоянных
			// промахах, где экономить нужнее всего. Кэш сохранён: посчитанное
			// значение кладётся в него и переживает следующие вызовы с тем же
			// состоянием оружия.
			if ( i < cached_samples && i >= cache.count )
			{
				cache.values[ i ] = this->calculate_spread( i, inaccuracy, spread, this->m_ctx.recoil_index, this->m_ctx.item_def_idx, this->m_ctx.num_bullets );
				cache.count = i + 1;
			}

			const auto calculated_spread = i < cache.count
				? cache.values[ i ]
				: this->calculate_spread( i, inaccuracy, spread, this->m_ctx.recoil_index, this->m_ctx.item_def_idx, this->m_ctx.num_bullets );
			const auto direction = forward + ( left * calculated_spread.x ) + ( up * calculated_spread.y );
			const auto ray_end = direction.normalized( ) * 8192.0f;

			auto hit{ false };
			if ( is_capsule )
			{
				auto fraction{ 1.0f };
				hit = this->ray_vs_capsule( shoot_position, ray_end, capsule_start, capsule_end, hitbox.radius, fraction );
			}
			else
			{
				hit = ray_vs_box( ray_end );
			}

			if ( hit )
			{
				++hits;
			}
			else
			{
				++misses;
			}

			// Early exit: СЂРµР·СѓР»СЊС‚Р°С‚ РћР§Р•Р’РР”Р•Рќ - РЅРµ С‚СЂР°С‚РёРј CPU
			const auto processed = i + 1;
			if ( processed >= min_samples_before_early_exit && ( processed % early_check_interval == 0 ) )
			{
				// hits/processed after 32+ samples is the sample mean, and it is a
				// perfectly good estimator of the final ratio -- the shipped design
				// was a deliberate speed/accuracy trade, not a bug. A guaranteed
				// bound over the full sample count can only clear these thresholds
				// once ~90% of the work is already done, so gating on one meant
				// every call ran all 256 samples. This is the hottest function in
				// the whole scan; that cost is not worth the last decimal place.
				const auto current_ratio = static_cast<float>( hits ) / static_cast<float>( processed );

				if ( current_ratio >= certain_hit_ratio )
				{
					return current_ratio;
				}

				if ( current_ratio <= certain_miss_ratio )
				{
					return current_ratio;
				}
			}
		}

		return static_cast< float >( hits ) / static_cast< float >( samples );
	}

	// =====================================================================
	// Bullet Speed Table + Travel Time Calculator
	// =====================================================================
	float shared::get_bullet_speed( std::uint16_t item_def_idx ) const
	{
		using namespace cstypes::item_definition_index;

		switch ( item_def_idx ) {
			// Snipers (highest velocity)
			case weapon_awp:              return 3960.0f;
			case weapon_ssg_08:           return 3300.0f;   // Scout
			case weapon_g3sg1:            return 3400.0f;
			case weapon_scar_20:          return 3400.0f;

			// Rifles
			case weapon_ak_47:            return 2400.0f;
			case weapon_m4a4:             return 2200.0f;
			case weapon_aug:              return 2400.0f;
			case weapon_sg_553:           return 2400.0f;
			case weapon_famas:            return 2300.0f;
			case weapon_galil_ar:         return 2200.0f;

			// Pistols
			case weapon_desert_eagle:     return 2000.0f;
			case weapon_glock_18:         return 1200.0f;
			case weapon_five_seven:       return 1650.0f;
			case weapon_p250:             return 1300.0f;
			case weapon_tec_9:            return 1200.0f;
			case weapon_dual_berettas:    return 1200.0f;

			// SMG
			case weapon_mac_10:           return 1300.0f;
			case weapon_p90:              return 2350.0f;

			// LMG
			case weapon_m249:             return 2600.0f;

			// Fallback (unknown weapon)
			default:                      return 2500.0f;
		}
	}

	int shared::calculate_bullet_travel_ticks(
		const math::vector3& shoot_pos,
		const math::vector3& target_pos,
		std::uint16_t item_def_idx
	) const
	{
		const float bullet_speed = this->get_bullet_speed( item_def_idx );
		if ( bullet_speed < 1.0f ) return 0;

		const float distance = ( target_pos - shoot_pos ).length();
		const float travel_time = distance / bullet_speed;

		return static_cast< int >( travel_time / cstypes::tick_interval + 0.5f );
	}

	math::vector3 shared::find_spread_correction( const math::vector3& aim_angle, int tick ) const
	{		// Roll is what makes this solvable in closed form.
		//
		// The engine fires along  forward + right * s.x + up * s.y, with the basis
		// built from the angles we send -- and roll rotates ( right, up ) about
		// forward. A roll of -atan2( s.x, s.y ) therefore turns the deviation into a
		// purely vertical one of magnitude |s|, whichever way it originally pointed,
		// and a pitch offset of atan( |s| ) cancels that exactly. No tolerance, no
		// residual error.
		//
		// What is left is circular: the seed depends on the angle we send, and the
		// angle we send depends on the seed. So solve it as a fixed point. Sweeping
		// pitch enumerates candidate seeds cheaply; for each one, build the angle
		// that seed implies and ask whether that angle reproduces the same seed. The
		// test is exact.
		//
		// A two-dimensional grid search over pitch and yaw, matching the resulting
		// bullet direction against the target within some epsilon, replaced this for
		// a while. It cannot work: the epsilon has to grow with the cone, so a wide
		// cone lands the shot as far off as the grid is coarse -- and it never emits
		// roll, which is the only thing that can cancel the horizontal half of the
		// deviation in the first place.
		//
		// The whole method rests on the seed actually varying with the send angle.
		// If the engine function behind get_spread_seed did not resolve, memory::call
		// hands back 0 for every candidate and the sweep "succeeds" on the first
		// angle it tries -- a silent failure that looks exactly like the aimbot being
		// unable to hit anything, so say it out loud once instead.
		static const auto seed_fn = PATTERN( patterns::get_tick_view_angles );
		if ( !seed_fn )
		{
			static std::atomic_bool warned{};
			if ( !warned.exchange( true, std::memory_order_relaxed ) )
			{
				diag::step( "[no spread] get_tick_view_angles pattern is missing; spread correction cannot work" );
			}

			return {};
		}

		const auto& no_spread_cfg = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );

		// Насколько плотно перебирать пару (pitch, yaw) на втором проходе.
		// Настройка задаёт число итераций внешнего цикла: больше -- мельче шаг
		// и выше шанс найти неподвижную точку на широком конусе, но линейно
		// дороже. 96 -- прежний шаг 0.5 в пределах ±6/+10, то есть поведение
		// по умолчанию не меняется.
		const auto iteration_scale = std::clamp( no_spread_cfg.no_spread_iterations.value, 16, 512 );

		for ( auto i = 0; i < 720; i++ )
		{
			const auto test_angles = math::vector3{ static_cast< float >( i ) / 2.0f, aim_angle.y, 0.0f };
			const auto seed = this->get_spread_seed( test_angles, tick );
			const auto spread = this->calculate_spread( seed, this->m_ctx.inaccuracy, this->m_ctx.spread, this->m_ctx.recoil_index, this->m_ctx.item_def_idx, this->m_ctx.num_bullets );

			auto adj_angle = aim_angle;
			adj_angle.x += math::helpers::rad_to_deg( std::atan( std::sqrt( spread.x * spread.x + spread.y * spread.y ) ) );
			adj_angle.z = -math::helpers::rad_to_deg( std::atan2( spread.x, spread.y ) );

			if ( this->get_spread_seed( adj_angle, tick ) == seed )
			{
				return adj_angle;
			}
		}

		// Второй проход: yaw тоже свободен.
		//
		// seed -- функция ОБОИХ углов, а первый проход держит yaw жёстко на
		// aim_angle.y. Для узких конусов этого хватает, но чем шире разброс
		// (автомат в движении, дробовик, большая дистанция), тем чаще
		// неподвижной точки на фиксированном yaw просто нет -- и выстрел
		// отбрасывался целиком, хотя решение существовало рядом.
		//
		// Здесь перебираем пару (pitch, yaw) вокруг исходного направления.
		// Критерий тот же (угол воспроизводит свой seed), но из всех найденных
		// берём тот, что ближе всего к исходному aim_angle: компенсация не
		// должна заметно уводить ствол.
		{
			// Границы подобраны по реальным конусам, а не на глаз.
			//
			// yaw: движущийся автомат даёт разброс порядка 1.5-2 градусов, а
			// револьвер/дробовик -- заметно больше. Первоначальные ±2 градуса
			// обрезали ровно те случаи, ради которых проход и писался: при
			// широком конусе неподвижной точки на фиксированном yaw нет, а
			// ближайшая лежит дальше двух градусов. ±6 покрывает дробовик в
			// прыжке с запасом, цена -- 25 значений вместо 9.
			//
			// pitch: вертикальный диапазон шире потому, что компенсация именно
			// питчем и гасит |s| (см. вывод выше), и на широком конусе |s|
			// переваливает за 5 градусов.
			//
			// Шаг 0.5 держим: seed -- кусочно-постоянная функция квантованного
			// угла, и мельче шага квантования он бессмысленен (соседние
			// отсчёты вернут тот же seed, удвоив работу впустую).
			constexpr auto yaw_span{ 6.0f };      // ± градусов вокруг исходного yaw
			constexpr auto pitch_span{ 10.0f };   // ± градусов вокруг исходного pitch

			// Шаг выводится из настройки так, чтобы произведение числа
			// значений по обеим осям было примерно равно заданному числу
			// итераций. При значении по умолчанию (96) это даёт прежние
			// 0.5/0.5 -- то есть дефолт повторяет проверенное поведение,
			// а не задаёт новое.
			const auto per_axis = std::max( 3, static_cast< int >( std::sqrt( static_cast< float >( iteration_scale ) ) ) );
			const auto yaw_step = ( yaw_span * 2.0f ) / static_cast< float >( per_axis );
			const auto pitch_step = ( pitch_span * 2.0f ) / static_cast< float >( per_axis );

			auto best_angle = math::vector3{};
			auto best_distance = std::numeric_limits< float >::max( );
			auto found{ false };

			for ( auto pitch_offset = -pitch_span; pitch_offset <= pitch_span; pitch_offset += pitch_step )
			{
				for ( auto yaw_offset = -yaw_span; yaw_offset <= yaw_span; yaw_offset += yaw_step )
				{
					// Кандидат крутим вокруг НУЛЕВОГО roll: именно так
					// компенсация и устроена (см. комментарий выше -- roll
					// гасит горизонтальную половину отклонения).
					const auto candidate_pitch = aim_angle.x + pitch_offset;
					const auto candidate_yaw = aim_angle.y + yaw_offset;

					const auto test_angles = math::vector3{ candidate_pitch, candidate_yaw, 0.0f };
					const auto seed = this->get_spread_seed( test_angles, tick );
					if ( seed == 0 )
					{
						continue;
					}

					const auto spread = this->calculate_spread( seed, this->m_ctx.inaccuracy, this->m_ctx.spread, this->m_ctx.recoil_index, this->m_ctx.item_def_idx, this->m_ctx.num_bullets );

					auto adj_angle = math::vector3{ candidate_pitch, candidate_yaw, 0.0f };
					adj_angle.x += math::helpers::rad_to_deg( std::atan( std::sqrt( spread.x * spread.x + spread.y * spread.y ) ) );
					adj_angle.z = -math::helpers::rad_to_deg( std::atan2( spread.x, spread.y ) );

					if ( this->get_spread_seed( adj_angle, tick ) != seed )
					{
						continue;
					}

					// Насколько далеко компенсация уводит ствол от исходной
					// точки прицеливания. Это цена решения.
					const auto distance = math::helpers::angle_distance( adj_angle, aim_angle );
					if ( distance < best_distance )
					{
						best_distance = distance;
						best_angle = adj_angle;
						found = true;
					}
				}
			}

			if ( found )
			{
				return best_angle;
			}
		}

		// Третий проход: широкий конус и грубая сетка.
		//
		// Первые два прохода перебирают окрестность aim_angle. На узком
		// конусе это правильно -- компенсация не должна уводить ствол. Но
		// чем шире конус, тем дальше от исходного направления лежит
		// неподвижная точка, и на дробовике/пулемёте она может оказаться
		// вообще за пределами ±6°.
		//
		// Здесь считается точная верхняя граница: питч, который гасит |s|,
		// равен asin(|s|) / 2 в градусах, и искать дальше этого нет смысла.
		// Зато внутри этой границы сетка берётся пошире (шаг 1°), потому
		// что грубое разрешение здесь не портит выстрел: точка и так
		// далеко от прицела, и уточнять её до десятых долей градуса
		// бессмысленно.
		{
			// Оцениваем |s| по опорной точке, чтобы знать, где искать.
			const auto seed0 = this->get_spread_seed( math::vector3{ aim_angle.x, aim_angle.y, 0.0f }, tick );

			if ( seed0 != 0 )
			{
				const auto spread0 = this->calculate_spread( seed0, this->m_ctx.inaccuracy, this->m_ctx.spread, this->m_ctx.recoil_index, this->m_ctx.item_def_idx, this->m_ctx.num_bullets );
				const auto magnitude = std::sqrt( spread0.x * spread0.x + spread0.y * spread0.y );
				const auto reach_deg = math::helpers::rad_to_deg( std::atan( magnitude ) ) + 2.0f;

				// Ищем по большому кресту: pitch вверх/вниз на
				// reach_deg, yaw в сторону на столько же.
				const auto span = std::min( reach_deg, 25.0f );
				constexpr auto step{ 1.0f };

				auto best_angle = math::vector3{};
				auto best_distance = std::numeric_limits< float >::max( );
				auto found{ false };

				for ( auto pitch_offset = -span; pitch_offset <= span; pitch_offset += step )
				{
					for ( auto yaw_offset = -span; yaw_offset <= span; yaw_offset += step )
					{
						const auto candidate_pitch = aim_angle.x + pitch_offset;
						const auto candidate_yaw = aim_angle.y + yaw_offset;
						const auto test_angles = math::vector3{ candidate_pitch, candidate_yaw, 0.0f };
						const auto seed = this->get_spread_seed( test_angles, tick );

						if ( seed == 0 )
						{
							continue;
						}

						const auto spread = this->calculate_spread( seed, this->m_ctx.inaccuracy, this->m_ctx.spread, this->m_ctx.recoil_index, this->m_ctx.item_def_idx, this->m_ctx.num_bullets );

						auto adj_angle = math::vector3{ candidate_pitch, candidate_yaw, 0.0f };
						adj_angle.x += math::helpers::rad_to_deg( std::atan( std::sqrt( spread.x * spread.x + spread.y * spread.y ) ) );
						adj_angle.z = -math::helpers::rad_to_deg( std::atan2( spread.x, spread.y ) );

						if ( this->get_spread_seed( adj_angle, tick ) != seed )
						{
							continue;
						}

						// Здесь цена решения -- не близость к прицелу, а
						// малость самой компенсации: чем меньше |s|
						// оказался у решения, тем меньше ствол уводит.
						const auto cost = magnitude;
						if ( cost < best_distance )
						{
							best_distance = cost;
							best_angle = adj_angle;
							found = true;
						}
					}
				}

				if ( found )
				{
					return best_angle;
				}
			}
		}

		// No fixed point in the sweep, so the shot is dropped. Worth counting: a
		// steady stream of these is the difference between no_spread being off and
		// no_spread aiming at something it cannot solve for.
		{
			static std::atomic<std::uint32_t> misses{};
			const auto count = misses.fetch_add( 1, std::memory_order_relaxed ) + 1;

			if ( count == 1 || ( count % 100 ) == 0 )
			{
				char line[ 160 ]{};
				_snprintf_s( line, sizeof( line ), _TRUNCATE,
					"[no spread] correction search failed %u times (inaccuracy %.4f, spread %.4f)",
					count, this->m_ctx.inaccuracy, this->m_ctx.spread );
				diag::step( line );
			}
		}

		return {};
	}

	math::vector3 shared::get_eye_position( std::uintptr_t local_pawn ) const
	{
		const auto game_scene_node = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		const auto view_offset = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );
		return origin + view_offset;
	}

	math::vector3 shared::get_shoot_position( ) const
	{
		math::vector3 out{};
		memory::call_vfunc<void>( this->m_ctx.weapon_services, 29, reinterpret_cast< std::uintptr_t >( &out ) );
		return out;
	}

	math::vector3 shared::get_interpolated_shoot_position( std::uintptr_t local_pawn, bool newest ) const
	{
		const auto ws = this->m_ctx.weapon_services;
		const auto head = memory::read<int>( ws + 872 );
		const auto count = memory::read<int>( ws + 876 );

		if ( count < 1 )
		{
			return this->get_shoot_position( );
		}

		if ( newest )
		{
			const auto newest_idx = ( head + count - 1 ) % 32;
			const auto newest_off = ws + 232 + 20ull * newest_idx;
			return memory::read<math::vector3>( newest_off + 8 );
		}

		if ( count < 2 )
		{
			return this->get_shoot_position( );
		}

		const auto interp = memory::call<float>(PATTERN (patterns::get_interp_amount), local_pawn );
		const auto newest_idx = ( head + count - 1 ) % 32;
		const auto newest_off = ws + 232 + 20ull * newest_idx;
		const auto newest_tick = memory::read<int>( newest_off );
		const auto newest_frac = memory::read<float>( newest_off + 4 );

		const auto target = cstypes::tick_fraction{ newest_tick, newest_frac }.subtract_value( interp * 64.0f );

		for ( auto i = 0; i < count - 1; ++i )
		{
			const auto idx_a = ( head + static_cast< std::size_t >( i ) ) % 32;
			const auto idx_b = ( head + static_cast< std::size_t >( i ) + 1 ) % 32;

			const auto a_off = ws + 232 + 20ull * idx_a;
			const auto b_off = ws + 232 + 20ull * idx_b;

			const auto a_tick = memory::read<int>( a_off );
			const auto a_frac = memory::read<float>( a_off + 4 );
			const auto b_tick = memory::read<int>( b_off );
			const auto b_frac = memory::read<float>( b_off + 4 );

			const auto a_before = a_tick < target.tick || ( a_tick == target.tick && a_frac <= target.frac );
			if ( !a_before )
			{
				break;
			}

			const auto b_after = b_tick > target.tick || ( b_tick == target.tick && b_frac >= target.frac );
			if ( !b_after )
			{
				continue;
			}

			const auto a_pos = memory::read<math::vector3>( a_off + 8 );
			const auto b_pos = memory::read<math::vector3>( b_off + 8 );

			const auto span = cstypes::tick_fraction{ b_tick, b_frac }.subtract( { a_tick, a_frac } );
			const auto partial = target.subtract( { a_tick, a_frac } );

			const auto total_f = static_cast< float >( span.tick ) + span.frac;
			const auto partial_f = static_cast< float >( partial.tick ) + partial.frac;

			const auto t = total_f > 0.0f ? partial_f / total_f : 0.0f;

			return a_pos + ( b_pos - a_pos ) * t;
		}

		return this->get_shoot_position( );
	}

	int shared::calculate_stop_ticks( const math::vector3& velocity, float max_speed, std::uintptr_t local_pawn ) const
	{
		auto vel = velocity;
		vel.z = 0.0f;

		auto ticks{ 0 };
		const auto sv_friction = CONVAR ("sv_friction")->get<float>( );
		const auto sv_stopspeed = CONVAR ("sv_stopspeed")->get<float>( );
		const auto sv_accelerate = CONVAR ("sv_accelerate")->get<float>( );
		const auto surface_friction = systems::g_prediction.pre( ).surface_friction;
		const auto accurate_threshold = max_speed * 0.34f;

		const auto is_scoped = this->m_ctx.is_scoped;
		auto max_move_speed{ 250.0f };

		if ( is_scoped && local_pawn )
		{
			const auto movement_services = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
			if ( movement_services )
			{
				max_move_speed = memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) );
			}
		}

		while ( vel.length_2d( ) > accurate_threshold && ticks < 15 )
		{
			const auto speed = vel.length_2d( );
			if ( speed <= 0.0f )
			{
				break;
			}

			const auto control = std::fmaxf( speed, sv_stopspeed );
			const auto drop = sv_friction * surface_friction * control * cstypes::tick_interval;
			auto new_speed = std::fmaxf( speed - drop, 0.0f );

			auto accel = sv_accelerate;

			if ( is_scoped )
			{
				const auto weapon_ratio = std::fminf( 1.0f, max_speed / 250.0f );
				const auto scoped_max = std::fmaxf( 250.0f, max_move_speed ) * weapon_ratio * 0.52f;

				if ( new_speed > scoped_max - 5.0f )
				{
					const auto t = 1.0f - std::fmaxf( 0.0f, new_speed - ( scoped_max - 5.0f ) ) / std::fmaxf( 0.01f, 5.0f );
					accel *= std::clamp( t, 0.0f, 1.0f );
				}
			}

			const auto accel_speed = std::fminf( accel * max_speed * surface_friction * cstypes::tick_interval, new_speed );
			new_speed = std::fmaxf( new_speed - accel_speed, 0.0f );

			vel *= ( new_speed / speed );
			ticks++;
		}

		return ticks;
	}

	float shared::get_spread( ) const
	{
		// Same two-entry table as the inaccuracy above, same reason for switching.
		const auto mode_offset = SCHEMA( "C_CSWeaponBase", "m_weaponMode"_hash );
		const auto ask_as_secondary = mode_offset > 0 && this->quick_revolver_active( );
		const auto previous_mode = ask_as_secondary
			? memory::read<int>( this->m_ctx.weapon + mode_offset )
			: 0;

		if ( ask_as_secondary )
		{
			memory::write( this->m_ctx.weapon + mode_offset, 1 );
		}

		static const auto get_spread = PATTERN( patterns::get_spread );
		const auto spread = memory::call<float>( get_spread, this->m_ctx.weapon );

		if ( ask_as_secondary )
		{
			memory::write( this->m_ctx.weapon + mode_offset, previous_mode );
		}

		return spread;
	}

	float shared::get_inaccuracy( bool update_accuracy_penalty ) const
	{
		const auto accuracy_state_begin = SCHEMA( "C_CSWeaponBase", "m_flTurningInaccuracyDelta"_hash );
		const auto accuracy_state_end = SCHEMA( "C_CSWeaponBase", "m_flRecoilIndex"_hash );
		if ( !this->m_ctx.weapon || accuracy_state_begin <= 0 || accuracy_state_end < accuracy_state_begin )
		{
			return 0.0f;
		}

		const auto accuracy_state_size = static_cast< std::size_t >( accuracy_state_end - accuracy_state_begin ) + sizeof( float );
		if ( accuracy_state_size > 0x100 )
		{
			return 0.0f;
		}

		std::vector<std::uint8_t> backup( accuracy_state_size );
		std::memcpy( backup.data( ), reinterpret_cast< const void* >( this->m_ctx.weapon + accuracy_state_begin ), accuracy_state_size );

		if ( update_accuracy_penalty )
		{
			memory::call<void>(PATTERN (patterns::weapon_update_accuracy), this->m_ctx.weapon );
		}

		// A weapon carries two sets of accuracy figures, one per fire mode, and the
		// engine picks between them with m_weaponMode. While we plan a shot the
		// mode is still Primary, so asking about a revolver quick shot answered for
		// the hammer-cocked one instead -- a far narrower cone than the shot really
		// uses. no_spread then solved that narrower cone exactly and the bullet
		// still went wide, which is what the log showed: hitchance 100%, reason
		// spread, over and over.
		//
		// So ask in the mode the round will leave in, and put the field back.
		const auto mode_offset = SCHEMA( "C_CSWeaponBase", "m_weaponMode"_hash );
		const auto ask_as_secondary = mode_offset > 0 && this->quick_revolver_active( );
		const auto previous_mode = ask_as_secondary
			? memory::read<int>( this->m_ctx.weapon + mode_offset )
			: 0;

		if ( ask_as_secondary )
		{
			memory::write( this->m_ctx.weapon + mode_offset, 1 );
		}

		static const auto get_inaccuracy = PATTERN( patterns::get_inaccuracy );
		m_in_self_inaccuracy_call = true;
		const auto inaccuracy = memory::call<float>(
			get_inaccuracy, this->m_ctx.weapon,
			static_cast<float*>( nullptr ), static_cast<float*>( nullptr ) );
		m_in_self_inaccuracy_call = false;

		if ( ask_as_secondary )
		{
			memory::write( this->m_ctx.weapon + mode_offset, previous_mode );
		}

		std::memcpy( reinterpret_cast< void* >( this->m_ctx.weapon + accuracy_state_begin ), backup.data( ), accuracy_state_size );

		return inaccuracy;
	}

	float shared::get_inaccuracy_at_velocity( std::uintptr_t local_pawn, const math::vector3& velocity ) const
	{
		const auto accuracy_state_begin = SCHEMA( "C_CSWeaponBase", "m_flTurningInaccuracyDelta"_hash );
		const auto accuracy_state_end = SCHEMA( "C_CSWeaponBase", "m_flRecoilIndex"_hash );
		if ( !this->m_ctx.weapon || !local_pawn || accuracy_state_begin <= 0 || accuracy_state_end < accuracy_state_begin )
		{
			return 0.0f;
		}

		const auto accuracy_state_size = static_cast< std::size_t >( accuracy_state_end - accuracy_state_begin ) + sizeof( float );
		if ( accuracy_state_size > 0x100 )
		{
			return 0.0f;
		}

		std::vector<std::uint8_t> backup( accuracy_state_size );
		std::memcpy( backup.data( ), reinterpret_cast< const void* >( this->m_ctx.weapon + accuracy_state_begin ), accuracy_state_size );

		const auto old_velocity = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
		const auto old_eflags = memory::read<std::uint32_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_iEFlags"_hash ) );

		memory::write( local_pawn + SCHEMA( "C_BaseEntity", "m_iEFlags"_hash ), old_eflags & ~0x1000u );
		memory::write( local_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ), velocity );

		memory::call<void>(PATTERN (patterns::weapon_update_accuracy), this->m_ctx.weapon );

		// A weapon carries two sets of accuracy figures, one per fire mode, and the
		// engine picks between them with m_weaponMode. While we plan a shot the
		// mode is still Primary, so asking about a revolver quick shot answered for
		// the hammer-cocked one instead -- a far narrower cone than the shot really
		// uses. no_spread then solved that narrower cone exactly and the bullet
		// still went wide, which is what the log showed: hitchance 100%, reason
		// spread, over and over.
		//
		// So ask in the mode the round will leave in, and put the field back.
		const auto mode_offset = SCHEMA( "C_CSWeaponBase", "m_weaponMode"_hash );
		const auto ask_as_secondary = mode_offset > 0 && this->quick_revolver_active( );
		const auto previous_mode = ask_as_secondary
			? memory::read<int>( this->m_ctx.weapon + mode_offset )
			: 0;

		if ( ask_as_secondary )
		{
			memory::write( this->m_ctx.weapon + mode_offset, 1 );
		}

		static const auto get_inaccuracy = PATTERN( patterns::get_inaccuracy );
		m_in_self_inaccuracy_call = true;
		const auto inaccuracy = memory::call<float>(
			get_inaccuracy, this->m_ctx.weapon,
			static_cast<float*>( nullptr ), static_cast<float*>( nullptr ) );
		m_in_self_inaccuracy_call = false;

		if ( ask_as_secondary )
		{
			memory::write( this->m_ctx.weapon + mode_offset, previous_mode );
		}

		memory::write( local_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ), old_velocity );
		memory::write( local_pawn + SCHEMA( "C_BaseEntity", "m_iEFlags"_hash ), old_eflags );

		std::memcpy( reinterpret_cast< void* >( this->m_ctx.weapon + accuracy_state_begin ), backup.data( ), accuracy_state_size );

		return inaccuracy;
	}

	float shared::get_air_inaccuracy( float vertical_speed, float jump_initial, float jump_apex ) const
	{
		constexpr auto sqrt_threshold{ 17.37795666f };
		const auto val = ( ( std::sqrtf( std::fabsf( vertical_speed ) ) - sqrt_threshold * 0.25f ) * ( jump_initial - jump_apex ) ) / ( sqrt_threshold * 0.75f ) + jump_apex;
		return std::clamp( val, 0.0f, jump_initial * 2.0f );
	}

	bool shared::can_shoot( systems::input::usercmd* cmd, std::uintptr_t local_controller, bool check_next_attack ) const
	{
		if ( this->m_ctx.weapon_type != cstypes::weapon_type::knife )
		{
			if ( memory::read<bool>( this->m_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_bInReload"_hash ) ) )
			{
				return false;
			}

			if ( memory::read<int>( this->m_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_iClip1"_hash ) ) <= 0 )
			{
				return false;
			}
		}

		if ( !check_next_attack )
		{
			return true;
		}

		const auto tick_base = memory::read<int>( local_controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );
		const auto base_cmd = cmd->csgo_user_cmd.base( );
		const auto client_tick = base_cmd ? base_cmd->client_tick( ) : tick_base;
		const auto next_primary = memory::read<int>( this->m_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash ) );

		if ( this->m_ctx.weapon_type == cstypes::weapon_type::knife )
		{
			const auto next_secondary = memory::read<int>( this->m_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextSecondaryAttackTick"_hash ) );
			return tick_base >= this->m_last_shoot_tick + 2 && ( client_tick >= next_primary || client_tick >= next_secondary );
		}

		// A quick-shot revolver fires on attack2, which the game rate-limits on its
		// own timer. Gating only on the primary tick let the bot spam the button
		// through the secondary cooldown, and every command sent during it was a
		// shot that never happened.
		if ( this->m_ctx.item_def_idx == cstypes::item_definition_index::weapon_r8_revolver
			&& settings::g_combat.m_autos.revolver_quick.value )
		{
			const auto next_secondary = memory::read<int>( this->m_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextSecondaryAttackTick"_hash ) );
			return tick_base >= this->m_last_shoot_tick + 2 && client_tick >= next_primary && client_tick >= next_secondary;
		}

		return tick_base >= this->m_last_shoot_tick + 2 && client_tick >= next_primary;
	}

	bool shared::quick_revolver_active( ) const
	{
		return this->m_ctx.item_def_idx == cstypes::item_definition_index::weapon_r8_revolver
			&& settings::g_combat.m_autos.revolver_quick.value;
	}

	void shared::note_pending_shot( float predicted_inaccuracy, bool quick_revolver, int command_tick )
	{
		m_pending_shot_inaccuracy.store( predicted_inaccuracy, std::memory_order_relaxed );
		m_pending_shot_tick.store( command_tick, std::memory_order_relaxed );
		m_pending_shot_quick_revolver.store( quick_revolver, std::memory_order_relaxed );
	}

	void shared::on_engine_fire_inaccuracy( std::uintptr_t weapon, float actual ) const
	{
		// This used to be gated on "the rage bot is firing on this very tick", and
		// across four sessions it never once matched. That is itself the finding:
		// the revolver's alt shot does not leave on the command that asks for it.
		//
		// So measure the gap instead of assuming there is none. The latch below
		// stays armed after the shot is sent, and the first time the engine prices
		// a shot afterwards we record how many ticks had passed. That number is the
		// offset find_spread_correction has to solve for -- the seed is a function
		// of the tick, so being one tick out makes the compensation worthless and
		// leaves exactly the full-width random spread the impact log shows.
		if ( !m_pending_shot_quick_revolver.exchange( false, std::memory_order_relaxed ) )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		const auto now_tick = local.controller
			? memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) )
			: 0;

		const auto command_tick = m_pending_shot_tick.load( std::memory_order_relaxed );
		const auto predicted = m_pending_shot_inaccuracy.load( std::memory_order_relaxed );
		const auto delta = now_tick - command_tick;

		static std::atomic<std::uint32_t> seen{};
		const auto count = seen.fetch_add( 1, std::memory_order_relaxed ) + 1;

		if ( count <= 12 || ( count % 25 ) == 0 )
		{
			const auto ratio = predicted > 1.0e-6f ? actual / predicted : 0.0f;

			char line[ 208 ]{};
			_snprintf_s( line, sizeof( line ), _TRUNCATE,
				"[revolver] engine priced shot #%u: %+d ticks after command (cmd %d, now %d), predicted %.5f, used %.5f, ratio %.3f",
				count, delta, command_tick, now_tick, predicted, actual, ratio );
			diag::step( line );
		}
	}

	bool shared::is_max_accuracy( float inaccuracy ) const
	{
		const auto& prestate = systems::g_prediction.pre( );
		const auto on_ground = ( prestate.flags & 1 ) != 0;
		const auto is_ducking = ( prestate.flags & 4 ) != 0;
		const auto speed = prestate.networked_velocity.length_2d( );

		if ( on_ground )
		{
			if ( this->m_ctx.weapon_type == cstypes::weapon_type::sniper )
			{
				if ( !this->m_ctx.is_scoped )
				{
					return false;
				}

				if ( is_ducking )
				{
					const auto rounded = std::floorf( inaccuracy * 300.0f ) / 300.0f;
					return rounded < inaccuracy;
				}

				if ( speed <= 0.1f )
				{
					const auto rounded = std::floorf( inaccuracy * 170.0f ) / 170.0f;
					return rounded < inaccuracy;
				}

				return false;
			}

			// speed <= max_speed * 0.34f is the accurate-fire threshold, not maximum
			// accuracy: at 85 u/s it still answered true, so force_shot fired into a
			// cone that was nowhere near settled. Judge the inaccuracy we were handed
			// against the weapon's standing floor instead.
			if ( !this->m_ctx.weapon_vdata )
			{
				return speed <= this->m_ctx.weapon_max_speed * 0.34f;
			}

			const auto inaccuracy_stand = memory::read<float>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyStand"_hash ) );
			const auto inaccuracy_floor = std::max( inaccuracy_stand, 0.004f );
			return inaccuracy <= inaccuracy_floor * 1.15f + 0.001f;
		}

		const auto inaccuracy_jump_apex = memory::read<float>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_hash ) );
		const auto accuracy_penalty = memory::read<float>( this->m_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_fAccuracyPenalty"_hash ) );
		const auto min_air_inaccuracy = accuracy_penalty + inaccuracy_jump_apex;

		constexpr auto tolerance{ 0.001f };
		return inaccuracy <= min_air_inaccuracy + tolerance;
	}

	math::vector3 shared::simulate_aim_punch( int recoil_index ) const
	{
		if ( recoil_index <= 0 || !this->m_ctx.valid )
		{
			return {};
		}

		const auto weapon_mode = memory::read<int>( this->m_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_weaponMode"_hash ) );
		const auto cycle_time = memory::read<float>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flCycleTime"_hash ) );

		constexpr auto decay_rate{ 4.5f };
		constexpr auto decay2_exp{ 8.0f };
		constexpr auto decay2_lin{ 18.0f };
		constexpr auto recoil_scale{ 2.0f };

		math::vector3 punch{};
		math::vector3 punch_vel{};

		auto hybrid_decay = [ ]( math::vector3& v, float exp, float lin, float dt )
			{
				v *= std::expf( -exp * dt );

				const auto mag = v.length( );
				if ( mag > lin * dt )
				{
					v *= ( 1.0f - ( lin * dt ) / mag );
				}
				else
				{
					v = {};
				}
			};

		for ( auto i = 0; i < recoil_index; ++i )
		{
			float angle{}, magnitude{};
			memory::call<void>(PATTERN (patterns::weapon_get_recoil_offset), addresses::globals::weapon_recoil_data, this->m_ctx.weapon, weapon_mode, i, &angle, &magnitude );

			math::vector3 offset{};
			offset.x = std::cosf( math::helpers::deg_to_rad( angle ) ) * magnitude;
			offset.y = std::sinf( math::helpers::deg_to_rad( angle ) ) * magnitude;

			punch_vel -= offset;

			for ( auto time = 0.0f; time <= cycle_time; time += cstypes::tick_interval )
			{
				hybrid_decay( punch, decay2_exp, decay2_lin, cstypes::tick_interval );

				punch += punch_vel * cstypes::tick_interval * 0.5f;
				punch_vel *= std::expf( -decay_rate * cstypes::tick_interval );

				if ( punch_vel.length( ) < 0.03125f )
				{
					punch_vel = {};
				}

				punch += punch_vel * cstypes::tick_interval * 0.5f;
			}
		}

		return punch * recoil_scale;
	}

	bool shared::ray_vs_capsule( const math::vector3& ray_origin, const math::vector3& ray_dir, const math::vector3& capsule_a, const math::vector3& capsule_b, float radius, float& out_fraction ) const
	{
		const auto ab = capsule_b - capsule_a;
		const auto ab_sq = ab.dot( ab );
		const auto oc = ray_origin - capsule_a;
		const auto dir_sq = ray_dir.dot( ray_dir );

		if ( dir_sq < 1e-8f )
		{
			return false;
		}

		auto best_t{ 1.0f };
		auto hit{ false };

		if ( ab_sq > 1e-8f )
		{
			const float m = ab.dot( ray_dir ) / ab_sq;
			const float n = ab.dot( oc ) / ab_sq;

			const auto d_perp = ray_dir - ab * m;
			const auto oc_perp = oc - ab * n;

			const auto a = d_perp.dot( d_perp );
			const auto half_b = d_perp.dot( oc_perp );
			const auto c = oc_perp.dot( oc_perp ) - radius * radius;

			if ( a > 1e-8f )
			{
				const auto disc = half_b * half_b - a * c;
				if ( disc >= 0.0f )
				{
					const auto sqrt_disc = std::sqrt( disc );

					for ( int r = 0; r < 2; r++ )
					{
						const auto t = ( -half_b + ( r == 0 ? -sqrt_disc : sqrt_disc ) ) / a;
						if ( t < 0.0f || t >= best_t )
						{
							continue;
						}

						const auto s = m * t + n;
						if ( s >= 0.0f && s <= 1.0f )
						{
							best_t = t;
							hit = true;
							break;
						}
					}
				}
			}
		}

		const math::vector3 caps[ ]{ capsule_a, capsule_b };

		for ( int i = 0; i < 2; i++ )
		{
			const auto co = ray_origin - caps[ i ];
			const auto half_b = co.dot( ray_dir );
			const auto c = co.dot( co ) - radius * radius;
			const auto disc = half_b * half_b - dir_sq * c;

			if ( disc < 0.0f )
			{
				continue;
			}

			const auto sqrt_disc = std::sqrt( disc );

			for ( int r = 0; r < 2; r++ )
			{
				const auto t = ( -half_b + ( r == 0 ? -sqrt_disc : sqrt_disc ) ) / dir_sq;
				if ( t < 0.0f || t >= best_t )
				{
					continue;
				}

				if ( ab_sq > 1e-8f )
				{
					const auto hit_point = ray_origin + ray_dir * t - caps[ i ];
					const auto sign = i == 0 ? -1.0f : 1.0f;

					if ( sign * ab.dot( hit_point ) < 0.0f )
					{
						continue;
					}
				}

				best_t = t;
				hit = true;
				break;
			}
		}

		if ( hit )
		{
			out_fraction = best_t;
		}

		return hit;
	}


// =====================================================================
// SHOT TRACKING & FALLBACK LOGIC
// =====================================================================

void shared::resolver::on_shot_fired( std::uintptr_t target_pawn )
{
	if ( !target_pawn )
	{
		return;
	}

	std::unique_lock lock( this->m_mtx );
	auto& data = this->m_entries[ target_pawn ];
	data.last_shot_time = memory::read<float>( addresses::globals::global_vars + 0x30 );
}

void shared::resolver::on_shot_target_hitbox( std::uintptr_t target_pawn, int hitbox_index )
{
	if ( !target_pawn )
	{
		return;
	}

	std::unique_lock lock( this->m_mtx );
	auto& data = this->m_entries[ target_pawn ];
	data.last_shot_hitbox = hitbox_index;
	data.last_shot_time = memory::read<float>( addresses::globals::global_vars + 0x30 );
}

void shared::resolver::on_shot_hit( std::uintptr_t target_pawn )
{
	if ( !target_pawn )
	{
		return;
	}

	std::unique_lock lock( this->m_mtx );
	auto& data = this->m_entries[ target_pawn ];
	data.hits++;
	data.misses = 0;

	// Head hit → сбрасываем fallback (голова снова видна)
	if ( data.last_shot_hitbox == 0 )
	{
		data.head_miss_streak = 0;
		data.force_body_aim = false;
		data.force_body_until = 0.0f;
	}

	// Доверяем текущему angle delta — усиливаем сглаженную историю
	if ( data.has_angle_data && data.history_count > 0 )
	{
		const auto idx = ( data.history_index - 1 + 8 ) % 8;
		data.desync_history[ idx ] = data.detected_desync * 1.15f;
	}
}

void shared::resolver::on_shot_missed( std::uintptr_t target_pawn )
{
	if ( !target_pawn )
	{
		return;
	}

	std::unique_lock lock( this->m_mtx );
	auto& data = this->m_entries[ target_pawn ];
	data.misses++;

	// Отдельный счётчик промахов В ГОЛОВУ → триггер fake duck fallback
	if ( data.last_shot_hitbox == 0 )
	{
		data.head_miss_streak++;

		// NEW: используем настройки вместо hardcoded значений
		if ( data.head_miss_streak >= settings::g_combat.m_resolver.head_misses_trigger )
		{
			const auto curtime = memory::read<float>( addresses::globals::global_vars + 0x30 );
			data.force_body_aim = true;
			data.force_body_until = curtime + settings::g_combat.m_resolver.body_aim_duration;
		}
	}

	// Общий brute-force резольв — раз в 2 промаха меняем сторону
	if ( data.misses >= 2 )
	{
		data.brute_side = next_side( data.brute_side );
		data.misses = 0;
	}
}

bool shared::resolver::should_force_body_aim( std::uintptr_t target_pawn ) const
{
	if ( !target_pawn )
	{
		return false;
	}

	std::shared_lock lock( this->m_mtx );
	const auto it = this->m_entries.find( target_pawn );
	if ( it == this->m_entries.end( ) )
	{
		return false;
	}

	const auto& data = it->second;

	// Активен ли force body aim и не истёк ли таймаут?
	if ( !data.force_body_aim )
	{
		return false;
	}

	if ( memory::read<float>( addresses::globals::global_vars + 0x30 ) > data.force_body_until )
	{
		return false;  // таймаут — снова пробуем head в следующем скане
	}

	return true;
}

float shared::resolver::resolve_yaw( std::uintptr_t target_pawn, float original_yaw ) const
	{
		if ( !target_pawn )
		{
			return original_yaw;
		}

		std::shared_lock lock( this->m_mtx );
		const auto it = this->m_entries.find( target_pawn );
		if ( it == this->m_entries.end( ) )
		{
			return original_yaw;
		}

		return std::remainderf( original_yaw + side_offset( it->second.brute_side ), 360.0f );
	}

	bool shared::resolver::is_prone( std::uintptr_t target_pawn, const lagcomp::record* record ) const
	{
		if ( !record || !record->valid || !target_pawn )
		{
			return false;
		}

		constexpr auto k_head_bone = 6;
		constexpr auto k_pelvis_bone = 2;
		if ( record->bone_count <= k_head_bone )
		{
			return false;
		}

		const auto head_z = record->bones[ k_head_bone ].position.z;
		const auto pelvis_z = record->bones[ k_pelvis_bone ].position.z;
		return ( head_z - pelvis_z ) < 20.0f;
	}

	shared::resolver::yaw_side shared::resolver::current_side( std::uintptr_t target_pawn ) const
	{
		if ( !target_pawn )
		{
			return yaw_side::real;
		}

		std::shared_lock lock( this->m_mtx );
		const auto it = this->m_entries.find( target_pawn );
		if ( it == this->m_entries.end( ) )
		{
			return yaw_side::real;
		}

		return it->second.brute_side;
	}

	/* NEW: обновление данных резольва per-target (вызывается каждый tick) */
	void shared::resolver::update_target_data( std::uintptr_t target_pawn )
	{
		if ( !target_pawn )
		{
			return;
		}

		std::unique_lock lock( this->m_mtx );
		auto& data = this->m_entries[ target_pawn ];

		/* Считываем eye и body yaw */
		const auto eye_yaw = this->read_eye_yaw( target_pawn );
		const auto body_yaw = this->read_body_yaw( target_pawn );

		/* Вычисляем desync */
		auto delta = std::remainderf( eye_yaw - body_yaw, 360.0f );
		if ( delta < 0.0f )
		{
			delta += 360.0f;
		}
		if ( delta > 180.0f )
		{
			delta = 360.0f - delta;
		}

		data.last_eye_yaw = eye_yaw;
		data.last_body_yaw = body_yaw;
		data.detected_desync = delta;
		data.has_angle_data = true;
		data.last_update_time = g_shared.ctx( ).current_time;

		/* Обновляем историю desync */
		data.desync_history[ data.history_index % 8 ] = delta;
		data.history_index++;
		if ( data.history_count < 8 )
		{
			data.history_count++;
		}
	}

	/* NEW: диагностика — сколько desync выявлено (для HUD) */
	float shared::resolver::get_detected_desync( std::uintptr_t target_pawn ) const
	{
		if ( !target_pawn )
		{
			return 0.0f;
		}

		std::shared_lock lock( this->m_mtx );
		const auto it = this->m_entries.find( target_pawn );
		if ( it == this->m_entries.end( ) )
		{
			return 0.0f;
		}

		return it->second.detected_desync;
	}

	/* NEW: чтение eye yaw из памяти */
	float shared::resolver::read_eye_yaw( std::uintptr_t pawn ) const
	{
		if ( !pawn )
		{
			return 0.0f;
		}

		/* Получаем viewangles через ССPlayer_ViewSetup */
		const auto view_angles = systems::g_input.get_view_angles( );
		return view_angles.y;
	}

	/* NEW: чтение body yaw из памяти */
	float shared::resolver::read_body_yaw( std::uintptr_t pawn ) const
	{
		if ( !pawn )
		{
			return 0.0f;
		}

		/* body_yaw хранится в m_flPoseParameter.x (bone controller) */
		/* Но для CS2 это сложнее — используем m_angEyeAngles */
		const auto eye_angles_addr = pawn + SCHEMA( "C_CSPlayerPawn", "m_angEyeAngles"_hash );
		if ( !eye_angles_addr )
		{
			return 0.0f;
		}

		const auto eye_angles = memory::read<math::vector3>( eye_angles_addr );
		return eye_angles.y;
	}

	/* NEW: сглаживание desync через историю */
	float shared::resolver::compute_smoothed_desync( const resolve_data& data ) const
	{
		if ( !data.has_angle_data || data.history_count < 2 )
		{
			return data.detected_desync;
		}

		/* Простое среднее значение из истории */
		float sum = 0.0f;
		for ( int i = 0; i < data.history_count; ++i )
		{
			sum += data.desync_history[ i ];
		}

		return sum / static_cast< float >( data.history_count );
	}

} // namespace features::combat
