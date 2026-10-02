#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/addresses/addresses.hpp>
#include <protection/game_addresses.hpp>
#include "../systems.hpp"

namespace
{
	// Вызов функции игры под SEH.
	//
	// Обработчик исключений чита (entry.cpp) намеренно возвращает
	// EXCEPTION_CONTINUE_SEARCH -- он только пишет лог и пропускает исключение
	// дальше, поэтому падение внутри функции трассировки убивает всю игру.
	// Трассировка -- операция только на чтение, так что пережить её отказ
	// безопасно: отдаём «не попали» и живём дальше.
	//
	// Аргументы только указатели: функция не должна требовать раскрутки
	// объектов, иначе MSVC не даст поставить __try (C2712).
	template <typename... args_t>
	bool safe_trace_call( std::uintptr_t address, args_t... args )
	{
		// Неразрешённая сигнатура -- это адрес 0. Дёргать его нельзя: исключение
		// поймалось бы, но каждый вызов стоил бы разбора SEH. Выходим заранее.
		if ( !memory::is_executable_address( address ) )
		{
			return false;
		}

		__try
		{
			reinterpret_cast< bool( __fastcall* )( args_t... ) >( address )( args... );
			return true;
		}
		__except ( EXCEPTION_EXECUTE_HANDLER )
		{
			return false;
		}
	}

	// Невалидный handle в фильтре -- гарантированное падение трассировки.
	//
	// make_filter превращает переданный entity в handle. Если entity нулевой
	// (локальной пешки ещё нет -- например, на экране выбора команды), в
	// skip_handles остаётся -1. Игра пытается разрешить такой handle в
	// сущность, получает ноль и читает поле по [0+0x10] -- это и был крэш при
	// выборе команды.
	[[nodiscard]] bool filter_handles_valid( const systems::tracing::filter& filter )
	{
		for ( const auto handle : filter.skip_handles )
		{
			if ( static_cast< std::uint32_t >( handle ) == 0xFFFFFFFFu )
			{
				return false;
			}
		}

		return true;
	}

	// Всё, что игра будет разыменовывать, проверено до вызова.
	[[nodiscard]] bool trace_callable( std::uintptr_t trace_fn, const systems::tracing::filter& filter )
	{
		return memory::is_executable_address( trace_fn )
			&& memory::is_readable_address( addresses::globals::game_trace_manager )
			&& memory::is_executable_address( filter.vtable )
			&& filter_handles_valid( filter );
	}

	// Кэш фильтров и результат проверки адресов трассировки.
	//
	// thread_local, потому что скан rage-бота идёт по пулу воркеров
	// (threadpool::parallel_for), и каждый воркер должен иметь свой слот --
	// иначе гонка записи в общий кэш. Состояние живёт здесь, а не полем
	// класса: systems.hpp включают 42 TU, и добавление поля ради двух слотов
	// означало бы их полную перекомпиляцию.
	struct filter_cache_state
	{
		static constexpr auto k_slots{ 8 };

		struct slot
		{
			std::uintptr_t skip_entity{ 0 };
			std::uintptr_t mask{ 0 };
			std::uint8_t layer{ 0 };
			int type{ 0 };
			systems::tracing::filter value{};
		};

		std::array<slot, k_slots> slots{};
		std::uint32_t next{ 0 };

		// -1 не проверяли, 0 нет, 1 да.
		std::int8_t traces_valid{ -1 };
	};

	filter_cache_state& cache_state( )
	{
		thread_local filter_cache_state state{};
		return state;
	}
}

namespace systems {

	bool tracing::is_visible( const math::vector3& start, const math::vector3& end, std::uintptr_t target_entity, std::uintptr_t skip_entity, std::uintptr_t mask ) const
	{
		auto current_start = start;
		auto entity_to_skip = skip_entity;

		constexpr auto max_penetrations{ 3 };

		for ( auto i = 0; i < max_penetrations; ++i )
		{
			const auto result = this->trace( current_start, end, entity_to_skip, mask );

			if ( result.hit_entity == target_entity || result.fraction > 0.97f )
			{
				return true;
			}

			if ( !result.hit_entity )
			{
				break;
			}

			const auto hit_health = memory::read<int>( result.hit_entity + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
			if ( hit_health > 0 && hit_health <= 100 )
			{
				entity_to_skip = result.hit_entity;
				current_start = result.end_pos + ( end - current_start ).normalized( );
				continue;
			}

			break;
		}

		return false;
	}

	tracing::result tracing::trace( const math::vector3& start, const math::vector3& end, std::uintptr_t skip_entity, std::uintptr_t mask, std::uint8_t layer ) const
	{
		auto filter = this->make_filter( skip_entity, mask, layer );
		return this->trace( start, end, filter );
	}

	tracing::result tracing::trace( const math::vector3& start, const math::vector3& end, const filter& filter ) const
	{
		ray ray{};
		result result{};

		const auto trace_ray = PATTERN (patterns::trace_ray);
		const auto trace_manager = addresses::globals::game_trace_manager;

		// Проверяем всё, что игра будет разыменовывать, до вызова.
		//
		// game_trace_manager -- это глобал-указатель: если он ещё не заполнен
		// (или сигнатура после обновления игры указывает не на тот mov), вызов
		// уходит в игру с нулевым this, и та падает на чтении [0+0x10]. Именно
		// так падал map_scan -- он трассирует каждый кадр, поэтому падение было
		// стабильным. Пустой результат означает "луч прошёл", и вызывающий
		// штатно выходит по своей проверке fraction.
		const auto filter_vtable = filter.vtable;

		if ( !memory::is_executable_address( trace_ray ) ||
			!memory::is_readable_address( trace_manager ) ||
			!memory::is_executable_address( filter_vtable ) )
		{
			static bool logged = false;
			if ( !logged )
			{
				logging::console::print( xs( "[tracing] ray trace disabled: trace manager or filter not usable" ) );
				logged = true;
			}

			result.fraction = 1.0f;
			result.position = end;
			return result;
		}

		if ( !safe_trace_call( trace_ray, trace_manager, &ray, &start, &end, &filter, &result ) )
		{
			static bool logged = false;
			if ( !logged )
			{
				logging::console::print( xs( "[tracing] ray trace faulted inside the game, result ignored" ) );
				logged = true;
			}

			result = {};
			result.fraction = 1.0f;
			result.position = end;
		}

		return result;
	}

	tracing::result tracing::trace_hull( const math::vector3& start, const math::vector3& end, const math::vector3& mins, const math::vector3& maxs, std::uintptr_t skip_entity, std::uintptr_t mask, std::uint8_t layer ) const
	{
		const auto filter = this->make_filter( skip_entity, mask, layer );
		return this->trace_hull( start, end, mins, maxs, filter );
	}

	tracing::result tracing::trace_hull( const math::vector3& start, const math::vector3& end, const math::vector3& mins, const math::vector3& maxs, const filter& filter ) const
	{
		ray ray{};
		ray.mins = mins;
		ray.maxs = maxs;
		ray.type = 2;

		result result{};

		const auto trace_ray = PATTERN (patterns::trace_ray );

		if ( !trace_callable( trace_ray, filter ) )
		{
			static bool logged = false;
			if ( !logged )
			{
				logging::console::print( xs( "[tracing] hull trace disabled: trace manager or filter not usable" ) );
				logged = true;
			}

			result.fraction = 1.0f;
			result.position = end;
			return result;
		}

		if ( !safe_trace_call( trace_ray, addresses::globals::game_trace_manager, &ray, &start, &end, &filter, &result ) )
		{
			static bool logged = false;
			if ( !logged )
			{
				logging::console::print( xs( "[tracing] hull trace faulted inside the game, result ignored" ) );
				logged = true;
			}

			result = {};
			result.fraction = 1.0f;
			result.position = end;
		}

		return result;
	}

	tracing::result tracing::trace_sphere( const math::vector3& start, const math::vector3& end, float radius, const filter& filter ) const
	{
		ray ray{};
		ray.mins = {};
		*reinterpret_cast< float* >( reinterpret_cast< std::uintptr_t >( &ray ) + 12 ) = radius;
		ray.type = 1;

		result result{};

		const auto trace_ray = PATTERN (patterns::trace_ray );

		if ( !trace_callable( trace_ray, filter ) )
		{
			static bool logged = false;
			if ( !logged )
			{
				logging::console::print( xs( "[tracing] sphere trace disabled: trace manager or filter not usable" ) );
				logged = true;
			}

			result.fraction = 1.0f;
			result.position = end;
			return result;
		}

		if ( !safe_trace_call( trace_ray, addresses::globals::game_trace_manager, &ray, &start, &end, &filter, &result ) )
		{
			static bool logged = false;
			if ( !logged )
			{
				logging::console::print( xs( "[tracing] sphere trace faulted inside the game, result ignored" ) );
				logged = true;
			}

			result = {};
			result.fraction = 1.0f;
			result.position = end;
		}

		return result;
	}

	tracing::result tracing::trace_to_entity( const math::vector3& start, const math::vector3& end, std::uintptr_t target_entity, std::uintptr_t skip_entity, std::uintptr_t mask, std::uint8_t layer ) const
	{
		const auto filter = this->make_filter( skip_entity, mask, layer );
		return this->trace_to_entity( start, end, target_entity, filter );
	}

	tracing::result tracing::trace_to_entity( const math::vector3& start, const math::vector3& end, std::uintptr_t target_entity, const filter& filter ) const
	{
		ray ray{};
		result result{};

		if ( !safe_trace_call( PATTERN (patterns::trace_ray_entity ), addresses::globals::game_trace_manager, &ray, &start, &end, target_entity, &filter, &result ) )
		{
			result = {};
			result.fraction = 1.0f;
			result.position = end;
		}

		return result;
	}

	tracing::filter tracing::make_filter( std::uintptr_t skip_entity, std::uintptr_t mask, std::uint8_t layer, int type ) const
	{
		filter filter{};

		// Инициализация фильтра -- тоже вызов в игру. Если она упадёт, фильтр
		// останется нулевым, и это поймают проверки перед трассировкой; но само
		// падение не должно доходить до движка.
		safe_trace_call( PATTERN (patterns::trace_filter_init ), &filter, skip_entity, mask, layer, type );

		return filter;
	}

	tracing::filter tracing::make_filter( std::uintptr_t skip_entity, std::uintptr_t mask, std::uint8_t layer ) const
	{
		filter filter{};

		safe_trace_call( PATTERN (patterns::trace_filter_init ), &filter, skip_entity, mask, layer, 7 );

		return filter;
	}

	void tracing::reset_filter_cache( ) const
	{
		auto& state = cache_state( );
		state.slots = {};
		state.next = 0;
	}

	// Кэш фильтров.
	//
	// Игровая trace_filter_init -- не бесплатная операция: она инициализирует
	// внутренности фильтра (vtable, маску, список пропускаемых handle'ов).
	// В горячем пути rage-скана make_filter вызывался на КАЖДУЮ трассу, а
	// трасс в тике порядка двадцати тысяч. Результат полностью определяется
	// четвёркой аргументов, поэтому считаем один раз на комбинацию.
	//
	// Ключ включает skip_entity: смена пешки (респавн, смена карты) даёт
	// другое значение, и старый слот просто перестаёт находиться -- мусор в
	// нём безвреден, потому что не совпадает по сравнению. mask/layer/type в
	// ключе, поэтому смена оружия (другая маска) берёт отдельный слот, а не
	// выбивает рабочий.
	tracing::filter tracing::make_filter_cached( std::uintptr_t skip_entity, std::uintptr_t mask, std::uint8_t layer, int type ) const
	{
		auto& state = cache_state( );

		for ( const auto& slot : state.slots )
		{
			if ( slot.value.vtable != 0
				&& slot.skip_entity == skip_entity
				&& slot.mask == mask
				&& slot.layer == layer
				&& slot.type == type )
			{
				return slot.value;
			}
		}

		auto built = this->make_filter( skip_entity, mask, layer, type );

		auto& slot = state.slots[ state.next % filter_cache_state::k_slots ];
		slot.skip_entity = skip_entity;
		slot.mask = mask;
		slot.layer = layer;
		slot.type = type;
		slot.value = built;
		++state.next;

		return built;
	}

	// Проверяются только адреса, которые игра разыменовывает при трассировке.
	// Результат кэшируется на всё время жизни потока: ни trace_ray, ни
	// game_trace_manager не переезжают после инициализации.
	bool tracing::traces_valid( ) const
	{
		auto& state = cache_state( );

		if ( state.traces_valid >= 0 )
		{
			return state.traces_valid != 0;
		}

		const auto trace_ray = PATTERN (patterns::trace_ray );
		const auto ok = memory::is_executable_address( trace_ray )
			&& memory::is_readable_address( addresses::globals::game_trace_manager );

		state.traces_valid = ok ? 1 : 0;

		if ( !ok )
		{
			logging::console::print( xs( "[tracing] game trace path unusable: trace_ray or trace manager not resolved" ) );
		}

		return ok;
	}

	tracing::result tracing::trace_fast( const math::vector3& start, const math::vector3& end, const filter& filter ) const
	{
		ray ray{};
		result result{};

		if ( filter.vtable == 0 || !filter_handles_valid( filter ) )
		{
			result.fraction = 1.0f;
			result.position = end;
			return result;
		}

		if ( !safe_trace_call( PATTERN (patterns::trace_ray ), addresses::globals::game_trace_manager,
			&ray, &start, &end, &filter, &result ) )
		{
			result = {};
			result.fraction = 1.0f;
			result.position = end;
		}

		return result;
	}

	tracing::result tracing::trace_hull_fast( const math::vector3& start, const math::vector3& end, const math::vector3& mins, const math::vector3& maxs, const filter& filter ) const
	{
		ray ray{};
		ray.mins = mins;
		ray.maxs = maxs;
		ray.type = 2;

		result result{};

		if ( filter.vtable == 0 || !filter_handles_valid( filter ) )
		{
			result.fraction = 1.0f;
			result.position = end;
			return result;
		}

		if ( !safe_trace_call( PATTERN (patterns::trace_ray ), addresses::globals::game_trace_manager,
			&ray, &start, &end, &filter, &result ) )
		{
			result = {};
			result.fraction = 1.0f;
			result.position = end;
		}

		return result;
	}

	tracing::result tracing::trace_sphere_fast( const math::vector3& start, const math::vector3& end, float radius, const filter& filter ) const
	{
		ray ray{};
		ray.mins = {};
		*reinterpret_cast< float* >( reinterpret_cast< std::uintptr_t >( &ray ) + 12 ) = radius;
		ray.type = 1;

		result result{};

		if ( filter.vtable == 0 || !filter_handles_valid( filter ) )
		{
			result.fraction = 1.0f;
			result.position = end;
			return result;
		}

		if ( !safe_trace_call( PATTERN (patterns::trace_ray ), addresses::globals::game_trace_manager,
			&ray, &start, &end, &filter, &result ) )
		{
			result = {};
			result.fraction = 1.0f;
			result.position = end;
		}

		return result;
	}

	tracing::player_movement_filter tracing::make_player_movement_filter( std::uintptr_t entity, std::uintptr_t mask, std::uint8_t collision_group ) const
	{
		player_movement_filter filter{};

		safe_trace_call( PATTERN (patterns::trace_filter_set_collision ), &filter, entity, mask, static_cast< int >( collision_group ) );

		return filter;
	}

	tracing::result tracing::trace_player_bbox( const math::vector3& start, const math::vector3& end, const bbox_collision& bbox, const player_movement_filter& filter, std::uintptr_t movement_services ) const
	{
		result result{};

		const auto trace_hull = PATTERN (patterns::trace_hull);

		// Проверяем ВСЁ, что игра будет разыменовывать, до вызова.
		//
		// После обновления игры адрес трассировки или содержимое фильтра могут
		// оказаться не теми, и тогда виртуальный вызов внутри игры уходит по
		// мусорной vtable -- это падение ВСЕЙ игры, а не отказ одной фичи.
		// Пустой результат означает "не попали", и фича просто ничего не делает.
		const auto filter_vtable = memory::safe_read<std::uintptr_t>( reinterpret_cast< std::uintptr_t >( filter.data ) ).value_or( 0 );

		if ( !memory::is_executable_address( trace_hull ) ||
			!memory::is_readable_address( movement_services ) ||
			!memory::is_executable_address( filter_vtable ) )
		{
			static bool logged = false;
			if ( !logged )
			{
				logging::console::print( xs( "[tracing] player bbox trace disabled: target or filter not usable" ) );
				logged = true;
			}

			return result;
		}

		if ( !safe_trace_call( trace_hull, movement_services + 1592, &result, &start, &end, &bbox, &filter ) )
		{
			static bool logged = false;
			if ( !logged )
			{
				logging::console::print( xs( "[tracing] player bbox trace faulted inside the game, result ignored" ) );
				logged = true;
			}

			return result;
		}

		return result;
	}

	void tracing::setup_trace( trace_data* trace_data, const math::vector3& start, const math::vector3& delta, const filter& filter, int penetration_count, bool trace_world ) const
	{
		memory::call<void>(PATTERN (patterns::trace_bullet_data_init), trace_data, start, delta, filter, penetration_count, trace_world );
	}

	void tracing::init_result( result* trace_result ) const
	{
		memory::call<void>(PATTERN (patterns::trace_bullet_free), trace_result );
	}

	void tracing::finalize_trace( trace_data* trace_data, result* hit, float unknown_float, void* unknown ) const
	{
		memory::call<void>(PATTERN (patterns::trace_bullet_update), trace_data, hit, unknown_float, unknown );
	}

} // namespace systems
