// DarkFox loader.
//
// Отдельный x64 EXE:
//   * при запуске ставит ярлык на самого себя на рабочий стол;
//   * LAUNCH & INJECT -- поднимает Steam, запускает CS2 с
//     -allow_third_party_software (то самое "allow third party software",
//     которое иначе приходится подтверждать руками), ждёт процесс и вливает DLL;
//   * INJECT -- влить в уже запущенную игру.
//
// Собирать только x64: cs2.exe 64-битная, 32-битный LoadLibrary туда не встанет.
// Интерфейс на GDI+: нужен антиалиасинг на скруглениях, обычный GDI его не умеет.

#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#include <gdiplus.h>

#include <atomic>
#include <cmath>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment( lib, "ole32.lib" )
#pragma comment( lib, "shell32.lib" )
#pragma comment( lib, "shlwapi.lib" )
#pragma comment( lib, "dwmapi.lib" )
#pragma comment( lib, "gdiplus.lib" )

using namespace Gdiplus;

namespace {

	// -----------------------------------------------------------------------
	// Что именно инжектим
	//
	// Имя DLL задаётся на этапе сборки: из одного исходника получаются два
	// лоадера -- обычный (DarkFox.dll) и дев (DarkFox-dev.dll). Дев-сборка
	// помечается оранжевым акцентом и бейджем, чтобы их не путать на глаз.
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
	// диск не тащил за собой устаревший путь. Формат -- одна строка UTF-8 с
	// полным путём к DLL; при промахе файл молча игнорируется и перезаписывается.
	//
	// Имя кэша ОБЯЗАНО зависеть от сборки. Раньше оно было общим
	// ("DarkFox.loader.cache"), и релиз с девом затирали друг другу путь:
	// релизный лоадер клал туда D:\...\DarkFox.dll, после чего дев-лоадер
	// читал кэш, видел существующий файл и инжектил ОБЫЧНУЮ версию.
	constexpr auto k_cache_suffix{ L".loader.cache" };

	// Маска поиска по дискам. Шире, чем точное имя: ловит и
	// DarkFox.dll, и DarkFox-dev.dll, и копии с суффиксами вида
	// DarkFox.dll.bak_20260919 -- последние отсеиваются проверкой расширения.
	constexpr auto k_search_pattern{ L"DarkFox*.dll" };

	// -----------------------------------------------------------------------
	// Геометрия и палитра
	// -----------------------------------------------------------------------

	constexpr auto k_window_w{ 660 };
	constexpr auto k_window_h{ 520 };

	constexpr auto k_header_h{ 96.0f };
	constexpr auto k_card_h{ 58.0f };
	constexpr auto k_card_gap{ 9.0f };
	constexpr auto k_card_top{ 116.0f };
	constexpr auto k_margin{ 24.0f };
	constexpr auto k_button_h{ 50.0f };

	inline Color rgba( BYTE r, BYTE g, BYTE b, BYTE a = 255 )
	{
		return Color( a, r, g, b );
	}

	// Палитра из theme.hpp проекта, чтобы лоадер и меню читались как одно.
	const auto c_background = rgba( 10, 11, 14 );
	const auto c_surface = rgba( 16, 17, 21 );
	const auto c_raised = rgba( 24, 26, 31 );
	const auto c_hover = rgba( 33, 36, 43 );
	const auto c_border = rgba( 255, 255, 255, 20 );
	const auto c_text = rgba( 244, 246, 250 );
	const auto c_dim = rgba( 154, 159, 172 );
	const auto c_muted = rgba( 104, 109, 122 );

	// Дев-сборка -- янтарный акцент вместо фиолетового.
	const auto c_accent = k_dev_build ? rgba( 255, 190, 96 ) : rgba( 198, 128, 240 );
	const auto c_accent_press = k_dev_build ? rgba( 232, 152, 70 ) : rgba( 168, 104, 210 );
	const auto c_accent_soft = k_dev_build ? rgba( 255, 216, 140 ) : rgba( 240, 130, 208 );

	const auto c_ok = rgba( 122, 224, 160 );
	const auto c_warn = rgba( 255, 190, 96 );
	const auto c_err = rgba( 255, 108, 116 );

	// -----------------------------------------------------------------------
	// Состояние
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

	// Кадровый буфер. Раньше кадр рисовался ПРЯМО в DC окна, и любая
	// частичная перерисовка (обрезка по invalid-региону, перекрытие окна,
	// медленная отрисовка градиентов) оставляла на экране куски прошлого
	// кадра -- это и было моргание. Теперь кадр собирается в памяти и
	// показывается одним BitBlt, промежуточных состояний на экране нет.
	HDC     g_back_dc{};
	HBITMAP g_back_bmp{};
	HBITMAP g_back_old{};
	int     g_back_w{};
	int     g_back_h{};

	RectF g_launch_button{};
	RectF g_inject_button{};

	int g_hovered{ -1 };   // 0 -- launch, 1 -- inject, -1 -- ничего
	int g_pressed{ -1 };

	std::wstring g_dll_path{};
	bool         g_dll_present{};
	std::wstring g_dll_source{};      // откуда взялся путь: кэш, рядом, обход
	bool         g_dll_from_scan{};   // путь получен полным обходом дисков
	bool         g_scanning{};
	std::wstring g_scan_where{};
	std::atomic<int> g_scan_visited{};
	bool         g_shortcut_ok{};
	std::wstring g_shortcut_note{};
	bool         g_steam_running{};
	bool         g_is_admin{};
	DWORD        g_target_pid{};

	// Анимация: фаза для пульсации статус-пилюли и сглаженный hover.
	std::atomic<int> g_anim_phase{};
	int              g_hover_mix[ 2 ]{};

	std::atomic<int>  g_stage{ static_cast< int >( stage::idle ) };
	std::atomic<bool> g_abort{ false };
	std::mutex        g_message_mtx{};
	std::wstring      g_message{};
	std::thread       g_worker{};

	ULONG_PTR g_gdiplus_token{};

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

	void request_repaint( )
	{
		if ( g_window ) {
			PostMessageW( g_window, WM_APP + 1, 0, 0 );
		}
	}

	// -----------------------------------------------------------------------
	// Поиск DLL
	//
	// Порядок ровно такой, и он важен:
	//   1. кэш найденного пути -- мгновенно, если файл на месте;
	//   2. ближние кандидаты (папка EXE, ..\bin) -- дешёвый скан без обхода;
	//   3. фоновый обход ВСЕХ локальных дисков по маске DarkFox*.dll.
	// Шаги 1-2 выполняются до показа окна, поэтому старт остаётся быстрым,
	// даже если шаг 3 занимает десятки секунд на забитом диске.
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

		// Путь пишется в UTF-8: русские имена папок в системе не редкость,
		// а кэш должен переживать смену кодовой страницы консоли.
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

	// Каталоги, которые заведомо не содержат нашу DLL и обход которых стоит
	// десятки секунд: системные, служебные и снапшоты томов. Без этого фильтра
	// поиск по C: влезает в минуты.
	bool is_skipped_directory( const std::wstring& full, const std::wstring& name )
	{
		static const wchar_t* const skipped[ ] = {
			L"Windows", L"WinSxS", L"$Recycle.Bin", L"System Volume Information",
			L"$WinREAgent", L"$SysReset", L"Recovery", L"PerfLogs",
			L"node_modules", L".git", L".svn", L"__pycache__",
			L"Installer", L"assembly", L"Microsoft", L"WindowsPowerShell",
			L"DriverStore", L"catroot", L"catroot2", L"Logs",
		};

		for ( const auto* skip : skipped )
		{
			if ( _wcsicmp( name.c_str( ), skip ) == 0 ) {
				return true;
			}
		}

		// vcpkg_installed и промежуточные каталоги сборки -- сотни тысяч файлов.
		if ( _wcsnicmp( name.c_str( ), L"vcpkg_installed", 16 ) == 0
			|| _wcsnicmp( name.c_str( ), L"intermediates", 13 ) == 0
			|| _wcsnicmp( name.c_str( ), L"$", 1 ) == 0 )
		{
			return true;
		}

		( void )full;
		return false;
	}

	// Ширина в глубину по каталогам с ОГРАНИЧЕНИЕМ глубины и собственным
	// стеком: рекурсия на глубоком дереве Windows рискует переполнить стек
	// рабочего потока, а max_depth отсекает бесконечные цепочки junction'ов.
	void scan_directory( const std::wstring& root, int max_depth,
		const std::function< bool( const std::wstring& ) >& on_file,
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

			if ( ++visited % 64 == 0 ) {
				request_repaint( );
			}

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
					// Junction/symlink не разворачиваем: они дают бесконечные
					// циклы (например, Documents and Settings -> Users).
					if ( data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ) {
						continue;
					}

					if ( is_skipped_directory( full, name ) ) {
						continue;
					}

					stack.push_back( { full, current.depth + 1 } );
					continue;
				}

				if ( !( data.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE ) && !( data.dwFileAttributes & FILE_ATTRIBUTE_NORMAL ) ) {
					// Всё, что не обычный файл, для нас бесполезно (device, offline).
					if ( data.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE ) {
						continue;
					}
				}

				if ( on_file( full ) ) {
					return;
				}

			} while ( FindNextFileW( find, &data ) );

			FindClose( find );
		}
	}

	// Короткое имя без каталога: нужно везде, где сравниваем с k_dll_name.
	std::wstring file_name_of( const std::wstring& path )
	{
		const auto slash = path.find_last_of( L"\\/" );
		return slash == std::wstring::npos ? path : path.substr( slash + 1 );
	}

	// Точное совпадение имени -- обязательное условие. Лоадеру НУЖНА именно
	// его DLL: обычная сборка инжектит DarkFox.dll, дев -- DarkFox-dev.dll.
	// Соблазн подставить "похожий" файл при промахе надо гасить: инжект
	// чужой сборки тихо ломает игру, и диагностировать это крайне неприятно.
	// Поэтому fallback ниже ограничен вариантами ТОГО ЖЕ семейства.
	bool is_exact_match( const std::wstring& path )
	{
		return _wcsicmp( file_name_of( path ).c_str( ), k_dll_name ) == 0;
	}

	// Допустимая замена при отсутствии точного совпадения: та же сборка,
	// но с суффиксом копии, который проставляет наш же бэкап-скрипт.
	// Пример: DarkFox-dev.dll.bak_20260919_143051 -> всё ещё наша сборка.
	// Проверяем префикс ИМЕНИ ФАЙЛА, а не вхождение подстроки где-то в пути.
	bool is_same_family( const std::wstring& path )
	{
		const auto name = file_name_of( path );
		const auto family = std::wstring( k_dll_name );

		if ( name.size( ) <= family.size( ) ) {
			return false;
		}

		return _wcsnicmp( name.c_str( ), family.c_str( ), family.size( ) ) == 0;
	}

	// Свежесть файла: у цели несколько копий (бэкапы, старые сборки), и брать
	// надо самую новую. GetFileTime в 100-нс интервалах, сравниваем как ULARGE_INTEGER.
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

	// Локальные фиксированные диски. Сетевые и съёмные пропускаем: обход
	// сетевого диска без ответа сервера вешает поток на минуты.
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

	std::wstring resolve_dll_path( )
	{
		const auto directory = exe_directory( );
		if ( directory.empty( ) ) {
			return {};
		}

		// 1. Кэш -- самый быстрый путь и он же подтверждает прошлый поиск.
		// Проверяем не только существование файла, но и то, что это НАША
		// сборка: кэш мог остаться от другого варианта лоадера или от
		// скопированного руками файла. Без этой проверки дев-лоадер
		// инжектил обычную версию.
		const auto cached = read_cache( );
		if ( !cached.empty( ) && file_exists( cached )
			&& ( is_exact_match( cached ) || is_same_family( cached ) ) ) {
			return cached;
		}

		// 2. Ближние кандидаты. Их проверяем до окна: лоадер лежит в bin\
		//    рядом с DLL в 99% случаев, и полный обход тут не нужен.
		const std::wstring local[ ] = {
			directory + L"\\" + k_dll_name,
			directory + L"\\..\\bin\\" + k_dll_name,
			directory + L"\\..\\" + k_dll_name,
		};

		for ( const auto& candidate : local )
		{
			if ( file_exists( candidate ) ) {
				write_cache( candidate );
				return candidate;
			}
		}

		return directory + L"\\" + k_dll_name;
	}

	// Полный обход всех фиксированных дисков. Вызывается ИЗ РАБОЧЕГО ПОТОКА
	// после старта окна, поэтому прогресс виден в UI, а запуск не блокируется.
	// Возвращает пустую строку, если ничего не нашлось.
	std::wstring deep_search_dll( std::atomic<int>& visited, std::wstring& out_where )
	{
		std::wstring best{};
		std::wstring best_fallback{};
		std::mutex best_mtx{};

		const auto roots = local_drive_roots( );

		for ( const auto& root : roots )
		{
			if ( g_abort ) {
				break;
			}

			set_message( L"поиск DLL на " + root + L"... найдено каталогов: " + std::to_wstring( visited.load( ) ) );

			scan_directory( root, 12, [ & ]( const std::wstring& path ) -> bool
				{
					// Инжектить имеет смысл только настоящую библиотеку:
					// бэкапы (.bak_*) и .pdb отсеиваются по расширению.
					const auto name = file_name_of( path );

					if ( name.size( ) < 4 || _wcsicmp( name.c_str( ) + name.size( ) - 4, L".dll" ) != 0 ) {
						return false;
					}

					// Файл-кэш и мусорные хвосты: в проекте бэкапы зовутся
					// DarkFox-dev.dll.bak_20260919_143051, и после отсечения
					// по расширению они уже не пройдут, но подстрахуемся.
					if ( name.find( L".bak" ) != std::wstring::npos ) {
						return false;
					}

					std::scoped_lock lock( best_mtx );

					if ( is_exact_match( path ) )
					{
						if ( best.empty( ) || is_newer( path, best ) ) {
							best = path;
						}

						out_where = best;
						return false;
					}

					// Резерв принимает только файлы того же семейства:
					// DarkFox-dev.dll.bak_* для дев-лоадера, DarkFox.dll.bak_*
					// для обычного. Чужая сборка сюда не попадёт никогда.
					if ( is_same_family( path ) )
					{
						if ( best_fallback.empty( ) || is_newer( path, best_fallback ) ) {
							best_fallback = path;
						}
					}

					return false;
				}, g_abort, visited );

			if ( g_abort ) {
				break;
			}
		}

		std::scoped_lock lock( best_mtx );

		if ( !best.empty( ) ) {
			out_where = best;
			return best;
		}

		// Точного совпадения нет -- отдаём свежайший похожий файл, если он
		// есть. Лучше предложить найденное, чем молча сказать "не найдено".
		out_where = best_fallback;
		return best_fallback;
	}


	// -----------------------------------------------------------------------
	// Ярлык
	// -----------------------------------------------------------------------

	bool create_shortcut( std::wstring& out_path, std::wstring& out_error )
	{
		wchar_t exe[ MAX_PATH ]{};
		if ( !GetModuleFileNameW( nullptr, exe, MAX_PATH ) ) {
			out_error = L"не удалось получить путь к себе: " + error_text( GetLastError( ) );
			return false;
		}

		wchar_t desktop[ MAX_PATH ]{};
		if ( FAILED( SHGetFolderPathW( nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, SHGFP_TYPE_CURRENT, desktop ) ) ) {
			out_error = L"не нашёл рабочий стол";
			return false;
		}

		out_path = std::wstring( desktop ) + L"\\" + k_shortcut_name;

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
			// TRUE -- перезаписываем: если EXE перенесли, ярлык не должен сломаться.
			saved = SUCCEEDED( file->Save( out_path.c_str( ), TRUE ) );
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
	// Права администратора
	//
	// Инжект в cs2.exe требует PROCESS_VM_WRITE в чужой процесс. Без элевации
	// OpenProcess падает с ERROR_ACCESS_DENIED, и лоадер выглядит сломанным,
	// хотя дело только в токене. Манифест (см. loader.manifest) поднимает
	// процесс при старте; эта функция -- страховка на случай, если манифест
	// не подхватился (например, EXE пересобрали и ресурс потерялся).
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

	// Перезапуск себя с запросом UAC. Возвращает true, если элевация запущена:
	// тогда текущий процесс должен немедленно выйти, иначе рядом повиснут две
	// копии лоадера и вторая займёт DLL.
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
			// Пользователь отказал в UAC (ERROR_CANCELLED) либо провайдер
			// элевации недоступен -- работаем как есть и сообщаем об этом.
			return false;
		}

		if ( info.hProcess ) {
			CloseHandle( info.hProcess );
		}

		return true;
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
	// движок: отсюда и падения при запуске игры. Возвращает true, если движок
	// подтверждённо поднялся.
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
				set_message( L"движок поднялся, даю 3 с на инициализацию..." );
				request_repaint( );

				for ( auto settle = 0; settle < k_settle_ms && !g_abort; settle += 100 )
				{
					Sleep( 100 );
				}

				return !g_abort;
			}

			set_message( L"жду движок... нет " + std::wstring( missing )
				+ L" (" + std::to_wstring( waited / 1000 ) + L" с)" );
			request_repaint( );

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
	// отрапортовал бы «готово». Проверяем по списку модулей.
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

	// Steam поднимаем по его же протоколу: если он не запущен, обработчик URL
	// стартует его сам, и не надо угадывать путь к steam.exe.
	void ensure_steam( )
	{
		if ( find_process( L"steam.exe" ) ) {
			return;
		}

		set_message( L"запускаю Steam..." );
		request_repaint( );

		ShellExecuteW( nullptr, L"open", L"steam://open/main", nullptr, nullptr, SW_SHOWNORMAL );

		for ( auto tick = 0; tick < 120 && !g_abort; ++tick )
		{
			Sleep( 500 );

			if ( find_process( L"steam.exe" ) )
			{
				// Сразу после старта Steam ещё не готов принять запрос на запуск
				// игры и молча его теряет -- даём ему подняться.
				Sleep( 2500 );
				return;
			}
		}
	}

	// -allow_third_party_software -- тот самый флаг, из-за которого иначе
	// вылезает подтверждение "third party software". Форма
	// steam://run/<appid>//<args> передаёт аргументы игре.
	bool launch_game( std::wstring& out_error )
	{
		set_message( L"запускаю CS2 с -allow_third_party_software..." );
		request_repaint( );

		const auto result = reinterpret_cast< INT_PTR >( ShellExecuteW( nullptr, L"open",
			L"steam://run/730//-allow_third_party_software", nullptr, nullptr, SW_SHOWNORMAL ) );

		if ( result > 32 ) {
			return true;
		}

		// Обработчик URL мог не взяться -- пробуем обычный запуск по appid.
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

	// Гарантирует, что g_dll_path указывает на реально существующий файл.
	// Сначала пробует ближние кандидаты, и только если их нет -- уходит в
	// полный обход дисков. Вызывается из рабочего потока: обход может занять
	// десятки секунд, в UI это видно как "поиск DLL на D:\...".
	bool ensure_dll_located( std::wstring& out_error )
	{
		// Кэш и ближние пути проверяются мгновенно, повторно обход не нужен.
		const auto quick = resolve_dll_path( );
		if ( !quick.empty( ) && file_exists( quick ) )
		{
			g_dll_path = quick;
			g_dll_present = true;
			g_dll_from_scan = false;
			g_dll_source = L"рядом с лоадером";
			return true;
		}

		set_message( L"ищу DLL по всем дискам..." );
		request_repaint( );

		g_scanning = true;
		g_scan_visited = 0;

		std::wstring found{};
		const auto result = deep_search_dll( g_scan_visited, found );

		g_scanning = false;

		if ( result.empty( ) )
		{
			g_dll_present = false;
			out_error = L"не нашёл " + std::wstring( k_dll_name ) + L" ни на одном диске";
			return false;
		}

		g_dll_path = result;
		g_dll_present = true;
		g_dll_from_scan = true;
		g_dll_source = L"найден обходом дисков";

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
			request_repaint( );
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
			request_repaint( );
			return;
		}

		g_stage = static_cast< int >( stage::waiting_game );

		// От запуска до появления процесса проходит заметное время, а до
		// готовности движка -- ещё больше. Ждём процесс, потом даём движку
		// подняться: инжект в первые секунды прилетает в пустоту.
		DWORD pid{};
		for ( auto tick = 0; tick < 360 && !g_abort; ++tick )
		{
			pid = find_process( L"cs2.exe" );
			if ( pid ) {
				break;
			}

			set_message( L"жду cs2.exe... " + std::to_wstring( tick / 2 ) + L" с" );
			request_repaint( );
			Sleep( 500 );
		}

		if ( g_abort ) {
			return;
		}

		if ( !pid )
		{
			g_stage = static_cast< int >( stage::failed );
			set_message( L"cs2.exe так и не появился за 3 минуты" );
			request_repaint( );
			return;
		}

		set_message( L"игра найдена, жду загрузки движка..." );
		request_repaint( );

		const auto engine_ready = wait_for_engine( pid );

		if ( g_abort ) {
			return;
		}

		g_stage = static_cast< int >( stage::injecting );
		set_message( L"инжекчу..." );
		request_repaint( );

		if ( !inject( pid, g_dll_path, error ) )
		{
			g_stage = static_cast< int >( stage::failed );
			set_message( L"инжект: " + error );
			request_repaint( );
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

		request_repaint( );
	}

	void run_inject_only( )
	{
		g_abort = false;

		std::wstring error{};

		if ( !ensure_dll_located( error ) )
		{
			g_stage = static_cast< int >( stage::failed );
			set_message( error );
			request_repaint( );
			return;
		}

		const auto pid = find_process( L"cs2.exe" );
		if ( !pid )
		{
			g_stage = static_cast< int >( stage::failed );
			set_message( L"cs2.exe не запущен -- жми LAUNCH & INJECT" );
			request_repaint( );
			return;
		}

		g_stage = static_cast< int >( stage::injecting );
		set_message( L"инжекчу в pid " + std::to_wstring( pid ) + L"..." );
		request_repaint( );

		if ( !inject( pid, g_dll_path, error ) )
		{
			g_stage = static_cast< int >( stage::failed );
			set_message( L"инжект: " + error );
			request_repaint( );
			return;
		}

		const auto dll_name = file_name_of( g_dll_path );
		const auto loaded = verify_loaded( pid, dll_name );

		g_stage = static_cast< int >( loaded ? stage::done : stage::failed );
		set_message( loaded
			? L"готово -- " + dll_name + L" в игре, pid " + std::to_wstring( pid )
			: L"DLL загрузилась, но в списке модулей её нет -- смотри bin\\DarkFox_init.log" );

		request_repaint( );
	}

	void start( bool launch_first )
	{
		if ( busy( ) ) {
			return;
		}

		// Прошлый поток мог остаться -- джойним, иначе std::thread упадёт на
		// присваивании живому объекту.
		if ( g_worker.joinable( ) ) {
			g_worker.join( );
		}

		g_worker = std::thread( launch_first ? run_launch_and_inject : run_inject_only );
	}

	// -----------------------------------------------------------------------
	// Состояние для отрисовки
	// -----------------------------------------------------------------------

	void refresh_status( )
	{
		g_steam_running = find_process( L"steam.exe" ) != 0;
		g_target_pid = find_process( L"cs2.exe" );
		g_is_admin = is_elevated( );
	}

	struct card_state
	{
		std::wstring   title{};
		std::wstring   detail{};
		Color          color{};
	};

	std::vector< card_state > build_cards( )
	{
		std::vector< card_state > cards;

		cards.push_back( { L"Steam",
			g_steam_running ? L"запущен" : L"не запущен",
			g_steam_running ? c_ok : c_muted } );

		cards.push_back( { L"Counter-Strike 2",
			g_target_pid ? ( L"pid " + std::to_wstring( g_target_pid ) ) : L"не запущена",
			g_target_pid ? c_ok : c_muted } );

		// Источник пути -- в подписи справа: сразу видно, взяли из кэша,
		// нашли рядом с лоадером или вытащили полным обходом дисков.
		std::wstring dll_detail;
		if ( g_scanning ) {
			dll_detail = L"поиск... " + std::to_wstring( g_scan_visited.load( ) );
		}
		else if ( g_dll_present ) {
			dll_detail = g_dll_from_scan ? L"найдена обходом" : L"найдена";
		}
		else {
			dll_detail = L"не найдена";
		}

		cards.push_back( { k_dll_name, dll_detail, g_dll_present ? c_ok : ( g_scanning ? c_accent : c_warn ) } );

		cards.push_back( { L"права администратора",
			g_is_admin ? L"полные" : L"нет -- UAC",
			g_is_admin ? c_ok : c_warn } );

		return cards;
	}

	// -----------------------------------------------------------------------
	// GDI+ рисование
	// -----------------------------------------------------------------------

	void add_round_rect( GraphicsPath& path, const RectF& rect, float radius )
	{
		const auto diameter = radius * 2.0f;
		const auto right = rect.X + rect.Width;
		const auto bottom = rect.Y + rect.Height;

		path.AddArc( rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f );
		path.AddArc( right - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f );
		path.AddArc( right - diameter, bottom - diameter, diameter, diameter, 0.0f, 90.0f );
		path.AddArc( rect.X, bottom - diameter, diameter, diameter, 90.0f, 90.0f );
		path.CloseFigure( );
	}

	void fill_round( Graphics& graphics, const RectF& rect, float radius, const Color& color )
	{
		GraphicsPath path;
		add_round_rect( path, rect, radius );

		SolidBrush brush{ color };
		graphics.FillPath( &brush, &path );
	}

	void stroke_round( Graphics& graphics, const RectF& rect, float radius, const Color& color, float width = 1.0f )
	{
		GraphicsPath path;
		add_round_rect( path, rect, radius );

		Pen pen{ color, width };
		graphics.DrawPath( &pen, &path );
	}

	void draw_text( Graphics& graphics, const std::wstring& text, const RectF& rect,
		float size, const Color& color, INT style = FontStyleRegular,
		StringAlignment align = StringAlignmentNear )
	{
		Font font{ L"Segoe UI", size, style, UnitPixel };
		SolidBrush brush{ color };

		StringFormat format;
		format.SetAlignment( align );
		format.SetLineAlignment( StringAlignmentCenter );

		graphics.DrawString( text.c_str( ), -1, &font, rect, &format, &brush );
	}

	void draw_button( Graphics& graphics, const RectF& rect, const std::wstring& label,
		bool primary, bool enabled, bool hovered, bool pressed )
	{
		const auto radius = 8.0f;

		if ( primary && enabled )
		{
			// Акцентная кнопка: градиент от фиолетового к розовому, как в меню.
			GraphicsPath path;
			add_round_rect( path, rect, radius );

			LinearGradientBrush brush{ PointF( rect.X, rect.Y ), PointF( rect.GetRight( ), rect.GetBottom( ) ),
				pressed ? c_accent_press : c_accent, c_accent_soft };

			graphics.FillPath( &brush, &path );
		}
		else
		{
			auto fill = enabled ? c_raised : rgba( 20, 21, 25 );
			if ( enabled && hovered ) {
				fill = c_hover;
			}

			fill_round( graphics, rect, radius, fill );
			stroke_round( graphics, rect, radius, enabled ? c_border : rgba( 255, 255, 255, 10 ) );
		}

		const auto ink = primary && enabled ? rgba( 12, 13, 16 ) : ( enabled ? c_text : c_muted );
		draw_text( graphics, label, rect, 14.0f, ink, FontStyleBold, StringAlignmentCenter );
	}

	void draw( HDC dc )
	{
		Graphics graphics{ dc };
		graphics.SetSmoothingMode( SmoothingModeAntiAlias );
		graphics.SetTextRenderingHint( TextRenderingHintClearTypeGridFit );
		graphics.SetPixelOffsetMode( PixelOffsetModeHalf );
		graphics.SetCompositingQuality( CompositingQualityHighQuality );

		const RectF client{ 0.0f, 0.0f, static_cast< REAL >( k_window_w ), static_cast< REAL >( k_window_h ) };

		SolidBrush background{ c_background };
		graphics.FillRectangle( &background, client );

		// Мягкое цветное свечение в левом верхнем углу: задаёт "глубину" фону
		// без картинок. Рисуется радиальным градиентом от акцента к прозрачному.
		{
			const auto glow = RectF{ -140.0f, -160.0f, 520.0f, 400.0f };
			GraphicsPath path;
			path.AddEllipse( glow );

			PathGradientBrush brush{ &path };
			brush.SetCenterPoint( PointF{ glow.X + glow.Width * 0.5f, glow.Y + glow.Height * 0.5f } );
			brush.SetCenterColor( rgba( c_accent.GetR( ), c_accent.GetG( ), c_accent.GetB( ), 34 ) );

			Color surround[ ] = { rgba( c_accent.GetR( ), c_accent.GetG( ), c_accent.GetB( ), 0 ) };
			INT count{ 1 };
			brush.SetSurroundColors( surround, &count );

			graphics.FillPath( &brush, &path );
		}

		// Акцентная полоса сверху: тонкая, но сразу опознаёт сборку на глаз.
		{
			LinearGradientBrush brush{ PointF( 0.0f, 0.0f ), PointF( client.Width, 0.0f ),
				c_accent, rgba( c_accent_soft.GetR( ), c_accent_soft.GetG( ), c_accent_soft.GetB( ), 0 ) };

			graphics.FillRectangle( &brush, RectF{ 0.0f, 0.0f, client.Width, 2.0f } );
		}

		// Шапка: вертикальный градиент от приподнятого тона к фону.
		{
			LinearGradientBrush brush{ PointF( 0.0f, 0.0f ), PointF( 0.0f, k_header_h ),
				rgba( 24, 26, 33 ), rgba( 13, 14, 18 ) };
			graphics.FillRectangle( &brush, RectF{ 0.0f, 0.0f, client.Width, k_header_h } );
		}

		{
			Pen separator{ c_border, 1.0f };
			graphics.DrawLine( &separator, 0.0f, k_header_h, client.Width, k_header_h );
		}

		draw_text( graphics, L"DarkFox", RectF{ k_margin, 20.0f, 320.0f, 34.0f }, 26.0f, c_text, FontStyleBold );

		const auto name_width = [ & ]
			{
				Font font{ L"Segoe UI", 26.0f, FontStyleBold, UnitPixel };
				RectF measure{};
				graphics.MeasureString( L"DarkFox", -1, &font, PointF{ 0.0f, 0.0f }, &measure );
				return measure.Width;
			}( );

		draw_text( graphics, L"loader", RectF{ k_margin + name_width + 8.0f, 26.0f, 220.0f, 26.0f },
			13.0f, c_muted );

		if ( k_dev_build )
		{
			// Дев-сборка подписана явно: два лоадера рядом, перепутать легко.
			const RectF badge{ k_margin + name_width + 62.0f, 28.0f, 44.0f, 20.0f };
			fill_round( graphics, badge, 10.0f, rgba( c_accent.GetR( ), c_accent.GetG( ), c_accent.GetB( ), 42 ) );
			stroke_round( graphics, badge, 10.0f, rgba( c_accent.GetR( ), c_accent.GetG( ), c_accent.GetB( ), 110 ) );
			draw_text( graphics, L"DEV", badge, 10.0f, c_accent, FontStyleBold, StringAlignmentCenter );
		}

		// Подпись с версией инжектируемой библиотеки -- под именем.
		draw_text( graphics, k_dll_name, RectF{ k_margin, 58.0f, 420.0f, 22.0f }, 12.0f, c_dim );

		// Плашка состояния справа в шапке. Пульсирует, пока идёт работа:
		// альфа дышит по фазе таймера, и статус читается боковым зрением.
		{
			const auto current = static_cast< stage >( g_stage.load( ) );

			const wchar_t* label = L"READY";
			auto color = c_muted;

			switch ( current )
			{
			case stage::launching:    label = L"LAUNCHING"; color = c_accent; break;
			case stage::waiting_game: label = L"WAITING";   color = c_accent; break;
			case stage::injecting:    label = L"INJECTING"; color = c_accent; break;
			case stage::done:         label = L"INJECTED";  color = c_ok;     break;
			case stage::failed:       label = L"FAILED";    color = c_err;    break;
			default:                  label = L"READY";     color = c_muted;  break;
			}

			// Пульс: 0..1 по синусу от фазы. На покое (idle/done/failed) не мигаем.
			auto pulse = 0.0f;
			if ( current == stage::launching || current == stage::waiting_game || current == stage::injecting )
			{
				const auto phase = static_cast< float >( g_anim_phase.load( ) % 40 ) / 40.0f;
				pulse = 0.5f + 0.5f * std::sinf( phase * 6.2831853f );
			}

			const RectF pill{ client.Width - k_margin - 124.0f, 32.0f, 124.0f, 30.0f };

			// Ореол вокруг пилюли -- тем сильнее, чем выше пульс.
			fill_round( graphics, RectF{ pill.X - 3.0f, pill.Y - 3.0f, pill.Width + 6.0f, pill.Height + 6.0f },
				17.0f, rgba( color.GetR( ), color.GetG( ), color.GetB( ),
					static_cast< BYTE >( 14.0f + 22.0f * pulse ) ) );

			fill_round( graphics, pill, 15.0f, rgba( color.GetR( ), color.GetG( ), color.GetB( ), 30 ) );
			stroke_round( graphics, pill, 15.0f, rgba( color.GetR( ), color.GetG( ), color.GetB( ),
				static_cast< BYTE >( 80.0f + 80.0f * pulse ) ) );

			// Точка-индикатор тоже дышит и слегка растёт на пике.
			const auto dot_size = 6.0f + 2.0f * pulse;
			SolidBrush dot{ rgba( color.GetR( ), color.GetG( ), color.GetB( ),
				static_cast< BYTE >( 180.0f + 75.0f * pulse ) ) };
			graphics.FillEllipse( &dot, RectF{ pill.X + 15.0f - pulse, pill.Y + 15.0f - dot_size * 0.5f,
				dot_size, dot_size } );

			draw_text( graphics, label, RectF{ pill.X + 30.0f, pill.Y, pill.Width - 36.0f, pill.Height },
				11.0f, color, FontStyleBold );
		}

		// Карточки состояния
		auto y = k_card_top;
		for ( const auto& card : build_cards( ) )
		{
			const RectF rect{ k_margin, y, client.Width - k_margin * 2.0f, k_card_h };

			// Тень: сдвинутая вниз копия скругления под карточкой.
			fill_round( graphics, RectF{ rect.X, rect.Y + 3.0f, rect.Width, rect.Height }, 11.0f, rgba( 0, 0, 0, 60 ) );
			fill_round( graphics, rect, 11.0f, c_surface );
			stroke_round( graphics, rect, 11.0f, c_border );

			// Цветная засечка слева: статус считывается без чтения текста.
			fill_round( graphics, RectF{ rect.X + 1.0f, rect.Y + 9.0f, 3.0f, rect.Height - 18.0f }, 1.5f, card.color );

			SolidBrush dot{ rgba( card.color.GetR( ), card.color.GetG( ), card.color.GetB( ), 200 ) };
			graphics.FillEllipse( &dot, RectF{ rect.X + 20.0f, rect.Y + 24.0f, 9.0f, 9.0f } );

			draw_text( graphics, card.title, RectF{ rect.X + 44.0f, rect.Y, 360.0f, rect.Height }, 14.0f, c_text );
			draw_text( graphics, card.detail, RectF{ rect.GetRight( ) - 290.0f, rect.Y, 266.0f, rect.Height },
				13.0f, card.color, FontStyleRegular, StringAlignmentFar );

			y += k_card_h + k_card_gap;
		}

		// Полоса прогресса обхода дисков. Показывается только во время поиска:
		// заполнение асимптотическое (никогда не 100%), потому что общее число
		// каталогов заранее неизвестно, а полный прогресс соврёт.
		if ( g_scanning )
		{
			const RectF track{ k_margin, y + 4.0f, client.Width - k_margin * 2.0f, 6.0f };
			fill_round( graphics, track, 3.0f, rgba( 255, 255, 255, 12 ) );

			const auto visited = static_cast< float >( g_scan_visited.load( ) );
			const auto ratio = 1.0f - 1.0f / ( 1.0f + visited / 900.0f );
			fill_round( graphics, RectF{ track.X, track.Y, track.Width * ratio, track.Height }, 3.0f, c_accent );
		}

		// Кнопки
		const auto current = static_cast< stage >( g_stage.load( ) );
		const auto running = busy( );
		const auto ready = g_dll_present && !g_scanning;

		const auto button_y = static_cast< REAL >( k_window_h ) - 104.0f;
		g_launch_button = RectF{ k_margin, button_y, client.Width - k_margin * 2.0f - 158.0f, k_button_h };
		g_inject_button = RectF{ client.Width - k_margin - 146.0f, button_y, 146.0f, k_button_h };

		draw_button( graphics, g_launch_button, running ? L"РАБОТАЮ..." : L"LAUNCH & INJECT",
			true, ready && !running, g_hovered == 0, g_pressed == 0 );

		draw_button( graphics, g_inject_button, L"INJECT",
			false, ready && !running, g_hovered == 1, g_pressed == 1 );

		// Разделитель перед строкой сообщения: отделяет её от кнопок.
		{
			Pen separator{ c_border, 1.0f };
			graphics.DrawLine( &separator, k_margin, static_cast< REAL >( k_window_h ) - 44.0f,
				client.Width - k_margin, static_cast< REAL >( k_window_h ) - 44.0f );
		}

		// Строка сообщения
		const auto message = get_message( );
		if ( !message.empty( ) )
		{
			const auto color = current == stage::failed ? c_err : ( current == stage::done ? c_ok : c_dim );
			draw_text( graphics, message, RectF{ k_margin, static_cast< REAL >( k_window_h ) - 38.0f,
				client.Width - k_margin * 2.0f, 24.0f }, 12.0f, color );
		}
	}

	int hit_test( POINT point )
	{
		const auto x = static_cast< REAL >( point.x );
		const auto y = static_cast< REAL >( point.y );

		if ( g_launch_button.Contains( x, y ) ) {
			return 0;
		}

		if ( g_inject_button.Contains( x, y ) ) {
			return 1;
		}

		return -1;
	}

	void release_back_buffer( )
	{
		if ( g_back_dc ) {
			if ( g_back_old ) {
				SelectObject( g_back_dc, g_back_old );
				g_back_old = nullptr;
			}
			DeleteDC( g_back_dc );
			g_back_dc = nullptr;
		}

		if ( g_back_bmp ) {
			DeleteObject( g_back_bmp );
			g_back_bmp = nullptr;
		}

		g_back_w = 0;
		g_back_h = 0;
	}

	// Готовит кадровый буфер под размер клиентской области. Вызывается из
	// WM_PAINT: буфер переживает кадры, поэтому пересоздаётся только при
	// смене размера.
	void ensure_back_buffer( HDC reference, int width, int height )
	{
		if ( width <= 0 || height <= 0 ) {
			return;
		}

		if ( g_back_dc && g_back_w == width && g_back_h == height ) {
			return;
		}

		release_back_buffer( );

		g_back_dc = CreateCompatibleDC( reference );
		if ( !g_back_dc ) {
			return;
		}

		g_back_bmp = CreateCompatibleBitmap( reference, width, height );
		if ( !g_back_bmp ) {
			DeleteDC( g_back_dc );
			g_back_dc = nullptr;
			return;
		}

		g_back_old = static_cast< HBITMAP >( SelectObject( g_back_dc, g_back_bmp ) );
		g_back_w = width;
		g_back_h = height;
	}

	LRESULT CALLBACK window_proc( HWND window, UINT message, WPARAM wparam, LPARAM lparam )
	{
		switch ( message )
		{
		case WM_CREATE:
		{
			// Тёмный заголовок окна: иначе над тёмным клиентом висит светлая полоса.
			BOOL dark = TRUE;
			DwmSetWindowAttribute( window, 20, &dark, sizeof( dark ) );
			DwmSetWindowAttribute( window, 19, &dark, sizeof( dark ) );

			// Скруглённые углы клиентской области (Windows 11). На Windows 10
			// атрибут не поддерживается, вызов молча вернёт ошибку и углы
			// останутся прямыми -- это нормально, падать не из-за чего.
			enum : DWORD { k_dwm_window_corner_preference = 33 };
			DWORD corner{ 2 /*DWMWCP_ROUND*/ };
			DwmSetWindowAttribute( window, k_dwm_window_corner_preference, &corner, sizeof( corner ) );

			std::wstring error{};
			g_shortcut_ok = create_shortcut( g_shortcut_note, error );
			if ( !g_shortcut_ok ) {
				g_shortcut_note = error;
			}

			// Быстрый путь: кэш + папка рядом с EXE. Полный обход дисков
			// пойдёт из рабочего потока, если здесь ничего не найдётся.
			g_dll_path = resolve_dll_path( );
			g_dll_present = file_exists( g_dll_path );
			g_dll_source = g_dll_present ? L"рядом с лоадером" : L"";

			refresh_status( );

			if ( g_dll_present ) {
				set_message( g_shortcut_ok ? L"готов -- ярлык на рабочем столе" : L"ярлык: " + g_shortcut_note );
			}
			else {
				set_message( L"DLL не найдена рядом -- ищу по всем дискам..." );
			}

			// Таймер анимации: 40 мс -- 25 кадров в секунду. Чаще нет смысла,
			// пульсация и hover-переходы смотрятся гладко и на 25.
			SetTimer( window, 1, 40, nullptr );
			return 0;
		}

		case WM_APP + 1:
			InvalidateRect( window, nullptr, FALSE );
			return 0;

		case WM_PAINT:
		{
			PAINTSTRUCT paint{};
			const auto dc = BeginPaint( window, &paint );

			RECT client{};
			GetClientRect( window, &client );
			const auto width = client.right - client.left;
			const auto height = client.bottom - client.top;

			ensure_back_buffer( dc, width, height );

			if ( g_back_dc ) {
				// Кадр целиком собирается в памяти и уходит на экран одним
				// BitBlt. Именно это убирает моргание: видимая поверхность
				// никогда не показывает полунарисованный кадр.
				draw( g_back_dc );
				BitBlt( dc, 0, 0, width, height, g_back_dc, 0, 0, SRCCOPY );
			}
			else {
				// Памяти под буфер не дали -- рисуем напрямую. Хуже по
				// качеству, но окно остаётся рабочим.
				draw( dc );
			}

			EndPaint( window, &paint );
			return 0;
		}

		case WM_SIZE:
			// Буфер привязан к размеру клиентской области: при смене
			// размера его надо отпустить, следующий WM_PAINT создаст новый.
			release_back_buffer( );
			return 0;

		case WM_ERASEBKGND:
			return 1;

		case WM_MOUSEMOVE:
		{
			const POINT point{ GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) };
			const auto hovered = hit_test( point );

			if ( hovered != g_hovered )
			{
				g_hovered = hovered;
				InvalidateRect( window, nullptr, FALSE );

				TRACKMOUSEEVENT track{ sizeof( track ), TME_LEAVE, window, 0 };
				TrackMouseEvent( &track );
			}

			return 0;
		}

		case WM_MOUSELEAVE:
			g_hovered = -1;
			InvalidateRect( window, nullptr, FALSE );
			return 0;

		case WM_LBUTTONDOWN:
		{
			const POINT point{ GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) };
			g_pressed = hit_test( point );
			if ( g_pressed >= 0 ) {
				InvalidateRect( window, nullptr, FALSE );
			}
			return 0;
		}

		case WM_LBUTTONUP:
		{
			const POINT point{ GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) };
			const auto was = g_pressed;
			g_pressed = -1;

			if ( was >= 0 && hit_test( point ) == was )
			{
				start( was == 0 );
				InvalidateRect( window, nullptr, FALSE );
			}
			return 0;
		}

		case WM_TIMER:
		{
			// Фаза анимации растёт всегда: пульсация пилюли и прогресс-бар
			// должны идти даже когда статус не меняется.
			g_anim_phase.fetch_add( 1, std::memory_order_relaxed );

			// Опрос процессов -- дорогая часть (снапшот всех процессов).
			// На 25 к/с он не нужен: раз в 10 кадров (400 мс) достаточно,
			// задержка появления "запущен" незаметна.
			static auto tick{ 0 };
			if ( ( ++tick % 10 ) == 0 && !busy( ) ) {
				refresh_status( );
			}

			InvalidateRect( window, nullptr, FALSE );
			return 0;
		}

		case WM_KEYDOWN:
			if ( wparam == VK_RETURN ) {
				start( true );
			}
			else if ( wparam == VK_ESCAPE ) {
				DestroyWindow( window );
			}
			return 0;

		case WM_DESTROY:
			KillTimer( window, 1 );
			g_abort = true;
			if ( g_worker.joinable( ) ) {
				g_worker.join( );
			}
			release_back_buffer( );
			PostQuitMessage( 0 );
			return 0;
		}

		return DefWindowProcW( window, message, wparam, lparam );
	}

} // namespace

int WINAPI wWinMain( HINSTANCE instance, HINSTANCE, LPWSTR command_line, int )
{
	// Если манифест по какой-то причине не сработал и процесс стартовал без
	// прав администратора -- перезапускаемся с UAC. Аргумент --elevated
	// выставляется самим ShellExecuteEx и служит меткой "я уже элевирован",
	// чтобы не уйти в бесконечный цикл перезапусков при отказе пользователя.
	const auto already_elevated_attempt = command_line && wcsstr( command_line, L"--elevated" ) != nullptr;

	if ( !is_elevated( ) && !already_elevated_attempt )
	{
		if ( relaunch_elevated( ) ) {
			return 0;
		}
	}

	CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED );

	GdiplusStartupInput gdiplus_input{};
	GdiplusStartup( &g_gdiplus_token, &gdiplus_input, nullptr );

	WNDCLASSEXW window_class{};
	window_class.cbSize = sizeof( window_class );
	window_class.lpfnWndProc = window_proc;
	window_class.hInstance = instance;
	window_class.hCursor = LoadCursorW( nullptr, IDC_ARROW );
	window_class.lpszClassName = L"DarkFoxLoader";

	// Иконка класса: одна и та же для большого и малого размера, Windows
	// сама выберет подходящий кадр из многомасштабного .ico. Без этого
	// в заголовке и панели задач висит стандартная иконка приложения.
	const auto big_icon = LoadIconW( instance, MAKEINTRESOURCEW( 1 ) );
	window_class.hIcon = big_icon;
	window_class.hIconSm = big_icon;

	RegisterClassExW( &window_class );

	// Клиент ровно k_window_w x k_window_h: считаем размер окна от клиентской
	// области, иначе содержимое уезжает за край.
	RECT desired{ 0, 0, k_window_w, k_window_h };
	const auto style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	AdjustWindowRect( &desired, style, FALSE );

	g_window = CreateWindowExW( 0, window_class.lpszClassName, k_window_title,
		style, CW_USEDEFAULT, CW_USEDEFAULT,
		desired.right - desired.left, desired.bottom - desired.top,
		nullptr, nullptr, instance, nullptr );

	if ( !g_window )
	{
		GdiplusShutdown( g_gdiplus_token );
		CoUninitialize( );
		return 1;
	}

	ShowWindow( g_window, SW_SHOW );
	UpdateWindow( g_window );

	MSG message{};
	while ( GetMessageW( &message, nullptr, 0, 0 ) > 0 )
	{
		TranslateMessage( &message );
		DispatchMessageW( &message );
	}

	GdiplusShutdown( g_gdiplus_token );
	CoUninitialize( );
	return 0;
}
