#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

namespace features::changer {

	std::string g_last_applied_model{};

namespace {
	bool apply_model_safe (std::uintptr_t pawn, const char* model)
	{
		struct buffer_string {
			std::uint32_t m_unknown1 {};
			std::uint32_t m_unknown2 { 0xc00000c8 };
			union { std::uintptr_t m_str_ptr; std::uint8_t data[ 0xc8 ]; };
			std::uintptr_t m_unknown3 {};
			std::uintptr_t m_unknown4 {};
		} buffer;

		const auto init_path_buffer = PATTERN (patterns::init_particle_path_buffer);
		const auto precache_resource = PATTERN (patterns::resource_system_precache);
		const auto set_model = PATTERN (patterns::set_player_model);

		g_last_applied_model = model;

		__try
		{
			if ( init_path_buffer && precache_resource && addresses::globals::resource_system )
			{
				memory::call<void>( init_path_buffer, &buffer, model );
				buffer.m_unknown4 = 'ldmv';
				memory::call<void>( precache_resource, addresses::globals::resource_system, &buffer, "" );
			}

			memory::call<void>( set_model, pawn, model );
		}
		__except ( EXCEPTION_EXECUTE_HANDLER )
		{
			return false;
		}

		return true;
	}

	// MSVC rejects __try in any frame that also needs C++ unwinding (C2712), and
	// on_frame_stage_notify holds std::string / std::vector locals -- so the
	// guarded call gets its own frame, where every local is trivially
	// destructible. Resolve the pattern on the caller's side, not in here.
	bool set_model_guarded (std::uintptr_t set_model, std::uintptr_t pawn, const char* model)
	{
		__try
		{
			memory::call<void>( set_model, pawn, model );
		}
		__except ( EXCEPTION_EXECUTE_HANDLER )
		{
			return false;
		}

		return true;
	}
}

	void agents::on_frame_stage_notify( )
	{
		const auto local = systems::g_local.get( );
		if ( !local.is_alive || systems::g_local.is_in_cinematic( ) || !local.pawn )
		{
			return;
		}

		const auto team = memory::read<int>( local.pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
		const auto selected_def_index = ( team == 3 ) ? settings::g_changer.agents.ct_def : ( team == 2 ) ? settings::g_changer.agents.t_def : static_cast< std::int16_t >( 0 );

		const econ_item_system::item_def* selected{ nullptr };
		if ( selected_def_index != 0 )
		{
			selected = g_econ_item_system.find_def( selected_def_index );
		}

		// custom player model (agent) ?
		std::string model_path;
		{
			const auto& custom_agents = settings::g_changer.custom_agents;
			const auto custom_idx = ( team == 3 ) ? custom_agents.selected_ct
			                     : ( team == 2 ) ? custom_agents.selected_t : -1;

			if ( custom_idx >= 0 && custom_idx < static_cast< int >( custom_agents.entries.size( ) ) )
			{
				const auto& entry = custom_agents.entries[ custom_idx ];
				if ( entry.team == team && !entry.model_path.empty( ) )
					model_path = entry.model_path;
			}
		}

		if ( model_path.empty( ) && selected )
			model_path = selected->model_player;

		if ( this->m_tracked_pawn != local.pawn )
		{
			this->m_original_model.clear( );
			this->m_overridden = false;
			this->m_applied_handle = 0;
			this->m_applied_def = 0;
			this->m_tracked_team = 0;
			this->m_tracked_pawn = local.pawn;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return;
		}

		const auto model_state = game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash );

		// Don't apply a model to a pawn that hasn't finished loading its own model
		// yet -- calling set_player_model on a pawn with no model loaded (m_hModel
		// == 0) corrupts its model state and crashes the engine later at render.
		if ( memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_hModel"_hash ) ) == 0 )
		{
			return;
		}

		if ( model_path.empty( ) )
		{
			if ( this->m_overridden && !this->m_original_model.empty( ) )
			{
				memory::call<void>(PATTERN (patterns::set_player_model), local.pawn, this->m_original_model.c_str( ) );

				//const auto controller = memory::read<std::uintptr_t>( local.pawn + 0x3268 );
				//if ( controller )
				//{
				//    memory::call<void>( addresses::functions::pawn_graph_unbind_all, controller );
				//    memory::call<void>( addresses::functions::pawn_graph_rebind_all, controller );
				//}

				this->cycle_weapon_owners( local.pawn );

				this->m_applied_handle = 0;
				this->m_applied_def = 0;
				this->m_applied_model.clear( );
				this->m_tracked_team = 0;
				this->m_overridden = false;
			}
			return;
		}

		if ( !this->m_overridden && this->m_original_model.empty( ) )
		{
			const auto model_name_ptr = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_ModelName"_hash ) );
			if ( model_name_ptr )
			{
				this->m_original_model = memory::read_string( model_name_ptr );
			}
		}

		const auto current_handle = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_hModel"_hash ) );
		const auto selection_matches = ( this->m_applied_def == ( selected ? selected->def_index : 0 ) ) && this->m_applied_model == model_path;
		const auto team_matches = ( this->m_tracked_team == team );
		const auto handle_matches = ( this->m_applied_handle != 0 && current_handle == this->m_applied_handle );

		if ( this->m_overridden && selection_matches && team_matches && handle_matches )
		{
			return;
		}

		if ( this->m_overridden && !team_matches )
		{
			this->m_original_model.clear( );
			this->m_applied_model.clear( );
			this->m_overridden = false;

			const auto model_name_ptr = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_ModelName"_hash ) );
			if ( model_name_ptr )
			{
				this->m_original_model = memory::read_string( model_name_ptr );
			}
		}

		// движок требует .vmdl, а не скомпилированный .vmdl_c — убираем "_c"
		if ( model_path.size( ) >= 7 && model_path.substr( model_path.size( ) - 7 ) == ".vmdl_c" )
			model_path = model_path.substr( 0, model_path.size( ) - 2 );

		// отклоняем не-player модели (arm/viewmodel) — ломают скелет игрока
		{
			auto to_lower = [] (unsigned char c) { return ( c >= 'A' && c <= 'Z' ) ? static_cast< char >( c + 32 ) : static_cast< char >( c ); };
			auto icontains = [ & ] ( const std::string& s, const char* needle, std::size_t nlen )
			{
				if ( s.size( ) < nlen ) return false;
				for ( std::size_t i = 0; i + nlen <= s.size( ); ++i )
				{
					bool ok = true;
					for ( std::size_t j = 0; j < nlen; ++j )
						if ( to_lower( static_cast< unsigned char >( s[ i + j ] ) ) != to_lower( static_cast< unsigned char >( needle[ j ] ) ) )
						{ ok = false; break; }
					if ( ok ) return true;
				}
				return false;
			};

			const bool bad =
				icontains( model_path, "_arm", 4 ) ||
				icontains( model_path, "arms", 4 ) ||
				icontains( model_path, "viewmodel", 8 ) ||
				icontains( model_path, "/arm.", 5 ) ||
				icontains( model_path, "\\arm.", 5 );

			if ( bad )
			{
				this->m_applied_model.clear( );
				auto& ca = settings::g_changer.custom_agents;
				if ( team == 3 ) ca.selected_ct = -1; else ca.selected_t = -1;
				return;
			}
		}

		// защищённое применение модели (SEH): битая кастомка не роняет игру.
		// Модель, из-за которой упал set_player_model, заносим в чёрный список на
		// сессию, чтобы не пытаться накладывать её каждый кадр (повторное
		// применение на сломанном pawn роняет игру уже при рендере).
		if ( !model_path.empty( ) )
		{
			static std::vector<std::string> s_bad_models;

			bool known_bad = false;
			for ( const auto& m : s_bad_models )
			{
				if ( m == model_path )
				{
					known_bad = true;
					break;
				}
			}

			if ( known_bad )
			{
				this->m_applied_model.clear( );
				auto& ca = settings::g_changer.custom_agents;
				if ( team == 3 ) ca.selected_ct = -1; else ca.selected_t = -1;
			}
			else if ( model_path.size( ) >= 5 && model_path.compare( model_path.size( ) - 5, 5, ".vmdl" ) == 0 )
			{
				if ( !apply_model_safe( local.pawn, model_path.c_str( ) ) )
				{
					s_bad_models.push_back( model_path );
					this->m_applied_model.clear( );
					auto& ca = settings::g_changer.custom_agents;
					if ( team == 3 ) ca.selected_ct = -1; else ca.selected_t = -1;
				}
			}
			else
			{
				const auto set_model = PATTERN ( patterns::set_player_model );

				if ( !set_model_guarded( set_model, local.pawn, model_path.c_str( ) ) )
				{
					s_bad_models.push_back( model_path );
					auto& ca = settings::g_changer.custom_agents;
					if ( team == 3 ) ca.selected_ct = -1; else ca.selected_t = -1;
				}
			}
		}

		//const auto controller = memory::read<std::uintptr_t>( local_pawn + 0x3268 );
		//if ( controller )
		//{
		//    memory::call<void>( addresses::functions::pawn_graph_unbind_all, controller );
		//    memory::call<void>( addresses::functions::pawn_graph_rebind_all, controller );
		//}

		const auto collision = local.pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash );
		memory::write<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ), math::vector3( -16.0f, -16.0f, 0.0f ) );
		memory::write<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ), math::vector3( 16.0f, 16.0f, 72.0f ) );

		this->cycle_weapon_owners( local.pawn );

		this->m_applied_handle = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_hModel"_hash ) );
		this->m_applied_def = selected ? selected->def_index : 0;
		this->m_applied_model = model_path;
		this->m_tracked_team = team;
		this->m_overridden = true;
	}

	void agents::cycle_weapon_owners( std::uintptr_t pawn )
	{
		const auto weapon_services = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
		if ( !weapon_services )
		{
			return;
		}

		const auto weapons_base = weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hMyWeapons"_hash );
		const auto weapons_size = memory::read<int>( weapons_base );
		const auto weapons_data = memory::read<std::uintptr_t>( weapons_base + 0x8 );

		if ( !weapons_data || weapons_size <= 0 )
		{
			return;
		}

		for ( auto i = 0; i < weapons_size; ++i )
		{
			const auto handle = memory::read<std::uint32_t>( weapons_data + i * sizeof( std::uint32_t ) );
			const auto weapon = systems::g_entities.lookup( handle );

			if ( !weapon )
			{
				continue;
			}

			const auto owner_off = SCHEMA( "C_BaseEntity", "m_hOwnerEntity"_hash );
			const auto saved_owner = memory::read<std::uint32_t>( weapon + owner_off );

			memory::write<std::uint32_t>( weapon + owner_off, 0xffffffff );
			memory::write<std::uint32_t>( weapon + owner_off, saved_owner );
		}
	}

} // namespace features::changer