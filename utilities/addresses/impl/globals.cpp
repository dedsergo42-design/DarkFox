#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/logging/logging.hpp>
#include <protection/game_addresses.hpp>
#include "../addresses.hpp"

namespace addresses::globals {

	bool initialize () {
		source2client              = INTERFACE_ ("Source2Client002");
		panorama = INTERFACE_ ("PanoramaUIEngine001");
		source2engine_to_client    = INTERFACE_ ("Source2EngineToClient001");
		scene_system               = INTERFACE_ ("SceneSystem_002");
		material_system            = INTERFACE_ ("VMaterialSystem2_001");
		schema_system              = INTERFACE_ ("SchemaSystem_001");
		input_system               = INTERFACE_ ("InputSystemVersion001");
		particle_system_mgr        = INTERFACE_ ("ParticleSystemMgr003");
		cvar                       = (interfaces::c_engine_cvar*)INTERFACE_ ("VEngineCvar007");
		source2client_prediction   = INTERFACE_ ("Source2ClientPrediction001");
		network_client_service     = INTERFACE_ ("NetworkClientService_001");
		resource_system            = INTERFACE_ ("ResourceSystem013");
		localize                   = INTERFACE_ ("Localize_001");
		mesh_system                = INTERFACE_ ("MeshSystem001");
		file_system                = INTERFACE_ ("VFileSystem017");

		csgo_input             = PATTERN (patterns::csgo_input);
		entity_list            = PATTERN (patterns::entity_list);
		local_player_controller = PATTERN (patterns::local_player_controller);
		global_vars            = PATTERN (patterns::global_vars);
		view_matrix            = PATTERN (patterns::view_matrix);
		game_rules             = PATTERN (patterns::game_rules);
		light_data_queue       = PATTERN (patterns::light_data_queue);
		particle_manager       = PATTERN (patterns::particle_manager);
		game_event_manager     = PATTERN (patterns::game_event_manager);
		game_trace_manager     = PATTERN (patterns::game_trace_manager);
		render_game_system_storage = PATTERN (patterns::render_game_system_storage);
		material_manager       = PATTERN (patterns::material_manager);
		game_entity_system     = PATTERN (patterns::game_entity_system);
		weapon_recoil_data     = PATTERN (patterns::weapon_recoil_data);
		hud                    = PATTERN (patterns::hud);
		csgo_hud_panel         = PATTERN (patterns::csgo_hud_panel);
		main_menu_panel        = PATTERN (patterns::main_menu_panel);
		prediction_seed        = PATTERN (patterns::prediction_seed);
		simulation_player      = PATTERN (patterns::simulation_player);
		prediction_player      = PATTERN (patterns::prediction_player);
		planted_c4             = PATTERN (patterns::planted_c4);
		item_system            = PATTERN (patterns::item_system);
		frame_input_ring_idx   = PATTERN (patterns::frame_input_ring_idx);
		frame_input_ring_base  = PATTERN (patterns::frame_input_ring_base);
		prediction_state       = PATTERN (patterns::prediction_state);

		if (const auto ptr = MODULE_EXPORT("tier0.dll:g_pMemAlloc"))
			mem_alloc = *reinterpret_cast<std::uintptr_t*>(ptr);

		const std::pair<std::string_view, std::uintptr_t> required_globals[] {
			{ "csgo_input", csgo_input },
			{ "entity_list", entity_list },
			{ "local_player_controller", local_player_controller },
			{ "global_vars", global_vars },
			{ "view_matrix", view_matrix },
			{ "game_rules", game_rules },
			{ "light_data_queue", light_data_queue },
			{ "particle_manager", particle_manager },
			{ "game_event_manager", game_event_manager },
			{ "game_trace_manager", game_trace_manager },
			{ "render_game_system_storage", render_game_system_storage },
			{ "mem_alloc", mem_alloc },
			{ "material_manager", material_manager },
			{ "game_entity_system", game_entity_system },
			{ "weapon_recoil_data", weapon_recoil_data },
			{ "hud", hud },
			{ "prediction_seed", prediction_seed },
			{ "simulation_player", simulation_player },
			{ "planted_c4", planted_c4 },
			{ "item_system", item_system },
			{ "network_client_service", network_client_service },
			{ "frame_input_ring_idx", frame_input_ring_idx },
			{ "frame_input_ring_base", frame_input_ring_base },
			{ "prediction_state", prediction_state },
		};

		// Не в required: без prediction_player предсказание не работает, но чит
		// живёт. Ошибка здесь валила бы ВСЮ инициализацию (INIT_FAIL делает
		// return), и тогда не поднимались бы ни хуки, ни меню -- из-за одной
		// сигнатуры. Сам потребитель (prediction::simulate) проверяет адрес
		// перед использованием и отключается сам.
		const std::pair<std::string_view, std::uintptr_t> optional_globals[] {
			{ "prediction_player", prediction_player },
		};

		auto initialized = true;
		for (const auto& [name, address] : required_globals) {
			if (!address) {
				logging::console::print (xs ("[error] global address not initialized | {}"), name);
				initialized = false;
			}
		}

		for (const auto& [name, address] : optional_globals) {
			if (!address) {
				logging::console::print (xs ("[warn] optional global missing, feature disabled | {}"), name);
			}
		}

		return initialized;
	}

} // namespace addresses::globals
