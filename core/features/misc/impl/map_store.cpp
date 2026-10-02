#include <pch/pch.hpp>

#include <core/features/misc/impl/map_store.hpp>

#include <cstring>
#include <fstream>

namespace features::misc
{
	namespace
	{
		// Соль ключа. Не секрет -- часть раскладки формата. Её роль в том,
		// чтобы ключ карты не совпал с ключом, посчитанным чужой утилитой
		// с другой солью; одинаковой реализации "в лоб" уже не хватит.
		constexpr std::uint64_t k_key_salt{ 0x9E3779B97F4A7C15ull };

		// Константы XTEA. Стандартные (32 раунда, золотое сечение).
		constexpr auto k_xtea_rounds{ 32u };
		constexpr std::uint32_t k_delta{ 0x9E3779B9u };

		// xorshift64 для гаммы. Побитовый, без таблиц -- годится и как
		// генератор потока, и как дешёвый источник лавины.
		inline std::uint64_t xorshift64( std::uint64_t& state )
		{
			state ^= state << 13;
			state ^= state >> 7;
			state ^= state << 17;
			return state;
		}

		inline void put_u32( std::uint8_t*& p, std::uint32_t v )
		{
			p[ 0 ] = static_cast< std::uint8_t >( v & 0xFFu );
			p[ 1 ] = static_cast< std::uint8_t >( ( v >> 8 ) & 0xFFu );
			p[ 2 ] = static_cast< std::uint8_t >( ( v >> 16 ) & 0xFFu );
			p[ 3 ] = static_cast< std::uint8_t >( ( v >> 24 ) & 0xFFu );
			p += 4;
		}

		inline std::uint32_t get_u32( const std::uint8_t*& p )
		{
			const auto v = static_cast< std::uint32_t >( p[ 0 ] )
				| ( static_cast< std::uint32_t >( p[ 1 ] ) << 8 )
				| ( static_cast< std::uint32_t >( p[ 2 ] ) << 16 )
				| ( static_cast< std::uint32_t >( p[ 3 ] ) << 24 );
			p += 4;
			return v;
		}

		inline void put_f32( std::uint8_t*& p, float v )
		{
			std::uint32_t bits{};
			std::memcpy( &bits, &v, sizeof( bits ) );
			put_u32( p, bits );
		}

		inline float get_f32( const std::uint8_t*& p )
		{
			const auto bits = get_u32( p );
			float v{};
			std::memcpy( &v, &bits, sizeof( v ) );
			return v;
		}
	}

	std::uint64_t map_store::mix_key( const std::string& map_name )
	{
		// Ключ считается от КАНОНИЧЕСКОГО имени -- того же, из которого
		// строится имя файла. Иначе "de_dust2.bsp" и "de_dust2" дали бы
		// один файл, но разные ключи, и сохранённое не читалось бы.
		const auto canonical = canonical_name( map_name );

		// FNV-1a по имени.
		std::uint64_t hash{ 0xCBF29CE484222325ull };

		for ( const auto ch : canonical )
		{
			hash ^= static_cast< std::uint64_t >( static_cast< unsigned char >( ch ) );
			hash *= 0x100000001B3ull;
		}

		// Смешиваем с солью, чтобы ключ не равнялся голому хэшу имени.
		auto key = hash ^ k_key_salt;

		// Одна прогонка xorshift для лавины: ключ карты, отличающейся одной
		// буквой, не должен отличаться парой бит.
		key = xorshift64( key );
		return key;
	}

	std::uint64_t map_store::checksum( const std::uint8_t* data, std::size_t size )
	{
		std::uint64_t hash{ 0xCBF29CE484222325ull };

		for ( std::size_t i = 0; i < size; ++i )
		{
			hash ^= static_cast< std::uint64_t >( data[ i ] );
			hash *= 0x100000001B3ull;
		}

		return hash;
	}

	void map_store::xtea_encrypt( std::uint32_t& v0, std::uint32_t& v1, const std::uint32_t key[ 4 ] )
	{
		std::uint32_t sum{};
		auto a = v0;
		auto b = v1;

		for ( auto i = 0u; i < k_xtea_rounds; ++i )
		{
			a += ( ( ( b << 4 ) ^ ( b >> 5 ) ) + b ) ^ ( sum + key[ sum & 3 ] );
			sum += k_delta;
			b += ( ( ( a << 4 ) ^ ( a >> 5 ) ) + a ) ^ ( sum + key[ ( sum >> 11 ) & 3 ] );
		}

		v0 = a;
		v1 = b;
	}

	void map_store::xtea_decrypt( std::uint32_t& v0, std::uint32_t& v1, const std::uint32_t key[ 4 ] )
	{
		std::uint32_t sum{ k_delta * k_xtea_rounds };
		auto a = v0;
		auto b = v1;

		for ( auto i = 0u; i < k_xtea_rounds; ++i )
		{
			b -= ( ( ( a << 4 ) ^ ( a >> 5 ) ) + a ) ^ ( sum + key[ ( sum >> 11 ) & 3 ] );
			sum -= k_delta;
			a -= ( ( ( b << 4 ) ^ ( b >> 5 ) ) + b ) ^ ( sum + key[ sum & 3 ] );
		}

		v0 = a;
		v1 = b;
	}

	void map_store::crypt( std::uint8_t* data, std::size_t size, std::uint64_t key, bool encrypt )
	{
		const std::uint32_t xtea_key[ 4 ]
		{
			static_cast< std::uint32_t >( key & 0xFFFFFFFFull ),
			static_cast< std::uint32_t >( ( key >> 32 ) & 0xFFFFFFFFull ),
			static_cast< std::uint32_t >( ( key ^ k_key_salt ) & 0xFFFFFFFFull ),
			static_cast< std::uint32_t >( ( ( key >> 32 ) ^ k_key_salt ) & 0xFFFFFFFFull )
		};

		// Потоковый слой: XOR с xorshift64-гаммой. Именно он даёт лавину
		// по всему файлу при изменении одного байта и прячет размер.
		// Он симметричен сам себе, поэтому выполняется одинаково в обе
		// стороны -- но строго до блочного слоя при расшифровке.
		const auto apply_xor = [ & ]( )
			{
				auto state = key | 1ull;

				for ( std::size_t i = 0; i < size; ++i )
				{
					if ( ( i % 8 ) == 0 )
					{
						xorshift64( state );
					}

					data[ i ] ^= static_cast< std::uint8_t >( ( state >> ( ( i % 8 ) * 8 ) ) & 0xFFu );
				}
			};

		// Блочный слой: 8-байтовые блоки. Неполный хвост оставляем как
		// есть -- его закрывает XOR-слой, а выравнивание делается на
		// уровне всего буфера при записи.
		const auto apply_xtea = [ & ]( )
			{
				for ( std::size_t offset = 0; offset + 8 <= size; offset += 8 )
				{
					std::uint32_t a{};
					std::uint32_t b{};
					std::memcpy( &a, data + offset, 4 );
					std::memcpy( &b, data + offset + 4, 4 );

					if ( encrypt )
					{
						xtea_encrypt( a, b, xtea_key );
					}
					else
					{
						xtea_decrypt( a, b, xtea_key );
					}

					std::memcpy( data + offset, &a, 4 );
					std::memcpy( data + offset + 4, &b, 4 );
				}
			};

		if ( encrypt )
		{
			apply_xtea( );
			apply_xor( );
		}
		else
		{
			apply_xor( );
			apply_xtea( );
		}
	}

	std::string map_store::directory( )
	{
		// Рядом с модулем: <папка DLL>/df_maps/. Своя папка, чтобы не
		// засорять корень и чтобы её было видно в проводнике.
		std::array<wchar_t, 32768> module_path{};
		const auto length = GetModuleFileNameW( nullptr, module_path.data(), static_cast< DWORD >( module_path.size() ) );

		std::filesystem::path base{};

		if ( length && length < module_path.size() )
		{
			base = std::filesystem::path{ module_path.data(), module_path.data() + length };
			base = base.parent_path();
		}
		else
		{
			base = std::filesystem::current_path();
		}

		const auto dir = base / "df_maps";

		std::error_code ec;
		std::filesystem::create_directories( dir, ec );

		return dir.string();
	}

	std::string map_store::canonical_name( const std::string& map_name )
	{
		// Чистим имя: оно приходит из движка и может содержать путь
		// ("maps/de_dust2.bsp") и точку расширения.
		std::string clean{};
		clean.reserve( map_name.size() );

		for ( const auto ch : map_name )
		{
			const auto lower = static_cast< char >( std::tolower( static_cast< unsigned char >( ch ) ) );

			if ( ( lower >= 'a' && lower <= 'z' ) || ( lower >= '0' && lower <= '9' ) || lower == '_' || lower == '-' )
			{
				clean.push_back( lower );
			}
		}

		// Отрезаем ".bsp", если он уцелел (точка отфильтрована выше, так
		// что остаётся хвост "bsp" после имени карты). Дешевле проверить
		// суффикс, чем усложнять фильтр.
		if ( clean.size() > 3 && clean.compare( clean.size() - 3, 3, "bsp" ) == 0 )
		{
			clean.erase( clean.size() - 3 );
		}

		if ( clean.empty() )
		{
			clean = "unknown";
		}

		return clean;
	}

	std::string map_store::path_for( const std::string& map_name )
	{
		return directory( ) + "\\" + canonical_name( map_name ) + ".dfm";
	}

	bool map_store::save( const std::string& map_name, const std::vector<entry>& entries )
	{
		if ( map_name.empty() || entries.empty() )
		{
			return false;
		}

		// Полезная нагрузка: версия, число записей, записи. Число
		// шифруется вместе со всем остальным -- размер файла не должен
		// выдавать количество прострелов.
		constexpr auto header_floats{ 2u };  // version, count
		const auto floats = header_floats + entries.size() * k_entry_floats;

		std::vector<std::uint8_t> payload( static_cast< std::size_t >( floats ) * 4u );
		auto* p = payload.data();

		put_u32( p, k_version );
		put_u32( p, static_cast< std::uint32_t >( entries.size() ) );

		for ( const auto& e : entries )
		{
			put_f32( p, e.position.x );
			put_f32( p, e.position.y );
			put_f32( p, e.position.z );

			put_f32( p, e.normal.x );
			put_f32( p, e.normal.y );
			put_f32( p, e.normal.z );

			put_f32( p, e.end.x );
			put_f32( p, e.end.y );
			put_f32( p, e.end.z );

			put_f32( p, e.damage );
			put_f32( p, e.distance );
		}

		// Контрольная сумма считается ДО шифрования -- по открытым данным.
		// Так она проверяет именно целостность содержимого, а не совпадение
		// ключа: файл другой карты отсечётся ключом и развалится раньше.
		const auto sum = checksum( payload.data(), payload.size() );

		// Выравниваем до 8 байт под блочный слой.
		if ( ( payload.size() % 8 ) != 0 )
		{
			payload.resize( payload.size() + ( 8 - ( payload.size() % 8 ) ), 0 );
		}

		const auto key = mix_key( map_name );
		crypt( payload.data(), payload.size(), key, true );

		std::ofstream file( path_for( map_name ), std::ios::out | std::ios::binary | std::ios::trunc );

		if ( !file )
		{
			return false;
		}

		// Заголовок-обманка. Не магия, а правдоподобный мусор, чтобы
		// hex-редактор и file(1) не подсказывали формат.
		std::array<std::uint8_t, k_decoy_size> decoy{};
		auto decoy_state = key ^ 0xDEADBEEFCAFEBABEull;

		for ( auto& byte : decoy )
		{
			byte = static_cast< std::uint8_t >( xorshift64( decoy_state ) & 0xFFu );
		}

		file.write( reinterpret_cast< const char* >( decoy.data() ), static_cast< std::streamsize >( decoy.size() ) );
		file.write( reinterpret_cast< const char* >( &sum ), sizeof( sum ) );
		file.write( reinterpret_cast< const char* >( payload.data() ), static_cast< std::streamsize >( payload.size() ) );

		return static_cast< bool >( file );
	}

	std::vector<map_store::entry> map_store::load( const std::string& map_name )
	{
		std::vector<entry> out{};

		if ( map_name.empty() )
		{
			return out;
		}

		std::ifstream file( path_for( map_name ), std::ios::in | std::ios::binary );

		if ( !file )
		{
			return out;
		}

		std::vector<std::uint8_t> raw(
			( std::istreambuf_iterator< char >( file ) ),
			std::istreambuf_iterator< char >( ) );

		// Минимум: заголовок-обманка, контрольная сумма, версия и счётчик.
		const auto min_size = k_decoy_size + sizeof( std::uint64_t ) + 8u;

		if ( raw.size() < min_size )
		{
			return out;
		}

		const auto key = mix_key( map_name );

		// Пропускаем обманку и читаем контрольную сумму.
		std::uint64_t stored_sum{};
		std::memcpy( &stored_sum, raw.data() + k_decoy_size, sizeof( stored_sum ) );

		std::vector<std::uint8_t> payload( raw.begin() + static_cast< std::ptrdiff_t >( k_decoy_size + sizeof( std::uint64_t ) ), raw.end() );

		if ( ( payload.size() % 8 ) != 0 )
		{
			// Такой файл мы не писали (мы всегда выравниваем), но не
			// падаем -- просто не доверяем.
			return out;
		}

		// Обратная обфускация: XOR снимается первым, затем XTEA-расшифровка.
		crypt( payload.data(), payload.size(), key, false );

		// Проверяем контрольную сумму по расшифрованным данным.
		if ( checksum( payload.data(), payload.size() ) != stored_sum )
		{
			return out;
		}

		const std::uint8_t* p = payload.data();
		const auto version = get_u32( p );
		const auto count = get_u32( p );

		if ( version != k_version )
		{
			return out;
		}

		// Защита от повреждённого счётчика: считаем, сколько записей
		// реально влезает в буфер, и берём минимум.
		const auto available = ( payload.size() - 8u ) / ( k_entry_floats * 4u );
		const auto safe_count = std::min< std::uint32_t >( count, static_cast< std::uint32_t >( available ) );

		out.reserve( safe_count );

		for ( auto i = 0u; i < safe_count; ++i )
		{
			entry e{};

			e.position.x = get_f32( p );
			e.position.y = get_f32( p );
			e.position.z = get_f32( p );

			e.normal.x = get_f32( p );
			e.normal.y = get_f32( p );
			e.normal.z = get_f32( p );

			e.end.x = get_f32( p );
			e.end.y = get_f32( p );
			e.end.z = get_f32( p );

			e.damage = get_f32( p );
			e.distance = get_f32( p );

			out.push_back( e );
		}

		return out;
	}

	bool map_store::erase( const std::string& map_name )
	{
		if ( map_name.empty() )
		{
			return false;
		}

		std::error_code ec;
		return std::filesystem::remove( path_for( map_name ), ec );
	}
}
