// DarkFox loader -- вариант на xui/xdraw (D3D11).
//
// Почему переписан: GDI-версия рисовала напрямую в HDC окна и моргала.
// Причина не в таймере, а в отсутствии кадрового буфера: каждый WM_PAINT
// заливал видимую поверхность, и любая частичная перерисовка (перекрытие
// окна, обрезка по invalid-региону) оставляла на экране куски прошлого
// кадра -- отсюда полосы и чёрные поля на скриншоте.
//
// Здесь кадр собирается в D3D11 back buffer и показывается ОДИН раз за
// кадр через Present. Промежуточных состояний на экране не бывает по
// построению, моргание исчезает как класс. Бонус -- оформление совпадает
// с игровым меню: та же типографика inter, тот же движок отрисовки.
//
// Логика не изменилась: поиск DLL по всем дискам с кэшем, запрос прав
// администратора, запуск Steam + CS2, инжект.

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <dwmapi.h>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <external/xdraw/xdraw.hpp>
#include <external/xdraw/xui/xui.hpp>
#include <core/resources/fonts/inter.hpp>

#include "verification.hpp"

#pragma comment( lib, "d3d11.lib" )
#pragma comment( lib, "dxgi.lib" )
#pragma comment( lib, "d3dcompiler.lib" )
#pragma comment( lib, "dwmapi.lib" )
#pragma comment( lib, "shlwapi.lib" )
#pragma comment( lib, "shell32.lib" )
#pragma comment( lib, "ole32.lib" )
#pragma comment( lib, "advapi32.lib" )

using Microsoft::WRL::ComPtr;

namespace {

	// -----------------------------------------------------------------------
	// Что инжектим
	//
	// Имя DLL задаётся на этапе сборки: из одного исходника получаются два
	// лоадера -- обычный (DarkFox.dll) и дев (DarkFox-dev.dll). Дев-сборка
	// помечается янтарным акцентом и бейджем, чтобы их не путать на глаз.
	// -----------------------------------------------------------------------

// Имя DLL приходит из сборки как ОБЫЧНЫЙ ТОКЕН без кавычек (DarkFox или
// DarkFox-dev) и здесь превращается в широкий литерал через L"" #name.
//
// Почему не "/DDARKFOX_DLL_NAME=L\"DarkFox-dev.dll\"": в .bat cmd.exe съедает
// вложенные кавычки, cl получает два отдельных токена (L и DarkFox-dev.dll),
// макрос раскрывается в мусор и сборка падает на k_dll_name. Передача голого
// имени убирает кавычки из командной строки полностью.
#ifndef DARKFOX_DLL_NAME
#define DARKFOX_DLL_NAME DarkFox
#endif

// DARKFOX_STR склеивает имя в широкий литерал: DarkFox -> L"DarkFox.dll".
// Нужны ОБА уровня: первый раскрывает аргумент, второй клеит префикс L.
// Без промежуточного макроса ##x не заменяется на значение (получится "Lx").
#define DARKFOX_STR_(x) L"" #x L".dll"
#define DARKFOX_STR(x) DARKFOX_STR_(x)

#ifdef DARKFOX_DEV_BUILD
	constexpr auto k_dev_build{ true };
#else
	constexpr auto k_dev_build{ false };
#endif

	constexpr auto k_dll_name{ DARKFOX_STR( DARKFOX_DLL_NAME ) };
	constexpr auto k_shortcut_name{ k_dev_build ? L"DarkFox dev.lnk" : L"DarkFox.lnk" };
	constexpr auto k_window_title{ k_dev_build ? L"DarkFox loader (dev)" : L"DarkFox loader" };

	// Кэш найденного пути. Лежит рядом с EXE, чтобы перенос лоадера на другой
	// диск не тащил за собой устаревший путь.
	//
	// Имя кэша ОБЯЗАНО зависеть от сборки. Раньше оно было общим
	// ("DarkFox.loader.cache"), и релиз с девом затирали друг другу путь:
	// релизный лоадер клал туда D:\...\DarkFox.dll, после чего дев-лоадер
	// читал кэш, видел существующий файл и инжектил ОБЫЧНУЮ версию.
	// Теперь у каждой сборки свой файл: DarkFox.loader.cache /
	// DarkFox-dev.loader.cache.
	constexpr auto k_cache_suffix{ L".loader.cache" };

	// Маска поиска по дискам: шире точного имени, чтобы поймать и копии.
	constexpr auto k_search_pattern{ L"DarkFox*.dll" };

	// Геометрия окна.
	constexpr auto k_window_w{ 620 };
	constexpr auto k_window_h{ 470 };

	// -----------------------------------------------------------------------
	// Состояние, общее для UI и рабочего потока
	// -----------------------------------------------------------------------

	enum class stage : int
	{
		idle,
		launching,
		waiting_game,
		injecting,
		done,
		failed
	};

	HWND g_window{};

	std::wstring g_dll_path{};
	bool         g_dll_present{};
	bool         g_dll_from_scan{};
	bool         g_scanning{};
	std::atomic<int> g_scan_visited{};
	// Результат проверки выбранного файла. Заполняется тем же потоком, что
	// ищет DLL; читается UI-потоком. Храним целиком, а не флагом: в карточке
	// нужно показать, ЧЕМ именно файл не подошёл (не x64 / нет маркера /
	// хеш не сошёлся), иначе пользователь видит только «не найдена».
	verification::result g_dll_check{};
	std::mutex           g_check_mtx{};

	bool         g_shortcut_ok{};
	std::wstring g_shortcut_note{};
	bool         g_steam_running{};
	bool         g_is_admin{};
	DWORD        g_target_pid{};

	std::atomic<int>  g_stage{ static_cast< int >( stage::idle ) };
	std::atomic<bool> g_abort{ false };
	// Взводится воркером в самом конце: WM_DESTROY по нему решает,
	// можно ли делать join() или пора detach().
	std::atomic<bool> g_worker_done{ true };
	std::mutex        g_message_mtx{};
	std::wstring      g_message{};
	std::thread       g_worker{};

	// D3D11
	ComPtr<ID3D11Device>           g_device{};
	ComPtr<ID3D11DeviceContext>    g_context{};
	ComPtr<IDXGISwapChain>         g_swap_chain{};
	ComPtr<ID3D11RenderTargetView> g_render_target{};

	// Шрифты под размеры интерфейса.
	xdraw::font* g_font_regular{};
	xdraw::font* g_font_bold{};
	xdraw::font* g_font_small{};

	bool g_running{ true };

	// -----------------------------------------------------------------------
	// Вспомогательное
	// -----------------------------------------------------------------------

	std::wstring error_text( DWORD code )
	{
		wchar_t* buffer{};
		const auto length = FormatMessageW( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, code, 0, reinterpret_cast< wchar_t* >( &buffer ), 0, nullptr );

		std::wstring result = length && buffer ? std::wstring( buffer, length ) : L"код " + std::to_wstring( code );

		if ( buffer ) {
			LocalFree( buffer );
		}

		while ( !result.empty( ) && ( result.back( ) == L'\r' || result.back( ) == L'\n' ) ) {
			result.pop_back( );
		}

		return result;
	}

	std::wstring exe_directory( )
	{
		wchar_t path[ MAX_PATH ]{};
		if ( !GetModuleFileNameW( nullptr, path, MAX_PATH ) ) {
			return {};
		}

		std::wstring result{ path };
		const auto slash = result.find_last_of( L"\\/" );
		return slash == std::wstring::npos ? std::wstring{} : result.substr( 0, slash );
	}

	bool file_exists( const std::wstring& path )
	{
		const auto attributes = GetFileAttributesW( path.c_str( ) );
		return attributes != INVALID_FILE_ATTRIBUTES && !( attributes & FILE_ATTRIBUTE_DIRECTORY );
	}

	void set_message( const std::wstring& text )
	{
		std::scoped_lock lock( g_message_mtx );
		g_message = text;
	}

	std::wstring get_message( )
	{
		std::scoped_lock lock( g_message_mtx );
		return g_message;
	}

	// xui работает со std::string (UTF-8). Конвертация нужна везде, где
	// строка приходит из WinAPI в UTF-16.
	std::string to_utf8( const std::wstring& wide )
	{
		if ( wide.empty( ) ) {
			return {};
		}

		const auto count = WideCharToMultiByte( CP_UTF8, 0, wide.c_str( ), static_cast< int >( wide.size( ) ),
			nullptr, 0, nullptr, nullptr );

		if ( count <= 0 ) {
			return {};
		}

		std::string result( static_cast< std::size_t >( count ), '\0' );
		WideCharToMultiByte( CP_UTF8, 0, wide.c_str( ), static_cast< int >( wide.size( ) ),
			result.data( ), count, nullptr, nullptr );
		return result;
	}

	// Обратная конвертация: тексты проверки подлинности живут в verification.hpp
	// как ASCII (их пишет и читает только лоадер), но в UI-строку их надо
	// вклеить рядом с широкими литералами.
	std::wstring to_wide( const std::string& narrow )
	{
		if ( narrow.empty( ) ) {
			return {};
		}

		const auto count = MultiByteToWideChar( CP_UTF8, 0, narrow.c_str( ),
			static_cast< int >( narrow.size( ) ), nullptr, 0 );

		if ( count <= 0 ) {
			return {};
		}

		std::wstring result( static_cast< std::size_t >( count ), L'\0' );
		MultiByteToWideChar( CP_UTF8, 0, narrow.c_str( ), static_cast< int >( narrow.size( ) ),
			result.data( ), count );
		return result;
	}

	// -----------------------------------------------------------------------
	// Поиск DLL
	//
	// Порядок: кэш -> ближние кандидаты (до показа окна) -> обход всех
	// фиксированных дисков в рабочем потоке.
	// -----------------------------------------------------------------------

	std::wstring cache_path( )
	{
		const auto directory = exe_directory( );
		if ( directory.empty( ) ) {
			return {};
		}

		// Имя файла кэша строим из имени инжектируемой DLL без расширения:
		// DarkFox.dll -> DarkFox.loader.cache, DarkFox-dev.dll ->
		// DarkFox-dev.loader.cache. Так имя кэша не может разойтись с тем,
		// что реально ищет эта сборка, даже если вариантов станет больше.
		auto stem = std::wstring( k_dll_name );
		if ( const auto dot = stem.rfind( L'.' ); dot != std::wstring::npos ) {
			stem.resize( dot );
		}

		return directory + L"\\" + stem + k_cache_suffix;
	}

	std::wstring read_cache( )
	{
		const auto path = cache_path( );
		if ( path.empty( ) ) {
			return {};
		}

		const auto file = CreateFileW( path.c_str( ), GENERIC_READ, FILE_SHARE_READ, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );

		if ( file == INVALID_HANDLE_VALUE ) {
			return {};
		}

		char buffer[ MAX_PATH * 2 ]{};
		DWORD read{};
		const auto ok = ReadFile( file, buffer, sizeof( buffer ) - 1, &read, nullptr );
		CloseHandle( file );

		if ( !ok || read == 0 ) {
			return {};
		}

		buffer[ read ] = '\0';

		const auto wide = MultiByteToWideChar( CP_UTF8, 0, buffer, -1, nullptr, 0 );
		if ( wide <= 0 ) {
			return {};
		}

		std::wstring result( static_cast< std::size_t >( wide ), L'\0' );
		MultiByteToWideChar( CP_UTF8, 0, buffer, -1, result.data( ), wide );

		while ( !result.empty( ) && ( result.back( ) == L'\n' || result.back( ) == L'\r' ) ) {
			result.pop_back( );
		}

		return result;
	}

	void write_cache( const std::wstring& dll )
	{
		const auto path = cache_path( );
		if ( path.empty( ) ) {
			return;
		}

		const auto narrow = WideCharToMultiByte( CP_UTF8, 0, dll.c_str( ), -1, nullptr, 0, nullptr, nullptr );
		if ( narrow <= 0 ) {
			return;
		}

		std::string utf8( static_cast< std::size_t >( narrow ), '\0' );
		WideCharToMultiByte( CP_UTF8, 0, dll.c_str( ), -1, utf8.data( ), narrow, nullptr, nullptr );

		const auto file = CreateFileW( path.c_str( ), GENERIC_WRITE, 0, nullptr,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

		if ( file == INVALID_HANDLE_VALUE ) {
			return;
		}

		DWORD written{};
		WriteFile( file, utf8.c_str( ), static_cast< DWORD >( utf8.size( ) ), &written, nullptr );
		CloseHandle( file );
	}

	bool is_skipped_directory( const std::wstring& name )
	{
		static const wchar_t* const skipped[ ] = {
			L"Windows", L"WinSxS", L"$Recycle.Bin", L"System Volume Information",
			L"$WinREAgent", L"$SysReset", L"Recovery", L"PerfLogs",
			L"node_modules", L".git", L".svn", L"__pycache__",
			L"Installer", L"assembly", L"WindowsPowerShell",
			L"DriverStore", L"catroot", L"catroot2", L"Logs",
		};

		for ( const auto* skip : skipped )
		{
			if ( _wcsicmp( name.c_str( ), skip ) == 0 ) {
				return true;
			}
		}

		if ( _wcsnicmp( name.c_str( ), L"vcpkg_installed", 16 ) == 0
			|| _wcsnicmp( name.c_str( ), L"intermediates", 13 ) == 0
			|| _wcsnicmp( name.c_str( ), L"$", 1 ) == 0 )
		{
			return true;
		}

		return false;
	}

	void scan_directory( const std::wstring& root, int max_depth,
		const std::function< void( const std::wstring& ) >& on_file,
		std::atomic<bool>& abort, std::atomic<int>& visited )
	{
		struct frame
		{
			std::wstring path;
			int depth;
		};

		std::vector< frame > stack;
		stack.push_back( { root, 0 } );

		while ( !stack.empty( ) && !abort )
		{
			auto current = std::move( stack.back( ) );
			stack.pop_back( );

			++visited;

			if ( current.depth > max_depth ) {
				continue;
			}

			WIN32_FIND_DATAW data{};
			const auto pattern = current.path + L"\\*";
			const auto find = FindFirstFileExW( pattern.c_str( ), FindExInfoBasic, &data,
				FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH );

			if ( find == INVALID_HANDLE_VALUE ) {
				continue;
			}

			do
			{
				if ( abort ) {
					break;
				}

				const std::wstring name{ data.cFileName };

				if ( name == L"." || name == L".." ) {
					continue;
				}

				const auto full = current.path + L"\\" + name;

				if ( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
				{
					// Junction/symlink не разворачиваем: бесконечные циклы.
					if ( data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ) {
						continue;
					}

					if ( is_skipped_directory( name ) ) {
						continue;
					}

					stack.push_back( { full, current.depth + 1 } );
					continue;
				}

				on_file( full );

			} while ( FindNextFileW( find, &data ) );

			FindClose( find );
		}
	}

	std::wstring file_name_of( const std::wstring& path )
	{
		const auto slash = path.find_last_of( L"\\/" );
		return slash == std::wstring::npos ? path : path.substr( slash + 1 );
	}

	bool is_exact_match( const std::wstring& path )
	{
		return _wcsicmp( file_name_of( path ).c_str( ), k_dll_name ) == 0;
	}

	// Допустимая замена: та же сборка с суффиксом копии от нашего бэкапа.
	// Чужая сборка сюда не попадёт -- инжект не той DLL тихо ломает игру.
	bool is_same_family( const std::wstring& path )
	{
		const auto name = file_name_of( path );
		const auto family = std::wstring( k_dll_name );

		if ( name.size( ) <= family.size( ) ) {
			return false;
		}

		return _wcsnicmp( name.c_str( ), family.c_str( ), family.size( ) ) == 0;
	}

	bool is_newer( const std::wstring& lhs, const std::wstring& rhs )
	{
		const auto stamp = [ ]( const std::wstring& path ) -> ULONGLONG
			{
				const auto file = CreateFileW( path.c_str( ), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
					nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );

				if ( file == INVALID_HANDLE_VALUE ) {
					return 0;
				}

				FILETIME write{};
				GetFileTime( file, nullptr, nullptr, &write );
				CloseHandle( file );

				ULARGE_INTEGER value{};
				value.LowPart = write.dwLowDateTime;
				value.HighPart = write.dwHighDateTime;
				return value.QuadPart;
			};

		return stamp( lhs ) > stamp( rhs );
	}

	std::vector< std::wstring > local_drive_roots( )
	{
		std::vector< std::wstring > roots;

		const auto mask = GetLogicalDrives( );
		for ( auto bit = 0; bit < 26; ++bit )
		{
			if ( !( mask & ( 1u << bit ) ) ) {
				continue;
			}

			wchar_t root[ ] = { static_cast< wchar_t >( L'A' + bit ), L':', L'\\', L'\0' };

			if ( GetDriveTypeW( root ) != DRIVE_FIXED ) {
				continue;
			}

			roots.emplace_back( root );
		}

		return roots;
	}

	// Ожидаемая сборка для этого лоадера. Маркер в DLL хранит "dev" или "ship",
	// и лоадер обязан инжектить только своё: дев-лоадер с релизной DLL --
	// это не косметика, а разные наборы хуков и разная диагностика.
	constexpr const char* k_expected_build = k_dev_build ? "dev" : "ship";

	// Проверяет кандидата и, если он годен, публикует результат для UI.
	// Возвращает true только для файла, который действительно можно инжектить.
	bool check_candidate( const std::wstring& path, bool publish )
	{
		const auto result = verification::verify( path, k_expected_build );

		if ( publish ) {
			std::scoped_lock lock( g_check_mtx );
			g_dll_check = result;
		}

		if ( result.state != verification::status::ok ) {
			return false;
		}

		// Мягкий режим: хеш не сошёлся -- всё равно инжектим (файл-то наш,
		// маркер на месте), но пользователь увидит предупреждение в карточке.
		return true;
	}

	// То же самое, но результат отдаётся наружу и ничего не публикуется.
	// Нужно обходу дисков: он проверяет десятки кандидатов и не должен
	// затирать состояние UI промежуточными диагнозами.
	bool check_candidate_silent( const std::wstring& path, verification::result& out )
	{
		out = verification::verify( path, k_expected_build );
		return out.state == verification::status::ok;
	}

	std::wstring resolve_dll_path( bool publish = true )
	{
		const auto directory = exe_directory( );
		if ( directory.empty( ) ) {
			return {};
		}

		// 1. Кэш -- самый быстрый путь и он же подтверждает прошлый поиск.
		// Проверяем не только существование файла, но и подлинность: кэш мог
		// остаться от другого варианта лоадера, файл могли подменить или он
		// мог испортиться. Только имя в этом месте ничего не гарантирует.
		const auto cached = read_cache( );
		if ( !cached.empty( ) && file_exists( cached ) && check_candidate( cached, publish ) ) {
			return cached;
		}

		// 2. Ближние кандидаты. Лоадер лежит в bin\ рядом с DLL в 99% случаев,
		//    и полный обход тут не нужен.
		//
		//    Порядок важен: сначала точное имя, потом родственные (бэкапы).
		//    Первый же ГОДНЫЙ файл побеждает -- «самый новый» тут не критерий,
		//    потому что свежим может оказаться и битый.
		//
		//    Плюс рабочий каталог и %TEMP%: DLL часто кладут туда при
		//    распаковке архива, а лоадер запускают из другого места.
		std::vector< std::wstring > local;
		local.push_back( directory + L"\\" + k_dll_name );
		local.push_back( directory + L"\\..\\bin\\" + k_dll_name );
		local.push_back( directory + L"\\..\\" + k_dll_name );

		{
			wchar_t cwd[ MAX_PATH ]{};
			if ( GetCurrentDirectoryW( MAX_PATH, cwd ) && _wcsicmp( cwd, directory.c_str( ) ) != 0 ) {
				local.push_back( std::wstring( cwd ) + L"\\" + k_dll_name );
			}

			wchar_t temp[ MAX_PATH ]{};
			if ( GetTempPathW( MAX_PATH, temp ) ) {
				local.push_back( std::wstring( temp ) + k_dll_name );
			}
		}

		for ( const auto& candidate : local )
		{
			if ( !file_exists( candidate ) ) {
				continue;
			}

			if ( check_candidate( candidate, publish ) ) {
				write_cache( candidate );
				return candidate;
			}
		}

		// Ни один ближний не прошёл -- фиксируем причину по «главному» пути,
		// чтобы UI показал не «не найдена», а конкретный диагноз.
		if ( publish && file_exists( local[ 0 ] ) ) {
			check_candidate( local[ 0 ], true );
		}

		return {};
	}

	std::wstring deep_search_dll( std::atomic<int>& visited )
	{
		std::wstring best{};
		verification::result best_check{};

		// Собираем все кандидаты и проверяем ИХ В КОНЦЕ, а не на ходу.
		// Причина: проверка читает файл целиком (десятки МБ) и считает SHA-256,
		// и делать это для каждого встреченного DarkFox*.dll на диске --
		// неприемлемо. Сначала дешёвый фильтр по имени, потом дорогая проверка
		// только для лучших.
		std::vector< std::wstring > exact;
		std::vector< std::wstring > family;
		std::mutex collect_mtx{};

		for ( const auto& root : local_drive_roots( ) )
		{
			if ( g_abort ) {
				break;
			}

			set_message( L"поиск на " + root + L"...  каталогов: " + std::to_wstring( visited.load( ) ) );

			scan_directory( root, 12, [ & ]( const std::wstring& path )
				{
					const auto name = file_name_of( path );

					if ( name.size( ) < 4 || _wcsicmp( name.c_str( ) + name.size( ) - 4, L".dll" ) != 0 ) {
						return;
					}

					// Бэкапы (.bak_*) отсеиваем сразу: это копии прошлых
					// сборок, инжектить их нельзя даже если файл цел.
					if ( name.find( L".bak" ) != std::wstring::npos ) {
						return;
					}

					std::scoped_lock lock( collect_mtx );

					if ( is_exact_match( path ) ) {
						exact.push_back( path );
					}
					else if ( is_same_family( path ) ) {
						family.push_back( path );
					}
				}, g_abort, visited );

			if ( g_abort ) {
				break;
			}
		}

		// Точное имя в приоритете. Внутри группы -- самые новые вперёд:
		// свежая сборка обычно и есть нужная.
		const auto by_newest = [ ]( std::vector< std::wstring >& list )
			{
				std::sort( list.begin( ), list.end( ),
					[ ]( const std::wstring& lhs, const std::wstring& rhs )
					{
						return is_newer( lhs, rhs );
					} );
			};

		by_newest( exact );
		by_newest( family );

		for ( const auto& candidate : exact )
		{
			if ( g_abort ) {
				break;
			}

			verification::result check{};
			if ( check_candidate_silent( candidate, check ) )
			{
				best = candidate;
				best_check = check;
				break;
			}

			// Запоминаем первую же диагностику, чтобы объяснить пользователю,
			// ПОЧЕМУ файл с правильным именем не подошёл.
			if ( best_check.state == verification::status::not_found ) {
				best_check = check;
			}
		}

		if ( best.empty( ) )
		{
			for ( const auto& candidate : family )
			{
				if ( g_abort ) {
					break;
				}

				verification::result check{};
				if ( check_candidate_silent( candidate, check ) )
				{
					best = candidate;
					best_check = check;
					break;
				}
			}
		}

		{
			std::scoped_lock lock( g_check_mtx );
			g_dll_check = best_check;
		}

		return best;
	}

	// -----------------------------------------------------------------------
	// Права администратора
	// -----------------------------------------------------------------------

	bool is_elevated( )
	{
		HANDLE token{};
		if ( !OpenProcessToken( GetCurrentProcess( ), TOKEN_QUERY, &token ) ) {
			return false;
		}

		TOKEN_ELEVATION elevation{};
		DWORD size{};
		const auto ok = GetTokenInformation( token, TokenElevation, &elevation, sizeof( elevation ), &size );
		CloseHandle( token );

		return ok && elevation.TokenIsElevated;
	}

	bool relaunch_elevated( )
	{
		wchar_t exe[ MAX_PATH ]{};
		if ( !GetModuleFileNameW( nullptr, exe, MAX_PATH ) ) {
			return false;
		}

		SHELLEXECUTEINFOW info{};
		info.cbSize = sizeof( info );
		info.fMask = SEE_MASK_NOCLOSEPROCESS;
		info.lpVerb = L"runas";
		info.lpFile = exe;
		info.lpParameters = L"--elevated";
		info.nShow = SW_SHOWNORMAL;

		if ( !ShellExecuteExW( &info ) ) {
			return false;
		}

		if ( info.hProcess ) {
			CloseHandle( info.hProcess );
		}

		return true;
	}

	// -----------------------------------------------------------------------
	// Ярлык
	// -----------------------------------------------------------------------

	bool create_shortcut( std::wstring& out_error )
	{
		wchar_t exe[ MAX_PATH ]{};
		if ( !GetModuleFileNameW( nullptr, exe, MAX_PATH ) ) {
			out_error = L"не удалось получить путь к себе";
			return false;
		}

		wchar_t desktop[ MAX_PATH ]{};
		if ( FAILED( SHGetFolderPathW( nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, SHGFP_TYPE_CURRENT, desktop ) ) ) {
			out_error = L"не нашёл рабочий стол";
			return false;
		}

		const auto path = std::wstring( desktop ) + L"\\" + k_shortcut_name;

		IShellLinkW* link{};
		const auto hr = CoCreateInstance( CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
			IID_IShellLinkW, reinterpret_cast< void** >( &link ) );

		if ( FAILED( hr ) ) {
			out_error = L"IShellLink: " + error_text( static_cast< DWORD >( hr ) );
			return false;
		}

		link->SetPath( exe );
		link->SetIconLocation( exe, 0 );
		link->SetDescription( L"DarkFox" );

		const auto directory = exe_directory( );
		if ( !directory.empty( ) ) {
			link->SetWorkingDirectory( directory.c_str( ) );
		}

		IPersistFile* file{};
		auto saved{ false };

		if ( SUCCEEDED( link->QueryInterface( IID_IPersistFile, reinterpret_cast< void** >( &file ) ) ) ) {
			saved = SUCCEEDED( file->Save( path.c_str( ), TRUE ) );
			if ( !saved ) {
				out_error = L"не удалось записать ярлык (занят?)";
			}
			file->Release( );
		}
		else {
			out_error = L"IPersistFile недоступен";
		}

		link->Release( );
		return saved;
	}

	// -----------------------------------------------------------------------
	// Процессы
	// -----------------------------------------------------------------------

	DWORD find_process( const wchar_t* name )
	{
		const auto snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
		if ( snapshot == INVALID_HANDLE_VALUE ) {
			return 0;
		}

		PROCESSENTRY32W entry{};
		entry.dwSize = sizeof( entry );

		DWORD pid{};
		if ( Process32FirstW( snapshot, &entry ) )
		{
			do
			{
				if ( _wcsicmp( entry.szExeFile, name ) == 0 )
				{
					pid = entry.th32ProcessID;
					break;
				}
			} while ( Process32NextW( snapshot, &entry ) );
		}

		CloseHandle( snapshot );
		return pid;
	}

	// Есть ли модуль в целевом процессе.
	//
	// Инжект в недогруженный движок -- не «иногда не работает», а
	// гарантированный отказ: резолверы адресов стартуют раньше, чем Source 2
	// создаст свои системы, не находят цели и игра падает при загрузке. Поэтому
	// ждать надо по факту, а не по часам.
	bool module_present( DWORD pid, const wchar_t* module )
	{
		const auto snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid );
		if ( snapshot == INVALID_HANDLE_VALUE ) {
			return false;
		}

		MODULEENTRY32W entry{};
		entry.dwSize = sizeof( entry );

		auto found = false;
		if ( Module32FirstW( snapshot, &entry ) )
		{
			do
			{
				if ( _wcsicmp( entry.szModule, module ) == 0 )
				{
					found = true;
					break;
				}
			} while ( Module32NextW( snapshot, &entry ) );
		}

		CloseHandle( snapshot );
		return found;
	}

	// Модули, по которым видно, что движок поднялся. Это те, что чит резолвит
	// на старте (known_modules в utilities/memory/memory.cpp): client.dll и
	// engine2.dll -- сам движок, panorama.dll -- UI-слой, tier0 --
	// аллокатор (MODULE_EXPORT "tier0.dll:g_pMemAlloc"), последние два --
	// рендер. Без любого из них чит стартует в пустоту.
	constexpr const wchar_t* k_engine_modules[]
	{
		L"client.dll",
		L"engine2.dll",
		L"panorama.dll",
		L"tier0.dll",
		L"materialsystem2.dll",
		L"rendersystemdx11.dll",
	};

	// Ждёт готовности движка по факту загрузки модулей.
	//
	// Раньше здесь стояла глухая пауза 30 с. На быстрой машине она лишняя, а на
	// медленной -- недостаточная, и тогда чит прилетал в ещё не собранный
	// движок: отсюда и падения при запуске игры.
	bool wait_for_engine( DWORD pid )
	{
		constexpr auto k_poll_ms = 250;
		constexpr auto k_timeout_ms = 180000;
		constexpr auto k_settle_ms = 3000;

		for ( auto waited = 0; waited < k_timeout_ms && !g_abort; waited += k_poll_ms )
		{
			const wchar_t* missing{};

			for ( const auto* module : k_engine_modules )
			{
				if ( !module_present( pid, module ) )
				{
					missing = module;
					break;
				}
			}

			if ( !missing )
			{
				// Модули на месте, но движок ещё достраивает системы. Без этой
				// паузы инжект попадает ровно между загрузкой модуля и его
				// инициализацией.
				//
				// Перерисовку не просим: у этого лоадера окно на xui/D3D11 и
				// рисуется непрерывным циклом, set_message достаточно.
				set_message( L"движок поднялся, даю 3 с на инициализацию..." );

				for ( auto settle = 0; settle < k_settle_ms && !g_abort; settle += 100 )
				{
					Sleep( 100 );
				}

				return !g_abort;
			}

			set_message( L"жду движок... нет " + std::wstring( missing )
				+ L" (" + std::to_wstring( waited / 1000 ) + L" с)" );

			Sleep( k_poll_ms );
		}

		// Таймаут не отменяет инжект: игра могла стартовать необычно, и отказ
		// тут был бы хуже попытки. Но сказать об этом надо -- вызывающий это
		// делает по возвращённому false.
		return false;
	}

	// Проверяет, что DLL действительно в процессе.
	//
	// LoadLibrary возвращает handle -- это ещё не значит, что чит жив: DllMain
	// мог уйти в ранний выход, и игра осталась бы без оверлея, а лоадер
	// отрапортовал бы «готово».
	bool verify_loaded( DWORD pid, const std::wstring& dll_name )
	{
		for ( auto tick = 0; tick < 20 && !g_abort; ++tick )
		{
			if ( module_present( pid, dll_name.c_str( ) ) )
			{
				return true;
			}

			Sleep( 250 );
		}

		return false;
	}

	bool inject( DWORD pid, const std::wstring& dll, std::wstring& out_error )
	{
		const auto process = OpenProcess( PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
			PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid );

		if ( !process ) {
			out_error = L"OpenProcess: " + error_text( GetLastError( ) ) + L" (нужен админ?)";
			return false;
		}

		const auto bytes = ( dll.size( ) + 1 ) * sizeof( wchar_t );
		auto remote = VirtualAllocEx( process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );

		if ( !remote ) {
			out_error = L"VirtualAllocEx: " + error_text( GetLastError( ) );
			CloseHandle( process );
			return false;
		}

		if ( !WriteProcessMemory( process, remote, dll.c_str( ), bytes, nullptr ) ) {
			out_error = L"WriteProcessMemory: " + error_text( GetLastError( ) );
			VirtualFreeEx( process, remote, 0, MEM_RELEASE );
			CloseHandle( process );
			return false;
		}

		const auto load_library = reinterpret_cast< LPTHREAD_START_ROUTINE >(
			GetProcAddress( GetModuleHandleW( L"kernel32.dll" ), "LoadLibraryW" ) );

		if ( !load_library ) {
			out_error = L"LoadLibraryW не найден";
			VirtualFreeEx( process, remote, 0, MEM_RELEASE );
			CloseHandle( process );
			return false;
		}

		const auto thread = CreateRemoteThread( process, nullptr, 0, load_library, remote, 0, nullptr );
		if ( !thread ) {
			out_error = L"CreateRemoteThread: " + error_text( GetLastError( ) );
			VirtualFreeEx( process, remote, 0, MEM_RELEASE );
			CloseHandle( process );
			return false;
		}

		const auto wait = WaitForSingleObject( thread, 15000 );
		DWORD exit_code{};
		GetExitCodeThread( thread, &exit_code );

		CloseHandle( thread );
		VirtualFreeEx( process, remote, 0, MEM_RELEASE );
		CloseHandle( process );

		if ( wait != WAIT_OBJECT_0 ) {
			out_error = L"инжект не завершился за 15 с";
			return false;
		}

		if ( exit_code == 0 ) {
			out_error = L"LoadLibrary вернул 0 -- DLL не загрузилась";
			return false;
		}

		return true;
	}

	// -----------------------------------------------------------------------
	// Запуск Steam и CS2
	// -----------------------------------------------------------------------

	void ensure_steam( )
	{
		if ( find_process( L"steam.exe" ) ) {
			return;
		}

		set_message( L"запускаю Steam..." );
		ShellExecuteW( nullptr, L"open", L"steam://open/main", nullptr, nullptr, SW_SHOWNORMAL );

		for ( auto tick = 0; tick < 120 && !g_abort; ++tick )
		{
			Sleep( 500 );

			if ( find_process( L"steam.exe" ) )
			{
				Sleep( 2500 );
				return;
			}
		}
	}

	bool launch_game( std::wstring& out_error )
	{
		set_message( L"запускаю CS2 с -allow_third_party_software..." );

		const auto result = reinterpret_cast< INT_PTR >( ShellExecuteW( nullptr, L"open",
			L"steam://run/730//-allow_third_party_software", nullptr, nullptr, SW_SHOWNORMAL ) );

		if ( result > 32 ) {
			return true;
		}

		const auto fallback = reinterpret_cast< INT_PTR >( ShellExecuteW( nullptr, L"open",
			L"steam://rungameid/730", nullptr, nullptr, SW_SHOWNORMAL ) );

		if ( fallback > 32 ) {
			return true;
		}

		out_error = L"Steam не принял запрос на запуск (установлен ли он?)";
		return false;
	}

	// -----------------------------------------------------------------------
	// Сценарии
	// -----------------------------------------------------------------------

	bool busy( )
	{
		const auto value = static_cast< stage >( g_stage.load( ) );
		return value == stage::launching || value == stage::waiting_game || value == stage::injecting;
	}

	// Человекочитаемое объяснение, почему файл не годится. Раньше на любой
	// отказ было одно «не нашёл» -- и по нему нельзя было понять, файла нет
	// или он лежит, но помечен чужой сборкой.
	std::wstring describe_failure( )
	{
		verification::result snapshot{};
		{
			std::scoped_lock lock( g_check_mtx );
			snapshot = g_dll_check;
		}

		if ( snapshot.state == verification::status::not_found ) {
			return L"не нашёл " + std::wstring( k_dll_name ) + L" ни на одном диске";
		}

		return std::wstring( k_dll_name ) + L": "
			+ to_wide( verification::status_text( snapshot.state ) );
	}

	bool ensure_dll_located( std::wstring& out_error )
	{
		const auto quick = resolve_dll_path( );
		if ( !quick.empty( ) && file_exists( quick ) )
		{
			g_dll_path = quick;
			g_dll_present = true;
			g_dll_from_scan = false;
			return true;
		}

		set_message( L"ищу DLL по всем дискам..." );
		g_scanning = true;
		g_scan_visited = 0;

		const auto result = deep_search_dll( g_scan_visited );

		g_scanning = false;

		if ( result.empty( ) )
		{
			g_dll_present = false;
			out_error = describe_failure( );
			return false;
		}

		g_dll_path = result;
		g_dll_present = true;
		g_dll_from_scan = true;

		// Запоминаем, чтобы следующий запуск стартовал мгновенно.
		write_cache( result );
		return true;
	}

	void run_launch_and_inject( )
	{
		g_abort = false;
		g_stage = static_cast< int >( stage::launching );

		std::wstring error{};

		if ( !ensure_dll_located( error ) )
		{
			g_stage = static_cast< int >( stage::failed );
			set_message( error );
			return;
		}

		ensure_steam( );

		if ( g_abort ) {
			return;
		}

		if ( !find_process( L"cs2.exe" ) && !launch_game( error ) )
		{
			g_stage = static_cast< int >( stage::failed );
			set_message( error );
			return;
		}

		g_stage = static_cast< int >( stage::waiting_game );

		DWORD pid{};
		for ( auto tick = 0; tick < 360 && !g_abort; ++tick )
		{
			pid = find_process( L"cs2.exe" );
			if ( pid ) {
				break;
			}

			set_message( L"жду cs2.exe... " + std::to_wstring( tick / 2 ) + L" с" );
			Sleep( 500 );
		}

		if ( g_abort ) {
			return;
		}

		if ( !pid )
		{
			g_stage = static_cast< int >( stage::failed );
			set_message( L"cs2.exe так и не появился за 3 минуты" );
			return;
		}

		set_message( L"игра найдена, жду загрузки движка..." );

		const auto engine_ready = wait_for_engine( pid );

		if ( g_abort ) {
			return;
		}

		g_stage = static_cast< int >( stage::injecting );
		set_message( L"инжекчу..." );

		if ( !inject( pid, g_dll_path, error ) )
		{
			g_stage = static_cast< int >( stage::failed );
			set_message( L"инжект: " + error );
			return;
		}

		const auto dll_name = file_name_of( g_dll_path );
		const auto loaded = verify_loaded( pid, dll_name );

		g_stage = static_cast< int >( loaded ? stage::done : stage::failed );

		if ( loaded )
		{
			// Отдельно отмечаем, если движок так и не подтвердился за 3 минуты:
			// чит всё равно загружен, но если игра после этого упадёт, причина
			// уже видна здесь, а не выясняется по логу постфактум.
			set_message( L"готово -- " + dll_name + L" в игре, pid " + std::to_wstring( pid )
				+ ( engine_ready ? std::wstring{} : L" (движок не подтвердился, смотри лог)" ) );
		}
		else
		{
			set_message( L"DLL загрузилась, но в списке модулей её нет -- смотри bin\\DarkFox_init.log" );
		}
	}

	void run_inject_only( )
	{
		g_abort = false;

		std::wstring error{};

		if ( !ensure_dll_located( error ) )
		{
			g_stage = static_cast< int >( stage::failed );
			set_message( error );
			return;
		}

		const auto pid = find_process( L"cs2.exe" );
		if ( !pid )
		{
			g_stage = static_cast< int >( stage::failed );
			set_message( L"cs2.exe не запущен -- жми LAUNCH & INJECT" );
			return;
		}

		g_stage = static_cast< int >( stage::injecting );
		set_message( L"инжекчу в pid " + std::to_wstring( pid ) + L"..." );

		if ( !inject( pid, g_dll_path, error ) )
		{
			g_stage = static_cast< int >( stage::failed );
			set_message( L"инжект: " + error );
			return;
		}

		const auto dll_name = file_name_of( g_dll_path );
		const auto loaded = verify_loaded( pid, dll_name );

		g_stage = static_cast< int >( loaded ? stage::done : stage::failed );
		set_message( loaded
			? L"готово -- " + dll_name + L" в игре, pid " + std::to_wstring( pid )
			: L"DLL загрузилась, но в списке модулей её нет -- смотри bin\\DarkFox_init.log" );
	}

	void start( bool launch_first )
	{
		if ( busy( ) ) {
			return;
		}

		if ( g_worker.joinable( ) ) {
			g_worker.join( );
		}

		g_worker_done = false;

		const auto entry = launch_first ? run_launch_and_inject : run_inject_only;
		g_worker = std::thread( [ entry ]( )
			{
				entry( );
				g_worker_done = true;
			} );
	}

	void refresh_status( )
	{
		g_steam_running = find_process( L"steam.exe" ) != 0;
		g_target_pid = find_process( L"cs2.exe" );
		g_is_admin = is_elevated( );
	}

	// -----------------------------------------------------------------------
	// D3D11
	// -----------------------------------------------------------------------

	// Создание swap chain. Сначала пробуем FLIP_DISCARD, и только если он
	// не поддержан -- откатываемся на старый DISCARD.
	//
	// Почему не DISCARD по умолчанию: это bitblt-модель, и на современных
	// Windows она не презентует -- Present() возвращает DXGI_STATUS_OCCLUDED
	// (0x087A0001) на каждом кадре, окно остаётся пустым (белым), хотя
	// устройство и render target созданы успешно. Проверено минимальным
	// тестом: тот же код на DISCARD даёт OCCLUDED, на FLIP_DISCARD -- S_OK.
	// FLIP_DISCARD -- это ещё и штатный путь для DWM: кадр не копируется
	// через GDI, а отдаётся композитору напрямую.
	bool create_device( HWND window )
	{
		const auto build = [ & ]( DXGI_SWAP_EFFECT effect ) -> HRESULT
		{
			DXGI_SWAP_CHAIN_DESC desc{};
			// FLIP-модель требует минимум два буфера.
			desc.BufferCount = 2;
			desc.BufferDesc.Width = k_window_w;
			desc.BufferDesc.Height = k_window_h;
			desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.BufferDesc.RefreshRate.Numerator = 60;
			desc.BufferDesc.RefreshRate.Denominator = 1;
			desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			desc.OutputWindow = window;
			// FLIP-модель не поддерживает MSAA у swap chain.
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Windowed = TRUE;
			desc.SwapEffect = effect;

			D3D_FEATURE_LEVEL level{};
			const D3D_FEATURE_LEVEL levels[ ] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

			return D3D11CreateDeviceAndSwapChain(
				nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
				levels, 2, D3D11_SDK_VERSION,
				&desc, &g_swap_chain, &g_device, &level, &g_context );
		};

		auto hr = build( DXGI_SWAP_EFFECT_FLIP_DISCARD );

		if ( FAILED( hr ) )
		{
			// Старые системы без flip-модели.
			g_swap_chain.Reset( );
			g_device.Reset( );
			g_context.Reset( );
			hr = build( DXGI_SWAP_EFFECT_DISCARD );
		}

		if ( FAILED( hr ) ) {
			return false;
		}

		ComPtr< ID3D11Texture2D > back_buffer{};
		if ( FAILED( g_swap_chain->GetBuffer( 0, IID_PPV_ARGS( &back_buffer ) ) ) ) {
			return false;
		}

		return SUCCEEDED( g_device->CreateRenderTargetView( back_buffer.Get( ), nullptr, &g_render_target ) );
	}

	void destroy_render_target( )
	{
		g_render_target.Reset( );
	}

	void create_render_target( )
	{
		ComPtr< ID3D11Texture2D > back_buffer{};
		if ( FAILED( g_swap_chain->GetBuffer( 0, IID_PPV_ARGS( &back_buffer ) ) ) ) {
			return;
		}
		g_device->CreateRenderTargetView( back_buffer.Get( ), nullptr, &g_render_target );
	}

	void render_ui( xdraw::color accent ); // определение во втором anon-namespace ниже

	// Ограничитель частоты кадров (~60 fps), чтобы цикл не жёг CPU вхолостую.
	void frame_pace( )
	{
		static ULONGLONG last{};
		constexpr ULONGLONG target_ms{ 16 };

		const auto now = GetTickCount64( );
		const auto spent = now - last;

		if ( last != 0 && spent < target_ms ) {
			Sleep( static_cast< DWORD >( target_ms - spent ) );
		}

		last = GetTickCount64( );
	}

	void render_frame( )
	{
		if ( !g_render_target ) {
			return;
		}

		// Фон: тёмный, чуть светлее к верху. Заливаем сами -- xui рисует
		// поверх, но чистый фон ему не принадлежит.
		const auto accent = k_dev_build
			? xdraw::color{ 255, 190, 96, 255 }
			: xdraw::color{ 198, 128, 240, 255 };

		g_context->OMSetRenderTargets( 1, g_render_target.GetAddressOf( ), nullptr );

		D3D11_VIEWPORT viewport{};
		viewport.Width = static_cast< float >( k_window_w );
		viewport.Height = static_cast< float >( k_window_h );
		viewport.MaxDepth = 1.0f;
		g_context->RSSetViewports( 1, &viewport );

		const float clear[ 4 ] = { 10.0f / 255.0f, 11.0f / 255.0f, 14.0f / 255.0f, 1.0f };
		g_context->ClearRenderTargetView( g_render_target.Get( ), clear );

		// Кадр xdraw/xui. begin_frame сбрасывает списки отрисовки, end_frame
		// их проигрывает. Present вызывается РОВНО ОДИН раз на кадр --
		// именно это убирает моргание, которое было в GDI-версии.
		xdraw::begin_frame( );
		xui::begin( );

		render_ui( accent );

		xui::end( );
		xdraw::end_frame( );

		// Present(0, 0) -- БЕЗ вертикальной синхронизации.
		// Present(1, ...) блокирует поток до кадрового гашения, и когда
		// активного vsync-источника нет, он не возвращается вовсе: насос
		// сообщений встаёт, окно перестаёт реагировать на закрытие.
		// Частоту кадров держим сами -- frame_pace().
		//
		// Возврат DXGI_STATUS_OCCLUDED (0x087A0001) -- это НЕ ошибка: окно
		// перекрыто, композитор пропустил кадр. Код успеха (severity 0),
		// поэтому его не проверяем и продолжаем рисовать.
		g_swap_chain->Present( 0, 0 );
	}

} // namespace

// ---------------------------------------------------------------------------
// UI на xui
// ---------------------------------------------------------------------------

namespace {

	void draw_card( const char* title, const std::string& detail, xdraw::color tint, float w )
	{
		auto& dl = xui::draw::current( );

		const auto pos = xui::layout::item( w, 46.0f );

		dl.rect_filled( pos.x + 1.0f, pos.y + 3.0f, pos.w, pos.h, xdraw::color{ 0, 0, 0, 70 }, 10.0f );

		dl.rect_filled( pos.x, pos.y, pos.w, pos.h, xdraw::color{ 18, 18, 22, 235 }, 10.0f );
		dl.rect( pos.x, pos.y, pos.w, pos.h, xdraw::color{ 255, 255, 255, 26 }, xdraw::corner_radius{ 10.0f }, 1.0f );

		// Цветная засечка слева: статус читается без чтения текста.
		dl.rect_filled( pos.x + 1.0f, pos.y + 8.0f, 3.0f, pos.h - 16.0f, tint, 1.5f );

		dl.circle_filled( pos.x + 24.0f, pos.y + pos.h * 0.5f, 4.5f, tint );

		dl.text( pos.x + 44.0f, pos.y + pos.h * 0.5f - 7.5f, title, xdraw::color{ 244, 246, 250, 255 }, g_font_regular );

		const auto detail_w = xdraw::measure_text( detail, g_font_regular ).first;
		dl.text( pos.x + pos.w - detail_w - 18.0f, pos.y + pos.h * 0.5f - 6.5f, detail, tint, g_font_small );
	}

	void render_ui( xdraw::color accent )
	{
		auto& dl = xui::draw::current( );

		float x = 0.0f;
		float y = 0.0f;
		float w = static_cast< float >( k_window_w );
		float h = static_cast< float >( k_window_h );

		if ( !xui::begin_window( "##loader", x, y, w, h, false, 400.0f, 300.0f ) ) {
			return;
		}

		const auto current = static_cast< stage >( g_stage.load( ) );

		// --- шапка ---------------------------------------------------------
		{
			const auto header_h = 74.0f;

			dl.rect_filled( 0.0f, 0.0f, w, header_h, xdraw::color{ 14, 14, 18, 245 } );
			dl.rect_filled( 0.0f, header_h - 1.0f, w, 1.0f, xdraw::color{ 255, 255, 255, 20 } );

			// Акцентная полоса сверху: тонкая, но сразу опознаёт сборку.
			dl.rect_filled( 0.0f, 0.0f, w, 2.0f, accent );

			dl.text( 22.0f, 18.0f, "DARKFOX", xdraw::color{ 244, 246, 250, 255 }, g_font_bold );
			dl.text( 22.0f, 48.0f, "loader", xdraw::color{ 140, 144, 156, 255 }, g_font_small );

			if ( k_dev_build )
			{
				const auto name_w = xdraw::measure_text( "DARKFOX", g_font_bold ).first;
				const auto badge_x = 22.0f + name_w + 10.0f;

				dl.rect_filled( badge_x, 20.0f, 42.0f, 19.0f, accent.alpha( 42 ), 9.0f );
				dl.rect( badge_x, 20.0f, 42.0f, 19.0f, accent.alpha( 120 ), xdraw::corner_radius{ 9.0f }, 1.0f );
				dl.text( badge_x + 11.0f, 23.0f, "DEV", accent, g_font_small );
			}

			// Статус-пилюля
			{
				const char* label = "READY";
				auto color = xdraw::color{ 104, 109, 122, 255 };

				switch ( current )
				{
				case stage::launching:    label = "LAUNCHING"; color = accent; break;
				case stage::waiting_game: label = "WAITING";   color = accent; break;
				case stage::injecting:    label = "INJECTING"; color = accent; break;
				case stage::done:         label = "INJECTED";  color = xdraw::color{ 122, 224, 160, 255 }; break;
				case stage::failed:       label = "FAILED";    color = xdraw::color{ 255, 108, 116, 255 }; break;
				default:                  break;
				}

				// Пульс: сглаженная величина через xui::anim плюс дыхание синусом.
				const auto working = current == stage::launching || current == stage::waiting_game || current == stage::injecting;
				const auto pulse = xui::anim::smooth( xui::make_id( "##pulse" ), working ? 1.0f : 0.0f, 6.0f );
				const auto breathe = 0.5f + 0.5f * std::sinf( static_cast< float >( GetTickCount64( ) % 1600 ) / 1600.0f * 6.2831853f );

				const auto pill_w = 116.0f;
				const auto pill_x = w - pill_w - 22.0f;
				const auto pill_y = 24.0f;
				const auto pill_h = 28.0f;

				if ( pulse > 0.01f ) {
					dl.rect_filled( pill_x - 3.0f, pill_y - 3.0f, pill_w + 6.0f, pill_h + 6.0f,
						color.alpha( static_cast< std::uint8_t >( 26.0f * ( 0.5f + 0.5f * breathe ) * pulse ) ), 17.0f );
				}

				dl.rect_filled( pill_x, pill_y, pill_w, pill_h, color.alpha( 30 ), 14.0f );
				dl.rect( pill_x, pill_y, pill_w, pill_h,
					color.alpha( static_cast< std::uint8_t >( 80.0f + 80.0f * breathe * pulse ) ), xdraw::corner_radius{ 14.0f }, 1.0f );

				dl.circle_filled( pill_x + 18.0f, pill_y + pill_h * 0.5f, 3.0f + 1.5f * breathe * pulse,
					color.alpha( static_cast< std::uint8_t >( 180.0f + 75.0f * breathe * pulse ) ) );

				dl.text( pill_x + 32.0f, pill_y + pill_h * 0.5f - 6.5f, label, color, g_font_small );
			}
		}

		// --- карточки ------------------------------------------------------
		{
			xui::layout::set_cursor( 22.0f, 96.0f );

			const auto card_w = w - 44.0f;

			const auto steam = g_steam_running
				? std::pair{ std::string( "запущен" ), xdraw::color{ 122, 224, 160, 255 } }
				: std::pair{ std::string( "не запущен" ), xdraw::color{ 104, 109, 122, 255 } };
			draw_card( "Steam", steam.first, steam.second, card_w );

			const auto cs2_detail = g_target_pid ? ( "pid " + std::to_string( g_target_pid ) ) : std::string( "не запущена" );
			draw_card( "Counter-Strike 2", cs2_detail,
				g_target_pid ? xdraw::color{ 122, 224, 160, 255 } : xdraw::color{ 104, 109, 122, 255 }, card_w );

			std::string dll_detail;
			auto dll_color = xdraw::color{ 255, 190, 96, 255 };

			if ( g_scanning ) {
				dll_detail = "поиск... " + std::to_string( g_scan_visited.load( ) );
				dll_color = accent;
			}
			else if ( g_dll_present )
			{
				verification::result check{};
				{
					std::scoped_lock lock( g_check_mtx );
					check = g_dll_check;
				}

				// Хеш не сошёлся -- это не отказ, но пользователь обязан
				// увидеть, что файл не совпадает с записанным эталоном.
				if ( check.hash == verification::result::hash_state::mismatch ) {
					dll_detail = "хеш не сошёлся";
					dll_color = xdraw::color{ 255, 120, 120, 255 };
				}
				else if ( check.hash == verification::result::hash_state::match ) {
					dll_detail = "подлинность ok";
					dll_color = xdraw::color{ 122, 224, 160, 255 };
				}
				else {
					dll_detail = g_dll_from_scan ? "найдена обходом" : "найдена";
					dll_color = xdraw::color{ 122, 224, 160, 255 };
				}
			}
			else {
				verification::result check{};
				{
					std::scoped_lock lock( g_check_mtx );
					check = g_dll_check;
				}

				dll_detail = check.state == verification::status::not_found
					? "не найдена"
					: to_utf8( to_wide( verification::status_text( check.state ) ) );
				dll_color = xdraw::color{ 255, 120, 120, 255 };
			}

			draw_card( to_utf8( k_dll_name ).c_str( ), dll_detail, dll_color, card_w );

			// Вторая строка под карточкой DLL: полный путь и разбор маркера.
			// Когда файл найден по обходу дисков, путь критичен -- непонятно,
			// какой именно из копий лоадер выбрал.
			if ( g_dll_present && !g_dll_path.empty( ) )
			{
				verification::result check{};
				{
					std::scoped_lock lock( g_check_mtx );
					check = g_dll_check;
				}

				auto info = to_utf8( g_dll_path );

				if ( check.has_marker )
				{
					info += "  [" + to_utf8( to_wide( check.build ) )
						+ ", build " + std::to_string( check.build_id ) + "]";

					if ( check.size ) {
						info += "  " + std::to_string( check.size / ( 1024 * 1024 ) ) + " МБ";
					}
				}

				const auto pos = xui::layout::item( card_w, 18.0f );
				dl.text( pos.x + 4.0f, pos.y, info.c_str( ),
					xdraw::color{ 140, 144, 156, 255 }, g_font_small );
			}

			draw_card( "права администратора",
				g_is_admin ? "полные" : "нет -- UAC",
				g_is_admin ? xdraw::color{ 122, 224, 160, 255 } : xdraw::color{ 255, 190, 96, 255 }, card_w );
		}

		// --- полоса прогресса обхода ---------------------------------------
		if ( g_scanning )
		{
			auto cursor = xui::layout::get_cursor( );
			const auto bar_x = cursor.first;
			const auto bar_y = cursor.second + 6.0f;
			const auto bar_w = w - 44.0f;

			dl.rect_filled( bar_x, bar_y, bar_w, 5.0f, xdraw::color{ 255, 255, 255, 14 }, 2.5f );

			const auto visited = static_cast< float >( g_scan_visited.load( ) );
			const auto ratio = 1.0f - 1.0f / ( 1.0f + visited / 900.0f );

			dl.rect_filled( bar_x, bar_y, bar_w * ratio, 5.0f, accent, 2.5f );
		}

		// --- кнопки --------------------------------------------------------
		{
			const auto running = busy( );
			const auto ready = g_dll_present && !g_scanning;

			const auto button_y = h - 78.0f;
			xui::layout::set_cursor( 22.0f, button_y );

			const auto launch_w = w - 44.0f - 138.0f;

			xui::anim::set( xui::make_id( "##launch_en" ), ready && !running ? 1.0f : 0.0f );

			if ( xui::button( running ? "РАБОТАЮ..." : "LAUNCH & INJECT", launch_w, 42.0f ) && ready && !running ) {
				start( true );
			}

			// same_line(offset) ПРИБАВЛЯЕТ offset к правому краю предыдущего
			// элемента, а не задаёт абсолютную координату -- поэтому здесь
			// просто отступ, а не launch_w + 12 (из-за этого кнопка INJECT
			// уезжала за пределы окна и её не было видно).
			xui::layout::same_line( 12.0f );

			if ( xui::button( "INJECT", 126.0f, 42.0f ) && ready && !running ) {
				start( false );
			}
		}

		// --- строка сообщения ----------------------------------------------
		{
			const auto message = get_message( );
			if ( !message.empty( ) )
			{
				const auto color = current == stage::failed
					? xdraw::color{ 255, 108, 116, 255 }
					: ( current == stage::done ? xdraw::color{ 122, 224, 160, 255 } : xdraw::color{ 150, 152, 160, 255 } );

				const auto text = xui::truncate( to_utf8( message ), w - 44.0f );
				dl.text( 22.0f, h - 30.0f, text, color, g_font_small );
			}
		}

		xui::end_window( );
	}

	// -----------------------------------------------------------------------
	// Оконная процедура
	// -----------------------------------------------------------------------

	LRESULT CALLBACK window_proc( HWND window, UINT msg, WPARAM wp, LPARAM lp )
	{
		// Ввод отдаём xui: он сам разберёт клики, скролл и клавиши.
		if ( xui::wndproc( msg, wp, lp ) ) {
			return 0;
		}

		switch ( msg )
		{
		case WM_CREATE:
		{
			BOOL dark = TRUE;
			DwmSetWindowAttribute( window, 20, &dark, sizeof( dark ) );
			DwmSetWindowAttribute( window, 19, &dark, sizeof( dark ) );

			enum : DWORD { k_dwm_window_corner_preference = 33 };
			DWORD corner{ 2 };
			DwmSetWindowAttribute( window, k_dwm_window_corner_preference, &corner, sizeof( corner ) );

			std::wstring error{};
			g_shortcut_ok = create_shortcut( error );
			g_shortcut_note = g_shortcut_ok ? L"" : error;

			// resolve_dll_path возвращает путь ТОЛЬКО для проверенного файла
			// (PE x64 DLL + маркер). Пустая строка = годного файла нет, и
			// отдельный file_exists тут не нужен: он бы отметил «найдена»
			// для файла, который на самом деле отвергнут проверкой.
			g_dll_path = resolve_dll_path( );
			g_dll_present = !g_dll_path.empty( );
			g_dll_from_scan = false;

			refresh_status( );

			if ( g_dll_present ) {
				set_message( g_shortcut_ok ? L"готов -- ярлык на рабочем столе" : L"ярлык: " + g_shortcut_note );
			}
			else {
				set_message( L"DLL не найдена рядом -- ищу по всем дискам..." );
			}

			return 0;
		}

		case WM_SIZE:
			// Буфер под размер окна: при перетаскивании за край swapchain
			// надо пересоздать, иначе картинка растянется.
			// destroy_render_target() обязателен ДО ResizeBuffers: у
			// flip-модели нельзя менять буферы, пока на back buffer есть
			// живые ссылки (RTV), -- ResizeBuffers вернёт
			// DXGI_ERROR_INVALID_CALL и кадр перестанет выводиться.
			if ( g_swap_chain && wp != SIZE_MINIMIZED )
			{
				destroy_render_target( );
				g_swap_chain->ResizeBuffers( 0, LOWORD( lp ), HIWORD( lp ), DXGI_FORMAT_UNKNOWN, 0 );
				create_render_target( );
			}
			return 0;

		case WM_KEYDOWN:
			if ( wp == VK_RETURN ) {
				start( true );
			}
			else if ( wp == VK_ESCAPE ) {
				// Не DestroyWindow: цикл сам завершится по флагу и сделает
				// ExitProcess -- иначе рискуем рисовать в мёртвый HWND.
				g_abort = true;
				g_running = false;
				PostMessageW( window, WM_NULL, 0, 0 );
			}
			return 0;

		case WM_PAINT:
		{
			// Окно рисует D3D, а не GDI. WM_PAINT нельзя обрабатывать
			// через DefWindowProc: тот зальёт клиентскую область фоном
			// (hbrBackground = nullptr, то есть ЧЁРНЫМ), отсюда чёрные
			// полосы поверх кадра. Просто помечаем регион обработанным --
			// ближайший виток основного цикла нарисует свежий кадр.
			// ВАЖНО: кадр здесь НЕ рисуем. Вызов render_frame() из
			// WM_PAINT вложен в насос сообщений и ведёт к клинчу
			// (окно перестаёт обрабатывать WM_CLOSE).
			PAINTSTRUCT ps{};
			BeginPaint( window, &ps );
			EndPaint( window, &ps );
			return 0;
		}

		case WM_ERASEBKGND:
			// Мерцание стирания не нужно: кадр и так перекрывает всё.
			return 1;

		case WM_SYSCOMMAND:
			// Крестик в заголовке и Alt+F4 приходят именно сюда.
			if ( ( wp & 0xFFF0 ) == SC_CLOSE ) {
				g_abort = true;
				g_running = false;
				PostMessageW( window, WM_NULL, 0, 0 );
				return 0;
			}
			break;

		case WM_CLOSE:
			// Закрытие окна. DestroyWindow здесь НЕ вызываем: он сносит
			// окно прямо из обработки сообщения, а цикл продолжает рисовать
			// в уже мёртвый HWND. Достаточно снять флаг -- цикл завершится
			// на ближайшем витке.
			g_abort = true;
			g_running = false;
			PostMessageW( window, WM_NULL, 0, 0 );
			return 0;

		case WM_DESTROY:
			g_abort = true;
			g_running = false;
			// Воркер может висеть в Sleep/скане диска -- join() тут легко
			// залипает. Процесс всё равно завершается, поэтому detach.
			if ( g_worker.joinable( ) ) {
				g_worker.detach( );
			}
			PostQuitMessage( 0 );
			return 0;
		}

		return DefWindowProcW( window, msg, wp, lp );
	}

} // namespace

int WINAPI wWinMain( HINSTANCE instance, HINSTANCE, LPWSTR command_line, int )
{
	// Манифест обычно уже поднял права; это страховка на случай, если
	// ресурс потерялся при пересборке.
	const auto already_elevated_attempt = command_line && wcsstr( command_line, L"--elevated" ) != nullptr;

	if ( !is_elevated( ) && !already_elevated_attempt )
	{
		if ( relaunch_elevated( ) ) {
			return 0;
		}
	}

	CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED );

	WNDCLASSEXW window_class{};
	window_class.cbSize = sizeof( window_class );
	window_class.lpfnWndProc = window_proc;
	window_class.hInstance = instance;
	window_class.hCursor = LoadCursorW( nullptr, IDC_ARROW );
	window_class.lpszClassName = L"DarkFoxLoader";

	const auto icon = LoadIconW( instance, MAKEINTRESOURCEW( 1 ) );
	window_class.hIcon = icon;
	window_class.hIconSm = icon;

	// Фон окна не закрашиваем: его полностью закрывает кадр D3D.
	window_class.hbrBackground = nullptr;

	RegisterClassExW( &window_class );

	RECT desired{ 0, 0, k_window_w, k_window_h };
	const auto style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	AdjustWindowRect( &desired, style, FALSE );

	g_window = CreateWindowExW( 0, window_class.lpszClassName, k_window_title,
		style, CW_USEDEFAULT, CW_USEDEFAULT,
		desired.right - desired.left, desired.bottom - desired.top,
		nullptr, nullptr, instance, nullptr );

	if ( !g_window )
	{
		CoUninitialize( );
		return 1;
	}

	if ( !create_device( g_window ) )
	{
		MessageBoxW( nullptr, L"не удалось создать D3D11 устройство", k_window_title, MB_ICONERROR );
		CoUninitialize( );
		return 1;
	}

	if ( !xdraw::initialize( g_device.Get( ), g_context.Get( ) ) )
	{
		MessageBoxW( nullptr, L"не удалось инициализировать xdraw", k_window_title, MB_ICONERROR );
		CoUninitialize( );
		return 1;
	}

	g_font_regular = xdraw::load_font( std::as_bytes( std::span{ resources::fonts::inter::regular } ), 15.0f );
	g_font_bold = xdraw::load_font( std::as_bytes( std::span{ resources::fonts::inter::bold } ), 22.0f );
	g_font_small = xdraw::load_font( std::as_bytes( std::span{ resources::fonts::inter::regular } ), 12.5f );

	if ( !g_font_regular || !g_font_bold || !g_font_small )
	{
		MessageBoxW( nullptr, L"не удалось загрузить шрифты", k_window_title, MB_ICONERROR );
		CoUninitialize( );
		return 1;
	}

	xdraw::push_font( g_font_regular );

	ShowWindow( g_window, SW_SHOW );
	UpdateWindow( g_window );

	// Основной цикл. Ключевой момент против моргания: PeekMessage вместо
	// GetMessage. GetMessage СПИТ, когда очередь пуста, -- кадр не рисуется,
	// WM_PAINT не обрабатывается, и окно показывает мусор до следующего
	// сообщения (мышь/таймер). Именно так лоадер "моргал".
	// PeekMessage не блокирует: каждый проход цикла гарантированно доходит
	// до render_frame, поэтому на экране всегда целый свежий кадр.
	MSG message{};
	while ( g_running )
	{
		while ( PeekMessageW( &message, nullptr, 0, 0, PM_REMOVE ) )
		{
			if ( message.message == WM_QUIT ) {
				g_running = false;
				break;
			}

			TranslateMessage( &message );
			DispatchMessageW( &message );
		}

		if ( !g_running ) {
			break;
		}

		render_frame( );
		frame_pace( );
	}

	xdraw::pop_font( );
	CoUninitialize( );

	// Явный выход. Воркер детачнут и может быть жив, статики (std::thread,
	// ComPtr) разрушать некому. Loader -- одноразовая утилита, корректный
	// teardown ей не нужен, а зависнуть на нём легко.
	ExitProcess( 0 );
}
