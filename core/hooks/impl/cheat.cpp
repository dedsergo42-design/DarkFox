#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/diag.hpp>
#include <utilities/perf.hpp>
#include <utilities/threadpool/threadpool.hpp>
#include <utilities/hooking/hooking.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/security/security.hpp>
#include <core/rendering/rendering.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>
#include "../hooks.hpp"
#include <ctime>
#include <core/settings.hpp>
#include <core/features/changer/changer.hpp>
#include <exploits.h>

namespace hooks {

// Handle of the texture object backing the preview panel, captured in
// get_resource_view. File scope because the resize and level hooks below have
// to drop it when the composition texture is torn down.
static void* s_preview_texture_handle = nullptr;

// Texture objects already inspected and found not to be the preview target.
//
// The same handful of textures is requested again on every frame, and until the
// preview texture is captured each request re-ran the whole identification path
// below. That path is far more expensive than it looks: ahead of its
// VirtualQuery sit two raw dereferences, and on texture types whose name slot is
// not a pointer they fault. Every fault runs the vectored crash handler before
// SEH swallows it, so a few thousand of them per second is enough to hold the
// pre-match main menu at 4 fps -- while a post-match menu, where the texture is
// already captured and this path never runs at all, sits at 120-140.
//
// Open addressing with a single slot per bucket and no eviction policy: a
// collision just means one texture gets re-examined later, which is harmless.
// Lock-free because this hook runs on the render thread.
static constexpr std::size_t k_rejected_texture_slots = 4096;
static std::array<std::atomic<void*>, k_rejected_texture_slots> s_rejected_textures{};

// A committed, readable mapping of at least `size` bytes at `address`.
//
// The identification path below used to simply dereference and let SEH pick up
// the pieces. That works, but an access violation is expensive even when it is
// expected: it still unwinds through every vectored handler on the way to the
// __except. One VirtualQuery is far cheaper than one fault, and the faults were
// frequent enough to show up as tens of milliseconds a frame.
[[nodiscard]] static bool is_readable_pointer( const void* address, std::size_t size )
{
	if ( !address )
	{
		return false;
	}

	MEMORY_BASIC_INFORMATION info{};
	if ( !VirtualQuery( address, &info, sizeof( info ) ) || info.State != MEM_COMMIT )
	{
		return false;
	}

	constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ
		| PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
	if ( ( info.Protect & readable ) == 0 || ( info.Protect & PAGE_GUARD ) != 0 )
	{
		return false;
	}

	// The region has to actually cover the bytes we are about to touch.
	const auto region_end = reinterpret_cast< std::uintptr_t >( info.BaseAddress ) + info.RegionSize;
	const auto read_end = reinterpret_cast< std::uintptr_t >( address ) + size;
	return read_end <= region_end;
}

[[nodiscard]] static std::size_t rejected_texture_slot( void* texture )
{
	// Texture objects are heap allocations, so the low bits carry no entropy.
	return ( reinterpret_cast< std::uintptr_t >( texture ) >> 4 ) % k_rejected_texture_slots;
}

static void clear_rejected_textures( )
{
	for ( auto& slot : s_rejected_textures )
	{
		slot.store( nullptr, std::memory_order_relaxed );
	}
}

void forget_preview_textures( )
{
	s_preview_texture_handle = nullptr;
	clear_rejected_textures( );
}

void release_preview_texture( )
{
	// The cheap half of the above, for the common case.
	//
	// Wiping the whole reject cache means every texture the renderer asks about
	// gets fully inspected again -- a VirtualQuery and a name scan, five thousand
	// times a frame until the preview turns up. Doing that every time the menu
	// opens is what put the frame rate on the floor.
	//
	// A rebuilt composition layer is a new object, so it was never in the cache
	// to begin with; only the slot belonging to the handle we are dropping can
	// collide with it, and that one slot is all that needs clearing.
	if ( s_preview_texture_handle )
	{
		s_rejected_textures[ rejected_texture_slot( s_preview_texture_handle ) ].store( nullptr, std::memory_order_relaxed );
		s_preview_texture_handle = nullptr;
	}
}

namespace {
	LONG WINAPI darkfox_crash_handler (PEXCEPTION_POINTERS pInfo) {
		const auto code = pInfo->ExceptionRecord->ExceptionCode;

		// --- Debug / non-fatal исключения: не пишем краш-лог ---
		switch ( code )
		{
			case 0x40010006: // DBG_PRINTEXCEPTION_C (OutputDebugString)
			case 0x4001000A: // DBG_PRINTEXCEPTION_WIDE_C
			case 0x406D1388: // MS_VC_EXCEPTION (SetThreadName и т.п.)
			case 0x400000AA: // Valve DBG_PRINTF
			case 0x80000003: // BREAKPOINT
			case 0x80000004: // SINGLE_STEP
				return EXCEPTION_CONTINUE_SEARCH;
		}

		// An explicit memory probe is expected to fault; that is the whole point
		// of the SEH around it, and the handler downstream will deal with it.
		if ( diag::probe_active( ) )
		{
			return EXCEPTION_CONTINUE_SEARCH;
		}

		// A vectored handler sees first-chance exceptions, and the game raises
		// plenty of its own that it goes on to handle perfectly well. Those are
		// none of our business: entry.cpp's vectored filter already limits itself
		// to faults inside our own module, and this one has to do the same.
		// Without it a fault in client.dll wrote a full crash report -- getenv, a
		// string build, and an ofstream creating and truncating a file on the
		// Desktop -- and at the rate the engine produces them that alone is worth
		// tens of frames a second.
		if ( !diag::is_module_address( pInfo->ExceptionRecord->ExceptionAddress ) )
		{
			return EXCEPTION_CONTINUE_SEARCH;
		}

		// One report per process. A genuinely fatal fault takes the process with
		// it, so the first one is the one worth having; anything after it is
		// either noise or a cascade, and writing each would reintroduce the storm
		// through a different door.
		static std::atomic<bool> crash_logged{ false };
		if ( crash_logged.exchange( true, std::memory_order_relaxed ) )
		{
			return EXCEPTION_CONTINUE_SEARCH;
		}

		// --- Пишем лог только для fatal-ошибок ---
		const bool is_ntstatus_fatal = ( code & 0xF0000000 ) == 0xC0000000;
		const bool is_fatal_known =
			code == EXCEPTION_ACCESS_VIOLATION ||
			code == EXCEPTION_STACK_OVERFLOW ||
			code == EXCEPTION_ILLEGAL_INSTRUCTION ||
			code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
			code == 0xC0000409 || // STACK_BUFFER_OVERRUN (/GS)
			code == 0xC0000374;   // HEAP_CORRUPTION
		if ( !is_ntstatus_fatal && !is_fatal_known )
			return EXCEPTION_CONTINUE_SEARCH;

		std::string path;
		if ( const char* prof = std::getenv ( "USERPROFILE" ) )
			path = std::string ( prof ) + "\\Desktop\\DarkFox_crash_log.txt";
		else
			path = "DarkFox_crash_log.txt";

		std::ofstream f ( path, std::ios::out | std::ios::trunc );
		if ( !f )
			return EXCEPTION_CONTINUE_SEARCH;

		std::time_t now = std::time ( nullptr );
		char tbuf [64] {};
		if ( std::tm* tm = std::localtime ( &now ) )
			std::strftime ( tbuf, sizeof ( tbuf ), "%Y-%m-%d %H:%M:%S", tm );
		else
			std::strcpy ( tbuf, "unknown" );

		f << "=== DarkFox CRASH LOG ===\n";
		f << "note: only fatal exceptions are logged; debug prints ignored\n";
		f << "time: " << tbuf << "\n";
		f << "exception code: 0x" << std::hex << code << std::dec;
		if ( code == EXCEPTION_ACCESS_VIOLATION ) f << " (ACCESS_VIOLATION)";
		else if ( code == EXCEPTION_STACK_OVERFLOW ) f << " (STACK_OVERFLOW)";
		else if ( code == EXCEPTION_ILLEGAL_INSTRUCTION ) f << " (ILLEGAL_INSTRUCTION)";
		else if ( code == EXCEPTION_BREAKPOINT ) f << " (BREAKPOINT)";
		else if ( code == EXCEPTION_INT_DIVIDE_BY_ZERO ) f << " (DIVIDE_BY_ZERO)";
		f << "\n";

		const auto addr = pInfo->ExceptionRecord->ExceptionAddress;
		f << "exception address: 0x" << std::hex << reinterpret_cast< std::uintptr_t >( addr ) << std::dec << "\n";
		f << "thread id: " << GetCurrentThreadId( ) << "\n";
		f << "last applied model: " << features::changer::g_last_applied_model << "\n";

		{
			HMODULE hmod = nullptr;
			if ( GetModuleHandleExA ( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			                           reinterpret_cast< LPCSTR >( addr ), &hmod ) && hmod ) {
				char modname [MAX_PATH] {};
				if ( GetModuleFileNameA ( hmod, modname, MAX_PATH ) )
					f << "module: " << modname << "\n";
				else
					f << "module: (unknown)\n";
			} else {
				f << "module: (unknown)\n";
			}
		}

		const auto& ca = settings::g_changer.custom_agents;
		const auto model_of = [] ( int idx, const std::vector< settings::changer::custom_agent_entry >& entries ) -> std::string {
			if ( idx >= 0 && idx < static_cast< int >( entries.size ( ) ) )
				return entries[ idx ].model_path;			return "(none)";
		};
		f << "selected_ct: " << ca.selected_ct << "\n";
		f << "selected_t: " << ca.selected_t << "\n";
		f << "applied ct model: " << model_of ( ca.selected_ct, ca.entries ) << "\n";
		f << "applied t model: " << model_of ( ca.selected_t, ca.entries ) << "\n";

		f.close ( );

		// --- АВТО-ДАМП ---
		// Текст на Рабочем столе человек прочитает, но по нему нельзя открыть
		// стек. Дамп -- можно, и он должен появляться там же, где текст, без
		// второго прохода и без ручных действий.
		//
		// capture_crash_dump сам замыкает "один дамп на процесс" на
		// diag::g_crash_claimed. Если дамп по этому же исключению уже начал
		// писать контур entry.cpp -- второй раз не пишем (флаг выставлен), но
		// и не остаёмся без файла: сам факт того, что ветка текста
		// выполнилась, означает, что исключение фатально и внутри НАШЕГО
		// модуля, а значит дамп обязан существовать.
		//
		// Передаём GetCurrentThreadId(): VEH исполняется в контексте упавшего
		// потока, и MiniDumpWriteDump должен получить именно его id, иначе
		// точка останова в дампе будет не та.
		diag::capture_crash_dump (
			pInfo,
			diag::g_exception_phase,
			GetCurrentThreadId ( ) );

		return EXCEPTION_CONTINUE_SEARCH;
	}
}

	bool cheat::initialize () {
		AddVectoredExceptionHandler (1, darkfox_crash_handler);

		if (!hooking::manager::create ({
			{ &m_present, &present, xs ("present"), addresses::functions::present },
			{ &m_resize_buffers, &resize_buffers, xs ("resize_buffers"), addresses::functions::resize_buffers }
			})) {
			return false;
		}

		const hooking::manager::entry feature_hooks[] {
			{ &m_cmd_interpreter, &cmd_interpreter, xs ("cmd_interpreter"), PATTERN (patterns::cmd_interpreter) },
			{ &m_frame_stage_notify, &frame_stage_notify, xs ("frame_stage_notify"), PATTERN (patterns::frame_stage_notify) },
			{ &m_create_move, &create_move, xs ("create_move"), PATTERN (patterns::create_move) },
			{ &m_handle_view_angles, &handle_view_angles, xs ("handle_view_angles"), PATTERN (patterns::handle_view_angles) },
			{ &m_add_entity, &add_entity, xs ("add_entity"), PATTERN (patterns::add_entity) },
			{ &m_remove_entity, &remove_entity, xs ("remove_entity"), PATTERN (patterns::remove_entity) },
			{ &m_render_view, &render_view, xs ("render_view"), PATTERN (patterns::render_view) },
			{ &m_draw_skybox_array, &draw_skybox_array, xs ("draw_skybox_array"), PATTERN (patterns::draw_skybox_array) },
			{ &m_light_scene_object, &light_scene_object, xs ("light_scene_object"), PATTERN (patterns::light_scene_object) },
			{ &m_draw_scene_object_array, &draw_scene_object_array, xs ("draw_scene_object_array"), PATTERN (patterns::draw_scene_object_array) },
			{ &m_draw_scene_object, &draw_scene_object, xs ("draw_scene_object"), PATTERN (patterns::draw_scene_object) },
			{ &m_is_glowing, &is_glowing, xs ("is_glowing"), PATTERN (patterns::is_glowing) },
			{ &m_get_glow_color, &get_glow_color, xs ("get_glow_color"), PATTERN (patterns::get_glow_color) },
			{ &m_generate_primitives, &generate_primitives, xs ("generate_primitives"), PATTERN (patterns::generate_primitives) },
			{ &m_parse_report_hit, &parse_report_hit, xs ("parse_report_hit"), PATTERN (patterns::parse_report_hit) },
			{ &m_setup_fog, &setup_fog, xs ("setup_fog"), PATTERN (patterns::setup_fog) },
			{ &m_set_shader_param, &set_shader_param, xs ("set_shader_param"), PATTERN (patterns::set_shader_param) },
			{ &m_set_postprocess_vec, &set_postprocess_vec, xs ("set_postprocess_vec"), PATTERN (patterns::set_postprocess_vec) },
			{ &m_override_view, &override_view, xs ("override_view"), PATTERN (patterns::override_view) },
			{ &m_update_fov_sensitivity, &update_fov_sensitivity, xs ("update_fov_sensitivity"), PATTERN (patterns::update_fov_sensitivity) },
			{ &m_render_scope, &render_scope, xs ("render_scope"), PATTERN (patterns::render_scope) },
			{ &m_render_crosshair, &render_crosshair, xs ("render_crosshair"), PATTERN (patterns::render_crosshair) },
			{ &m_prepare_scene_material, &prepare_scene_material, xs ("prepare_scene_material"), PATTERN (patterns::prepare_scene_material) },
			{ &m_post_network_data_received, &post_network_data_received, xs ("post_network_data_received"), PATTERN (patterns::post_network_data_received) },
			{ &m_draw_overhead, &draw_overhead, xs ("draw_overhead"), PATTERN (patterns::draw_overhead) },
			{ &m_draw_legs, &draw_legs, xs ("draw_legs"), PATTERN (patterns::draw_legs) },
			{ &m_get_transforms_for_hitbox_list, &get_transforms_for_hitbox_list, xs ("get_transforms_for_hitbox_list"), PATTERN (patterns::get_transforms_for_hitbox_list) },
			{ &m_sort_primitives, &sort_primitives, xs ("sort_primitives"), PATTERN (patterns::sort_primitives) },
			{ &m_get_interpolated_shoot_position, &get_interpolated_shoot_position, xs ("get_interpolated_shoot_position"), PATTERN (patterns::get_interpolated_shoot_position) },
			{ &m_level_initialization, &level_initialization, xs ("level_initialization"), PATTERN (patterns::level_initialization) },
			{ &m_level_shutdown, &level_shutdown, xs ("level_shutdown"), PATTERN (patterns::level_shutdown) },
			{ &m_read_frame_input, &read_frame_input, xs ("read_frame_input"), PATTERN (patterns::read_frame_input) },
			{ &m_process_input_event, &process_input_event, xs ("process_input_event"), PATTERN (patterns::process_input_event) },
			{ &m_render_decals, &render_decals, xs ("render_decals"), PATTERN (patterns::render_decals) },
			{ &m_render_smoke, &render_smoke, xs ("render_smoke"), PATTERN (patterns::render_smoke) },
			{ &m_draw_flash_effect, &draw_flash_effect, xs ("draw_flash_effect"), PATTERN (patterns::draw_flash_effect) },
			{ &m_set_info, &set_info, xs ("set_info"), PATTERN (patterns::set_info) },
			{ &m_get_resource_view, &get_resource_view, xs ("get_resource_view"), PATTERN (patterns::get_resource_view) },
			{ &m_quantize_movement, &quantize_movement, xs ("quantize_movement"), PATTERN (patterns::quantize_movement) }
		};

		g_unavailable_hooks.clear( );
		for (const auto& entry : feature_hooks) {
			if (!hooking::manager::create ({ entry })) {
				// Здесь стоял комментарий «имя копируем литералом, а не через
				// xs()» -- и код делал ровно обратное: клал в
				// g_unavailable_hooks результат xs(), то есть висячий
				// указатель. Комментарий описывал правильное намерение, но не
				// то, что происходило.
				//
				// Теперь и entry::name, и unavailable_hook_t::name --
				// std::string, поэтому копия реально владеет строкой.
				g_unavailable_hooks.push_back ({ entry.name });
				logging::console::print (xs ("skipping unavailable hook: {}"), entry.name);
			}
		}

		const auto unavailable_hooks = static_cast< unsigned >( g_unavailable_hooks.size ());

		if (unavailable_hooks) {
			logging::console::print (
				xs ("feature hooks initialized with {} unavailable"),
				unavailable_hooks);
		}

		return true;
	}

	void cheat::shutdown( )
	{
		m_wnd_proc.reset( );
		m_om_set_render_targets.reset( );
		m_present.reset( );
		m_resize_buffers.reset( );
		m_cmd_interpreter.reset( );
		m_frame_stage_notify.reset( );
		m_create_move.reset( );
		m_handle_view_angles.reset( );
		m_add_entity.reset( );
		m_remove_entity.reset( );
		m_render_view.reset( );
		m_draw_skybox_array.reset( );
		m_light_scene_object.reset( );
		m_draw_scene_object_array.reset( );
		m_draw_scene_object.reset( );
		m_is_glowing.reset( );
		m_get_glow_color.reset( );
		m_generate_primitives.reset( );
		m_parse_report_hit.reset( );
		m_setup_fog.reset( );
		m_set_shader_param.reset( );
		m_set_postprocess_vec.reset( );
		m_override_view.reset( );
		m_update_fov_sensitivity.reset( );
		m_render_scope.reset( );
		m_render_crosshair.reset( );
		m_prepare_scene_material.reset( );
		m_post_network_data_received.reset( );
		m_draw_overhead.reset( );
		m_draw_legs.reset( );
		m_get_transforms_for_hitbox_list.reset( );
		m_sort_primitives.reset( );
		m_get_inaccuracy.reset( );
		m_get_interpolated_shoot_position.reset( );
		m_level_initialization.reset( );
		m_read_frame_input.reset( );
		m_process_input_event.reset( );
		m_render_decals.reset( );
		m_render_smoke.reset( );
		m_render_smoke_map.reset( );
		m_render_smoke_unmap.reset( );
		m_draw_flash_effect.reset( );
		m_set_info.reset( );
		m_get_resource_view.reset( );
	}

	HRESULT __fastcall cheat::present( IDXGISwapChain* thisptr, UINT sync_interval, UINT flags )
	{
		{
			perf::scope probe{ perf::id::present };
			rendering::g_context.on_present( thisptr );
		}

		perf::on_frame( );

		if ( !m_wnd_proc.is_enabled( ) && rendering::g_context.get_window( ) )
		{
			if ( m_wnd_proc.create( reinterpret_cast< void* >( GetWindowLongPtrW( rendering::g_context.get_window( ), GWLP_WNDPROC ) ), &wnd_proc ) )
			{
				m_wnd_proc.enable( );
			}
		}

		if ( !m_om_set_render_targets.is_enabled( ) && rendering::g_context.is_initialized( ) )
		{
			const auto om_addr = memory::get_vfunc( reinterpret_cast< std::uintptr_t >( rendering::g_context.get_context( ) ), 33 );
			if ( m_om_set_render_targets.create( reinterpret_cast< void* >( om_addr ), &om_set_render_targets ) )
			{
				m_om_set_render_targets.enable( );
			}
		}

		return m_present.call<HRESULT>( thisptr, sync_interval, flags );
	}

	HRESULT __fastcall cheat::resize_buffers( IDXGISwapChain* thisptr, UINT buffer_count, UINT width, UINT height, DXGI_FORMAT new_format, UINT swap_chain_flags )
	{
		rendering::g_context.on_resize_buffers( );

		// the preview composition texture is recreated on resize; drop the cached
		// SRV so it gets recaptured from the new render target
		systems::g_model_preview.clear_texture( );
		s_preview_texture_handle = nullptr;
		clear_rejected_textures( );

		const auto result = m_resize_buffers.call<long>( thisptr, buffer_count, width, height, new_format, swap_chain_flags );
		if ( SUCCEEDED( result ) )
		{
			rendering::g_context.on_resize_buffers_post( thisptr );
		}

		return result;
	}

	LRESULT __stdcall cheat::wnd_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
	{
		if ( msg == WM_ACTIVATE && LOWORD( wparam ) != WA_INACTIVE && rendering::g_menu.is_open( ) && rendering::g_context.get_window( ) == hwnd )
		{
			if ( addresses::globals::input_system )
			{
				memory::call_vfunc<void>( addresses::globals::input_system, 76, false );
			}

			SetCursor( LoadCursor( nullptr, IDC_ARROW ) );
			rendering::g_menu.apply_saved_cursor( );
		}

		if ( msg == WM_KEYDOWN && !( lparam & ( 1 << 30 ) ) && static_cast< int >( wparam ) == settings::g_misc.menu_key )
		{
			rendering::g_menu.toggle( );
			return 0;
		}

		xui::wndproc( msg, wparam, lparam );

		if ( rendering::g_menu.is_open( ) )
		{
			switch ( msg )
			{
			case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
			case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
			case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
			case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
			case WM_MOUSEMOVE:
				return 0;
			default:
				break;
			}
		}

		return m_wnd_proc.call<LRESULT>( hwnd, msg, wparam, lparam );
	}

	void __stdcall cheat::om_set_render_targets( ID3D11DeviceContext* ctx, UINT num_views, ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv )
	{
		m_om_set_render_targets.call<void>( ctx, num_views, rtvs, dsv );
	}

	void __fastcall cheat::cmd_interpreter( std::uintptr_t render_thread, std::uintptr_t item, std::uint8_t flag )
	{
		m_cmd_interpreter.call<void>( render_thread, item, flag );
	}

	void __fastcall cheat::frame_stage_notify( std::uintptr_t thisptr, int stage )
	{
		if ( systems::g_entities.is_empty( ) )
		{
			systems::g_entities.force_update( );
		}

		systems::g_local.update( );

		// Deliberately outside the local-player gate below. The model preview has
		// to keep ticking with no local player at all -- that is exactly the main
		// menu, where the panel was created once at init and then never touched
		// again, so nothing re-armed it for display and the game had no reason to
		// render it. update( ) throttles its own expensive parts internally, so
		// running it per frame stage costs nothing worth counting.
		systems::g_model_preview.update( );

		if ( systems::g_local.get( ).is_valid( ) && systems::g_view.has_camera( ) )
		{
			if ( stage == 6 )
			{
				perf::scope changer_probe{ perf::id::changer };
				features::changer::g_guns.on_frame_stage_notify( );
			}

			if ( stage == 7 )
			{
				{
					perf::scope changer_probe{ perf::id::changer };
					features::changer::g_agents.on_frame_stage_notify( );
					features::changer::g_gloves.on_frame_stage_notify( );
					features::changer::g_knives.on_frame_stage_notify( );
				}

				features::world::g_scene.on_frame_stage_notify( );
				features::world::g_weather.on_frame_stage_notify( );
				features::misc::g_other.on_frame_stage_notify( );
				features::misc::g_impacts.on_frame_stage_notify( );

			}
		}

		// Счётчик кадров оптимизации идёт ВСЕГДА, а не только при живой игре:
		// кэш материалов живёт кадрами, и если счётчик встанет в меню, при
		// входе в матч все записи разом окажутся просроченными.
		features::misc::g_optimization.on_frame_stage_notify( );

		{
			static auto was_active{ false };
			const auto is_active = settings::g_misc.m_removals.skybox_3d.value;

			if ( is_active != was_active )
			{
				CONVAR ("r_draw3dskybox")->m_value.i1 = is_active;
				was_active = is_active;
			}
		}

		// Source 2 copies dynamic-light entries into scene objects during this stage.
		// Publish our entry first, while keeping all manager mutations on the game thread.
		if ( stage == 6 )
		{
			features::misc::g_dlight.on_frame_stage_notify( );
		}

		m_frame_stage_notify.call<void>( thisptr, stage );


		// The current frame's world-to-projection matrix is published by the
		// engine during render-start stage 12.
		if ( stage == 12 )
		{
			systems::g_view.update_matrix( );
			systems::g_frame_data.update( );
		}

		if (systems::g_local.get ().is_valid () && systems::g_view.has_camera ()) {
			if (stage == 6) {
				// Capture lag records only after Source 2 has committed this network update,
				// so the simulation timestamp, world origin and evaluated bones agree.
				features::combat::g_shared.lc( ).run( );
				features::esp::player::g_chams.bt( ).update( );
				features::esp::player::g_chams.os ().update ();

				features::misc::g_scoreboard_weapons.on_frame_stage_notify ();
				features::misc::g_other.do_kill_feed_preservation( );
			}
		}
	}

	void __fastcall cheat::create_move( std::uintptr_t thisptr, int slot, bool active )
	{
		// Pay the pool's start-up cost before the first shot rather than during it.
		//
		// The scan dispatches across worker threads, and the first dispatch of a
		// session is where those threads get created, their stacks committed and
		// their thread-local buffers first touched. That lands on whichever tick
		// the aimbot first has a target -- which is the first shot, and which is
		// what the freeze there looks like. An empty parallel_for costs nothing
		// afterwards and moves all of it to a tick where nothing is happening.
		{
			static std::atomic<bool> warmed{ false };
			if ( !warmed.exchange( true, std::memory_order_relaxed ) )
			{
				threadpool::parallel_for( 0, 16, [ ]( int, int ) { } );
			}
		}

		const auto local = systems::g_local.get( );

		if ( !local.pawn || !local.controller )
		{
			features::combat::g_exploits.reset( );
			return m_create_move.call<void>( thisptr, slot, active );
		}

		const auto cmd = systems::g_input.get_current_cmd( local.controller );
		if ( cmd && systems::g_input.is_subtick_overwrite( cmd ) )
		{
			systems::g_input.set_weapon_select( cmd, thisptr );
			return;
		}

		m_create_move.call<void>( thisptr, slot, active );

		{
			features::combat::g_shared.invalidate_if_needed( );
			features::combat::g_legit.invalidate_if_needed( );
			features::combat::g_misc.quickpeek( ).reset_if_needed( );
		}

		if ( !local.is_alive || !systems::g_view.has_camera( ) )
		{
			// The combat pipeline (and with it, g_exploits.pre_think's charge/release
			// cycle) is about to stop running for the rest of this death/spectate
			// period. Force-release any in-progress doubletap network suppression now
			// so a mid-charge death can't leave the connection choked with nothing
			// left to call the matching release.
			features::combat::g_exploits.reset( );
			return;
		}

		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		if ( !movement_services )
		{
			return;
		}

		const auto last_cmd_processed = memory::read<std::uint32_t>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_nLastCommandNumberProcessed"_hash ) );
		if ( !last_cmd_processed )
		{
			return;
		}

		systems::g_input.update( );
		{
			const auto current_cmd = systems::g_input.get( );
			if ( !current_cmd || !current_cmd->csgo_user_cmd.has_base( ) )
			{
				return;
			}

			static std::atomic_bool first_create_move_traced{};
			const auto trace = !first_create_move_traced.exchange( true, std::memory_order_relaxed );
			diag::exception_scope exception_scope{ "create_move: desubtick" };
			if ( trace )
			{
				diag::step( "create_move: feature pipeline begin" );
			}

			systems::g_input.desubtick( current_cmd );
			systems::g_prediction.capture_prestate( local.pawn, movement_services );

			{
				diag::set_exception_phase( "create_move: shared update" );
			features::movement::g_airstrafe.store_angles( );
			features::movement::g_test_strafer.store_real_angles( );
			features::movement::g_valve_strafer.store_real_angles( );
		features::combat::g_shared.update( );

			diag::set_exception_phase( "create_move: combat misc" );
			features::combat::g_misc.antiaim( ).on_create_move( current_cmd );
			features::combat::g_misc.autostop( ).on_create_move( current_cmd );
			}
			if ( trace )
			{
				diag::step( "create_move: shared and combat misc ready" );
			}

			{
				diag::set_exception_phase( "create_move: pre-combat movement" );
				features::movement::g_slowwalk.on_create_move( current_cmd );
				features::movement::g_edgebug.on_create_move( current_cmd );
				features::movement::g_edgejump.on_create_move( current_cmd );
				features::movement::g_edgestop.on_create_move( current_cmd );
				features::movement::g_jumpbug.on_create_move( current_cmd );
				features::movement::g_bhop.on_create_move( current_cmd );
				features::movement::g_fastladder.on_create_move( current_cmd );

				// Air movement runs BEFORE the rage bot, not after it.
				//
				// The ragebot resolves its shot against a prediction of the walk
				// it believes it will send, and part of that prediction is
				// prestate.last_movement_impulses -- which is captured from the
				// command the previous tick actually shipped. With the strafers
				// running after the ragebot, that prediction was built on a
				// command the ragebot had already rewritten, so the solve was
				// one tick stale and the correction landed on a walk it never
				// simulated. That is the whole difference between "the bot fires
				// and misses" and "the bot fires and hits".
				//
				// Nothing downstream depends on the old order: the ragebot only
				// reads movement state, it never writes it, and the strafers
				// read should_stop()/is_firing_this_tick(), which the ragebot is
				// free to set here because it runs next.
				{
					perf::scope probe{ perf::id::movement };
					features::movement::g_test_strafer.on_create_move( current_cmd );
					features::movement::g_valve_strafer.on_create_move( current_cmd );
					features::movement::g_airstrafe.on_create_move( current_cmd );
				}

				if ( trace )
				{
					diag::step( "create_move: pre-combat movement end" );
					diag::step( "create_move: rage begin" );
				}

				{
					diag::set_exception_phase( "create_move: rage" );
					{
						perf::scope probe{ perf::id::rage };
						features::combat::g_rage.on_create_move( current_cmd );
					}
					if ( trace )
					{
						diag::step( "create_move: rage end" );
						diag::step( "create_move: legit begin" );
					}
					diag::set_exception_phase( "create_move: legit" );
					{
						perf::scope probe{ perf::id::legit };
						features::combat::g_legit.on_create_move( current_cmd );
					}
					if ( trace )
					{
						diag::step( "create_move: legit end" );
					}
				}

				diag::set_exception_phase( "create_move: post-combat movement" );
				features::combat::g_misc.duckpeek( ).on_create_move( current_cmd );
				features::misc::g_projectile_trajectory.on_create_move( current_cmd );
			}
			if ( trace )
			{
				diag::step( "create_move: post-combat movement end" );
			}

			diag::set_exception_phase( "create_move: quickpeek" );
			features::combat::g_misc.quickpeek( ).on_create_move( current_cmd );
			if ( trace )
			{
				diag::step( "create_move: quickpeek end" );
				diag::step( "create_move: final subtick begin" );
			}

			diag::set_exception_phase( "create_move: final subtick" );
			const auto final_base = current_cmd->csgo_user_cmd.mutable_base( );
			if ( final_base && final_base->subtick_moves_size( ) > 0
				&& !features::movement::g_test_strafer.handled_this_tick( )
				&& !features::movement::g_valve_strafer.handled_this_tick( )
				&& !features::movement::g_airstrafe.handled_this_tick( ) )
			{
				final_base->set_forwardmove( 0.0f );
				final_base->set_leftmove( 0.0f );
			}
			if ( trace )
			{
				diag::step( "create_move: final subtick end" );
			}

			//systems::g_legit_input.on_create_move( current_cmd );
		}
		static std::atomic_bool first_input_apply_traced{};
		const auto trace_apply =
			!first_input_apply_traced.exchange( true, std::memory_order_relaxed );
		if ( trace_apply )
		{
			diag::step( "create_move: input apply begin" );
		}
		diag::exception_scope exception_scope{ "create_move: input apply" };
		systems::g_input.apply( );
		if ( trace_apply )
		{
			diag::step( "create_move: input apply end" );
		}
	}

	void __fastcall cheat::handle_view_angles( std::uintptr_t thisptr, int a2 )
	{
		const auto view_angles = systems::g_input.get_view_angles( );

		m_handle_view_angles.call<void>( thisptr, a2 );

		systems::g_input.set_view_angles( view_angles );
	}

	void __fastcall cheat::add_entity( std::uintptr_t thisptr, std::uintptr_t entity, std::uint32_t handle )
	{
		systems::g_entities.on_add_entity( entity, handle );

		m_add_entity.call<void>( thisptr, entity, handle );
	}

	void __fastcall cheat::remove_entity( std::uintptr_t thisptr, std::uintptr_t entity, std::uint32_t handle )
	{
		systems::g_entities.on_remove_entity( entity, handle );

		m_remove_entity.call<void>( thisptr, entity, handle );
	}

	void __fastcall cheat::render_view( std::uintptr_t thisptr )
	{
		m_render_view.call<void>( thisptr );

		systems::g_view.update( thisptr + 0x10 );
		systems::g_frame_data.update( );
	}

	void __fastcall cheat::draw_skybox_array( std::uintptr_t thisptr, std::uintptr_t a2, std::uintptr_t mesh_array, int mesh_count, int a5, std::uintptr_t a6, std::uintptr_t a7, std::uintptr_t a8 )
	{
		features::world::g_scene.on_draw_skybox_array_pre( mesh_array, mesh_count );

		m_draw_skybox_array.call<void>( thisptr, a2, mesh_array, mesh_count, a5, a6, a7, a8 );

		features::world::g_scene.on_draw_skybox_array_post( );
	}

	std::uintptr_t __fastcall cheat::light_scene_object( std::uintptr_t thisptr, std::uintptr_t object, std::uintptr_t a3 )
	{
		// Плоское освещение: переписываем цвет источника до того, как сцена
		// посчитает по нему затенение. Идёт первым, чтобы scene.cpp со своим
		// светом (если игрок его включил) остался главнее -- настройка игрока
		// всегда бьёт оптимизацию.
		features::misc::g_optimization.on_light_scene_object( object );

		features::world::g_scene.on_light_scene_object_pre( object );
		features::misc::g_dlight.apply_scene_color( object );

		const auto result = m_light_scene_object.call<std::uintptr_t>( thisptr, object, a3 );

		features::world::g_scene.on_light_scene_object_post( object );

		return result;
	}

	void __fastcall cheat::draw_scene_object_array( std::uintptr_t thisptr, std::uintptr_t a2, std::uintptr_t object_array )
	{
		m_draw_scene_object_array.call<void>( thisptr, a2, object_array );

		diag::exception_scope exception_scope{ "world: aggregate records" };
		features::world::g_scene.on_draw_scene_object_array( object_array );
	}

	std::uintptr_t __fastcall cheat::draw_scene_object( std::uintptr_t a1, std::uintptr_t a2, std::uintptr_t batch, int batch_count, int a5, std::uintptr_t a6, std::uintptr_t a7, std::uintptr_t a8 )
	{
		// Оптимизация обнуляет материал примитивов, которые надо вырезать,
		// поэтому обязана идти ДО записи батча: иначе вырезанные объекты
		// успеют попасть в буфер и движок их отрисует.
		{
			diag::exception_scope exception_scope{ "optimization: cull batch" };
			features::misc::g_optimization.on_draw_scene_object( batch, batch_count );
		}

		{
			diag::exception_scope exception_scope{ "world: primitive tint" };
			features::world::g_scene.on_draw_scene_object( batch, batch_count );
		}

		return m_draw_scene_object.call<std::uintptr_t>( a1, a2, batch, batch_count, a5, a6, a7, a8 );
	}

	bool __fastcall cheat::is_glowing( std::uintptr_t glow_property )
	{
		if ( glow_property )
		{
			const auto owner_entity = memory::read<std::uintptr_t>( glow_property + 0x18 );
			if ( owner_entity )
			{
				const auto owner_hash = fnv1a::runtime_hash( systems::g_entities.get_schema_name( owner_entity ) );
				if ( owner_hash )
				{
					if ( features::esp::player::g_glow.on_is_glowing( owner_entity, owner_hash ) )
					{
						return true;
					}

					if ( features::esp::item::g_glow.on_is_glowing( owner_entity, owner_hash ) )
					{
						return true;
					}
				}
			}
		}

		return m_is_glowing.call<bool>( glow_property );
	}

	void __fastcall cheat::get_glow_color( std::uintptr_t glow_property, float* color )
	{
		if ( glow_property )
		{
			const auto owner_entity = memory::read<std::uintptr_t>( glow_property + 0x18 );
			if ( owner_entity )
			{
				const auto owner_hash = fnv1a::runtime_hash( systems::g_entities.get_schema_name( owner_entity ) );
				if ( owner_hash )
				{
					if ( features::esp::player::g_glow.on_get_glow_color( owner_entity, owner_hash, color ) )
					{
						return;
					}

					if ( features::esp::item::g_glow.on_get_glow_color( owner_entity, owner_hash, color ) )
					{
						return;
					}
				}
			}
		}

		m_get_glow_color.call<void>( glow_property, color );
	}

	void __fastcall cheat::generate_primitives( std::uintptr_t thisptr, std::uintptr_t scene_object, std::uintptr_t scene_view, std::uintptr_t primitive_buffer )
	{
		diag::exception_scope exception_scope{ "chams: generate primitives" };

		if ( scene_object )
		{
			// Оптимизация идёт первой и до всех chams: принудительный LOD --
			// это изменение самого scene object, и chams, создавая свои
			// копии объектов, должен видеть уже готовое состояние.
			features::misc::g_optimization.on_scene_object( scene_object );

			if ( features::esp::player::g_chams.bt( ).is_active( scene_object ) )
			{
				return;
			}

			if ( features::esp::player::g_chams.os( ).is_active( scene_object ) ) 
			{
				return;
			}

			const auto owner_handle = memory::read<std::uint32_t>( scene_object + 0xc0 );
			if ( owner_handle )
			{
				const auto owner_entity = systems::g_entities.lookup( owner_handle );
				if ( owner_entity )
				{
					const auto owner_hash = fnv1a::runtime_hash( systems::g_entities.get_schema_name( owner_entity ) );
					if ( owner_hash )
					{
						if ( features::esp::player::g_chams.on_generate_primitives( owner_entity, owner_hash, scene_object, primitive_buffer, m_generate_primitives.original<void( __fastcall* )( std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t )>( ), thisptr, scene_view ) )
						{
							return;
						}

						if ( features::esp::item::g_chams.on_generate_primitives( owner_entity, owner_hash, scene_object, primitive_buffer, m_generate_primitives.original<void( __fastcall* )( std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t )>( ), thisptr, scene_view ) )
						{
							return;
						}

						systems::g_model_preview.on_generate_primitives(
							owner_entity,
							owner_hash,
							scene_object,
							primitive_buffer,
							m_generate_primitives.original<void( __fastcall* )( std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t )>( ),
							thisptr,
							scene_view
						);
					}
				}
			}
		}

		m_generate_primitives.call<void>( thisptr, scene_object, scene_view, primitive_buffer );
	}

	// Split out of get_resource_view so its caller can hold a diag::probe_scope.
	// MSVC forbids unwindable C++ locals in a function carrying __try, and the
	// scope is what tells the crash handler that the faults below are expected.
	static ID3D11ShaderResourceView* inspect_texture_for_preview( void** texture, ID3D11ShaderResourceView* srv )
	{
		// the texture name field is not a valid pointer for every texture type;
		// reading it can fault, and on a bad pointer strstr( ) would scan an
		// unbounded amount of mapped memory before faulting - a seconds-long
		// freeze on every texture load (inject/connect). validate the pointer
		// and bound the scan instead; SEH stays as a last-resort backstop
		__try
		{
			const auto current = static_cast< ID3D11ShaderResourceView* >( systems::g_model_preview.get_current_texture( ) );

			// fast path: once the preview texture is known, only its own handle
			// needs any work - every other texture request is one compare
			if ( texture == s_preview_texture_handle )
			{
				if ( current != srv )
				{
					srv->AddRef( );
					if ( current )
					{
						current->Release( );
					}

					systems::g_model_preview.set_preview_texture( srv );
				}

				return srv;
			}

			// already captured and this is not the preview handle: nothing to do
			if ( current )
			{
				return srv;
			}

			// Nothing can be captured before the preview panel's script is
			// injected -- the texture we are looking for is created by that
			// script, so until then this search can only ever fail.
			//
			// It is not a cheap failure. Everything below runs per texture
			// resource-view request and starts with a VirtualQuery, a kernel
			// transition. In a match that went unnoticed: the world renderer
			// reuses its views, and once the preview texture is found every later
			// call takes the single-compare fast path above. The main menu is the
			// opposite case -- Panorama requests views constantly and the script
			// is not injected there, so every request paid the syscall plus a
			// 256-byte scan. That is the whole gap between ~10 fps in the menu and
			// ~150 in a match.
			if ( !systems::g_model_preview.capture_pending( ) )
			{
				return srv;
			}

			// Already examined and rejected on an earlier frame: nothing has
			// changed about this texture, so do not pay for it again.
			const auto slot = rejected_texture_slot( texture );
			if ( s_rejected_textures[ slot ].load( std::memory_order_relaxed ) == texture )
			{
				return srv;
			}

			// the name lives behind the second pointer slot of the texture object.
			// Check each hop before taking it: on texture types where that slot is
			// not a pointer these dereferences fault, and SEH here is a backstop,
			// not a control-flow mechanism.
			const auto name_slot = reinterpret_cast< const char*** >( texture ) + 1;
			if ( !is_readable_pointer( name_slot, sizeof( const char** ) ) )
			{
				return srv;
			}

			const auto name_field = *name_slot;
			if ( !is_readable_pointer( name_field, sizeof( const char* ) ) )
			{
				return srv;
			}

			const auto texture_name = *name_field;
			if ( !is_readable_pointer( texture_name, 1 ) )
			{
				return srv;
			}

			static constexpr const char k_preview_texture_name[ ] = "example_model_preview";
			constexpr auto k_name_len = sizeof( k_preview_texture_name ) - 1;
			constexpr auto k_max_scan = 256u;

			// substring scan with a hard byte cap: a bad name can cost at most
			// a few hundred iterations instead of a multi-gigabyte strstr walk
			auto match = false;
			for ( auto i = 0u; i < k_max_scan; ++i )
			{
				const auto ch = texture_name[ i ];
				if ( ch == '\0' )
				{
					break;
				}

				if ( ch == k_preview_texture_name[ 0 ]
					&& std::memcmp( texture_name + i, k_preview_texture_name, k_name_len ) == 0 )
				{
					match = true;
					break;
				}
			}

			if ( match )
			{
				s_preview_texture_handle = texture;
				srv->AddRef( );
				systems::g_model_preview.set_preview_texture( srv );
			}
			else
			{
				// Only now, having actually read a name and compared it. Marking
				// before the read meant a texture we simply could not inspect this
				// frame -- name not populated yet, page not committed -- was
				// blacklisted forever, so the preview texture could be missed
				// permanently if we happened to look at it a moment too early.
				s_rejected_textures[ slot ].store( texture, std::memory_order_relaxed );
			}
		}
		__except ( EXCEPTION_EXECUTE_HANDLER )
		{
		}

		return srv;
	}


	// CPlayer_MovementServices::QuantizeMovement( services, cmd )
	//
	// With sv_quantize_movement_input on, the engine rounds the command's three
	// analog axes to whole steps -- V_roundf on cmd+0x2C, +0x30 and +0x34, and
	// nothing else. Valve's own description says why: it "restricts players from
	// using analog input to move at fractional speeds normally impossible with
	// digital button input".
	//
	// That is what breaks an anti-aim on such a server. The movement correction
	// computes a direction like "0.3 forward, 0.9 left" to keep the player walking
	// where they intended while the view is turned elsewhere; the engine rounds
	// that to "0 forward, 1 left", and the fraction is simply lost. Every tick.
	//
	// So carry the remainder. The value that gets rounded is nudged by whatever
	// was thrown away last time, and the leftovers accumulate until they are worth
	// a whole step -- three ticks of 0.3 forward eventually produce one tick of 1
	// forward, and the average direction is the one that was asked for. The engine
	// does the rounding itself here, so there is no guessing about the rule.
	//
	// Only the pair the correction actually controls is touched; up is left alone.
	std::uintptr_t __fastcall cheat::quantize_movement( std::uintptr_t movement_services, std::uintptr_t user_cmd )
	{
		if ( !user_cmd )
		{
			return m_quantize_movement.call<std::uintptr_t>( movement_services, user_cmd );
		}

		auto* forward = reinterpret_cast< float* >( user_cmd + 0x2C );
		auto* left = reinterpret_cast< float* >( user_cmd + 0x30 );

		// Carried remainder, per axis.
		static float carry_forward{};
		static float carry_left{};

		// Last command this ran for, and what it produced.
		//
		// Measured: the engine puts a command through here about thirteen times per
		// tick, replaying it through prediction, and each replay arrives with the
		// fractional values restored. Advancing the remainder on every one of those
		// would average over prediction passes instead of over commands actually
		// sent -- thirteen times the intended rate, and biased by however many
		// replays a given tick happened to need. A repeat gets the answer the first
		// pass reached, and the remainder stays where it was.
		static std::uintptr_t last_cmd{};
		static float last_raw_forward{};
		static float last_raw_left{};
		static float last_sent_forward{};
		static float last_sent_left{};

		const auto fractional = [ ]( float value )
			{
				return std::fabsf( value - std::roundf( value ) ) > 1.0e-4f;
			};

		// Prediction can run a command through here more than once. A value that is
		// already whole has nothing left to dither, which makes the second pass a
		// no-op without having to track command identity.
		const auto worth_dithering = features::combat::g_misc.antiaim( ).has_modified_angles( )
			&& ( fractional( *forward ) || fractional( *left ) );

		if ( !worth_dithering )
		{
			// Holding a stale remainder across a gap would spend it on a direction
			// nobody asked for any more.
			if ( !features::combat::g_misc.antiaim( ).has_modified_angles( ) )
			{
				carry_forward = 0.0f;
				carry_left = 0.0f;
			}

			return m_quantize_movement.call<std::uintptr_t>( movement_services, user_cmd );
		}

		const auto same_command = user_cmd == last_cmd
			&& std::fabsf( *forward - last_raw_forward ) < 1.0e-5f
			&& std::fabsf( *left - last_raw_left ) < 1.0e-5f;

		if ( same_command )
		{
			*forward = last_sent_forward;
			*left = last_sent_left;
			return m_quantize_movement.call<std::uintptr_t>( movement_services, user_cmd );
		}

		last_cmd = user_cmd;
		last_raw_forward = *forward;
		last_raw_left = *left;

		const auto wanted_forward = *forward + carry_forward;
		const auto wanted_left = *left + carry_left;

		*forward = wanted_forward;
		*left = wanted_left;

		const auto result = m_quantize_movement.call<std::uintptr_t>( movement_services, user_cmd );

		carry_forward = std::clamp( wanted_forward - *forward, -1.0f, 1.0f );
		carry_left = std::clamp( wanted_left - *left, -1.0f, 1.0f );

		last_sent_forward = *forward;
		last_sent_left = *left;

		{
			static std::atomic<std::uint32_t> seen{};
			const auto n = seen.fetch_add( 1, std::memory_order_relaxed ) + 1;

			if ( n <= 10 || ( n % 500 ) == 0 )
			{
				char line[ 192 ]{};
				_snprintf_s( line, sizeof( line ), _TRUNCATE,
					"[quantize] #%u wanted %.3f,%.3f -> sent %.1f,%.1f carry %.3f,%.3f",
					n, wanted_forward, wanted_left, *forward, *left, carry_forward, carry_left );
				diag::step( line );
			}
		}

		return result;
	}

	ID3D11ShaderResourceView* cheat::get_resource_view( void* texture_manager, void** texture, char a3, char a4, const char* a5 )
	{
		// No perf probe here on purpose. At roughly five thousand calls a frame the
		// two QueryPerformanceCounter calls cost more than everything this function
		// does in its normal state, and the measurement reported mostly itself.
		auto* srv = m_get_resource_view.call< ID3D11ShaderResourceView* >( texture_manager, texture, a3, a4, a5 );
		// Measured at roughly five thousand calls per frame, so the shape of this
		// function matters more than what is inside it. Everything that can decide
		// "not interesting" is hoisted above the probe scope: once the preview
		// texture is captured -- which is the state the game spends nearly all its
		// time in -- a call costs three compares and a return, with no thread-local
		// writes and no call into the identification path at all.
		if ( !srv || !texture || a4 != 0 || !systems::g_model_preview.capture_pending( ) )
		{
			return srv;
		}

		// Marks the faults raised by the probe below as expected, so the vectored
		// crash handler leaves them alone instead of writing a crash report for
		// each one.
		diag::probe_scope probe;

		return inspect_texture_for_preview( texture, srv );
	}

	std::uintptr_t __fastcall cheat::parse_report_hit( std::uintptr_t thisptr, std::uint8_t deleting )
	{
		// Capture the protobuf fields before the deleting destructor can free them.
		features::misc::g_impacts.on_report_hit( thisptr );

		return m_parse_report_hit.call<std::uintptr_t>( thisptr, deleting );
	}

	std::uintptr_t __fastcall cheat::setup_fog( __m128i* output, int* mode )
	{
		// Оптимизация перебивает и мир, и ванильный туман -- она и стоит
		// первой. Свой туман (weather.cpp) остаётся за игроком: owner проекта
		// просил именно вырезать лишний рендер, а не отнимать фичи.
		if ( features::misc::g_optimization.override_fog( output, mode ) )
		{
			return 0;
		}

		if ( features::world::g_scene.on_setup_fog( output, mode ) )
		{
			return 0;
		}

		return m_setup_fog.call<std::uintptr_t>( output, mode );
	}

	std::uintptr_t __fastcall cheat::set_shader_param( __m128i* map, std::uint32_t hash, __m128i* value )
	{
		features::world::g_scene.on_set_shader_param( value, hash );

		// scene.cpp мог подменить значение (гамма, блум, погода) -- поэтому
		// оптимизация фильтрует уже итоговый указатель, а не исходный.
		value = features::misc::g_optimization.intercept_shader_param( hash, value );

		return m_set_shader_param.call<std::uintptr_t>( map, hash, value );
	}

	std::uintptr_t __fastcall cheat::set_postprocess_vec( __m128i* map, std::uint32_t hash, __m128i* value )
	{
		constexpr std::uint32_t dof_ranges{ 0x2ACAB07C };

		// The engine only publishes DofRanges when the active camera enables DOF.
		// Insert it alongside any post-process vector, matching Artisan's live path.
		if ( settings::g_world.m_scene.dof.value && hash != dof_ranges )
		{
			__m128i* dof_value{};
			features::world::g_scene.on_set_shader_param( dof_value, dof_ranges );
			if ( dof_value )
			{
				m_set_postprocess_vec.call<std::uintptr_t>( map, dof_ranges, dof_value );
			}
		}

		features::world::g_scene.on_set_shader_param( value, hash );
		return m_set_postprocess_vec.call<std::uintptr_t>( map, hash, value );
	}

	void __fastcall cheat::override_view( std::uintptr_t thisptr, std::uintptr_t view_setup )
	{
		m_override_view.call<void>( thisptr, view_setup );

		features::misc::g_camera.on_override_view( view_setup );
		features::misc::g_removals.on_override_view( view_setup );
		features::combat::g_misc.duckpeek( ).on_override_view( view_setup );
	}

	void __fastcall cheat::update_fov_sensitivity( std::uintptr_t thisptr )
	{
		m_update_fov_sensitivity.call<void>( thisptr );

		features::misc::g_camera.update_fov_sensitivity( thisptr );
	}

	void __fastcall cheat::render_scope( std::uintptr_t a1, std::uintptr_t a2 )
	{
		m_render_scope.call<void>( a1, a2 );

		if ( settings::g_misc.m_removals.scope.value )
		{
			memory::write<std::uint8_t>( a2 + 4, 0 );
		}
	}

	bool __fastcall cheat::render_crosshair( std::uintptr_t a1 )
	{
		if ( settings::g_misc.m_removals.crosshair.value )
		{
			return false;
		}

		return m_render_crosshair.call<bool>( a1 );
	}

	float __fastcall cheat::prepare_scene_material( std::uintptr_t material, void* a2, float a3 )
	{
		// Оптимизация идёт ПЕРВОЙ: если объект вырезан, вызывать оригинал
		// незачем -- именно на этом и экономится время загрузки сцены.
		if ( features::misc::g_optimization.should_skip_object( material ) )
		{
			return 0.0f;
		}

		features::misc::g_removals.on_prepare_scene_material( material );

		return m_prepare_scene_material.call<float>( material, a2, a3 );
	}

	void __fastcall cheat::post_network_data_received( std::uintptr_t thisptr )
	{
		m_post_network_data_received.call<void>( thisptr );
	}

	bool __fastcall cheat::draw_overhead( std::uintptr_t pawn, std::uint32_t player_slot )
	{
		if ( settings::g_misc.m_removals.overhead.value && pawn == systems::g_local.get( ).pawn )
		{
			return false;
		}

		return m_draw_overhead.call<bool>( pawn, player_slot );
	}

	std::uintptr_t __fastcall cheat::draw_legs( std::uintptr_t a1, std::uintptr_t a2, std::uintptr_t a3, std::uintptr_t a4, std::uintptr_t a5 )
	{
		if ( settings::g_misc.m_removals.legs.value )
		{
			return 0;
		}

		return m_draw_legs.call<std::uintptr_t>( a1, a2, a3, a4, a5 );
	}

	bool __fastcall cheat::get_transforms_for_hitbox_list( std::uintptr_t a1, std::uintptr_t a2, int* a3 )
	{
		if ( !features::combat::g_shared.autowalling( ) )
		{
			return m_get_transforms_for_hitbox_list.call<bool>( a1, a2, a3 );
		}

		const auto record = features::combat::g_shared.current_autowall_record( );
		if ( !record || !record->valid )
		{
			return m_get_transforms_for_hitbox_list.call<bool>( a1, a2, a3 );
		}

		const auto count = memory::safe_read<int>( reinterpret_cast< std::uintptr_t >( a3 ) ).value_or( 0 );
		const auto shape_array = memory::safe_read<std::uintptr_t>( reinterpret_cast< std::uintptr_t >( a3 ) + 8 ).value_or( 0 );
		const auto entity_bone_cache = memory::safe_read<std::uintptr_t>( a1 + 0x1c0 ).value_or( 0 );
		const auto model_handle = memory::safe_read<std::uintptr_t>( a1 + 0x1e0 ).value_or( 0 );
		const auto model = model_handle ? memory::safe_read<std::uintptr_t>( model_handle ).value_or( 0 ) : 0;

		if ( count <= 0 || count > 256 || !shape_array || !entity_bone_cache || !model )
		{
			return false;
		}

		static const auto get_bone_index = PATTERN( patterns::get_bone_index );
		if ( !get_bone_index )
		{
			return false;
		}

		// The client dereferences every resolved transform without checking the
		// backing cache. Reject a stale scene node instead of faulting in it.
		for ( auto i = 0; i < count; ++i )
		{
			const auto shape_ptr = shape_array + 16ull * i;
			const auto bone_index = memory::call<int>( get_bone_index, model, shape_ptr );

			if ( bone_index >= 0 &&
				( bone_index >= 256 ||
					!memory::safe_read<systems::bones::data>( entity_bone_cache + sizeof( systems::bones::data ) * bone_index ) ) )
			{
				return false;
			}
		}

		const auto target_scene = record->game_scene_node ? record->game_scene_node : memory::read<std::uintptr_t>( record->pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		const auto target_bone_cache = target_scene ? memory::safe_read<std::uintptr_t>( target_scene + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x80 ).value_or( 0 ) : 0;
		const auto result = m_get_transforms_for_hitbox_list.call<bool>( a1, a2, a3 );

		if ( !result )
		{
			return false;
		}

		const auto is_plausible = [ ]( std::uintptr_t ptr ) noexcept -> bool
			{
				return ptr >= 0x10000ull && ptr < 0x00007FFFFFFFFFFFull;
			};

		if ( !is_plausible( entity_bone_cache ) || !is_plausible( target_bone_cache ) || entity_bone_cache != target_bone_cache )
		{
			return result;
		}

		const auto output_array = memory::safe_read<std::uintptr_t>( a2 + 16 ).value_or( 0 );

		if ( !output_array || count <= 0 )
		{
			return result;
		}

		for ( auto i = 0; i < count; ++i )
		{
			const auto shape_ptr = shape_array + 16ull * i;
			const auto bone_index = memory::call<int>( get_bone_index, model, shape_ptr );

			if ( bone_index < 0 || bone_index >= record->bone_count )
			{
				continue;
			}

			const auto dst = output_array + 32ull * i;
			std::memcpy( reinterpret_cast< void* >( dst ), &record->bones[ bone_index ], 32 );
		}

		return true;
	}

	void __fastcall cheat::sort_primitives( std::uintptr_t thisptr, std::uintptr_t a2, std::uintptr_t a3, std::uint32_t a4 )
	{
		m_sort_primitives.call<void>( thisptr, a2, a3, a4 );

		diag::exception_scope exception_scope{ "chams: sort primitives" };

		perf::scope timing{ perf::id::chams };
		features::esp::player::g_chams.on_sort_primitives( a3, a4 );
	}

	float __fastcall cheat::get_inaccuracy( std::uintptr_t thisptr, float* a2, float* a3 )
	{
		const auto result = m_get_inaccuracy.call<float>( thisptr, a2, a3 );

#if defined(__clang__) || defined(__GNUC__)
		const auto result_address = reinterpret_cast< std::uintptr_t >( __builtin_return_address( 0 ) );
#else
		const auto result_address = reinterpret_cast< std::uintptr_t >( _ReturnAddress( ) );
#endif

		static const auto base_fire_guns_get_inaccuracy = PATTERN( patterns::base_fire_guns_get_inaccuracy );
		if ( base_fire_guns_get_inaccuracy &&
			result_address > base_fire_guns_get_inaccuracy &&
			result_address < base_fire_guns_get_inaccuracy + 0x600 )
		{
			features::misc::g_impacts.on_base_fire_guns_get_inaccuracy( thisptr, result );
		}

		// There used to be a second branch here that tried to catch the engine
		// pricing a shot. It never fired once across five sessions, with and
		// without a tick gate, which is the answer to a different question: CS2
		// does not reach this function on the fire path at all. on_base_fire_guns_
		// get_inaccuracy above has therefore never run either, and the shot record
		// it feeds has always gone out unconfirmed.

		return result;
	}

	float* __fastcall cheat::get_interpolated_shoot_position( std::uintptr_t thisptr, float* out, int* tick_frac )
	{
		const auto result = m_get_interpolated_shoot_position.call<float*>( thisptr, out, tick_frac );

		// Prediction calls this helper while building the next command as well.
		// Only the call made for an actual rage shot is its authoritative origin.
		if ( features::combat::g_rage.is_firing_this_tick( ) )
		{
			features::misc::g_impacts.on_get_interpolated_shoot_position( thisptr, out );
		}

		return result;
	}

	std::uintptr_t __fastcall cheat::level_initialization( std::uintptr_t a1, const char* new_map )
	{
		if ( new_map && new_map[ 0 ] )
		{
			const char* leaf = std::strrchr( new_map, '/' );
			rendering::g_widgets.s_map_name = leaf ? leaf + 1 : new_map;
		}
		else
		{
			rendering::g_widgets.s_map_name.clear( );
		}

		// the HUD (and the preview panel created on it) is rebuilt per level;
		// drop the cached SRV and recreate the panel on the new HUD
		systems::g_model_preview.reset( );
		s_preview_texture_handle = nullptr;
		clear_rejected_textures( );

		features::world::g_scene.reset_skybox_state( );
		features::misc::g_impacts.on_level_change( );
		features::misc::g_scoreboard_weapons.on_level_change( );

		// Сканер прострелов хранит результат по картам (см. map_store).
		// on_level_change идёт ПЕРВЫМ: он чистит метки и привязку к карте,
		// поэтому имя задаётся после него -- иначе сброс его же и сотрёт,
		// и на новой карте грузился бы файл предыдущей.
		features::misc::g_map_scan.on_level_change( );
		features::misc::g_map_scan.set_map_name( rendering::g_widgets.s_map_name );

		// Кэш объектов сцены и плоский материал привязаны к уровню: адреса
		// scene-объектов и указатели на материалы после перезагрузки карты
		// невалидны, поэтому чистим и пересоздаём.
		features::misc::g_optimization.on_level_change( );

		return m_level_initialization.call<std::uintptr_t>( a1, new_map );
	}

	std::uintptr_t __fastcall cheat::level_shutdown( std::uintptr_t a1 )
	{
		rendering::g_widgets.s_map_name.clear();

		// Release feature-owned scene objects before Source 2 tears their parents down.
		features::misc::g_dlight.on_level_shutdown( );

		// Release any in-progress doubletap network suppression before the level
		// (and its netchannel) goes away.
		features::combat::g_exploits.reset( );

		// the HUD (and the preview panel created on it) is destroyed when the
		// level unloads; drop the cached SRV so the panel gets recreated on the
		// main-menu root when the game returns to the menu
		systems::g_model_preview.reset( );
		s_preview_texture_handle = nullptr;
		clear_rejected_textures( );

		// clear all local player data on level shutdown
		systems::g_local.reset();

		return m_level_shutdown.call<std::uintptr_t>( a1 );
	}

	void __fastcall cheat::read_frame_input( std::uintptr_t a1, std::uint32_t a2 )
	{
		// input is pumped every frame on the main thread in every state,
		// including the main menu - tick the preview here as well so it can
		// start outside a match (frame stages are not reliable there)
		systems::g_model_preview.update( );

		m_read_frame_input.call<void>( a1, a2 );
	}

	void __fastcall cheat::process_input_event( std::uintptr_t csgo_input, int slot, float frametime )
	{
		systems::g_legit_input.on_process_input_event( csgo_input, slot );
		m_process_input_event.call<void>( csgo_input, slot, frametime );
	}

	std::uintptr_t __fastcall cheat::render_decals( std::uintptr_t render_context, std::uintptr_t** render_view, bool pass_flag_a, bool pass_flag_b )
	{
		if ( settings::g_misc.m_removals.decals.value )
		{
			return 0;
		}

		return m_render_decals.call<std::uintptr_t>( render_context, render_view, pass_flag_a, pass_flag_b );
	}

	void __fastcall cheat::render_smoke( std::uintptr_t a1, std::uintptr_t a2, int a3, int a4, std::uintptr_t a5, std::uintptr_t a6 )
	{
		static std::once_flag alright;
		std::call_once( alright, [ & ]
			{
				if ( !a2 )
				{
					return;
				}

				if ( m_render_smoke_map.create( reinterpret_cast< void* >( memory::get_vfunc( a2, 32 ) ), &render_smoke_map ) )
				{
					m_render_smoke_map.enable( );
				}

				if ( m_render_smoke_unmap.create( reinterpret_cast< void* >( memory::get_vfunc( a2, 33 ) ), &render_smoke_unmap ) )
				{
					m_render_smoke_unmap.enable( );
				}
			} );

		if ( settings::g_misc.m_removals.smoke.value )
		{
			return;
		}

		m_render_smoke.call<void>( a1, a2, a3, a4, a5, a6 );
	}

	std::uintptr_t __fastcall cheat::render_smoke_map( std::uintptr_t thisptr, std::size_t size, std::uintptr_t* out_ptr )
	{
		const auto result = m_render_smoke_map.call<std::uintptr_t>( thisptr, size, out_ptr );

		features::world::g_smoke.on_map( result, size, out_ptr && *out_ptr ? *out_ptr : 0 );

		return result;
	}

	void __fastcall cheat::render_smoke_unmap( std::uintptr_t thisptr, std::uintptr_t ctx, std::size_t size )
	{
		features::world::g_smoke.on_unmap( ctx );
		m_render_smoke_unmap.call<void>( thisptr, ctx, size );
	}

	char __fastcall cheat::set_info( std::uintptr_t rcx, std::uintptr_t a2 )
	{
		const auto& cfg = settings::g_misc.m_name_changer;
		const auto should_override = cfg.clantag.value || cfg.override_name.value || features::misc::other::s_name_change_pending;
		if ( should_override && a2 )
		{
			const auto arg_list = memory::safe_read<std::uintptr_t>( a2 + 0x440 ).value_or( 0 );
			const auto key = arg_list
				? memory::safe_read<const char*>( arg_list + 0x8 ).value_or( nullptr )
				: nullptr;

			if ( key && _stricmp( key, "name" ) == 0 )
			{
				constexpr std::uint64_t fcvar_protected = 1ull << 5;
				constexpr std::uint64_t fcvar_userinfo = 1ull << 9;
				constexpr std::uint64_t fcvar_registry_restricted = 1ull << 10;

				if ( addresses::globals::cvar )
				{
					if ( const auto name_cvar = addresses::globals::cvar->find( "name"_hash ) )
					{
						const auto flags_address = reinterpret_cast<std::uintptr_t>( name_cvar ) + offsetof( c_convar, m_flags );
						if ( const auto flags = memory::safe_read<std::uint64_t>( flags_address ) )
						{
							static_cast<void>( memory::safe_write<std::uint64_t>( flags_address,
								( *flags | fcvar_userinfo ) & ~( fcvar_protected | fcvar_registry_restricted ) ) );
						}
					}
				}

				const auto& display = features::misc::other::s_display_name;
				if ( !display.empty( ) )
				{
					static_cast<void>( memory::safe_write<const char*>( arg_list + 0x10, display.c_str( ) ) );
				}
			}
		}

		return m_set_info.call<char>( rcx, a2 );
	}

	void __fastcall cheat::draw_flash_effect( std::uintptr_t a1, int a2, std::uintptr_t* a3, std::uintptr_t a4, __m128* a5 )
	{
		if ( settings::g_misc.m_removals.flash_alpha.value < 100.0f && settings::g_misc.m_removals.flash_alpha.value != 0.0f )
		{
			const auto view_pawn = systems::g_local.get( ).view_pawn( );
			if ( view_pawn )
			{
				const auto max = settings::g_misc.m_removals.flash_alpha.value / 100.0f * 255.0f;
				memory::write<float>( view_pawn + SCHEMA( "C_CSPlayerPawnBase", "m_flFlashMaxAlpha"_hash ), max );
			}
		}

		if ( settings::g_misc.m_removals.flash_alpha.value != 0.0f )
		{
			m_draw_flash_effect.call<void>( a1, a2, a3, a4, a5 );
		}
	}

} // namespace hooks
