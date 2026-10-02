// Проверка подлинности DLL перед инжектом.
//
// Лоадер выбирает файл по имени, но имя ничего не гарантирует: рядом может
// лежать чужая DLL с правильным названием, обрезанный бэкап, 32-битная
// библиотека (в cs2.exe она не поднимется) или просто мусор. Инжект такого
// файла -- это не «не сработало», а падение игры или загрузка чужого кода.
//
// Поэтому здесь всё проверяется ДО инжекта и БЕЗ выполнения кода из файла:
//   * минимальная валидация PE: MZ, PE\0\0, machine == AMD64, флаг DLL;
//   * размер файла в разумных границах;
//   * наличие экспорта darkfox_signature с корректным маркером в .rdata;
//   * опционально -- SHA-256 против файла DarkFox.hash (мягкий режим).
//
// Модуль header-only и намеренно не тянет ничего, кроме WinAPI: он
// подключается и в xui-лоадер (D3D11), и в GDI-лоадер.

#pragma once

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace verification
{
	// --- Константы, которые ОБЯЗАНЫ совпадать с utilities/security/signature.hpp
	// в самой DLL. Разойдутся -- лоадер перестанет узнавать свою сборку.
	inline constexpr std::uint32_t k_marker_magic{ 0x4B464458 }; // 'XDFK'
	inline constexpr std::uint32_t k_marker_format{ 1 };
	inline constexpr char          k_marker_export[] = "darkfox_signature";

	// Нижняя граница размера: настоящая DLL весит десятки мегабайт, а
	// обрезанный огрызок или заглушка -- килобайты. Порог защищает от
	// случайного «похожего» файла.
	inline constexpr std::uint64_t k_min_dll_size{ 1ull * 1024 * 1024 };

	enum class status
	{
		ok,
		not_found,
		too_small,
		unreadable,
		not_pe,          // нет MZ/PE или не разобрался заголовок
		wrong_machine,   // не x64
		not_dll,         // не помечен как DLL
		no_export,       // нет экспорта-маркера
		bad_marker,      // маркер есть, но поля не совпали
		build_mismatch,  // сборка (dev/ship) не та
	};

	struct result
	{
		status      state{ status::not_found };
		bool        pe_valid{};
		bool        is_x64{};
		bool        is_dll{};
		bool        has_marker{};
		std::uint32_t build_id{};
		std::uint64_t size{};
		std::string build;    // "dev" / "ship" из маркера
		std::string project;  // "DarkFox"

		// Отдельно от PE-проверок: результат сверки с хеш-файлом.
		enum class hash_state { not_checked, no_hash_file, match, mismatch };
		hash_state hash{ hash_state::not_checked };
		std::string hash_actual{};   // посчитанный SHA-256
		std::string hash_expected{}; // то, что лежало в файле

		bool trusted() const
		{
			const auto pe_ok = pe_valid && is_x64 && is_dll && has_marker
				&& state != status::bad_marker && state != status::build_mismatch;
			// Мягкий режим: отсутствие хеш-файла не блокирует инжект.
			return pe_ok && hash != hash_state::mismatch;
		}
	};

	// --- Чтение файла целиком -------------------------------------------------
	inline bool read_entire_file( const std::wstring& path, std::vector<std::uint8_t>& out,
		std::uint64_t* out_size = nullptr )
	{
		const auto file = CreateFileW( path.c_str( ), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );

		if ( file == INVALID_HANDLE_VALUE ) {
			return false;
		}

		LARGE_INTEGER size{};
		if ( !GetFileSizeEx( file, &size ) || size.QuadPart <= 0 ) {
			CloseHandle( file );
			return false;
		}

		if ( out_size ) {
			*out_size = static_cast< std::uint64_t >( size.QuadPart );
		}

		out.resize( static_cast< std::size_t >( size.QuadPart ) );

		std::size_t offset{};
		while ( offset < out.size( ) )
		{
			// min/max из <windows.h> недоступны: лоадеры собираются с /DNOMINMAX,
			// чтобы не ломать std::min. Считаем вручную.
			const auto remaining = out.size( ) - offset;
			const auto chunk = static_cast< DWORD >(
				remaining < ( 1u << 20 ) ? remaining : ( 1u << 20 ) );
			DWORD read{};

			if ( !ReadFile( file, out.data( ) + offset, chunk, &read, nullptr ) || read == 0 ) {
				CloseHandle( file );
				return false;
			}

			offset += read;
		}

		CloseHandle( file );
		return true;
	}

	// --- Разбор PE ------------------------------------------------------------

	struct pe_view
	{
		const std::uint8_t* base{};
		std::size_t         size{};
		bool                is_x64{};
		bool                is_dll{};
		std::uint32_t       export_rva{};
		std::uint32_t       export_size{};
	};

	inline bool parse_pe( const std::vector<std::uint8_t>& data, pe_view& out )
	{
		if ( data.size( ) < 0x40 ) {
			return false;
		}

		if ( data[ 0 ] != 'M' || data[ 1 ] != 'Z' ) {
			return false;
		}

		const auto lfanew = *reinterpret_cast< const std::uint32_t* >( data.data( ) + 0x3C );
		if ( lfanew + 0x18 > data.size( ) ) {
			return false;
		}

		const auto* nt = data.data( ) + lfanew;
		if ( nt[ 0 ] != 'P' || nt[ 1 ] != 'E' || nt[ 2 ] != 0 || nt[ 3 ] != 0 ) {
			return false;
		}

		const auto machine = *reinterpret_cast< const std::uint16_t* >( nt + 4 );
		// IMAGE_FILE_MACHINE_AMD64 = 0x8664
		const auto x64 = machine == 0x8664;

		const auto characteristics = *reinterpret_cast< const std::uint16_t* >( nt + 22 );
		// IMAGE_FILE_DLL = 0x2000
		const auto is_dll = ( characteristics & 0x2000 ) != 0;

		// Optional header начинается сразу после FileHeader (20 байт).
		const auto* opt = nt + 24;
		const auto magic = *reinterpret_cast< const std::uint16_t* >( opt );
		// PE32+ = 0x20B
		if ( magic != 0x20B ) {
			// 32-битный или неизвестный optional header -- не наш случай.
			out = { data.data( ), data.size( ), false, is_dll, 0, 0 };
			return true;
		}

		// DataDirectory начинается со смещения 112 в PE32+; export -- директория 0.
		const auto export_rva = *reinterpret_cast< const std::uint32_t* >( opt + 112 );
		const auto export_size = *reinterpret_cast< const std::uint32_t* >( opt + 116 );

		out = { data.data( ), data.size( ), x64, is_dll, export_rva, export_size };
		return true;
	}

	// --- RVA -> файловое смещение --------------------------------------------
	inline bool rva_to_offset( const pe_view& pe, std::uint32_t rva, std::size_t& out )
	{
		const auto lfanew = *reinterpret_cast< const std::uint32_t* >( pe.base + 0x3C );
		const auto* nt = pe.base + lfanew;
		const auto sections = *reinterpret_cast< const std::uint16_t* >( nt + 6 );
		const auto opt_size = *reinterpret_cast< const std::uint16_t* >( nt + 20 );

		const auto* section = nt + 24 + opt_size;

		for ( auto index = 0; index < sections; ++index, section += 40 )
		{
			const auto virtual_size = *reinterpret_cast< const std::uint32_t* >( section + 8 );
			const auto virtual_addr = *reinterpret_cast< const std::uint32_t* >( section + 12 );
			const auto raw_size = *reinterpret_cast< const std::uint32_t* >( section + 16 );
			const auto raw_ptr = *reinterpret_cast< const std::uint32_t* >( section + 20 );

			const auto span = virtual_size > raw_size ? virtual_size : raw_size;
			if ( rva >= virtual_addr && rva < virtual_addr + span )
			{
				const auto offset = static_cast< std::size_t >( raw_ptr ) + ( rva - virtual_addr );
				if ( offset < pe.size ) {
					out = offset;
					return true;
				}
				return false;
			}
		}

		return false;
	}

	// Читает ASCIIZ-строку по RVA с ограничением длины.
	inline std::string string_at_rva( const pe_view& pe, std::uint32_t rva )
	{
		std::size_t offset{};
		if ( !rva_to_offset( pe, rva, offset ) ) {
			return {};
		}

		std::string value;
		for ( auto index = offset; index < pe.size && pe.base[ index ]; ++index ) {
			value.push_back( static_cast< char >( pe.base[ index ] ) );
			if ( value.size( ) > 256 ) {
				break;
			}
		}

		return value;
	}

	// --- Поиск экспорта-маркера ----------------------------------------------
	//
	// Возвращает RVA функции darkfox_signature, либо 0.
	inline std::uint32_t find_marker_export( const pe_view& pe )
	{
		if ( pe.export_rva == 0 || pe.export_size == 0 ) {
			return 0;
		}

		std::size_t export_offset{};
		if ( !rva_to_offset( pe, pe.export_rva, export_offset ) ) {
			return 0;
		}

		if ( export_offset + 40 > pe.size ) {
			return 0;
		}

		const auto* directory = pe.base + export_offset;
		const auto name_count = *reinterpret_cast< const std::uint32_t* >( directory + 24 );
		const auto names_rva = *reinterpret_cast< const std::uint32_t* >( directory + 32 );
		const auto funcs_rva = *reinterpret_cast< const std::uint32_t* >( directory + 28 );

		// Разумный предел: таблица имён на тысячи записей -- это уже не наша DLL.
		if ( name_count == 0 || name_count > 65535 ) {
			return 0;
		}

		std::size_t names_offset{};
		std::size_t funcs_offset{};
		if ( !rva_to_offset( pe, names_rva, names_offset )
			|| !rva_to_offset( pe, funcs_rva, funcs_offset ) ) {
			return 0;
		}

		for ( std::uint32_t index = 0; index < name_count; ++index )
		{
			const auto entry = names_offset + static_cast< std::size_t >( index ) * 4;
			if ( entry + 4 > pe.size ) {
				break;
			}

			const auto name_rva = *reinterpret_cast< const std::uint32_t* >( pe.base + entry );
			if ( string_at_rva( pe, name_rva ) == k_marker_export )
			{
				// Младшее слово ordinal -> индекс в таблице адресов функций.
				const auto func_entry = funcs_offset + static_cast< std::size_t >( index ) * 4;
				if ( func_entry + 4 > pe.size ) {
					return 0;
				}

				return *reinterpret_cast< const std::uint32_t* >( pe.base + func_entry );
			}
		}

		return 0;
	}

	// --- Проверка маркера в .rdata -------------------------------------------
	//
	// Экспорт-функция тривиальна (`mov rax, offset marker; ret`), поэтому
	// вместо разбора её кода ищем саму структуру маркера в образе: она лежит
	// в .rdata и начинается с k_marker_magic, за которым идут поля. Так
	// проверка не зависит от того, как компилятор сгенерил функцию.
	struct marker_view
	{
		std::uint32_t magic{};
		std::uint32_t format{};
		std::uint32_t build_id{};
		std::uint32_t checksum{};
		std::string   build;
		std::string   project;
	};

	inline std::uint32_t marker_word( const char* text, std::size_t count )
	{
		std::uint32_t value{};
		for ( auto index = 0u; index < count && text[ index ]; ++index ) {
			value |= static_cast< std::uint32_t >(
				static_cast< unsigned char >( text[ index ] ) ) << ( ( index % 4 ) * 8 );
		}
		return value;
	}

	inline bool scan_marker( const pe_view& pe, marker_view& out )
	{
		// Структура выровнена по 4 и лежит в .rdata; сканируем по всему образу
		// с шагом 4 -- это дёшево (десятки МБ памяти уже прочитаны).
		constexpr auto k_struct_size = 48u;

		for ( std::size_t offset = 0; offset + k_struct_size <= pe.size; offset += 4 )
		{
			const auto magic = *reinterpret_cast< const std::uint32_t* >( pe.base + offset );
			if ( magic != k_marker_magic ) {
				continue;
			}

			const auto format = *reinterpret_cast< const std::uint32_t* >( pe.base + offset + 4 );
			const auto build_id = *reinterpret_cast< const std::uint32_t* >( pe.base + offset + 8 );
			const auto checksum = *reinterpret_cast< const std::uint32_t* >( pe.base + offset + 12 );

			if ( format != k_marker_format ) {
				continue;
			}

			const auto* build_raw = reinterpret_cast< const char* >( pe.base + offset + 16 );
			const auto* project_raw = reinterpret_cast< const char* >( pe.base + offset + 32 );

			marker_view candidate{};
			candidate.magic = magic;
			candidate.format = format;
			candidate.build_id = build_id;
			candidate.checksum = checksum;

			for ( auto index = 0u; index < 16 && build_raw[ index ]; ++index ) {
				candidate.build.push_back( build_raw[ index ] );
			}
			for ( auto index = 0u; index < 8 && project_raw[ index ]; ++index ) {
				candidate.project.push_back( project_raw[ index ] );
			}

			// Только DarkFox: чужая структура с совпавшим magic не пройдёт.
			if ( candidate.project != "DarkFox" ) {
				continue;
			}

			if ( candidate.build != "dev" && candidate.build != "ship" ) {
				continue;
			}

			const auto expected = candidate.magic ^ candidate.format ^ candidate.build_id
				^ marker_word( candidate.build.c_str( ), 16 )
				^ marker_word( candidate.project.c_str( ), 8 );

			if ( expected != candidate.checksum ) {
				continue;
			}

			out = candidate;
			return true;
		}

		return false;
	}

	// --- SHA-256 на CNG -------------------------------------------------------
	//
	// bcrypt доступен начиная с Vista и не тянет за собой CryptoAPI-провайдеры.
	// bcrypt.dll подгружается динамически: лоадер не должен падать на старте,
	// если библиотеки вдруг нет.
	//
	// CNG требует ТРИ шага, а не два:
	//   BCryptOpenAlgorithmProvider -> алгоритм,
	//   BCryptCreateHash            -> хеш-объект на буфере из BCRYPT_OBJECT_LENGTH,
	//   BCryptHashData / BCryptFinishHash.
	// Скормить алгоритм-хендл прямо в BCryptHashData нельзя -- вернётся
	// STATUS_INVALID_HANDLE (0xC0000008). Никакого ULONG* в BCryptFinishHash
	// тоже нет: четвёртый параметр -- это dwFlags по значению.

	struct sha256_api
	{
		using open_fn = LONG( WINAPI* )( void**, const wchar_t*, const wchar_t*, std::uint32_t );
		using property_fn = LONG( WINAPI* )( void*, const wchar_t*, std::uint8_t*, ULONG, ULONG*, std::uint32_t );
		using create_fn = LONG( WINAPI* )( void*, void**, std::uint8_t*, ULONG, const std::uint8_t*, ULONG, std::uint32_t );
		using hash_fn = LONG( WINAPI* )( void*, const std::uint8_t*, ULONG, std::uint32_t );
		using finish_fn = LONG( WINAPI* )( void*, std::uint8_t*, ULONG, std::uint32_t );
		using destroy_fn = LONG( WINAPI* )( void* );
		using close_fn = LONG( WINAPI* )( void*, std::uint32_t );

		HMODULE     module{};
		open_fn     open{};
		property_fn property{};
		create_fn   create{};
		hash_fn     hash{};
		finish_fn   finish{};
		destroy_fn  destroy{};
		close_fn    close{};

		bool load( )
		{
			if ( module ) {
				return true;
			}

			module = LoadLibraryW( L"bcrypt.dll" );
			if ( !module ) {
				return false;
			}

			open = reinterpret_cast< open_fn >( GetProcAddress( module, "BCryptOpenAlgorithmProvider" ) );
			property = reinterpret_cast< property_fn >( GetProcAddress( module, "BCryptGetProperty" ) );
			create = reinterpret_cast< create_fn >( GetProcAddress( module, "BCryptCreateHash" ) );
			hash = reinterpret_cast< hash_fn >( GetProcAddress( module, "BCryptHashData" ) );
			finish = reinterpret_cast< finish_fn >( GetProcAddress( module, "BCryptFinishHash" ) );
			destroy = reinterpret_cast< destroy_fn >( GetProcAddress( module, "BCryptDestroyHash" ) );
			close = reinterpret_cast< close_fn >( GetProcAddress( module, "BCryptCloseAlgorithmProvider" ) );

			return open && property && create && hash && finish && destroy && close;
		}
	};

	inline std::string to_hex( const std::uint8_t* data, std::size_t count )
	{
		static const char* const digits = "0123456789abcdef";
		std::string value;
		value.reserve( count * 2 );

		for ( std::size_t index = 0; index < count; ++index ) {
			value.push_back( digits[ data[ index ] >> 4 ] );
			value.push_back( digits[ data[ index ] & 0x0F ] );
		}

		return value;
	}

	inline bool sha256_of( const std::wstring& path, std::string& out_hex )
	{
		static sha256_api api{};
		if ( !api.load( ) ) {
			return false;
		}

		const auto file = CreateFileW( path.c_str( ), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr );

		if ( file == INVALID_HANDLE_VALUE ) {
			return false;
		}

		void* algorithm{};
		// BCRYPT_SHA256_ALGORITHM
		if ( api.open( &algorithm, L"SHA256", nullptr, 0 ) != 0 )
		{
			CloseHandle( file );
			return false;
		}

		// BCRYPT_OBJECT_LENGTH -- сколько байт нужно хеш-объекту. Спрашиваем у
		// самого провайдера, а не хардкодим: у разных версий CNG он разный.
		ULONG object_size{};
		ULONG returned{};

		if ( api.property( algorithm, L"ObjectLength", reinterpret_cast< std::uint8_t* >( &object_size ),
			sizeof( object_size ), &returned, 0 ) != 0 || object_size == 0 )
		{
			api.close( algorithm, 0 );
			CloseHandle( file );
			return false;
		}

		std::vector<std::uint8_t> object( object_size );
		void* hash_object{};

		if ( api.create( algorithm, &hash_object, object.data( ), object_size, nullptr, 0, 0 ) != 0 )
		{
			api.close( algorithm, 0 );
			CloseHandle( file );
			return false;
		}

		std::vector<std::uint8_t> buffer( 1u << 20 );
		auto failed{ false };

		for ( ;; )
		{
			DWORD read{};
			if ( !ReadFile( file, buffer.data( ), static_cast< DWORD >( buffer.size( ) ), &read, nullptr ) ) {
				failed = true;
				break;
			}

			if ( read == 0 ) {
				break;
			}

			if ( api.hash( hash_object, buffer.data( ), read, 0 ) != 0 ) {
				failed = true;
				break;
			}
		}

		std::uint8_t digest[ 32 ]{};

		if ( !failed ) {
			// Четвёртый параметр -- dwFlags по значению, не указатель.
			failed = api.finish( hash_object, digest, sizeof( digest ), 0 ) != 0;
		}

		api.destroy( hash_object );
		api.close( algorithm, 0 );
		CloseHandle( file );

		if ( failed ) {
			return false;
		}

		out_hex = to_hex( digest, sizeof( digest ) );
		return true;
	}

	// Ищет хеш-файл рядом с DLL: <DLL>.hash, затем <DLL без .dll>.hash.
	// Содержимое -- hex SHA-256, допускаются пробелы и переводы строк,
	// а также мусор после разделителя (формат `sha256sum`).
	inline std::string read_hash_file( const std::wstring& dll_path, std::wstring& out_file )
	{
		std::wstring candidates[ 2 ];
		candidates[ 0 ] = dll_path + L".hash";

		auto stem = dll_path;
		if ( const auto dot = stem.rfind( L'.' ); dot != std::wstring::npos ) {
			stem.resize( dot );
		}
		candidates[ 1 ] = stem + L".hash";

		for ( const auto& candidate : candidates )
		{
			std::vector<std::uint8_t> data;
			if ( !read_entire_file( candidate, data ) || data.empty( ) ) {
				continue;
			}

			std::string text( data.begin( ), data.end( ) );
			std::string hex;
			hex.reserve( 64 );

			for ( const auto ch : text )
			{
				const auto is_digit = ch >= '0' && ch <= '9';
				const auto is_lower = ch >= 'a' && ch <= 'f';
				const auto is_upper = ch >= 'A' && ch <= 'F';

				if ( is_digit || is_lower || is_upper ) {
					hex.push_back( is_upper ? static_cast< char >( ch - 'A' + 'a' ) : ch );
					if ( hex.size( ) == 64 ) {
						break;
					}
				}
				else if ( !hex.empty( ) ) {
					// Наткнулись на разделитель после начала хеша -- стоп.
					break;
				}
			}

			if ( hex.size( ) == 64 ) {
				out_file = candidate;
				return hex;
			}
		}

		return {};
	}

	// --- Полная проверка кандидата -------------------------------------------
	//
	// expected_build: "dev" или "ship" -- сборка, которую обязан инжектить
	// этот лоадер. Пустая строка -- сборку не проверяем.
	inline result verify( const std::wstring& path, const char* expected_build = nullptr )
	{
		result info{};

		const auto attributes = GetFileAttributesW( path.c_str( ) );
		if ( attributes == INVALID_FILE_ATTRIBUTES || ( attributes & FILE_ATTRIBUTE_DIRECTORY ) ) {
			info.state = status::not_found;
			return info;
		}

		LARGE_INTEGER size{};
		{
			const auto file = CreateFileW( path.c_str( ), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
			if ( file == INVALID_HANDLE_VALUE ) {
				info.state = status::unreadable;
				return info;
			}
			GetFileSizeEx( file, &size );
			CloseHandle( file );
		}

		info.size = static_cast< std::uint64_t >( size.QuadPart );

		if ( info.size < k_min_dll_size ) {
			info.state = status::too_small;
			return info;
		}

		std::vector<std::uint8_t> data;
		if ( !read_entire_file( path, data ) ) {
			info.state = status::unreadable;
			return info;
		}

		pe_view pe{};
		if ( !parse_pe( data, pe ) ) {
			info.state = status::not_pe;
			return info;
		}

		info.is_dll = pe.is_dll;
		info.is_x64 = pe.is_x64;

		if ( !pe.is_x64 ) {
			info.state = status::wrong_machine;
			return info;
		}

		if ( !pe.is_dll ) {
			info.state = status::not_dll;
			return info;
		}

		info.pe_valid = true;

		if ( find_marker_export( pe ) == 0 ) {
			info.state = status::no_export;
			return info;
		}

		marker_view marker{};
		if ( !scan_marker( pe, marker ) ) {
			info.state = status::bad_marker;
			return info;
		}

		info.has_marker = true;
		info.build_id = marker.build_id;
		info.build = marker.build;
		info.project = marker.project;

		if ( expected_build && *expected_build && marker.build != expected_build ) {
			info.state = status::build_mismatch;
			return info;
		}

		// Хеш-сверка -- мягкая: файла нет или не сошлось -- это предупреждение,
		// а не отказ. Жёсткий отказ оставил бы лоадер бесполезным сразу после
		// любой пересборки DLL, пока рядом не обновят .hash.
		std::wstring hash_file{};
		const auto expected = read_hash_file( path, hash_file );

		if ( expected.empty( ) ) {
			info.hash = result::hash_state::no_hash_file;
		}
		else
		{
			std::string actual{};
			if ( sha256_of( path, actual ) )
			{
				info.hash_actual = actual;
				info.hash_expected = expected;
				info.hash = actual == expected ? result::hash_state::match : result::hash_state::mismatch;
			}
			else {
				info.hash = result::hash_state::not_checked;
			}
		}

		info.state = status::ok;
		return info;
	}

	inline const char* status_text( status value )
	{
		switch ( value )
		{
			case status::ok:             return "ok";
			case status::not_found:      return "файл не найден";
			case status::too_small:      return "слишком мал -- не DLL";
			case status::unreadable:     return "не читается";
			case status::not_pe:         return "не PE-файл";
			case status::wrong_machine:  return "не x64";
			case status::not_dll:        return "не DLL";
			case status::no_export:      return "нет маркера DarkFox";
			case status::bad_marker:     return "маркер повреждён";
			case status::build_mismatch: return "другая сборка";
		}
		return "неизвестно";
	}
}
