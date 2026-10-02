#include <pch/pch.hpp>
#include <algorithm>
#include <cstring>

#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/fnv1a.hpp>

#include <core/systems/systems.hpp>
#include <core/rendering/rendering.hpp>
#include <core/features/features.hpp>
#include <core/hooks/hooks.hpp>

namespace systems
{
	// plain string literals: the preview panel scripts are injected verbatim
	static constexpr const char k_preview_script[ ] = R"PANORAMA(
(function () {
    var root = $.GetContextPanel();
    if (!root)
        return;

    var old = root.FindChildTraverse('ExampleModelPreviewRoot');
    if (old) {
        try { old.DeleteAsync(0.0); } catch (e) {}
    }

    var container = $.CreatePanel('Panel', root, 'ExampleModelPreviewRoot', {
        hittest: false,
        style: 'width: 512px; height: 512px; opacity: 0.01; brightness: 0.0; wash-color: #00000000; position: 0px 0px 0px; z-index: -99999;'
    });

    if (!container)
        return;

    var sequence = 't_main_menu_rifle_awp_lookat';
    var model = 'agents/models/tm_professional/tm_professional_varj.vmdl';
    var weaponDefIndex = 40; // SSG08

    var panel = $.CreatePanel('MapPlayerPreviewPanel', container, 'ExampleModelPreview', {
        map: 'ui/buy_menu',
        camera: 'cam_loadoutmenu_ct',
        'require-composition-layer': true,
        'composition-layer-texture-name': 'example_model_preview',
        playermodel: model,
        playername: 'vanity_character',
        animgraphcharactermode: 'main-menu',
        pose_sequence: sequence,
        player: true,
        mouse_rotate: true,
        sync_spawn_addons: true,
        'transparent-background': true,
        'pin-fov': 'vertical',
        csm_split_plane0_distance_override: '120.0',
        hittest: false,
        style: 'width: 100%; height: 100%;'
    });

    if (!panel)
        return;

    try {
        if (panel.EquipPlayerWithItem && typeof BigInt === 'function') {
            var itemId = BigInt('0xF000000000000000') | BigInt(weaponDefIndex);
            panel.EquipPlayerWithItem(itemId);
        }
    } catch (e) {}

    var replay = function () {
        try {
            if (!panel)
                return;

            if (panel.IsValid && !panel.IsValid())
                return;

            if (panel.PlaySequence)
                panel.PlaySequence(sequence);

            $.Schedule(8.0, replay);
        } catch (e) {}
    };

    var keepAliveFlip = false;

    var keepAlive = function () {
        try {
            if (!panel)
                return;

            if (panel.IsValid && !panel.IsValid())
                return;

            keepAliveFlip = !keepAliveFlip;
            panel.style.opacity = keepAliveFlip ? '1.0' : '0.999';

            $.Schedule(0.5, keepAlive);
        } catch (e) {}
    };

    $.Schedule(0.20, replay);
    $.Schedule(0.5, keepAlive);
})();
)PANORAMA";

	static constexpr const char k_ready_script[ ] = R"PANORAMA(
(function () {
    var root = $.GetContextPanel();
    var p = root ? root.FindChildTraverse('ExampleModelPreview') : null;
    if (!p)
        return;

    if (p.SetReadyForDisplay)
        p.SetReadyForDisplay(true);
})();
)PANORAMA";

	static constexpr const char k_kick_script[ ] = R"PANORAMA(
(function () {
    var root = $.GetContextPanel();
    var p = root ? root.FindChildTraverse('ExampleModelPreview') : null;
    if (!p)
        return;

    if (p.SetReadyForDisplay)
        p.SetReadyForDisplay(true);

    if (p.PlaySequence)
        p.PlaySequence('t_main_menu_rifle_awp_lookat');
})();
)PANORAMA";

	static constexpr const char k_pause_script[ ] = R"PANORAMA(
(function () {
    var root = $.GetContextPanel();
    var p = root ? root.FindChildTraverse('ExampleModelPreview') : null;
    if (p && p.SetReadyForDisplay)
        p.SetReadyForDisplay(false);
})();
)PANORAMA";

	bool model_preview::initialize( )
	{
		m_initialized = true;
		m_current_texture = nullptr;
		m_preview_pawn = 0;
		m_panel = 0;
		m_script_injected = false;
		m_ui_engine = 0;
		m_script_panel = 0;
		m_init_throttle = 0;
		m_panel_walk_throttle = 0;
		m_state_assert_throttle = 0;

		// false, not true. The flag is only a change detector: update() sends the
		// kick script when visibility differs from it, and the kick is what makes a
		// freshly injected panel start rendering. Seeding it true claimed the
		// preview was already up, so with the menu open on the Visuals tab no
		// transition ever occurred, the new panel was never kicked, and there was no
		// composition layer to capture. That is the whole of "nothing on first
		// launch, but the model appears once you exit to the menu" -- the
		// re-injection path seeds it false and kicks.
		m_menu_was_open = false;
		std::fill( std::begin( m_world_to_clip ), std::end( m_world_to_clip ), 0.0f );

		logging::console::print( xs( "[model_preview] initialized" ) );
		return true;
	}

	// Resolve a Panorama root global into the panel underneath it.
	//
	// The indirection is not the same for every one of these globals, and getting
	// it wrong yields a plausible-looking pointer that faults later, so try each
	// shape and let the data decide. A real panel has a readable vtable whose
	// first slot is also readable, and a printable name string at +0x10 -- the
	// same test discover_panel_array uses, and the reason that one survived a game
	// update that broke the hard-coded offsets next to it.
	std::uintptr_t model_preview::resolve_root_panel( std::uintptr_t global, const char* label ) const
	{
		// Deliberately not an early return: a null global and a global that never
		// gets asked about produce the same silence otherwise, and that ambiguity
		// has already cost one test round here.
		const auto looks_like_panel = [ ]( std::uintptr_t panel )
			{
				if ( panel <= 0x10000ull || ( panel >> 48 ) != 0 )
				{
					return false;
				}

				const auto vtable = memory::safe_read<std::uintptr_t>( panel ).value_or( 0 );
				if ( !vtable || !memory::safe_read<std::uintptr_t>( vtable ).value_or( 0 ) )
				{
					return false;
				}

				const auto name_ptr = memory::safe_read<std::uintptr_t>( panel + 0x10 ).value_or( 0 );
				if ( !name_ptr )
				{
					return false;
				}

				const auto first = memory::safe_read<char>( name_ptr ).value_or( 0 );
				return first > 0x20 && first < 0x7f;
			};

		const auto first_hop = global
			? memory::safe_read<std::uintptr_t>( global ).value_or( 0 )
			: 0;

		// Ordered widest-first: the wrapper form the HUD root uses, then the two
		// plainer ones.
		const std::uintptr_t candidates[]
		{
			first_hop ? memory::safe_read<std::uintptr_t>( first_hop + 0x8 ).value_or( 0 ) : 0,
			first_hop,
			first_hop ? memory::safe_read<std::uintptr_t>( first_hop ).value_or( 0 ) : 0
		};

		for ( auto i = 0; i < 3; ++i )
		{
			if ( looks_like_panel( candidates[ i ] ) )
			{
				if ( !this->m_logged_root[ i ] )
				{
					this->m_logged_root[ i ] = true;

					char line[ 176 ]{};
					_snprintf_s( line, sizeof( line ), _TRUNCATE,
						"[model_preview] root %s resolved via shape %d: panel 0x%llX",
						label, i, static_cast< unsigned long long >( candidates[ i ] ) );
					diag::step( line );
				}

				return candidates[ i ];
			}
		}

		// The pattern resolved -- it is absent from the "pattern not found" errors
		// -- yet nothing behind it looked like a panel. Two things can produce
		// that, and they want opposite fixes: the global holds its panel in a shape
		// this does not try, or the pattern matched the wrong instruction (the main
		// menu one carries only eight fixed bytes, and the scan takes the first
		// match in the module). Print the raw chain and the module offset so the
		// two can be told apart instead of guessed at.
		{
			static std::atomic<std::uint32_t> reported{};
			if ( reported.fetch_add( 1, std::memory_order_relaxed ) < 8u )
			{
				const auto client = memory::get_module_base( "client.dll" );

				char line[ 240 ]{};
				_snprintf_s( line, sizeof( line ), _TRUNCATE,
					"[model_preview] root %s unresolved: global client.dll+0x%llX, hop 0x%llX, cand 0x%llX / 0x%llX / 0x%llX",
					label,
					static_cast< unsigned long long >( client ? global - client : 0 ),
					static_cast< unsigned long long >( first_hop ),
					static_cast< unsigned long long >( candidates[ 0 ] ),
					static_cast< unsigned long long >( candidates[ 1 ] ),
					static_cast< unsigned long long >( candidates[ 2 ] ) );
				diag::step( line );
			}
		}

		return 0;
	}

	std::uintptr_t model_preview::find_script_panel( ) const
	{
		// The HUD root exists as an object in the main menu but is not laid out
		// there, and a panel that is never laid out never produces the composition
		// layer the preview reads -- which is why the script injected happily and
		// the texture never appeared before a match started.
		//
		// With the main menu root available the preview can live under whichever
		// tree is actually being drawn.
		// Unconditionally, and first. Gating this on being out of a match was my
		// own invention; the working implementation this was checked against tries
		// main menu, then the hud panel, then the hud global, in that order and in
		// every state -- the menu tree is the context this panel type expects, and
		// it is laid out during a match as well.
		//
		// It also removes the churn: the menu root survives a level change, so the
		// panel no longer has to be rebuilt every time a map loads.
		if ( const auto menu = this->resolve_root_panel( addresses::globals::main_menu_panel, "main menu" ) )
		{
			return menu;
		}

		if ( const auto hud = this->resolve_root_panel( addresses::globals::hud, "hud" ) )
		{
			return hud;
		}

		// Second route to the same tree, from a different instruction. Kept as a
		// fallback because the two patterns fail independently.
		return this->resolve_root_panel( addresses::globals::csgo_hud_panel, "hud (alt)" );
	}

	bool model_preview::create_panel( )
	{
		if ( m_script_injected )
		{
			return true;
		}

		if ( !addresses::globals::panorama )
		{
			return false;
		}

		const auto script_panel = find_script_panel( );
		if ( !script_panel )
		{
			return false;
		}

		const auto ui_engine = memory::call_vfunc<std::uintptr_t>( addresses::globals::panorama, 13 );
		if ( !ui_engine )
		{
			return false;
		}

		m_ui_engine = ui_engine;
		m_script_panel = script_panel;

		memory::call_vfunc<void>(
			m_ui_engine,
			77,
			reinterpret_cast<void*>( m_script_panel ),
			static_cast<const char*>( k_preview_script ),
			"",
			static_cast<std::uint64_t>( 1 ) );

		m_script_injected = true;
		logging::console::print( xs( "[model_preview] preview panel script injected" ) );
		return true;
	}

	// One-shot inventory of every panel the UI engine knows about, written to the
	// diagnostics log. The preview panel is parented to the HUD root, which exists
	// as an object in the main menu but is not necessarily rendered there -- and a
	// panel that is never laid out never produces its composition layer, which is
	// exactly the symptom: the script injects, the texture never appears.
	//
	// Rather than guess which root to reparent to, dump the names once and pick
	// from what is actually there.
	// Locate the UI engine's panel array by shape rather than by a hard-coded
	// offset.
	//
	// The offsets that used to be written in here (+0x228 for the array, +0x230
	// for the count) stopped being right at some game update: reading them back
	// gave fragments of the engine pointer itself, which meant find_preview_panel
	// had quietly never found anything, and with it m_world_to_clip was never
	// filled in either.
	//
	// The data has a recognisable shape, so search for that instead: an array of
	// 0x20-byte records, each holding a panel pointer at +0x10, each panel holding
	// a readable name string at +0x10. A candidate has to satisfy all of it across
	// several entries before it is believed, which is what makes this safe to run
	// against arbitrary memory -- a wrong guess fails the check rather than being
	// used.
	bool model_preview::discover_panel_array( )
	{
		if ( this->m_array_offset )
		{
			return true;
		}

		if ( !this->m_ui_engine )
		{
			return false;
		}

		const auto plausible_pointer = []( std::uintptr_t value )
			{
				return value > 0x10000ull && ( value >> 48 ) == 0;
			};

		// One record looks right if it leads to a panel with a printable name.
		const auto record_is_panel = [ & ]( std::uintptr_t array, int index )
			{
				const auto entry = array + static_cast<std::size_t>( index ) * 0x20;
				const auto panel = memory::safe_read<std::uintptr_t>( entry + 0x10 ).value_or( 0 );
				if ( !plausible_pointer( panel ) )
				{
					return false;
				}

				const auto name_ptr = memory::safe_read<std::uintptr_t>( panel + 0x10 ).value_or( 0 );
				if ( !plausible_pointer( name_ptr ) )
				{
					return false;
				}

				const auto first = memory::safe_read<char>( name_ptr ).value_or( 0 );
				return first > 0x20 && first < 0x7f;
			};

		for ( std::size_t offset = 0x100; offset <= 0x800; offset += 0x8 )
		{
			const auto array = memory::safe_read<std::uintptr_t>( this->m_ui_engine + offset ).value_or( 0 );
			if ( !plausible_pointer( array ) )
			{
				continue;
			}

			// The count usually sits next to the pointer; try both sides rather
			// than assuming which.
			for ( const auto count_offset : { offset + 0x8, offset + 0x10 } )
			{
				const auto count = memory::safe_read<int>( this->m_ui_engine + count_offset ).value_or( 0 );
				if ( count < 8 || count > 8192 )
				{
					continue;
				}

				// Demand agreement across several records. A single lucky pointer
				// proves nothing; eight in a row in the right shape is not chance.
				const auto probes = std::min( count, 8 );
				auto good = 0;

				for ( auto i = 0; i < probes; ++i )
				{
					if ( record_is_panel( array, i ) )
					{
						++good;
					}
				}

				if ( good < probes - 1 )
				{
					continue;
				}

				this->m_array_offset = offset;
				this->m_count_offset = count_offset;

				char line[ 160 ]{};
				_snprintf_s( line, sizeof( line ), _TRUNCATE,
					"[model_preview] panel array found: array +0x%zX count +0x%zX (%d panels, %d/%d probes ok)",
					offset, count_offset, count, good, probes );
				diag::step( line );

				return true;
			}
		}

		if ( ( ++this->m_discover_retry % 240 ) == 1 )
		{
			diag::step( "[model_preview] panel array not found in +0x100..+0x800" );
		}

		return false;
	}

	void model_preview::find_preview_panel( )
	{
		if ( !m_panel )
		{
			if ( ( ++m_panel_walk_throttle % 120 ) != 1 )
			{
				return;
			}

			const auto engine = m_ui_engine;
			if ( !engine )
			{
				return;
			}

			if ( !this->discover_panel_array( ) )
			{
				return;
			}

			const auto panels = memory::safe_read<std::uintptr_t>( engine + this->m_array_offset ).value_or( 0 );
			const auto count = memory::safe_read<int>( engine + this->m_count_offset ).value_or( 0 );
			if ( !panels || count <= 0 || count > 8192 )
			{
				return;
			}

			for ( auto i = 0; i < count; ++i )
			{
				const auto entry = panels + static_cast< std::size_t >( i ) * 0x20;
				const auto panel_ptr = memory::safe_read<std::uintptr_t>( entry + 0x10 ).value_or( 0 );
				if ( !panel_ptr )
				{
					continue;
				}

				const auto name_ptr = memory::safe_read<std::uintptr_t>( panel_ptr + 0x10 ).value_or( 0 );
				if ( !name_ptr )
				{
					continue;
				}

				char name[ 64 ]{};
				for ( auto k = 0; k < 63; ++k )
				{
					const auto ch = memory::safe_read<char>( name_ptr + k ).value_or( 0 );
					name[ k ] = ch;
					if ( ch == '\0' )
					{
						break;
					}
				}
				name[ 63 ] = '\0';

				if ( std::strstr( name, "ExampleModelPreview" ) )
				{
					m_panel = panel_ptr;
					break;
				}
			}

			if ( !m_panel )
			{
				return;
			}
		}

		const auto vtable = memory::safe_read<std::uintptr_t>( m_panel ).value_or( 0 );
		if ( !vtable || !memory::safe_read<std::uintptr_t>( vtable ).value_or( 0 ) )
		{
			m_panel = 0;
			return;
		}

		for ( auto i = 0; i < 16; ++i )
		{
			m_world_to_clip[ i ] = memory::safe_read<float>( m_panel + 0x2F8 + static_cast< std::size_t >( i ) * 4 ).value_or( 0.0f );
		}
	}

	void model_preview::update( )
	{
		if ( !m_initialized )
		{
			return;
		}

		static int update_count = 0;
		++update_count;

		if ( update_count == 1 )
		{
			logging::console::print( xs( "[model_preview] update() first call" ) );

			// Print the roots unconditionally, once. Whether a pattern produced an
			// address at all is the first fork in the diagnosis, and inferring it
			// from the absence of an error line proved unreliable.
			const auto client = memory::get_module_base( "client.dll" );
			const auto rva = [ client ]( std::uintptr_t a ) -> unsigned long long
				{
					return ( a && client ) ? static_cast< unsigned long long >( a - client ) : 0ull;
				};

			char line[ 224 ]{};
			_snprintf_s( line, sizeof( line ), _TRUNCATE,
				"[model_preview] globals: main_menu client.dll+0x%llX, hud client.dll+0x%llX, hud_alt client.dll+0x%llX, in_game %d",
				rva( addresses::globals::main_menu_panel ),
				rva( addresses::globals::hud ),
				rva( addresses::globals::csgo_hud_panel ),
				systems::g_local.get( ).pawn != 0 ? 1 : 0 );
			diag::step( line );
		}

		// Leaving the panel parented to the menu tree after a match starts (or the
		// other way round) leaves it under something that is no longer drawn, so
		// follow the move: drop what we built and inject again under the new root.
		if ( m_script_injected )
		{
			const auto wanted = this->find_script_panel( );

			// Debounced, because during a level load this disagrees with itself
			// from one frame to the next. Acting on the first disagreement fired
			// three milliseconds after a fresh injection and threw the panel away
			// again; the root has to hold its new value for a while before it
			// counts as a move rather than as load-time noise.
			if ( wanted && wanted != m_script_panel )
			{
				++m_root_change_ticks;
			}
			else
			{
				m_root_change_ticks = 0;
			}

			if ( m_root_change_ticks > 30 )
			{
				diag::step( "[model_preview] root changed, re-injecting preview panel" );

				m_root_change_ticks = 0;
				hooks::forget_preview_textures( );
				this->clear_texture( );
				this->grant_capture_budget( );
				m_panel = 0;
				m_script_panel = 0;
				m_script_injected = false;
				m_menu_was_open = false;
				m_init_throttle = 0;
			}
		}

		if ( !m_script_injected )
		{
			++m_init_throttle;

			// Let the tree settle before building anything.
			//
			// A level change tears the panel down and the Panorama roots keep
			// moving for several frames afterwards. Creating on the very next
			// update meant four panels went up in a second and a half, and each
			// one destroyed the texture the previous was about to produce -- which
			// is why the preview stayed empty through a lobby even though every
			// individual step was working.
			if ( m_init_throttle < 30 || ( m_init_throttle % 30 ) != 0 )
			{
				return;
			}

			if ( m_init_throttle == 30 )
			{
				logging::console::print( xs( "[model_preview] attempting panel creation" ) );
			}

			if ( !create_panel( ) )
			{
				return;
			}
		}

		find_preview_panel( );

		// The game pays to render this panel every frame it is marked ready, so
		// only ask for it while the preview is actually on screen: the menu open,
		// and the tab that contains the preview card selected. Gating on "menu is
		// open" alone kept a hidden 512x512 model rendering behind five other
		// tabs.
		constexpr auto k_visuals_tab{ 2 };
		const auto preview_visible = rendering::g_menu.is_open( )
			&& rendering::g_menu.get_tab( ) == k_visuals_tab;

		if ( preview_visible != m_menu_was_open )
		{
			m_menu_was_open = preview_visible;

			// Coming back from a pause is a fresh start for the texture, not a
			// resumption of the old one. SetReadyForDisplay(false) tears the
			// composition layer down and the game builds another on resume -- but
			// m_current_texture still held the dead one, capture_pending() was
			// therefore false, and the hook never even looked for the replacement.
			// The card went on drawing a released SRV, which is exactly the
			// "texture captured, nothing visible" the log showed: the same pointer
			// held for sixteen minutes because nothing was ever allowed to replace
			// it.
			//
			// The reference implementation sidesteps this by never pausing -- its
			// script re-asserts readiness every quarter second. Pausing is worth
			// keeping here (it was measured in frames), so pay for it by dropping
			// the texture on the way back up.
			if ( preview_visible )
			{
				hooks::release_preview_texture( );
				this->clear_texture( );
				this->grant_capture_budget( );
			}

			set_ready_for_display( preview_visible, true );
		}

		if ( ( ++m_state_assert_throttle % 60 ) == 0 )
		{
			// Kick rather than merely re-assert while the preview should be on
			// screen and still has no texture. One missed transition used to leave
			// the panel dormant for the rest of the session; a kick once a second
			// costs nothing once the texture arrives, since this stops asking the
			// moment it does.
			const auto stalled = preview_visible && !m_current_texture;
			set_ready_for_display( preview_visible, stalled );
		}

		// A kick is not always enough. Across a session the composition texture
		// only ever appeared shortly after a fresh injection under a root that had
		// settled -- never after a kick alone. Early on the root is still moving:
		// the panel goes up under whichever tree exists at inject time, the level
		// finishes loading, and the texture arrives only once we land on the tree
		// that is really being drawn. That is the twenty-three second gap in the
		// log, and the "nothing until the map is nearly loaded" it produces.
		//
		// So if the preview should be visible and has no texture for a couple of
		// seconds, do the thing that demonstrably works and build it again. Self
		// limiting: the counter only advances while there is nothing to show.
		if ( m_script_injected && !m_current_texture && m_capture_budget > 0 )
		{
			--m_capture_budget;
		}

		// Not while a root move is already being counted out: that path is about
		// to rebuild anyway, and two rebuild triggers racing each other is how the
		// panel ended up being recreated four times in a second and a half.
		if ( preview_visible && !m_current_texture && m_root_change_ticks == 0 )
		{
			++m_stall_ticks;
		}
		else
		{
			m_stall_ticks = 0;
			m_stall_rebuilds = 0;
		}

		// Back off as attempts accumulate. Five rebuilds in seven seconds is not
		// patience, it is thrashing -- and each one destroys and recreates a
		// texture, which is exactly the churn the address-keyed blacklist above
		// handles worst.
		const auto stall_limit = 600 + std::min( m_stall_rebuilds, 6 ) * 600;

		if ( m_stall_ticks > stall_limit )
		{
			diag::step( "[model_preview] no texture while visible, rebuilding preview panel" );

			// The texture this panel had is about to be replaced by a new object,
			// and the capture path must not still be holding the old handle or a
			// blacklist entry that the new one might collide with.
			hooks::forget_preview_textures( );
			this->grant_capture_budget( );

			++m_stall_rebuilds;
			m_stall_ticks = 0;
			m_panel = 0;
			m_script_panel = 0;
			m_script_injected = false;
			m_menu_was_open = false;
			m_init_throttle = 0;
		}
	}

	void model_preview::set_ready_for_display( bool ready, bool kick )
	{
		if ( !m_ui_engine || !m_script_panel )
		{
			return;
		}

		const auto* script = ready
			? ( kick ? k_kick_script : k_ready_script )
			: k_pause_script;

		memory::call_vfunc<void>(
			m_ui_engine,
			77,
			reinterpret_cast<void*>( m_script_panel ),
			static_cast<const char*>( script ),
			"",
			static_cast<std::uint64_t>( 1 ) );
	}

	void model_preview::clear_texture( )
	{
		if ( m_current_texture )
		{
			logging::console::print( xs( "[model_preview] clearing texture (was 0x{:X})" ), reinterpret_cast<std::uintptr_t>( m_current_texture ) );
		}

		if ( const auto tex = static_cast< ID3D11ShaderResourceView* >( m_current_texture ) )
		{
			tex->Release( );
		}

		m_current_texture = nullptr;
	}

	void model_preview::reset( )
	{
		logging::console::print( xs( "[model_preview] reset() called" ) );
		clear_texture( );

		m_preview_pawn = 0;
		m_panel = 0;
		m_script_injected = false;
		m_ui_engine = 0;
		m_script_panel = 0;
		m_panel_walk_throttle = 0;
		m_state_assert_throttle = 0;

		// false, not true. The flag is only a change detector: update() sends the
		// kick script when visibility differs from it, and the kick is what makes a
		// freshly injected panel start rendering. Seeding it true claimed the
		// preview was already up, so with the menu open on the Visuals tab no
		// transition ever occurred, the new panel was never kicked, and there was no
		// composition layer to capture. That is the whole of "nothing on first
		// launch, but the model appears once you exit to the menu" -- the
		// re-injection path seeds it false and kicks.
		m_menu_was_open = false;
		m_init_throttle = 0;
		std::fill( std::begin( m_world_to_clip ), std::end( m_world_to_clip ), 0.0f );
	}

	void model_preview::shutdown( )
	{
		reset( );
	}

	bool model_preview::on_generate_primitives(
		std::uintptr_t owner_entity,
		std::uint32_t owner_hash,
		std::uintptr_t scene_object,
		std::uintptr_t primitive_buffer,
		void( __fastcall* original_fn )( std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t ),
		std::uintptr_t a1,
		std::uintptr_t scene_view )
	{
		if ( !m_initialized || !owner_entity )
		{
			return false;
		}

		static const std::uint32_t preview_hash = fnv1a::runtime_hash( "C_CSGO_PreviewPlayer" );
		if ( owner_hash != preview_hash )
		{
			return false;
		}

		m_preview_pawn = owner_entity;

		// apply chams to the preview player using the enemy/team/local config
		// selected in the active ESP player subtab
		const auto& chams_cfg = settings::g_esp.m_player.m_chams;
		const auto subtab = rendering::g_menu.get_subtab( );

		const settings::esp::chams_config* target{ nullptr };
		if ( subtab == 0 )
		{
			target = &chams_cfg.enemy;
		}
		else if ( subtab == 1 )
		{
			target = &chams_cfg.team;
		}
		else if ( subtab == 2 )
		{
			target = &chams_cfg.local;
		}

		if ( !target || !target->enabled.value )
		{
			return false;
		}

		if ( target->secondary.enabled.value )
		{
			features::esp::player::g_chams.apply_layer( primitive_buffer, original_fn, a1, scene_object, scene_view, target->secondary.color, target->secondary.material );
		}

		if ( target->primary.enabled.value )
		{
			features::esp::player::g_chams.apply_layer( primitive_buffer, original_fn, a1, scene_object, scene_view, target->primary.color, target->primary.material );
		}

		if ( !target->secondary.enabled.value && !target->primary.enabled.value )
		{
			original_fn( a1, scene_object, scene_view, primitive_buffer );
		}

		return true;
	}

	void model_preview::set_preview_texture( void* srv )
	{
		static auto s_logged{ false };
		if ( srv && !s_logged )
		{
			s_logged = true;
			logging::console::print( xs( "[model_preview] composition texture captured" ) );
		}

		if ( !srv && m_current_texture )
		{
			logging::console::print( xs( "[model_preview] WARNING: texture set to null (was 0x{:X})" ), reinterpret_cast<std::uintptr_t>( m_current_texture ) );
		}

		m_current_texture = srv;
	}

} // namespace systems
