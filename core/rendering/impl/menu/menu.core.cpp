#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/settings.hpp>
#include <utilities/security/security.hpp>
#include <utilities/steam/steam.hpp>
#include <external/config.hpp>
#include <core/systems/systems.hpp>
#include <utilities/random/random.hpp>
#include <utilities/logging/logging.hpp>

#include "../../rendering.hpp"
#include "menu.widgets.hpp"
#include "theme.hpp"
#include "ui.hpp"


namespace rendering {

	namespace shell_detail {

		enum renderer_id : int
		{
			r_ragebot,
			r_legitbot,
			r_player,
			r_world,
			r_skins,
			r_misc,
			r_config,
			r_interface
		};

		struct entry
		{
			const char* label{};
			int group{};
			int renderer{};
			int subtab{};
		};

				// RAGEBOT: General + weapon groups
		constexpr entry k_ragebot_entries[ ] =
		{
			{ "Pistols", 0, r_ragebot,  0 },
			{ "SMGs",    0, r_ragebot,  1 },
			{ "Rifles",  0, r_ragebot,  2 },
			{ "Shotguns",0, r_ragebot,  3 },
			{ "Snipers", 0, r_ragebot,  4 },
			{ "LMGs",    0, r_ragebot,  5 },
		};
		constexpr const char* k_ragebot_groups[ ] = { "WEAPONS" };

		// LEGITBOT: General + weapon groups
		constexpr entry k_legitbot_entries[ ] =
		{
			{ "Pistols", 0, r_legitbot,  0 },
			{ "SMGs",    0, r_legitbot,  1 },
			{ "Rifles",  0, r_legitbot,  2 },
			{ "Shotguns",0, r_legitbot,  3 },
			{ "Snipers", 0, r_legitbot,  4 },
			{ "LMGs",    0, r_legitbot,  5 },
		};
		constexpr const char* k_legitbot_groups[ ] = { "WEAPONS" };


		// VISUALS: player subtabs + world subtabs
		constexpr entry k_visuals_entries[ ] =
		{
			{ "Enemies", 0, r_player, 0 },
			{ "Allies",  0, r_player, 1 },
			{ "Local",   0, r_player, 2 },
			{ "World",   1, r_world,  0 },
			{ "Scene",   1, r_world,  1 },
			{ "Weather", 1, r_world,  2 },
		};
		constexpr const char* k_visuals_groups[ ] = { "PLAYER", "WORLD" };

		// MISC subtabs
		constexpr entry k_misc_entries[ ] =
		{
			{ "Main",         0, r_misc, 0 },
			{ "Removals",     0, r_misc, 1 },
			{ "Optimization", 0, r_misc, 4 },
			{ "Camera",       0, r_misc, 2 },
			{ "HUD",          0, r_misc, 3 },
		};
		constexpr const char* k_misc_groups[ ] = { "MISC" };

		// SETTINGS
		constexpr entry k_settings_entries[ ] =
		{
			{ "Config",    0, r_config,    0 },
			{ "Interface", 0, r_interface, 0 },
		};
		constexpr const char* k_settings_groups[ ] = { "SETTINGS" };

		// SKINS subtabs
		constexpr entry k_skins_entries[ ] =
		{
			{ "Guns",   0, r_skins, 0 },
			{ "Knives", 0, r_skins, 1 },
			{ "Gloves", 0, r_skins, 2 },
			{ "Agents", 0, r_skins, 3 },
		};
		constexpr const char* k_skins_groups[ ] = { "SKINS" };

		struct top_tab
		{
			const char* name{};
			const entry* entries{};
			int count{};
			const char* const* groups{};
			int group_count{};
		};

		// Sizes come from the arrays themselves. They were written out by hand,
		// and removing one entry without touching the matching literal walked the
		// sidebar loop a whole element past the end of the array -- a garbage label
		// pointer handed straight to measure_text.
		template <std::size_t entry_n, std::size_t group_n>
		[[nodiscard]] constexpr top_tab make_tab( const char* name, const entry ( &entries )[ entry_n ], const char* const ( &groups )[ group_n ] )
		{
			return { name, entries, static_cast<int>( entry_n ), groups, static_cast<int>( group_n ) };
		}

		constexpr top_tab k_top_tabs[ ] =
		{
			make_tab( "RAGE",     k_ragebot_entries,  k_ragebot_groups  ),
			make_tab( "LEGIT",    k_legitbot_entries, k_legitbot_groups ),
			make_tab( "VISUALS",  k_visuals_entries,  k_visuals_groups  ),
			make_tab( "MISC",     k_misc_entries,     k_misc_groups     ),
			make_tab( "SKINS",    k_skins_entries,    k_skins_groups    ),
			make_tab( "SETTINGS", k_settings_entries, k_settings_groups ),
		};
		// SVG icons for top tabs (order = k_top_tabs)
		inline const char* const k_tab_icons[ ] = {
			R"(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="2.5"/><path d="M12 2v6M12 16v6M2 12h6M16 12h6"/></svg>)",
			R"(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="1.6" stroke-linecap="round"><circle cx="12" cy="12" r="3"/><path d="M12 6v3M12 15v3M6 12h3M15 12h3"/></svg>)",
			R"(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7-10-7-10-7z"/><circle cx="12" cy="12" r="3"/></svg>)",
			R"(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round"><path d="M4 8h10M18 8h2M4 16h2M10 16h10"/><circle cx="15" cy="8" r="2"/><circle cx="8" cy="16" r="2"/></svg>)",
			R"(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3a9 9 0 0 0 0 18c1.5 0 2-1 2-2 0-1.5 1-2 2-2h1a4 4 0 0 0 4-4c0-5-4-8-9-8z"/><circle cx="7.5" cy="11" r="1"/><circle cx="11" cy="7.5" r="1"/><circle cx="15.5" cy="8.5" r="1"/></svg>)",
			R"(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M19.1 4.9L17 7M7 17l-2.1 2.1"/></svg>)",
		};

		constexpr auto k_top_tab_count{ 6 };




	} // namespace shell_detail

	// Здесь жила TEMP-диагностика menu_probe: разбивка menu::draw по секциям
	// (top_bar/sidebar/body/tail) и отчёт [menu-probe] раз в 120 кадров. Она
	// показала, что бюджет съедает тело, а не шапка, -- задача была закрыта,
	// и счётчики QueryPerformanceCounter на каждом кадре в горячем пути стали
	// чистым оверхедом. Убрана; при новых тормозах разбор идёт через
	// utilities/perf.hpp ([perf] в DarkFox_init.log), а не отсюда.

	void menu::draw( )
	{
		if ( this->m_last_open != this->m_open )
		{
			if ( this->m_open )
			{
				this->m_saved_relative_mouse = memory::read<std::uint8_t>( addresses::globals::input_system + 84 );
				memory::call_vfunc<void>( addresses::globals::input_system, 76, false );
				this->apply_saved_cursor( );
			}
			else
			{
				POINT pt{};
				if ( GetCursorPos( &pt ) )
				{
					this->m_saved_cursor_x = pt.x;
					this->m_saved_cursor_y = pt.y;
					this->m_has_saved_cursor = true;
				}

				memory::call_vfunc<void>( addresses::globals::input_system, 76, this->m_saved_relative_mouse != 0 );
			}

			this->m_last_open = this->m_open;
		}

		// Загрузочного экрана нет. Раньше здесь играло полноэкранное интро с
		// музыкой (ресурс INTRO_WAV) и блокировало меню до конца трека, а пока
		// не были готовы ассеты -- показывало экран загрузки. Теперь меню
		// просто не рисуется до готовности ассетов, и открывается сразу.
		if ( !g_context.ui_assets_ready( ) )
		{
			return;
		}

		xui::begin( );
		this->sync_theme_style( );
		{
			const auto dt = xdraw::delta_time( );
			const auto anim_speed = this->m_open ? 14.0f : 16.0f;
			const auto anim_target = this->m_open ? 1.0f : 0.0f;
			this->m_open_anim += ( anim_target - this->m_open_anim ) * std::min( anim_speed * dt, 1.0f );

			if ( this->m_open_anim < 0.01f && !this->m_open )
			{
				xui::end( );
				return;
			}

			const auto menu_reveal = xui::ease::out_cubic( this->m_open_anim );

			if ( !xui::begin_window( "##menu", this->m_x, this->m_y, this->m_w, this->m_h, false, 200.0f, 200.0f, menu_reveal ) )
			{
				return;
			}

			auto& dl = xui::draw::current( );
			const auto wx = this->m_x;
			const auto wy = this->m_y;
			const auto ww = this->m_w;
			const auto wh = this->m_h;

			// No halo around the window. Glow in this interface is reserved for
			// the HUD keybind badges; a menu that glows competes with the game
			// behind it for attention and never wins that argument gracefully.

			const auto topbar_h = theme::metric::topbar_height;
			const auto cat_w = theme::metric::sidebar_width;
			const auto body_y = wy + topbar_h;
			const auto body_h = wh - topbar_h;

			// Top bar (Nonagon style, spans the whole window)
			{
				this->draw_top_bar( ww );
			}

			// Sidebar: one flat surface step above the window ground. The depth
			// comes from that step and the hairline, not from blur or shadow.
			dl.rect_filled( wx, body_y, cat_w, body_h, theme::pal( ).surface,
				xdraw::corner_radius{ 0.0f, 0.0f, 0.0f, theme::metric::radius_panel } );
			dl.line( wx + cat_w, body_y, wx + cat_w, body_y + body_h, theme::pal( ).border );

			{
				this->draw_categories_column( body_h );
			}

			// Content area
			const auto body_top = body_y + 4.0f;
			this->m_body_x = wx + cat_w + 4.0f;
			this->m_body_w = ww - cat_w - 8.0f;
			this->m_body_h = body_h - 8.0f;

			// -- Прокрутка тела ------------------------------------------------
			//
			// Смена вкладки/сабтаба сбрасывает прокрутку: иначе короткая
			// вкладка унаследует сдвиг длинной и откроется на пустом месте.
			if ( this->m_scroll_tab != this->m_tab || this->m_scroll_subtab != this->m_selected_category )
			{
				this->m_scroll_tab = this->m_tab;
				this->m_scroll_subtab = this->m_selected_category;
				this->m_scroll_target = 0.0f;
				this->m_scroll = 0.0f;
				this->m_scroll_max = 0.0f;
				this->m_content_bottom = 0.0f;
			}

			// Колесо работает только над телом меню и не когда курсор занят
			// другим виджетом (открытый попап/дропдаун).
			const auto body_rect = xui::rect{ this->m_body_x, body_top, this->m_body_w, this->m_body_h };
			const auto& input = xui::ctx( ).input;
			const auto wheel = input.scroll_delta;

			// Ввод клампится СТАРЫМ максимумом (измеренным в прошлом кадре):
			// новый максимум этого кадра ещё неизвестен, а клампить по нулю
			// на первом кадре -- значит молча съесть первое движение колеса.
			if ( wheel != 0.0f && input.in_rect( body_rect ) && !xui::ctx( ).overlay_blocking( ) )
			{
				this->m_scroll_target = std::max( 0.0f, this->m_scroll_target - wheel * 48.0f );
			}

			this->m_scroll_target = std::clamp( this->m_scroll_target, 0.0f, this->m_scroll_max );

			// Плавное доведение до цели; при смене направления ускоряемся,
			// чтобы прокрутка не ощущалась вязкой.
			{
				const auto dt = xdraw::delta_time( );
				const auto speed = ( this->m_scroll_target > this->m_scroll ) ? 16.0f : 22.0f;
				this->m_scroll += ( this->m_scroll_target - this->m_scroll ) * std::min( speed * dt, 1.0f );

				if ( std::fabsf( this->m_scroll_target - this->m_scroll ) < 0.5f )
				{
					this->m_scroll = this->m_scroll_target;
				}
			}

			// Точка отсчёта сабтабов сдвинута на прокрутку. Всё, что дальше,
			// рисуется от m_body_y и само попадает в клип тела.
			this->m_body_y = body_top - this->m_scroll;

			this->m_content_bottom = 0.0f;

			{
				// Клип по видимой области: без него прокрученный контент
				// вылезает на топбар и сайдбар.
				auto& body_dl = xui::draw::current( );
				body_dl.push_clip( this->m_body_x, body_top, this->m_body_w, this->m_body_h );

				ui::reset_content_bottom( );
				{
					this->draw_shell_body( );
				}
				this->m_content_bottom = ui::content_bottom( );

				body_dl.pop_clip( );
			}

			// Уточняем максимум прокрутки по факту: самый нижний край, который
			// сообщил контент, минус низ видимой области.
			{
				const auto overflow = this->m_content_bottom - ( body_top + this->m_body_h );
				const auto measured = std::max( 0.0f, overflow );

				// Максимум только растёт в пределах вкладки, иначе он дрожит
				// от кадровых колебаний раскладки и ползунок прыгает.
				this->m_scroll_max = std::max( this->m_scroll_max, measured );

				// И сразу пересчитываем позицию по уточнённому максимуму:
				// первый кадр вкладки только что узнал, сколько тут контента,
				// и без этого колесо начало бы работать лишь со второго кадра.
				this->m_scroll_target = std::clamp( this->m_scroll_target, 0.0f, this->m_scroll_max );
				this->m_scroll = std::clamp( this->m_scroll, 0.0f, this->m_scroll_max );
			}

			// -- Ползунок прокрутки --------------------------------------------
			//
			// Без индикатора прокрутка невидима: пользователь не знает, что
			// ниже есть ещё настройки, -- именно на это и была жалоба. Трек
			// рисуем всегда, когда контент не влез, и прижимаем к правому краю
			// тела.
			if ( this->m_scroll_max > 1.0f )
			{
				auto& dl = xui::draw::current( );

				constexpr auto k_bar_w{ 3.0f };
				constexpr auto k_bar_pad{ 3.0f };

				const auto track_x = this->m_body_x + this->m_body_w - k_bar_w - k_bar_pad;
				const auto track_y = body_top + k_bar_pad;
				const auto track_h = this->m_body_h - k_bar_pad * 2.0f;

				// Доля видимого к полному. Контент = видимое + прокручиваемое.
				const auto total = this->m_body_h + this->m_scroll_max;
				const auto thumb_h = std::max( 28.0f, track_h * ( this->m_body_h / total ) );
				const auto travel = track_h - thumb_h;
				const auto progress = ( this->m_scroll_max > 0.0f )
					? std::clamp( this->m_scroll / this->m_scroll_max, 0.0f, 1.0f )
					: 0.0f;

				dl.rect_filled( track_x, track_y, k_bar_w, track_h,
					theme::pal( ).border.alpha( 70 ), xdraw::corner_radius{ k_bar_w * 0.5f } );
				dl.rect_filled( track_x, track_y + travel * progress, k_bar_w, thumb_h,
					theme::pal( ).accent.alpha( 200 ), xdraw::corner_radius{ k_bar_w * 0.5f } );
			}

			// Результаты поиска ложатся поверх тела меню.
			if ( this->m_search_open && !this->m_search_query.empty( ) )
			{
				this->draw_search_results( this->m_body_x, this->m_body_y, this->m_body_w, this->m_body_h );
			}

			// Bottom-left "DarkFox" user profile section matching Image 2
			{
				auto& dl = xui::draw::current( );
				const auto profile_w = cat_w - 20.0f;
				const auto profile_h = 42.0f;

				const auto profile_x = wx + 10.0f;
				const auto profile_y = wy + wh - profile_h - 12.0f;

				// Плашка профиля -- та же ступень surface_raised, что и контролы,
				// с хайрлайном по краю. Раньше здесь лежал свой цвет
				// (10,13,20,240) и рамка из tokens::col_accent, и при смене
				// акцента плашка оставалась бирюзовой, пока всё остальное
				// перекрашивалось.
				const auto& pal = theme::pal( );

				dl.rect_filled( profile_x, profile_y, profile_w, profile_h,
					pal.surface_raised, xdraw::corner_radius{ theme::metric::radius_card } );
				dl.rect( profile_x, profile_y, profile_w, profile_h,
					pal.border, xdraw::corner_radius{ theme::metric::radius_card }, theme::metric::hairline );

				// Avatar Circle (Steam Avatar image with fallback)
				const float avatar_r = 13.0f;
				const float avatar_center_x = profile_x + 21.0f;
				const float avatar_center_y = profile_y + profile_h * 0.5f;

				this->try_load_user_avatar( );

				// Ник и SteamID из Steam.
				//
				// Здесь стоял `static const char* s_persona`, который
				// заполнялся один раз и больше никогда не обновлялся: сменил
				// игрок ник в Steam -- меню до перезапуска показывало старый,
				// а если первый кадр пришёлся на неготовый Steam, там навсегда
				// оставалось "DarkFox". Читаем каждый кадр: вызов дешёвый (это
				// указатель в интерфейсе Steam), а поведение -- правильное.
				const auto persona = steam::friends::get_persona_name( );
				const auto display_name = ( persona && *persona ) ? persona : "DarkFox";
				const auto steam_id = steam::user::get_steam_id( );

				if ( this->m_textures.user.resource ) {
					dl.image( avatar_center_x - avatar_r, avatar_center_y - avatar_r, avatar_r * 2.0f, avatar_r * 2.0f,
						this->m_textures.user.resource.Get( ), xdraw::corner_radius{ avatar_r },
						xdraw::color{ 255, 255, 255, 255 } );
				} else {
					dl.circle_filled( avatar_center_x, avatar_center_y, avatar_r, pal.accent.alpha( 190 ) );

					const auto initial_char = static_cast< char >( std::toupper( static_cast< unsigned char >( display_name[ 0 ] ) ) );
					const char initial[ 2 ]{ initial_char, '\0' };
					const auto iw = xdraw::measure_text( initial ).first;
					const auto ih = xdraw::measure_text( initial ).second;
					dl.text( avatar_center_x - iw * 0.5f, avatar_center_y - ih * 0.5f, initial,
						xdraw::color{ 255, 255, 255, 255 } );
				}

				// Ник обрезаем по ширине плашки: Steam отдаёт до 32 байт, а в
				// 140 пикселей они не влезают и наезжают на край меню.
				const auto name_x = profile_x + 40.0f;
				const auto name_w = profile_w - 48.0f;

				dl.text( name_x, profile_y + 5.0f, xui::truncate( display_name, name_w ), pal.text );

				char status[ 64 ]{};
				if ( steam_id ) {
					std::snprintf( status, sizeof( status ), "steam %llu",
						static_cast< unsigned long long >( steam_id ) );
				} else {
					std::snprintf( status, sizeof( status ), "offline" );
				}
				dl.text( name_x, profile_y + 21.0f, xui::truncate( status, name_w ), pal.text_muted );
			}

			// Пресеты акцента -- в той же колонке, над плашкой профиля.
			//
			// Раньше здесь стояло `wy + wh - 54.0f`, а плашка профиля занимает
			// wy + wh - 54 .. wy + wh - 12. Свотчи рисовались ровно по её
			// верхнему краю и наезжали на рамку. Теперь они отбиты от плашки
			// на высоту самой плашки плюс зазор, и это считается от тех же
			// чисел, что и сама плашка.
			this->draw_theme_swatches( wx + theme::metric::space_md,
				wy + wh - 42.0f - theme::metric::space_lg );

			xui::end_window( );
		}
		xui::end( );
	}

	void menu::draw_top_bar( float w )
	{
		auto& dl = xui::draw::current( );
		const auto& input = xui::ctx( ).input;
		const auto& pal = theme::pal( );
		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto topbar_h = theme::metric::topbar_height;

		// Flat header, opaque. The frosted-glass pass that used to live here was
		// the single most expensive thing the menu drew, and the rebuild does
		// without blur entirely.
		dl.rect_filled( wx, wy, w, topbar_h, pal.surface,
			xdraw::corner_radius::top( theme::metric::radius_panel ) );

		// -- Логотип -----------------------------------------------------------
		//
		// Как в референсе (ImGui-проект): слово набрано крупным начертанием и
		// положено дважды -- акцентом со сдвигом и текстом поверх. Из-под букв
		// справа выглядывает цветная кромка, и логотип читается как бренд, а не
		// как заголовок окна. Под ним -- акцентная линия по ширине слова:
		// единственная единица цвета в шапке.
		{
			// Не const: и measure_text, и text принимают xdraw::font* без const.
			auto* logo_font = g_fonts.inter_bold[ fonts::size::big ];
			constexpr const char* mark = "DARKFOX";

			const auto [mark_w, mark_h] = xdraw::measure_text( mark, logo_font );
			const auto mark_x = wx + theme::metric::space_lg + 2.0f;
			const auto mark_y = wy + ( topbar_h - mark_h ) * 0.5f;

			dl.text( mark_x + 2.0f, mark_y, mark, pal.accent, logo_font );
			dl.text( mark_x, mark_y, mark, pal.text, logo_font );

			const auto bar_a = pal.accent_violet.alpha( 230 );
			const auto bar_b = pal.accent_pink.alpha( 230 );
			dl.rect_filled_gradient( mark_x, wy + topbar_h - 3.0f, mark_w, 2.0f,
				bar_a, bar_b, bar_b, bar_a, xdraw::corner_radius{ 1.0f } );
		}

		// Кромка шапки: чуть ярче обычного hairline, иначе топбар сливается с
		// телом и граница читается только по смене оттенка.
		dl.line( wx, wy + topbar_h, wx + w, wy + topbar_h, pal.border_strong.alpha( 28 ) );

		// Правый край логотипа и геометрия поля поиска считаются ДО табов: табы
		// обязаны уместиться в промежутке между ними. Раньше они центрировались
		// по всему окну, и последний таб заезжал под поле поиска -- в кадре
		// SETTINGS накладывался на плейсхолдер «search settings».
		const auto logo_right = wx + theme::metric::space_lg + 2.0f
			+ xdraw::measure_text( "DARKFOX", g_fonts.inter_bold[ fonts::size::big ] ).first;

		const auto search_w = 190.0f;
		const auto search_h = 24.0f;
		const auto search_x = wx + w - search_w - theme::metric::space_lg;
		const auto search_y = wy + ( topbar_h - search_h ) * 0.5f;

		const auto tabs_left = logo_right + theme::metric::space_lg;
		const auto tabs_right = search_x - theme::metric::space_lg;
		const auto tabs_span = std::max( tabs_right - tabs_left, 0.0f );

		// === ТАБЫ В ШАПКЕ МЕНЮ ===
		const auto tab_h = 32.0f;
		const auto tab_y = wy + ( topbar_h - tab_h ) * 0.5f;
		const auto tab_gap = 4.0f;
		const auto icon_size = 15.0f;
		const auto icon_gap = 6.0f;

		// Текст и иконки не сжимаются -- сжимаются боковые отступы. Если сумма
		// не влезает в промежуток, уменьшаем tab_pad_x до 4 px: лучше плотные
		// табы, чем наложение на поиск.
		float tab_pad_x = 11.0f;
		float total_tabs_w = 0.0f;
		float tab_widths[ shell_detail::k_top_tab_count ]{};

		for ( auto attempt = 0; attempt < 8; ++attempt )
		{
			total_tabs_w = 0.0f;

			for ( auto i = 0; i < shell_detail::k_top_tab_count; ++i )
			{
				const auto [tw, th] = xdraw::measure_text( shell_detail::k_top_tabs[ i ].name );
				tab_widths[ i ] = icon_size + icon_gap + tw + tab_pad_x * 2.0f;
				total_tabs_w += tab_widths[ i ];
				if ( i < shell_detail::k_top_tab_count - 1 )
				{
					total_tabs_w += tab_gap;
				}
			}

			if ( total_tabs_w <= tabs_span || tab_pad_x <= 4.0f )
			{
				break;
			}

			tab_pad_x = std::max( 4.0f, tab_pad_x - 1.0f );
		}

		float cursor_x = tabs_left + std::max( 0.0f, ( tabs_span - total_tabs_w ) * 0.5f );

		struct tab_tex_slot { Microsoft::WRL::ComPtr< ID3D11ShaderResourceView > resource; int width{}; int height{}; };
		static tab_tex_slot s_tab_icon_tex[ shell_detail::k_top_tab_count ];

		for ( auto i = 0; i < shell_detail::k_top_tab_count; ++i )
		{
			const char* name = shell_detail::k_top_tabs[ i ].name;
			if ( !s_tab_icon_tex[ i ].resource )
				s_tab_icon_tex[ i ].resource = xdraw::load_svg( shell_detail::k_tab_icons[ i ], 1.0f, &s_tab_icon_tex[ i ].width, &s_tab_icon_tex[ i ].height );

			const auto tab_w = tab_widths[ i ];
			const auto rect = xui::rect{ cursor_x, tab_y, tab_w, tab_h };
			const auto hovered = input.in_rect( rect );
			const auto selected = ( i == this->m_tab );

			( void )hovered;
			( void )input;

			// Hover fill and the sliding underline both live in ui::tab, so the
			// top bar and any future tab strip cannot drift apart.
			const auto hover_t = theme::factor( name, xui::ctx( ).input.in_rect( rect ) && !xui::ctx( ).overlay_blocking( ), theme::motion::fast, 4 );
			const auto active_t = theme::factor( name, selected, theme::motion::normal, 5 );

			if ( hover_t > 0.01f || active_t > 0.01f )
			{
				dl.rect_filled( rect.x, rect.y, rect.w, rect.h,
					theme::mix( theme::pal( ).surface_raised.alpha( 0 ), theme::pal( ).surface_hover,
						std::max( hover_t * 0.8f, active_t * 0.5f ) ),
					xdraw::corner_radius{ theme::metric::radius_control } );
			}

			const auto icon_y = rect.y + ( tab_h - icon_size ) * 0.5f;
			const auto icon_x = rect.x + tab_pad_x;
			const auto ink = theme::mix( theme::mix( theme::pal( ).text_muted, theme::pal( ).text_dim, hover_t ),
				theme::pal( ).text, active_t );

			if ( s_tab_icon_tex[ i ].resource )
				dl.image( icon_x, icon_y, icon_size, icon_size, s_tab_icon_tex[ i ].resource.Get( ), ink );

			const auto [tw, th] = xdraw::measure_text( name );
			dl.text( rect.x + tab_pad_x + icon_size + icon_gap, rect.y + ( tab_h - th ) * 0.5f, name, ink );

			// Underline grows out of the centre as the tab takes over, which is
			// what makes switching read as movement instead of a repaint.
			if ( active_t > 0.01f )
			{
				const auto bar_w = ( rect.w - tab_pad_x * 2.0f ) * active_t;
				const auto a = theme::pal( ).accent_violet;
				const auto b = theme::pal( ).accent_pink;

				dl.rect_filled_gradient( rect.center_x( ) - bar_w * 0.5f, rect.bottom( ) - 2.5f, bar_w, 2.0f,
					a, b, b, a, xdraw::corner_radius{ 1.0f } );
			}

			if ( xui::ctx( ).input.in_rect( rect ) && xui::ctx( ).input.mouse_clicked && !xui::ctx( ).overlay_blocking( ) )
			{
				if ( this->m_tab != i ) {
					this->m_tab = i;
					this->m_selected_category = 0;
				}
			}

			cursor_x += tab_w + tab_gap;
		}

		// Поиск по настройкам. Живёт в шапке, чтобы не отнимать место у контента,
		// и ищет сразу по всем вкладкам -- см. rebuild_search_index.
		// Геометрия посчитана выше: по ней же выравниваются табы.
		if ( this->m_search_entries.empty( ) )
		{
			this->rebuild_search_index( );
		}

		xui::layout::set_cursor( search_x - wx, search_y - wy );
		xui::text_input( "##menu_search", this->m_search_query, 48, "search settings" );
		xui::layout::set_cursor( 0.0f, 0.0f );

		this->m_search_open = !this->m_search_query.empty( );
	}

	void menu::draw_categories_column( float h )
	{
		auto& dl = xui::draw::current( );
		const auto& input = xui::ctx( ).input;
		const auto wx = this->m_x;
		const auto wy = this->m_y;

		const auto& tab = shell_detail::k_top_tabs[ this->m_tab ];

		// Геометрия -- из theme::metric, а не из tokens::. Раскладка шелла
		// (ширина сайдбара, высота шапки) должна читаться из того же места,
		// что и палитра: пока одни числа жили в tokens::, а другие в theme::,
		// сайдбар уезжал относительно контента при любой правке шкалы.
		const auto pad_x = theme::metric::space_sm;
		const auto item_w = theme::metric::sidebar_width - pad_x * 2.0f;
		const auto group_header_h = 22.0f;
		const auto item_h = 28.0f;
		const auto item_pad_x = theme::metric::space_md;

		const auto topbar_h = theme::metric::topbar_height;
		const auto profile_h = 42.0f;
		const auto swatch_block_h = 42.0f;
		const auto footer_h = profile_h + swatch_block_h + theme::metric::space_lg;

		// Список может не влезть в колонку (у SETTINGS четыре пункта, у RAGE
		// шесть, а окно пользователь может утащить вниз). Обрезаем по той
		// высоте, что реально есть над плашкой профиля, -- иначе последний
		// пункт рисуется поверх неё.
		const auto list_top = wy + topbar_h + theme::metric::space_md;
		const auto list_bottom = wy + this->m_h - footer_h;
		const auto visible_h = std::max( list_bottom - list_top, 0.0f );

		auto y = list_top;
		int current_group = -1;

		for ( auto i = 0; i < tab.count; ++i )
		{
			const auto& e = tab.entries[ i ];

			// Заголовок группы (WEAPONS, PLAYER, MISC, SETTINGS и т.д.)
			if ( e.group != current_group ) {
				current_group = e.group;

				if ( y + group_header_h - list_top > visible_h ) {
					break;
				}

				if ( current_group < tab.group_count && tab.groups[ current_group ] ) {
					const auto& title = tab.groups[ current_group ];

					dl.text( wx + pad_x + theme::metric::space_xs, y + ( group_header_h - 11.0f ) * 0.5f,
						title, theme::pal( ).text_muted );

					// Короткая линейка под заголовком: тот же акцентный штрих,
					// что и в карточках, -- чтобы группы в сайдбаре и заголовки
					// разделов читались как одна система.
					const auto rule_x = wx + pad_x + theme::metric::space_xs;
					const auto rule_w = std::min( 18.0f, item_w * 0.35f );
					dl.rect_filled_gradient( rule_x, y + group_header_h - 5.0f, rule_w, theme::metric::hairline,
						theme::pal( ).accent_violet.alpha( 150 ), theme::pal( ).accent_pink.alpha( 150 ),
						theme::pal( ).accent_pink.alpha( 150 ), theme::pal( ).accent_violet.alpha( 150 ),
						xdraw::corner_radius{ 0.5f } );

					y += group_header_h;
				}
			}

			if ( y + item_h - list_top > visible_h ) {
				break;
			}

			const auto item_rect = xui::rect{ wx + pad_x, y, item_w, item_h };
			const bool item_hovered = input.in_rect( item_rect ) && !xui::ctx( ).overlay_blocking( );
			const bool selected = ( i == this->m_selected_category );

			if ( item_hovered && input.mouse_clicked )
				this->m_selected_category = i;

			// Hover and selection both animate, and the active marker grows on
			// the leading edge rather than switching on -- the same treatment the
			// top-bar underline gets, so the two read as one system.
			const auto hover_t = theme::factor( e.label, item_hovered, theme::motion::fast, 0 );
			const auto active_t = theme::factor( e.label, selected, theme::motion::normal, 1 );

			if ( hover_t > 0.01f || active_t > 0.01f ) {
				dl.rect_filled( item_rect.x, item_rect.y, item_rect.w, item_rect.h,
					theme::mix( theme::pal( ).surface_raised.alpha( 0 ),
						theme::mix( theme::pal( ).surface_hover, theme::pal( ).accent_soft, active_t ),
						std::max( hover_t * 0.7f, active_t ) ),
					xdraw::corner_radius{ theme::metric::radius_control } );
			}

			if ( active_t > 0.01f ) {
				const auto marker_h = ( item_rect.h - theme::metric::space_sm ) * active_t;
				const auto a = theme::pal( ).accent_violet;
				const auto b = theme::pal( ).accent_pink;

				dl.rect_filled_gradient( item_rect.x, item_rect.center_y( ) - marker_h * 0.5f, 2.5f, marker_h,
					a, a, b, b, xdraw::corner_radius{ 1.25f } );
			}

			const auto th = xdraw::measure_text( e.label ).second;
			dl.text( item_rect.x + item_pad_x, item_rect.y + ( item_h - th ) * 0.5f, e.label,
				theme::mix( theme::mix( theme::pal( ).text_muted, theme::pal( ).text_dim, hover_t ),
					theme::pal( ).text, active_t ) );

			y += item_h + 2.0f;
		}

		// Вертикальная линия среза: один хайрлайн по всей высоте, чтобы список
		// визуально заканчивался раньше плашки профиля и не сливался с ней.
		const auto cut_y = wy + this->m_h - footer_h;
		if ( cut_y > list_top && cut_y < wy + this->m_h )
		{
			dl.rect_filled( wx + pad_x, cut_y, item_w, theme::metric::hairline,
				theme::pal( ).border.alpha( 120 ) );
		}
	}

	void menu::draw_shell_body( )
	{
		const auto& tab = shell_detail::k_top_tabs[ this->m_tab ];

		if ( this->m_selected_category < 0 || this->m_selected_category >= tab.count )
		{
			this->m_selected_category = 0;
		}

		const auto& e = tab.entries[ this->m_selected_category ];
		this->m_subtab = e.subtab;

		switch ( e.renderer )
		{
		case shell_detail::r_ragebot:   this->draw_ragebot( 0 );    break;
		case shell_detail::r_legitbot:  this->draw_legitbot( 0 );   break;
		case shell_detail::r_player:    this->draw_player( 0 );     break;
		case shell_detail::r_world:     this->draw_world( 0 );      break;
		case shell_detail::r_skins:     this->draw_skins( 0 );      break;
		case shell_detail::r_misc:      this->draw_misc( 0 );       break;
		case shell_detail::r_config:    this->draw_config( 0 );     break;
		case shell_detail::r_interface: this->draw_interface_panel( this->m_body_h ); break;
		default: break;
		}
	}

	void menu::draw_interface_panel( float h )
	{
		( void )h;

		xui::layout::set_cursor( this->m_body_x - this->m_x, this->m_body_y - this->m_y );

		if ( !xui::begin_child( "##interface", this->m_body_w, this->m_body_h, false ) )
		{
			return;
		}

		widgets::separator_with_label( "Menu Theme" );

		// Акцент выбирается свотчами в сайдбаре (menu::draw_theme_swatches), а
		// не здесь: настройка одна, и два места, где её можно поменять, --
		// это способ гарантированно получить рассинхрон.
		xui::text( "DarkFox  •  accent preset", theme::pal( ).accent );

		widgets::separator_with_label( "Appearance" );

		ui::checkbox( "Menu Glow Effect", settings::g_misc.m_interface.menu_glow_enabled );
		ui::checkbox( "HUD Glass Blur", settings::g_misc.m_interface.hud_blur );

		widgets::separator_with_label( "General" );

		ui::slider_float( "Animation Speed", settings::g_misc.m_interface.animation_speed.value,
			1.0f, 50.0f, "%.1f" );

		widgets::separator_with_label( "Debug" );
		ui::checkbox( "Show rage profiler", settings::g_misc.m_interface.show_rage_profiler.value );

		xui::end_child( );
	}

	void menu::initialize_graphics( )
	{
		// Иконки табов грузятся лениво, при первом обращении из draw_top_bar:
		// это убирает задержку открытия меню на первом запуске. Готовить тут
		// больше нечего -- загрузочного экрана, которому нужен был флаг
		// готовности, тоже нет.
	}

	void menu::shutdown( ) const
	{
	}

	void menu::apply_saved_cursor( )
	{
		if ( this->m_has_saved_cursor )
		{
			SetCursorPos( this->m_saved_cursor_x, this->m_saved_cursor_y );
			this->m_has_saved_cursor = false;
		}
	}

	void menu::try_load_user_avatar( )
	{
		// Аватар уже есть -- делать нечего.
		if ( this->m_textures.user.resource ) {
			return;
		}

		// Steam поднимается не мгновенно, а окно меню может отрисоваться
		// раньше него. Раньше здесь стоял флаг "попробовали один раз": если
		// первый кадр пришёлся на неготовый Steam, аватар не появлялся уже
		// никогда. Поэтому -- повтор с задержкой, а не одна попытка.
		if ( this->m_user_avatar_retry_delay > 0.0f )
		{
			this->m_user_avatar_retry_delay -= xdraw::delta_time( );
			return;
		}

		const auto steam_id = steam::user::get_steam_id( );
		const auto image = steam_id ? steam::friends::get_medium_friend_avatar( steam_id ) : 0;

		std::uint32_t width{}, height{};
		if ( image <= 0 ||
			!steam::utils::get_image_size( image, &width, &height ) ||
			!width || !height )
		{
			this->m_user_avatar_retry_delay = 1.0f;
			return;
		}

		std::vector<std::uint8_t> rgba( static_cast< std::size_t >( width ) * height * 4 );
		if ( !steam::utils::get_image_rgba( image, rgba.data( ), static_cast< int >( rgba.size( ) ) ) )
		{
			this->m_user_avatar_retry_delay = 1.0f;
			return;
		}

		this->m_textures.user.resource = xdraw::create_srv_from_rgba(
			rgba.data( ), static_cast< int >( width ), static_cast< int >( height ) );
		this->m_textures.user.width = static_cast< int >( width );
		this->m_textures.user.height = static_cast< int >( height );

		if ( !this->m_textures.user.resource ) {
			this->m_user_avatar_retry_delay = 1.0f;
		}
	}

	void menu::sync_theme_style( ) const
	{
		const auto& pal = theme::pal( );
		auto& s = xui::ctx( ).style;

		// Одна палитра на всё меню.
		//
		// Раньше здесь жил второй набор цветов: tokens::col_* перебивался на
		// белый акцент, и получалось, что шелл (шапка, сайдбар, маркеры) рисуется
		// фиолетово-розовыми градиентами theme::pal, а контролы -- белым по
		// серому. Две темы в одном окне читаются как недоделка.
		//
		// Теперь источник один -- theme::pal, а tokens:: обновляется из неё же:
		// на нём ещё сидят старые вызовы, и оставлять их на дефолтах значит
		// вернуть тот же раскол.
		tokens::col_accent = pal.accent;
		tokens::col_accent_active = pal.accent_press;
		tokens::col_accent_picker = pal.accent;
		tokens::col_yellow_accent = pal.accent;
		tokens::col_glow = pal.keybind_glow;
		tokens::col_text = pal.text;
		tokens::col_text_dim = pal.text_dim;
		tokens::col_card = pal.surface;
		tokens::col_elevated = pal.surface_raised;
		tokens::col_border = pal.border;
		tokens::col_dark = pal.bg;
		tokens::col_sidebar = pal.surface;
		tokens::col_header = pal.surface;
		tokens::col_dropdown = pal.surface_raised;

		// Геометрия -- ровно та, что была. Менять её вместе с палитрой нельзя:
		// сдвиг checkbox_size на пиксель или item_spacing_y на два ломает
		// выравнивание строк, и меню начинает выглядеть кривым. Палитра -- это
		// перекраска, она безопасна; размеры -- это раскладка, её трогать
		// отдельно и с проверкой глазами.
		s.rounding = tokens::card_rounding;
		s.button_rounding = tokens::btn_rounding;
		s.popup_rounding = tokens::btn_rounding;
		s.checkbox_rounding = 3.0f;
		s.slider_rounding = 6.0f;
		s.combo_rounding = 6.0f;
		s.combo_popup_rounding = tokens::btn_rounding;
		s.border_thickness = 1.0f;

		// Полоса перетаскивания -- ровно высота нарисованного топбара.
		s.window_title_height = theme::metric::topbar_height;

		s.window_bg = pal.bg;
		s.window_border = pal.border;

		// Контролы лежат на карточке, поэтому их фон -- ступень surface_raised,
		// а не отдельный цвет: так один и тот же контрол выглядит правильно и
		// на карточке, и в попапе.
		s.child_bg = pal.surface_sunken;
		s.child_border = pal.border;

		s.checkbox_bg = pal.surface_raised;
		s.checkbox_border = pal.border;
		s.checkbox_mark = pal.accent;
		s.checkbox_mark_icon = pal.bg;

		s.slider_track = pal.surface_sunken;
		s.slider_fill = pal.accent;

		s.button_bg = pal.surface_raised;
		s.button_border = pal.border;
		s.button_hovered = pal.surface_hover;
		s.button_active = pal.accent;

		s.keybind_bg = pal.surface_raised;
		s.keybind_border = pal.border;
		s.keybind_waiting = pal.accent;

		s.combo_bg = pal.surface_raised;
		s.combo_border = pal.border;
		s.combo_arrow = pal.text_dim;
		s.combo_hovered = pal.surface_hover;
		s.combo_popup_bg = pal.bg;
		s.combo_popup_border = pal.border;
		s.combo_popup_item_hovered = pal.surface_hover;
		s.combo_popup_item_selected = pal.accent_soft;

		s.popup_bg = pal.bg;
		s.popup_border = pal.border;

		s.picker_bg = pal.surface_raised;
		s.picker_border = pal.border;
		s.picker_popup_bg = pal.bg;
		s.picker_popup_border = pal.border;

		s.text_input_bg = pal.surface_raised;
		s.text_input_border = pal.border;

		s.separator = pal.border;

		s.text = pal.text;
		s.text_dim = pal.text_dim;
		s.accent = pal.accent;
	}

	// -----------------------------------------------------------------------
	// Поиск по настройкам
	//
	// Индекс строится из реестра xui: каждый xui::setting регистрирует себя в
	// конструкторе и несёт читаемые name/category. Свой список настроек вести не
	// нужно -- он разъедется с реальностью на первой же новой галочке.
	// -----------------------------------------------------------------------
	namespace search_detail {

		[[nodiscard]] inline std::string lower_copy( std::string_view text )
		{
			std::string out{ text };
			for ( auto& c : out ) {
				c = static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) );
			}
			return out;
		}

		// Категория -> вкладка. Категория задаётся вторым аргументом xui::setting
		// в settings.hpp и это единственная связь настройки с её местом в меню.
		[[nodiscard]] inline int tab_for_category( std::string_view category )
		{
			if ( category == "ragebot" )  return static_cast< int >( menu::tab::ragebot );
			if ( category == "legitbot" ) return static_cast< int >( menu::tab::legitbot );
			if ( category == "player" )   return static_cast< int >( menu::tab::player );
			if ( category == "world" )    return static_cast< int >( menu::tab::world );
			if ( category == "skins" )    return static_cast< int >( menu::tab::skins );
			if ( category == "misc" )     return static_cast< int >( menu::tab::misc );
			return static_cast< int >( menu::tab::misc );
		}

		constexpr auto row_h{ 24.0f };
		constexpr auto max_visible{ 14 };

	} // namespace search_detail

	// -----------------------------------------------------------------------
	// Пресеты акцента
	//
	// Акцент задаётся не одним цветом, а тройкой: сам акцент, его тёмный конец и
	// светлый. Градиенты в шелле (подчёркивание активной вкладки, маркер в
	// сайдбаре) берут крайние точки, и подбирать их автоматически из одного
	// цвета -- значит гадать, как оно ляжет. Пресет задаёт все три сразу.
	// -----------------------------------------------------------------------
	namespace theme_detail {

		struct preset
		{
			const char* name;
			xdraw::color accent;
			xdraw::color violet;
			xdraw::color pink;
		};

		constexpr preset k_presets[ ] =
		{
			{ "violet", { 198, 128, 240, 255 }, { 160, 116, 246, 255 }, { 240, 130, 208, 255 } },
			{ "pink",   { 240, 130, 208, 255 }, { 214, 110, 190, 255 }, { 255, 168, 214, 255 } },
			{ "blue",   { 116, 168, 246, 255 }, {  92, 132, 236, 255 }, { 138, 200, 250, 255 } },
			{ "green",  { 122, 224, 160, 255 }, {  86, 194, 140, 255 }, { 158, 240, 186, 255 } },
			{ "amber",  { 255, 190,  96, 255 }, { 232, 152,  70, 255 }, { 255, 216, 140, 255 } },
			{ "mono",   { 235, 238, 245, 255 }, { 196, 202, 214, 255 }, { 255, 255, 255, 255 } },
		};

		constexpr auto k_preset_count{ static_cast< int >( sizeof( k_presets ) / sizeof( k_presets[ 0 ] ) ) };

	} // namespace theme_detail

	void menu::apply_theme_preset( int preset )
	{
		if ( preset < 0 || preset >= theme_detail::k_preset_count ) {
			return;
		}

		this->m_theme_preset = preset;

		const auto& p = theme_detail::k_presets[ preset ];
		auto& pal = theme::g_palette;

		pal.accent = p.accent;
		pal.accent_violet = p.violet;
		pal.accent_pink = p.pink;
		pal.accent_soft = p.accent.alpha( 38 );
		pal.accent_press = p.pink;
	}

	void menu::draw_theme_swatches( float sb_x, float avatar_y )
	{
		auto& dl = xui::draw::current( );
		const auto& input = xui::ctx( ).input;

		const auto sw = 16.0f;
		const auto gap = 6.0f;
		const auto y = avatar_y - sw - 16.0f;
		auto x = sb_x;

		for ( auto i = 0; i < theme_detail::k_preset_count; ++i )
		{
			const auto rect = xui::rect{ x, y, sw, sw };
			const auto hovered = input.in_rect( rect ) && !xui::ctx( ).overlay_blocking( );
			const auto selected = ( i == this->m_theme_preset );

			dl.rect_filled( x, y, sw, sw, theme_detail::k_presets[ i ].accent, xdraw::corner_radius{ 5.0f } );

			if ( selected || hovered )
			{
				const auto t = theme::factor( theme_detail::k_presets[ i ].name, true, theme::motion::fast, 7 );
				dl.rect( x - 2.0f, y - 2.0f, sw + 4.0f, sw + 4.0f,
					theme::mix( theme::pal( ).border, theme::pal( ).text, t ),
					xdraw::corner_radius{ 7.0f }, selected ? 1.5f : 1.0f );
			}

			if ( hovered && input.mouse_clicked )
			{
				this->apply_theme_preset( i );
			}

			x += sw + gap;
		}
	}

	void menu::rebuild_search_index( )
	{
		this->m_search_entries.clear( );
		this->m_search_visible_indices.clear( );

		for ( auto* s : xui::binds::all( ) )
		{
			if ( !s || s->name.empty( ) ) {
				continue;
			}

			search_entry e{};
			e.name = s->name;
			e.category = s->category;
			e.name_lower = search_detail::lower_copy( s->name );
			e.category_lower = search_detail::lower_copy( s->category );
			e.tab = search_detail::tab_for_category( s->category );
			e.bind_key = s->bind.key;
			e.setting = s;

			this->m_search_entries.push_back( std::move( e ) );
		}
	}

	void menu::close_search( )
	{
		this->m_search_open = false;
		this->m_search_query.clear( );
		this->m_search_visible_indices.clear( );
	}

	void menu::activate_search_result( std::size_t index )
	{
		if ( index >= this->m_search_visible_indices.size( ) ) {
			return;
		}

		const auto& e = this->m_search_entries[ this->m_search_visible_indices[ index ] ];
		this->m_tab = e.tab;
		this->m_selected_category = 0;

		this->close_search( );
	}

	void menu::draw_search_results( float x, float y, float w, float h )
	{
		( void )h;

		auto& dl = xui::draw::current( );
		const auto& input = xui::ctx( ).input;
		const auto& pal = theme::pal( );

		this->m_search_visible_indices.clear( );

		if ( this->m_search_query.empty( ) ) {
			return;
		}

		const auto needle = search_detail::lower_copy( this->m_search_query );

		for ( std::size_t i = 0; i < this->m_search_entries.size( ); ++i )
		{
			const auto& e = this->m_search_entries[ i ];
			if ( e.name_lower.find( needle ) != std::string::npos ||
				e.category_lower.find( needle ) != std::string::npos )
			{
				this->m_search_visible_indices.push_back( i );
			}
		}

		const auto shown = std::min( this->m_search_visible_indices.size( ),
			static_cast< std::size_t >( search_detail::max_visible ) );
		const auto panel_h = search_detail::row_h * static_cast< float >( shown ) + 34.0f;

		// Панель ложится поверх тела меню, а не вместо него: так видно, что поиск
		// -- надстройка, и закрывается он одним движением.
		dl.rect_filled( x, y, w, panel_h, pal.bg, xdraw::corner_radius{ theme::metric::radius_panel } );
		dl.rect( x, y, w, panel_h, pal.border, xdraw::corner_radius{ theme::metric::radius_panel }, theme::metric::hairline );

		{
			char header[ 96 ]{};
			std::snprintf( header, sizeof( header ), "%zu %s", this->m_search_visible_indices.size( ),
				this->m_search_visible_indices.size( ) == 1 ? "result" : "results" );
			dl.text( x + theme::metric::space_md, y + 10.0f, header, pal.text_dim );
			dl.line( x, y + 30.0f, x + w, y + 30.0f, pal.border );
		}

		if ( this->m_search_visible_indices.empty( ) )
		{
			dl.text( x + theme::metric::space_md, y + 40.0f, "nothing matches", pal.text_muted );
			return;
		}

		for ( std::size_t row = 0; row < shown; ++row )
		{
			const auto& e = this->m_search_entries[ this->m_search_visible_indices[ row ] ];
			const auto row_y = y + 34.0f + search_detail::row_h * static_cast< float >( row );
			const auto row_rect = xui::rect{ x + theme::metric::space_xs, row_y,
				w - theme::metric::space_sm, search_detail::row_h };
			const auto hovered = input.in_rect( row_rect ) && !xui::ctx( ).overlay_blocking( );

			const auto hover_t = theme::factor( e.name.c_str( ), hovered, theme::motion::fast, 0 );
			if ( hover_t > 0.01f )
			{
				dl.rect_filled( row_rect.x, row_rect.y, row_rect.w, row_rect.h,
					theme::mix( pal.surface.alpha( 0 ), pal.surface_hover, hover_t ),
					xdraw::corner_radius{ theme::metric::radius_control } );
			}

			// Тумблер слева: результат поиска чаще всего и есть галочка, которую
			// надо переключить, и гонять пользователя за ней на вкладку -- глупо.
			const auto box = xui::rect{ row_rect.x + theme::metric::space_sm,
				row_rect.center_y( ) - theme::metric::checkbox_size * 0.5f,
				theme::metric::checkbox_size, theme::metric::checkbox_size };
			const auto box_hovered = input.in_rect( box ) && !xui::ctx( ).overlay_blocking( );

			if ( e.setting )
			{
				const auto on_t = theme::factor( e.setting, e.setting->value, theme::motion::fast, 2 );
				dl.rect_filled( box.x, box.y, box.w, box.h,
					theme::mix( pal.surface_raised, pal.accent, on_t ),
					xdraw::corner_radius{ 3.0f } );
				dl.rect( box.x, box.y, box.w, box.h,
					theme::mix( pal.border, pal.accent, on_t ),
					xdraw::corner_radius{ 3.0f }, theme::metric::hairline );
			}

			const auto text_x = box.right( ) + theme::metric::space_sm;
			const auto row_mid = row_rect.y + ( row_rect.h - 14.0f ) * 0.5f;

			dl.text( text_x, row_mid,
				xui::truncate( e.name, row_rect.w - ( text_x - row_rect.x ) - 116.0f ),
				box_hovered ? pal.text : pal.text_dim );
			dl.text( row_rect.right( ) - 108.0f, row_mid,
				xui::truncate( e.category, 100.0f ), pal.text_muted );

			if ( !hovered || !input.mouse_clicked ) {
				continue;
			}

			if ( box_hovered && e.setting )
			{
				e.setting->value = !e.setting->value;
			}
			else
			{
				this->activate_search_result( row );
				return;
			}
		}
	}

#ifdef DARKFOX_PROBE

	// Оффскрин-проба вёрстки шелла: топбар + сайдбар без игры.
	//
	// Повторяет ту часть menu::draw(), которая отвечает за оболочку, и
	// намеренно НЕ рисует тело: сабтабы тянут настройки и фичи, то есть
	// игровую память. Цель пробы -- видеть шапку, сайдбар и их геометрию.
	void menu::probe_render_shell( float w, float h, int tab, int subtab )
	{
		this->m_x = 0.0f;
		this->m_y = 0.0f;
		this->m_w = w;
		this->m_h = h;
		this->m_tab = tab;
		this->m_subtab = subtab;
		this->m_selected_category = subtab;
		this->m_open = true;
		this->m_open_anim = 1.0f;

		if ( !xui::begin_window( "##probe", this->m_x, this->m_y, this->m_w, this->m_h, false, 200.0f, 200.0f, 1.0f ) )
		{
			return;
		}

		auto& dl = xui::draw::current( );

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto ww = this->m_w;
		const auto wh = this->m_h;

		const auto topbar_h = theme::metric::topbar_height;
		const auto cat_w = theme::metric::sidebar_width;
		const auto body_y = wy + topbar_h;
		const auto body_h = wh - topbar_h;

		this->draw_top_bar( ww );

		// Сайдбар: одна ступень поверхности над фоном окна. Глубина берётся из
		// этой ступени и hairline, а не из блюра или тени.
		dl.rect_filled( wx, body_y, cat_w, body_h, theme::pal( ).surface,
			xdraw::corner_radius{ 0.0f, 0.0f, 0.0f, theme::metric::radius_panel } );
		dl.line( wx + cat_w, body_y, wx + cat_w, body_y + body_h, theme::pal( ).border );

		this->draw_categories_column( body_h );

		// Тело оставляем пустым, но помечаем его границу: без этого не видно,
		// где кончается сайдбар и начинается контент.
		this->m_body_x = wx + cat_w + 4.0f;
		this->m_body_y = body_y + 4.0f;
		this->m_body_w = ww - cat_w - 8.0f;
		this->m_body_h = body_h - 8.0f;

		dl.rect( this->m_body_x, this->m_body_y, this->m_body_w, this->m_body_h,
			theme::pal( ).border, xdraw::corner_radius{ theme::metric::radius_panel }, 1.0f );
	}

	// Проба целиком живёт в этом же файле: так ей не нужен отдельный модуль,
	// который тянул бы определение класса menu ради одного вызова. Тот же
	// приём, что в ImGui-проекте (probe-хук и probe-main под #ifdef).
	namespace probe_detail
	{

		constexpr int k_width = 900;
		constexpr int k_height = 620;

		bool write_ppm( ID3D11Device* device, ID3D11DeviceContext* context,
			ID3D11Texture2D* src, const char* path )
		{
			D3D11_TEXTURE2D_DESC desc{};
			src->GetDesc( &desc );

			D3D11_TEXTURE2D_DESC sd = desc;
			sd.Usage = D3D11_USAGE_STAGING;
			sd.BindFlags = 0;
			sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			sd.MiscFlags = 0;

			Microsoft::WRL::ComPtr<ID3D11Texture2D> staging{};
			if ( FAILED( device->CreateTexture2D( &sd, nullptr, &staging ) ) )
			{
				std::printf( "shellprobe: staging failed\n" );
				return false;
			}

			context->CopyResource( staging.Get( ), src );

			D3D11_MAPPED_SUBRESOURCE mapped{};
			if ( FAILED( context->Map( staging.Get( ), 0, D3D11_MAP_READ, 0, &mapped ) ) )
			{
				std::printf( "shellprobe: map failed\n" );
				return false;
			}

			std::FILE* f = std::fopen( path, "wb" );
			if ( !f )
			{
				context->Unmap( staging.Get( ), 0 );
				std::printf( "shellprobe: cannot open %s\n", path );
				return false;
			}

			std::fprintf( f, "P6\n%u %u\n255\n", desc.Width, desc.Height );

			const auto* base = static_cast<const unsigned char*>( mapped.pData );
			for ( UINT y = 0; y < desc.Height; ++y )
			{
				const auto* row = base + static_cast< std::size_t >( y ) * mapped.RowPitch;
				for ( UINT x = 0; x < desc.Width; ++x )
				{
					// Цель -- R8G8B8A8_UNORM, значит байты уже в порядке RGB.
					std::fputc( row[ x * 4 + 0 ], f );
					std::fputc( row[ x * 4 + 1 ], f );
					std::fputc( row[ x * 4 + 2 ], f );
				}
			}

			std::fclose( f );
			context->Unmap( staging.Get( ), 0 );
			return true;
		}

	} // namespace probe_detail

	int probe_main( int argc, char** argv )
	{
		const auto tab = ( argc > 1 ) ? std::atoi( argv[ 1 ] ) : 0;
		const auto subtab = ( argc > 2 ) ? std::atoi( argv[ 2 ] ) : 0;

		Microsoft::WRL::ComPtr<ID3D11Device> device{};
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context{};

		// WARP: программный D3D11, окно и свопчейн не нужны.
		if ( FAILED( D3D11CreateDevice(
			nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, &device, nullptr, &context ) ) )
		{
			std::printf( "shellprobe: D3D11CreateDevice(WARP) failed\n" );
			return 1;
		}

		if ( !xdraw::initialize( device.Get( ), context.Get( ) ) )
		{
			std::printf( "shellprobe: xdraw::initialize failed\n" );
			return 1;
		}

		// Шрифты: без них measure_text вернёт нули и вся центровка развалится.
		g_fonts.initialize( );

		D3D11_TEXTURE2D_DESC td{};
		td.Width = probe_detail::k_width;
		td.Height = probe_detail::k_height;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_RENDER_TARGET;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> target{};
		if ( FAILED( device->CreateTexture2D( &td, nullptr, &target ) ) )
		{
			std::printf( "shellprobe: target texture failed\n" );
			return 1;
		}

		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv{};
		if ( FAILED( device->CreateRenderTargetView( target.Get( ), nullptr, &rtv ) ) )
		{
			std::printf( "shellprobe: RTV failed\n" );
			return 1;
		}

		ID3D11RenderTargetView* rtv_raw = rtv.Get( );

		D3D11_VIEWPORT vp{};
		vp.Width = static_cast< float >( probe_detail::k_width );
		vp.Height = static_cast< float >( probe_detail::k_height );
		vp.MaxDepth = 1.0f;

		const float clear[ 4 ]{ 0.035f, 0.039f, 0.051f, 1.0f };

		// Серия кадров: анимации считают шаг от delta_time, за один кадр они
		// остаются в нуле. Цель чистится каждый кадр, иначе текст копится
		// поверх себя и превращается в смазанный клубок.
		for ( auto frame = 0; frame < 90; ++frame )
		{
			context->OMSetRenderTargets( 1, &rtv_raw, nullptr );
			context->RSSetViewports( 1, &vp );
			context->ClearRenderTargetView( rtv.Get( ), clear );

			xdraw::begin_frame( true );
			xui::begin( );

			g_menu.probe_render_shell( 860.0f, 560.0f, tab, subtab );

			xui::end( );
			xdraw::end_frame( );

			::Sleep( 4 );
		}

		const char* out = "D:\\Phantom\\.workbuddy-ai\\probe\\shell.ppm";
		if ( !probe_detail::write_ppm( device.Get( ), context.Get( ), target.Get( ), out ) )
		{
			return 1;
		}

		std::printf( "shellprobe: wrote %s (tab %d, subtab %d)\n", out, tab, subtab );
		return 0;
	}

#endif // DARKFOX_PROBE

} // namespace rendering

// Точка входа пробы. Вне #ifdef её нет, поэтому релизная DLL не затрагивается.
#ifdef DARKFOX_PROBE
int main( int argc, char** argv )
{
	// Печатается ДО любой работы: если этой строки нет, падение случилось на
	// статической инициализации объектников проекта, а не в пробе.
	std::printf( "shellprobe: entry (static init passed)\n" );
	std::fflush( stdout );

	return rendering::probe_main( argc, argv );
}
#endif
