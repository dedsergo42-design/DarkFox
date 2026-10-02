#pragma once

#include "theme.hpp"

// ---------------------------------------------------------------------------
// DarkFox UI controls, drawn against rendering::theme.
//
// These replace the widgets:: set rather than wrapping it: the old ones read
// tokens:: directly and each carries its own literals for size, rounding and
// animation rate, which is why nothing lines up. Everything here takes its
// numbers from theme::metric and its rates from theme::motion.
//
// Convention throughout: a control animates on the address of the value it
// edits. That address is unique and lives as long as the control does, so no
// call site has to invent an id, and two controls editing different settings
// can never share animation state.
// ---------------------------------------------------------------------------

namespace rendering::ui {

	// Interaction result shared by every control, so call sites can react to a
	// press without re-testing the mouse themselves.
	struct interaction
	{
		xui::rect bounds{};
		bool hovered{};
		bool pressed{};   // held down this frame
		bool clicked{};   // released over the control this frame
	};

	// Hit-test plus press bookkeeping for a rect that layout already reserved.
	[[nodiscard]] interaction probe( const xui::rect& bounds );

	// -----------------------------------------------------------------------
	// Containers
	// -----------------------------------------------------------------------

	// Flat panel: fill, hairline border, no shadow and no blur. The depth comes
	// from the surface step, which is the whole reason the palette has four of
	// them.
	void panel( const xui::rect& bounds, xdraw::color fill, float radius = theme::metric::radius_panel );

	// Titled card. Returns false when collapsed, in which case skip the body.
	bool card_begin( const char* title, float width );
	void card_end( );

	// Faint rule between groups inside a card.
	void divider( );

	// -----------------------------------------------------------------------
	// Статус
	// -----------------------------------------------------------------------

	// Предупреждающая строка в стиле карточки: заголовок цветом accent, тело
	// text_dim, слева -- вертикальная полоса переданного цвета. Высоту считает
	// сам по layout, поэтому вызывать до card_end().
	void status_row( const char* title, std::string_view body, xdraw::color accent );

	// -----------------------------------------------------------------------
	// Controls
	// -----------------------------------------------------------------------

	bool checkbox( const char* label, bool& value );
	bool checkbox( const char* label, xui::setting& value );

	bool slider_float( const char* label, float& value, float min, float max, const char* format = "%.2f" );
	bool slider_int( const char* label, int& value, int min, int max, const char* format = "%d" );

	bool combo( const char* label, int& value, const char* const* items, int count );

	// Three-dot affordance that opens a settings popup for the row above it.
	// Returns true while the popup is open.
	bool dots( const char* id );

	// -----------------------------------------------------------------------
	// Shell pieces
	// -----------------------------------------------------------------------

	// Top-level tab. The active one carries the violet-to-pink gradient rule;
	// the indicator slides between tabs rather than jumping.
	bool tab( const char* label, bool active, const xui::rect& bounds );

	// Sidebar entry.
	bool nav_item( const char* label, bool active, const xui::rect& bounds );

	// Section heading inside the sidebar (WEAPONS, PLAYER, ...).
	void nav_heading( const char* label, const xui::rect& bounds );

	// -----------------------------------------------------------------------
	// Измерение контента
	// -----------------------------------------------------------------------

	// Сообщить, что контент доходит до этой координаты по Y (в координатах
	// окна). Нужно для прокрутки тела: сабтабы раскладываются абсолютно, и
	// никто, кроме них самих, не знает, где кончается последняя карточка.
	// Вызывать после отрисовки последней карточки, передавая её нижний край.
	void report_content_bottom( float y );

	// Нижний край, накопленный с последнего сброса. Меню сбрасывает значение
	// перед отрисовкой сабтаба и читает после.
	[[nodiscard]] float content_bottom( );
	void reset_content_bottom( );

} // namespace rendering::ui
