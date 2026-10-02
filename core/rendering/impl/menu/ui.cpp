#include <pch/pch.hpp>
#include <core/settings.hpp>

#include "../../rendering.hpp"
#include "ui.hpp"

namespace rendering::ui {

namespace {

	using namespace theme;

	// A control is "active" for dragging while the mouse went down on it and has
	// not been released, even if the cursor has since wandered off. Sliders need
	// this; without it a fast drag stops tracking the moment you leave the row.
	std::uintptr_t g_active_id{};

	[[nodiscard]] bool blocked( )
	{
		return xui::ctx( ).overlay_blocking( );
	}

	// Vertically centred baseline for a single line of text in a row.
	[[nodiscard]] float text_baseline( const xui::rect& row, std::string_view text )
	{
		return row.y + ( row.h - xdraw::measure_text( text ).second ) * 0.5f;
	}

	// Подпись в меню пишется как "hit logs##hl": часть после ## -- это id для
	// xui, а не текст. Свои подписи xui срезает через parse_label, а этот слой
	// рисовал строку целиком -- в меню висели хвосты вида "volume##hs",
	// "glow##hm", "duration##hl". Затронуто больше 60 подписей только у
	// слайдеров. Срезаем на входе, чтобы measure и draw считали одно и то же:
	// иначе значение справа встанет не туда.
	[[nodiscard]] std::string_view display_label( std::string_view label )
	{
		return xui::parse_label( label ).first;
	}

	// Ширина контента активной карточки; 0 -- карточка не открыта.
	//
	// xui::layout::avail() считает ширину от курсора до края ОКНА, а не
	// карточки. В двухколоночной раскладке из-за этого строка левой карточки
	// тянулась в правую колонку: значение слайдера вставало у правого края
	// окна, дорожка пересекала обе колонки, комбо уезжало за свою карточку.
	// Карточка объявляет ширину, контролы её уважают.
	//
	// Одного значения достаточно: карточки тут не вложены, а card_end его
	// сбрасывает. Пропущенный card_end тоже не отравит следующие строки --
	// следующий card_begin перезапишет.
	float g_card_width{};

	// Ширина строки внутри карточки: не больше её ширины, но и не больше того,
	// что вообще осталось до края окна.
	[[nodiscard]] float row_width( float avail_w )
	{
		if ( g_card_width <= 0.0f )
		{
			return avail_w;
		}

		return std::min( avail_w, g_card_width );
	}

	// The accent gradient, left to right, at a given alpha.
	void accent_bar( xdraw::draw_list& dl, float x, float y, float w, float h, std::uint8_t alpha, float radius )
	{
		const auto a = pal( ).accent_violet.alpha( alpha );
		const auto b = pal( ).accent_pink.alpha( alpha );
		dl.rect_filled_gradient( x, y, w, h, a, b, b, a, xdraw::corner_radius{ radius } );
	}

} // namespace

interaction probe( const xui::rect& bounds )
{
	interaction out{};
	out.bounds = bounds;

	const auto& input = xui::ctx( ).input;
	out.hovered = input.in_rect( bounds ) && !blocked( );

	const auto id = reinterpret_cast< std::uintptr_t >( &bounds );
	( void )id;

	out.pressed = out.hovered && input.mouse_down;
	out.clicked = out.hovered && input.mouse_clicked;

	return out;
}

// ---------------------------------------------------------------------------
// Containers
// ---------------------------------------------------------------------------

void panel( const xui::rect& bounds, xdraw::color fill, float radius )
{
	auto& dl = xui::draw::current( );

	dl.rect_filled( bounds.x, bounds.y, bounds.w, bounds.h, fill, xdraw::corner_radius{ radius } );
	dl.rect( bounds.x, bounds.y, bounds.w, bounds.h, pal( ).border, xdraw::corner_radius{ radius }, metric::hairline );
}

bool card_begin( const char* title, float width )
{
	auto& dl = xui::draw::current( );

	// Шапка карточки: полоса surface_raised со скруглением только сверху,
	// поверх неё -- градиентная линия акцента, под ней -- hairline. Голый
	// текст заголовка на поверхности карточки не читался как шапка: карточки
	// сливались друг с другом, а заголовок выглядел обычной строкой.
	const auto header_h = metric::card_header;
	const auto row = xui::layout::item( width, header_h );

	// Объявляем ширину контента: строки внутри будут ограничены ею, а не краем
	// окна (см. g_card_width).
	g_card_width = width;

	dl.rect_filled(
		row.x, row.y, row.w, header_h, pal( ).surface_raised,
		xdraw::corner_radius::top( metric::radius_card ) );

	// Линия акцента прижата к нижнему краю шапки -- на два пикселя выше
	// hairline, чтобы не спорить с ним за одну строку пикселей.
	accent_bar(
		dl, row.x + metric::radius_card, row.bottom( ) - 3.0f,
		std::max( 0.0f, row.w - metric::radius_card * 2.0f ), 2.0f, 190, 1.0f );

	dl.rect_filled( row.x, row.bottom( ) - metric::hairline, row.w, metric::hairline, pal( ).border );

	// Заголовок центрируется по шапке, а не по строке раскладки: строка
	// отдана под полосу, и baseline от неё уводил текст вниз.
	const auto [title_w, title_h] = xdraw::measure_text( title );
	( void )title_w;
	dl.text(
		row.x + metric::card_padding,
		row.y + ( header_h - title_h ) * 0.5f,
		title, pal( ).text );

	xui::layout::spacing( metric::space_sm );
	return true;
}

void card_end( )
{
	// Карточка закрыта: снимаем ограничение ширины, иначе оно утечёт в строки
	// за пределами карточки.
	g_card_width = 0.0f;

	// Карточка закончилась -- её нижний край и есть текущий курсор раскладки.
	// Сообщаем его наружу: только так шелл узнаёт, сколько контента вылезло
	// за пределы тела и на сколько вообще можно прокручивать.
	{
		const auto [avail_w, cursor_y] = xui::layout::avail( );
		( void )avail_w;

		const auto win = xui::layout::current_window_const( );
		if ( win )
		{
			report_content_bottom( win->bounds.y + cursor_y );
		}
	}

	xui::layout::spacing( metric::space_md );
}

void divider( )
{
	auto& dl = xui::draw::current( );

	const auto [avail_w, avail_h] = xui::layout::avail( );
	( void )avail_h;

	const auto row = xui::layout::item( row_width( avail_w ), metric::space_sm );
	dl.rect_filled( row.x, row.center_y( ), row.w, metric::hairline, pal( ).border );
}

// ---------------------------------------------------------------------------
// Статус
// ---------------------------------------------------------------------------

void status_row( const char* title, std::string_view body, xdraw::color accent )
{
	auto& dl = xui::draw::current( );

	const auto [avail_w, avail_h] = xui::layout::avail( );
	( void )avail_h;

	const auto [text_w, text_h] = xdraw::measure_text( body, g_fonts.inter_medium[ fonts::size::petite ] );
	( void )text_w;

	// Высота строки складывается из заголовка и тела. Обе линии рисуются
	// вручную, поэтому ни одна из них не должна уехать за нижний край карточки
	// -- card_end() измеряет контент по курсору раскладки, а не по пикселям.
	const auto line_h = text_h > 0.0f ? text_h : metric::row_height * 0.5f;
	const auto row_h = theme::metric::row_height + line_h + metric::space_xs;
	const auto row = xui::layout::item( row_width( avail_w ), row_h );

	const auto text_x = row.x + metric::space_sm + metric::space_xs;

	dl.text( text_x, row.y + 2.0f, title, pal( ).text );
	dl.text( text_x, row.y + 2.0f + line_h, body, pal( ).text_dim, g_fonts.inter_medium[ fonts::size::petite ] );

	// Полоса слева: единственный носитель цвета в этой строке. Рисуется после
	// текста, потому что перекрывает его начало на пару пикселей.
	dl.rect_filled( row.x, row.y + 2.0f, 2.0f, row.h - 4.0f, accent, xdraw::corner_radius{ 1.0f } );
}

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------

bool checkbox( const char* label, bool& value )
{
	auto& dl = xui::draw::current( );

	const auto [avail_w, avail_h] = xui::layout::avail( );
	( void )avail_h;

	const auto row = xui::layout::item( row_width( avail_w ), metric::row_height );
	const auto hit = probe( row );

	// Animate on the value's address: unique, stable, and never shared with a
	// different setting even when two rows carry the same label.
	const auto hover_t = factor( &value, hit.hovered, motion::fast, 0 );
	const auto on_t = factor( &value, value, motion::normal, 1 );

	const auto box_size = metric::checkbox_size;
	const auto box_x = row.x;
	const auto box_y = row.y + ( row.h - box_size ) * 0.5f;

	// Unchecked: sunken well that lifts slightly on hover. Checked: accent fill.
	const auto rest = mix( pal( ).surface_sunken, pal( ).surface_hover, hover_t * 0.6f );
	const auto fill = mix( rest, pal( ).accent, on_t );

	dl.rect_filled( box_x, box_y, box_size, box_size, fill, xdraw::corner_radius{ metric::radius_control } );
	dl.rect( box_x, box_y, box_size, box_size,
		mix( pal( ).border, pal( ).accent, on_t ),
		xdraw::corner_radius{ metric::radius_control }, metric::hairline );

	// Tick, scaled in with the same factor so it grows out of the box rather
	// than appearing all at once.
	if ( on_t > 0.01f )
	{
		const auto cx = box_x + box_size * 0.5f;
		const auto cy = box_y + box_size * 0.5f;
		const auto s = ( box_size * 0.28f ) * on_t;

		const auto ink = pal( ).bg.alpha( static_cast< std::uint8_t >( 255.0f * on_t ) );
		dl.line( cx - s, cy, cx - s * 0.15f, cy + s * 0.85f, ink, 1.8f );
		dl.line( cx - s * 0.15f, cy + s * 0.85f, cx + s, cy - s * 0.7f, ink, 1.8f );
	}

	const auto caption = display_label( label );
	dl.text( box_x + box_size + metric::space_sm, text_baseline( row, caption ),
		caption, mix( pal( ).text_dim, pal( ).text, hover_t ) );

	if ( hit.clicked )
	{
		value = !value;
		return true;
	}

	return false;
}

bool checkbox( const char* label, xui::setting& value )
{
	return checkbox( label, value.value );
}

namespace {

	// Shared body for both slider flavours: everything except the value text and
	// the type of the number is identical, and duplicating it is how the two
	// drift apart.
	bool slider_body( const char* label, void* owner, float& normalized, const char* value_text )
	{
		auto& dl = xui::draw::current( );

		const auto [avail_w, avail_h] = xui::layout::avail( );
		( void )avail_h;

		const auto row = xui::layout::item( row_width( avail_w ), metric::row_height );
		const auto hit = probe( row );

		const auto id = anim_key( owner, 7 );
		const auto& input = xui::ctx( ).input;

		if ( hit.hovered && input.mouse_clicked )
		{
			g_active_id = id;
		}
		if ( !input.mouse_down )
		{
			if ( g_active_id == id )
			{
				g_active_id = 0;
			}
		}

		const auto dragging = ( g_active_id == id );
		const auto hover_t = factor( owner, hit.hovered || dragging, motion::fast, 0 );

		// Label left, value right, track underneath spanning the full row.
		dl.text( row.x, row.y, display_label( label ), mix( pal( ).text_dim, pal( ).text, hover_t ) );

		const auto value_w = xdraw::measure_text( value_text ).first;
		dl.text( row.right( ) - value_w, row.y, value_text, pal( ).text );

		const auto track_y = row.bottom( ) - metric::slider_track - 2.0f;
		dl.rect_filled( row.x, track_y, row.w, metric::slider_track, pal( ).surface_sunken,
			xdraw::corner_radius{ metric::slider_track * 0.5f } );

		auto changed = false;

		if ( dragging )
		{
			const auto t = std::clamp( ( input.mouse_x - row.x ) / std::max( row.w, 1.0f ), 0.0f, 1.0f );
			if ( std::fabsf( t - normalized ) > 1.0e-4f )
			{
				normalized = t;
				changed = true;
			}
		}

		// The drawn fill chases the value instead of snapping, so a click on the
		// far end of the track reads as a sweep rather than a jump.
		const auto shown = approach( owner, normalized, motion::normal, 8 );
		const auto fill_w = std::max( row.w * std::clamp( shown, 0.0f, 1.0f ), 1.0f );

		accent_bar( dl, row.x, track_y, fill_w, metric::slider_track,
			static_cast< std::uint8_t >( 200 + 55 * hover_t ), metric::slider_track * 0.5f );

		const auto knob_r = ( metric::slider_knob * 0.5f ) * ( 0.85f + 0.15f * hover_t );
		dl.circle_filled( row.x + fill_w, track_y + metric::slider_track * 0.5f, knob_r, pal( ).text );

		return changed;
	}

} // namespace

bool slider_float( const char* label, float& value, float min, float max, const char* format )
{
	const auto span = std::max( max - min, 1.0e-6f );
	auto normalized = std::clamp( ( value - min ) / span, 0.0f, 1.0f );

	char text[ 64 ]{};
	_snprintf_s( text, sizeof( text ), _TRUNCATE, format, value );

	if ( slider_body( label, &value, normalized, text ) )
	{
		value = min + normalized * span;
		return true;
	}

	return false;
}

bool slider_int( const char* label, int& value, int min, int max, const char* format )
{
	const auto span = static_cast< float >( std::max( max - min, 1 ) );
	auto normalized = std::clamp( ( static_cast< float >( value ) - static_cast< float >( min ) ) / span, 0.0f, 1.0f );

	char text[ 64 ]{};
	_snprintf_s( text, sizeof( text ), _TRUNCATE, format, value );

	if ( slider_body( label, &value, normalized, text ) )
	{
		value = min + static_cast< int >( std::lroundf( normalized * span ) );
		return true;
	}

	return false;
}

bool combo( const char* label, int& value, const char* const* items, int count )
{
	auto& dl = xui::draw::current( );

	const auto [avail_w, avail_h] = xui::layout::avail( );
	( void )avail_h;

	const auto row = xui::layout::item( row_width( avail_w ), metric::row_height );
	const auto hit = probe( row );
	const auto hover_t = factor( &value, hit.hovered, motion::fast, 0 );

	const auto caption = display_label( label );
	dl.text( row.x, text_baseline( row, caption ), caption, mix( pal( ).text_dim, pal( ).text, hover_t ) );

	const auto box_w = metric::control_width;
	const auto box_x = row.right( ) - box_w;

	dl.rect_filled( box_x, row.y, box_w, row.h,
		mix( pal( ).surface_raised, pal( ).surface_hover, hover_t ),
		xdraw::corner_radius{ metric::radius_control } );
	dl.rect( box_x, row.y, box_w, row.h, pal( ).border,
		xdraw::corner_radius{ metric::radius_control }, metric::hairline );

	const auto index = std::clamp( value, 0, std::max( count - 1, 0 ) );
	if ( items && count > 0 )
	{
		dl.text( box_x + metric::space_sm, text_baseline( row, items[ index ] ), items[ index ], pal( ).text );
	}

	// Caret.
	const auto cx = row.right( ) - metric::space_md;
	const auto cy = row.center_y( );
	dl.line( cx - 4.0f, cy - 1.5f, cx, cy + 2.5f, pal( ).text_dim, 1.4f );
	dl.line( cx, cy + 2.5f, cx + 4.0f, cy - 1.5f, pal( ).text_dim, 1.4f );

	// Cycling on click keeps this self-contained; a drop-down list belongs with
	// the popup work and would need its own overlay ordering.
	if ( hit.clicked && count > 0 )
	{
		value = ( index + 1 ) % count;
		return true;
	}

	return false;
}

bool dots( const char* id )
{
	auto& dl = xui::draw::current( );

	const auto size = metric::row_height;
	const auto row = xui::layout::item( size, size );
	const auto hit = probe( row );
	const auto hover_t = factor( id, hit.hovered, motion::fast, 0 );

	const auto cx = row.center_x( );
	const auto cy = row.center_y( );
	const auto ink = mix( pal( ).text_muted, pal( ).text, hover_t );

	for ( auto i = -1; i <= 1; ++i )
	{
		dl.circle_filled( cx + static_cast< float >( i ) * 4.0f, cy, 1.5f, ink );
	}

	return xui::begin_popup( id, 220.0f );
}

// ---------------------------------------------------------------------------
// Shell pieces
// ---------------------------------------------------------------------------

bool tab( const char* label, bool active, const xui::rect& bounds )
{
	auto& dl = xui::draw::current( );

	const auto hit = probe( bounds );
	const auto hover_t = factor( label, hit.hovered, motion::fast, 0 );
	const auto active_t = factor( label, active, motion::normal, 1 );

	const auto ink = mix( mix( pal( ).text_muted, pal( ).text_dim, hover_t ), pal( ).text, active_t );

	const auto caption = display_label( label );
	const auto text_size = xdraw::measure_text( caption );
	dl.text( bounds.center_x( ) - text_size.first * 0.5f,
		bounds.center_y( ) - text_size.second * 0.5f, caption, ink );

	// The underline grows from the centre as the tab becomes active, which is
	// what makes a tab switch read as movement rather than a repaint.
	if ( active_t > 0.01f )
	{
		const auto full_w = text_size.first + metric::space_md;
		const auto w = full_w * active_t;

		accent_bar( dl, bounds.center_x( ) - w * 0.5f, bounds.bottom( ) - 2.0f, w, 2.0f,
			static_cast< std::uint8_t >( 255.0f * active_t ), 1.0f );
	}

	return hit.clicked;
}

bool nav_item( const char* label, bool active, const xui::rect& bounds )
{
	auto& dl = xui::draw::current( );

	const auto hit = probe( bounds );
	const auto hover_t = factor( label, hit.hovered, motion::fast, 0 );
	const auto active_t = factor( label, active, motion::normal, 1 );

	if ( hover_t > 0.01f || active_t > 0.01f )
	{
		const auto fill = mix( pal( ).surface_raised.alpha( 0 ),
			mix( pal( ).surface_hover, pal( ).accent_soft, active_t ),
			std::max( hover_t * 0.7f, active_t ) );

		dl.rect_filled( bounds.x, bounds.y, bounds.w, bounds.h, fill,
			xdraw::corner_radius{ metric::radius_control } );
	}

	// Active marker on the leading edge, sliding in from the left.
	if ( active_t > 0.01f )
	{
		const auto h = ( bounds.h - metric::space_sm ) * active_t;
		accent_bar( dl, bounds.x, bounds.center_y( ) - h * 0.5f, 2.5f, h, 255, 1.25f );
	}

	const auto caption = display_label( label );
	dl.text( bounds.x + metric::space_md, text_baseline( bounds, caption ), caption,
		mix( mix( pal( ).text_muted, pal( ).text_dim, hover_t ), pal( ).text, active_t ) );

	return hit.clicked;
}

void nav_heading( const char* label, const xui::rect& bounds )
{
	auto& dl = xui::draw::current( );
	const auto caption = display_label( label );
	dl.text( bounds.x + metric::space_md, text_baseline( bounds, caption ), caption, pal( ).text_muted );
}

namespace
{
	// Нижний край контента за текущий кадр. Хранится в файловой статике, а не
	// в меню, потому что пишут в него сабтабы, а читает шелл -- тащить ссылку
	// через все сигнатуры дороже, чем одно значение на кадр.
	float g_content_bottom{};
}

void report_content_bottom( float y )
{
	g_content_bottom = std::max( g_content_bottom, y );
}

float content_bottom( )
{
	return g_content_bottom;
}

void reset_content_bottom( )
{
	g_content_bottom = 0.0f;
}

} // namespace rendering::ui
