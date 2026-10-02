#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <Windows.h>

#include <utilities/diag.hpp>

// Coarse section timing.
//
// The target is a twenty to forty percent frame-rate gain, and there is no way
// to aim at that without knowing where the time goes. Guessing has already cost
// this project real sessions: four separate "optimisations" were reverted
// earlier because they addressed code that was never hot.
//
// So: two QueryPerformanceCounter calls per section, accumulated into fixed
// buckets, reported once every few seconds. QPC is about twenty nanoseconds, so
// a section has to be genuinely tiny before the probe distorts it -- these all
// sit at feature granularity, far above that.
namespace perf
{
	enum class id : std::size_t
	{
		present,          // whole present hook, the frame budget we are spending
		esp_players,      // player ESP overlay
		esp_other,        // item / projectile overlays and the rest of the draw list
		chams,            // sort_primitives path
		menu,             // menu rendering
		create_move,      // whole create_move, once per tick rather than per frame
		prediction,       // prediction run inside create_move
		rage,             // rage bot
		legit,            // legit bot
		movement,         // strafers and movement helpers
		resource_view,    // the texture identification hook
		changer,          // weapon changer frame work, all four features together
		changer_rebuild,  // material / skin rebuild inside the changer, the heavy part
		hud_clear,        // econ item description invalidation
		rage_scan,        // one full player scan of the rage bot (see scan_from_eye_candidates)
		rage_select,      // target selection, including the wallbang hitchance traces
		rage_trace,       // the per-point trace loop inside scan_player

		// Per-call breakdown of the esp_other block (context.cpp on_present).
		// The single esp_other bucket was the sum of fourteen unconditional
		// calls, which is useless for finding the hot one.
		r_impacts_early,  // impacts::on_render_early
		r_antiaim,        // combat::g_misc.antiaim().on_render
		r_edgebug,        // movement::g_edgebug.on_render
		r_item_esp,       // esp::item::g_overlay.on_render
		r_projectile_esp, // esp::projectile::g_overlay.on_render
		r_trajectory,     // misc::g_projectile_trajectory.on_render
		r_rage_overlay,   // combat::g_rage.on_render
		r_legit_overlay,  // combat::g_legit.on_render
		r_impacts,        // misc::g_impacts.on_render
		r_hud,            // misc::g_hud.on_render
		r_map_scan,       // misc::g_map_scan.on_render
		r_optimization,   // misc::g_optimization.on_render
		r_other_esp,      // esp::other::g_overlay.on_render
		r_widgets,        // g_widgets.draw
		count
	};

	inline constexpr const char* k_names[ static_cast< std::size_t >( id::count ) ]
	{
		"present", "esp_players", "esp_other", "chams", "menu",
		"create_move", "prediction", "rage", "legit", "movement", "resource_view",
		"changer", "changer_rebuild", "hud_clear", "rage_scan", "rage_select", "rage_trace",
		"r_impacts_early", "r_antiaim", "r_edgebug", "r_item_esp", "r_projectile_esp",
		"r_trajectory", "r_rage_overlay", "r_legit_overlay", "r_impacts", "r_hud",
		"r_map_scan", "r_optimization", "r_other_esp", "r_widgets"
	};

	namespace detail
	{
		inline std::array<std::atomic<std::int64_t>, static_cast< std::size_t >( id::count )> g_ticks{};
		inline std::array<std::atomic<std::uint32_t>, static_cast< std::size_t >( id::count )> g_calls{};
		inline std::atomic<std::uint32_t> g_frames{};
		inline std::atomic<std::int64_t> g_window_start{};

		[[nodiscard]] inline std::int64_t now( ) noexcept
		{
			LARGE_INTEGER v{};
			QueryPerformanceCounter( &v );
			return v.QuadPart;
		}

		[[nodiscard]] inline double frequency( ) noexcept
		{
			static const double f = [ ]
				{
					LARGE_INTEGER v{};
					QueryPerformanceFrequency( &v );
					return static_cast< double >( v.QuadPart );
				}( );
			return f;
		}
	}

	// RAII probe. Deliberately trivial: anything clever here would show up in the
	// numbers it is supposed to be measuring.
	struct scope
	{
		id which;
		std::int64_t start;

		explicit scope( id which ) noexcept
			: which{ which }, start{ detail::now( ) }
		{
		}

		~scope( ) noexcept
		{
			const auto index = static_cast< std::size_t >( this->which );
			detail::g_ticks[ index ].fetch_add( detail::now( ) - this->start, std::memory_order_relaxed );
			detail::g_calls[ index ].fetch_add( 1, std::memory_order_relaxed );
		}

		scope( const scope& ) = delete;
		scope& operator=( const scope& ) = delete;
	};

	// Called once per frame from the present hook. Reports and resets on its own
	// schedule so nothing else has to know about the window.
	inline void on_frame( )
	{
		const auto frames = detail::g_frames.fetch_add( 1, std::memory_order_relaxed ) + 1;

		auto start = detail::g_window_start.load( std::memory_order_relaxed );
		if ( start == 0 )
		{
			detail::g_window_start.store( detail::now( ), std::memory_order_relaxed );
			return;
		}

		const auto elapsed = static_cast< double >( detail::now( ) - start ) / detail::frequency( );
		if ( elapsed < 5.0 )
		{
			return;
		}

		{
			char header[ 128 ]{};
			_snprintf_s( header, sizeof( header ), _TRUNCATE,
				"[perf] %.1fs window, %u frames, %.0f fps",
				elapsed, frames, static_cast< double >( frames ) / elapsed );
			diag::step( header );
		}

		for ( std::size_t i = 0; i < static_cast< std::size_t >( id::count ); ++i )
		{
			const auto ticks = detail::g_ticks[ i ].exchange( 0, std::memory_order_relaxed );
			const auto calls = detail::g_calls[ i ].exchange( 0, std::memory_order_relaxed );

			if ( !calls )
			{
				continue;
			}

			const auto seconds = static_cast< double >( ticks ) / detail::frequency( );

			// Share of wall time is the number that matters: a section can look
			// small per call and still own the frame.
			char line[ 176 ]{};
			_snprintf_s( line, sizeof( line ), _TRUNCATE,
				"[perf]   %-14s %6.1f%% of wall | %8.3f ms/call | %6u calls | %7.2f ms/frame",
				k_names[ i ],
				100.0 * seconds / elapsed,
				1000.0 * seconds / static_cast< double >( calls ),
				calls,
				1000.0 * seconds / static_cast< double >( frames ) );
			diag::step( line );
		}

		detail::g_frames.store( 0, std::memory_order_relaxed );
		detail::g_window_start.store( detail::now( ), std::memory_order_relaxed );
	}
}
