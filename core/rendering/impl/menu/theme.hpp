#pragma once

// ---------------------------------------------------------------------------
// DarkFox UI style layer.
//
// One place that answers "what colour is this, how big is it, and how fast does
// it move", so the menu and the HUD widgets cannot drift apart. Everything that
// draws chrome should read from here rather than carrying its own literals.
//
// The old tokens namespace lives in external/xdraw/xdraw.hpp -- a rendering
// library is the wrong home for an application's theme, and it hard-codes the
// palette as mutable globals. That one stays for now so existing menu code keeps
// building; new drawing uses this.
//
// Deliberate constraints, from the design decision behind the rebuild:
//   - no frosted glass, no backdrop blur anywhere;
//   - no glow in the menu itself -- the menu is flat and quiet;
//   - glow exists only for the HUD keybind panel, and only faintly.
// The palette therefore carries exactly one glow entry, and it is named for the
// single place allowed to use it.
// ---------------------------------------------------------------------------

namespace rendering::theme {

	// -----------------------------------------------------------------------
	// Palette
	//
	// Named by role, not by appearance: "surface" rather than "dark grey", so a
	// re-skin changes one table instead of hunting for every literal. Layers go
	// bg < surface < surface_raised < surface_hover, each a step lighter, which
	// is what gives depth without shadows or blur.
	// -----------------------------------------------------------------------
	struct palette
	{
		// Backgrounds, darkest first.
		xdraw::color bg{ 9, 10, 13, 246 };              // window ground
		xdraw::color surface{ 16, 17, 21, 255 };        // cards, sidebar
		xdraw::color surface_raised{ 24, 26, 31, 255 }; // controls sitting on a card
		xdraw::color surface_hover{ 33, 36, 43, 255 };  // the same under the cursor
		xdraw::color surface_sunken{ 12, 13, 16, 255 }; // slider troughs, wells

		// Hairlines. border separates, border_strong outlines something active.
		xdraw::color border{ 255, 255, 255, 20 };
		xdraw::color border_strong{ 255, 255, 255, 46 };

		// Type, in descending emphasis.
		xdraw::color text{ 244, 246, 250, 255 };
		xdraw::color text_dim{ 154, 159, 172, 255 };
		xdraw::color text_muted{ 104, 109, 122, 255 };

		// Accent: violet leaning into pink. accent itself is the midpoint, used
		// wherever a single flat colour is wanted; accent_violet and accent_pink
		// are its two ends, for the places a gradient reads better than a fill --
		// an active tab underline, a slider fill, the keybind panel edge.
		// accent_soft is the same hue at low alpha so a selection highlight never
		// has to invent its own transparency.
		xdraw::color accent{ 198, 128, 240, 255 };
		xdraw::color accent_violet{ 160, 116, 246, 255 };
		xdraw::color accent_pink{ 240, 130, 208, 255 };
		xdraw::color accent_soft{ 198, 128, 240, 38 };
		xdraw::color accent_press{ 216, 158, 248, 255 };

		// State colours.
		xdraw::color danger{ 255, 108, 116, 255 };
		xdraw::color warn{ 255, 190, 96, 255 };
		xdraw::color ok{ 122, 224, 160, 255 };

		// The one sanctioned glow, for the HUD keybind panel. Alpha is low on
		// purpose: the brief asks for something barely there.
		xdraw::color keybind_glow{ 198, 128, 240, 54 };
	};

	inline palette g_palette{};

	[[nodiscard]] inline const palette& pal( ) { return g_palette; }

	// -----------------------------------------------------------------------
	// Metrics
	//
	// A small fixed scale. Picking from a scale keeps unrelated panels aligned;
	// free-hand pixel values are what makes a UI look assembled rather than
	// designed.
	// -----------------------------------------------------------------------
	namespace metric {

		// Spacing scale.
		constexpr float space_xs{ 4.0f };
		constexpr float space_sm{ 8.0f };
		constexpr float space_md{ 12.0f };
		constexpr float space_lg{ 16.0f };
		constexpr float space_xl{ 24.0f };

		// Corner radii.
		constexpr float radius_panel{ 10.0f };
		constexpr float radius_card{ 8.0f };
		constexpr float radius_control{ 5.0f };
		constexpr float radius_pill{ 999.0f };

		// Control geometry.
		constexpr float row_height{ 26.0f };
		constexpr float control_width{ 124.0f };
		constexpr float checkbox_size{ 15.0f };
		constexpr float slider_track{ 4.0f };
		constexpr float slider_knob{ 11.0f };
		constexpr float hairline{ 1.0f };

		// Shell geometry.
		constexpr float sidebar_width{ 178.0f };
		constexpr float topbar_height{ 48.0f };
		constexpr float card_padding{ 12.0f };

		// Height of the raised strip carrying a card's title. Cards draw a
		// header band in surface_raised, an accent rule on its lower edge and a
		// hairline under it -- without a dedicated number those three pieces
		// drift apart and the band stops reading as a header.
		constexpr float card_header{ 26.0f };

	} // namespace metric

	// -----------------------------------------------------------------------
	// Motion
	//
	// Speeds, not durations: xui::anim integrates per frame against a rate, so
	// the numbers below are what gets handed to it. Higher is snappier.
	// Everything the user drives directly (hover, press) is fast enough to feel
	// immediate; only whole panels are allowed to take their time.
	// -----------------------------------------------------------------------
	namespace motion {

		constexpr float instant{ 32.0f }; // press feedback
		constexpr float fast{ 20.0f };    // hover, focus
		constexpr float normal{ 14.0f };  // value tracking, selection slide
		constexpr float slow{ 9.0f };     // panel open/close, crossfade

	} // namespace motion

	// -----------------------------------------------------------------------
	// Animation helpers
	//
	// xui::anim keys its state on a uintptr_t. Callers should not be inventing
	// those by hand: the address of the thing being animated is already unique
	// and stable, so these take it directly. Pass a distinct `slot` when one
	// object animates more than one property.
	// -----------------------------------------------------------------------
	[[nodiscard]] inline std::uintptr_t anim_key( const void* owner, int slot = 0 )
	{
		// Slots are spread far apart so neighbouring objects one byte apart in a
		// struct cannot collide with each other's slots.
		return reinterpret_cast< std::uintptr_t >( owner ) + static_cast< std::uintptr_t >( slot ) * 0x1000u;
	}

	// Eased approach toward `target`. Returns the current value.
	[[nodiscard]] inline float approach( const void* owner, float target, float speed = motion::normal, int slot = 0 )
	{
		return xui::anim::smooth( anim_key( owner, slot ), target, speed );
	}

	// 0 -> 1 hover/active factor. The common case, spelled out so call sites read
	// as intent rather than as a magic target of 1.0f.
	[[nodiscard]] inline float factor( const void* owner, bool on, float speed = motion::fast, int slot = 0 )
	{
		return xui::anim::smooth( anim_key( owner, slot ), on ? 1.0f : 0.0f, speed );
	}

	// Colour blend driven by an animated factor, so a control can fade between
	// two palette roles instead of switching hard.
	[[nodiscard]] inline xdraw::color mix( xdraw::color from, xdraw::color to, float t )
	{
		return xui::lerp( from, to, std::clamp( t, 0.0f, 1.0f ) );
	}

	// Convenience: the usual "rest -> hover" surface blend for an interactive row.
	[[nodiscard]] inline xdraw::color surface_for( const void* owner, bool hovered, bool active = false )
	{
		const auto hover_t = factor( owner, hovered, motion::fast, 0 );
		const auto active_t = factor( owner, active, motion::fast, 1 );

		auto result = mix( pal( ).surface_raised, pal( ).surface_hover, hover_t );
		return mix( result, pal( ).accent_soft, active_t );
	}

} // namespace rendering::theme
