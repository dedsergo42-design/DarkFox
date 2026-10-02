#include <pch/pch.hpp>

#include <utilities/memory/memory.hpp>
#include <utilities/logging/logging.hpp>

#include <core/settings.hpp>
#include <core/systems/systems.hpp>
#include <core/rendering/rendering.hpp>
#include <protection/game_addresses.hpp>

#include "../misc.hpp"
#include "map_scan.hpp"
#include "map_store.hpp"

namespace features::misc {

	namespace {

		// Маска мира: geometry + solid + hitboxes, без прозрачного и без
		// невидимых. Та же, что у трассировщика по умолчанию (0x1c3003).
		constexpr std::uintptr_t k_world_mask{ 0x1c3003 };

		// Всё, что ближе этого к точке старта, считаем "в стене" -- трасса
		// вышла из геометрии, а не прошла через неё.
		constexpr auto k_inside_epsilon{ 1.0f };

		// Допуск на "пуля прошла сквозь стену": если fraction близок к 1, значит
		// трасса вообще ничего не встретила, и прострела тут нет -- это чистый
		// прямой видимый участок.
		constexpr auto k_open_fraction{ 0.985f };

		// Максимальная толщина пробиваемого слоя, юнитов. Толще -- пуля не
		// выйдет, и метка была бы ложной. 60 -- с запасом перекрывает
		// реальные тонкие стены и доски, но режет монолиты.
		constexpr auto k_max_thickness{ 60.0f };

		// Сколько пусто за стеной должно быть, чтобы считать это окном, а не
		// щелью между двумя стенами.
		//
		// Здесь важно не перегнуть: требование "много пустоты за стеной"
		// отсекает ровно те прострелы, ради которых фича и существует --
		// угол, за которым сразу идёт площадка. Достаточно убедиться, что
		// выход свободен; всё остальное ловит k_min_damage.
		constexpr auto k_min_open_depth{ 16.0f };

		// Насколько метка переживает тики, не подтверждаясь заново. Кадры
		// считаем по 1:120 в секунду, то есть ~2 секунды. Окно живёт ровно
		// столько, сколько не перепроверяется -- если за это время оно не
		// было найдено снова, оно ушло (закрыли дверь, разъехалась тачка).
		constexpr std::uint32_t k_marker_ttl_frames{ 240 };

	} // namespace

	bool map_scan::trace( const math::vector3& from, const math::vector3& to,
		systems::tracing::result& out, trace_budget* budget ) const
	{
		if ( budget != nullptr && !budget->take( ) )
		{
			// Бюджет исчерпан: отдаём "луч прошёл впустую", и вызывающий
			// штатно выйдет по своей обычной проверке fraction. Признак
			// "нет трассы" отдельным полем не заводим намеренно -- любой
			// другой контракт потребовал бы правок в каждом вызывающем.
			out = systems::tracing::result{};
			out.fraction = 1.0f;
			out.position = to;
			out.normal = {};
			return false;
		}

		// Пропускаем локальную пешку, а не ноль.
		//
		// make_filter превращает entity в handle, и нулевой entity даёт в
		// фильтре handle -1: игра пытается разрешить его в сущность, получает
		// ноль и падает на чтении [0+0x10]. Пешки может не быть вовсе (экран
		// выбора команды), тогда трассировку не делаем.
		const auto local_pawn = systems::g_local.get( ).pawn;
		if ( !local_pawn )
		{
			out = systems::tracing::result{};
			out.fraction = 1.0f;
			out.position = to;
			out.normal = {};
			return false;
		}

		// map_scan прогоняет тысячи трасс за кадр (веер лучей на скан прострелов),
		// поэтому здесь особенно важны оба кэша: готовый фильтр вместо игрового
		// trace_filter_init на каждую трассу и горячий путь без VirtualQuery.
		thread_local std::uintptr_t cached_pawn{ 0 };
		thread_local systems::tracing::filter cached_filter{};

		if ( cached_pawn != local_pawn || cached_filter.vtable == 0 )
		{
			cached_filter = systems::g_tracing.make_filter_cached( local_pawn, k_world_mask, 4 );
			cached_pawn = local_pawn;
		}

		out = systems::g_tracing.trace_fast( from, to, cached_filter );
		return true;
	}

	void map_scan::on_level_change( )
	{
		this->stop( );

		const std::lock_guard lock( this->m_mtx );

		// Кэш снимка обнуляем вместе с метками: если его не сбросить, а
		// m_frame случайно совпадёт (он сбрасывается на смене уровня), ragebot
		// получит метки прошлой карты.
		this->m_snapshot_cache.clear( );
		this->m_snapshot_cache_valid = false;

		this->m_grid.clear( );
		this->m_markers.clear( );
		this->m_marker_seen.clear( );
		this->m_grid_ready.store( false, std::memory_order_release );
		this->m_grid_fill_cursor.store( 0, std::memory_order_release );
		this->m_has_scan_origin = false;
		this->m_has_scan_time = false;
		this->m_last_scan_time = 0.0f;
		this->m_has_fan_angles = false;
		this->m_fan_cursor = 0;
		this->m_last_fan_eye = {};

		// Имя карты сбрасываем вместе с метками: оно придёт заново из
		// level_initialization, а до тех пор файл грузить не из чего.
		// Флаг restored тоже снимаем -- новый уровень, новый файл.
		this->m_map_name.clear( );
		this->m_restored = false;
	}

	void map_scan::start( )
	{
		if ( const auto current = this->get_state( ); current == state::scanning )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.is_valid( ) )
		{
			return;
		}

		// Origin живёт в prestate предсказания, а не в снапшоте local: снапшот
		// хранит только указатели/команды. Интерполированная позиция нам и
		// нужна -- трассы идут из неё.
		const auto origin = systems::g_prediction.pre( ).origin;

		{
			const std::lock_guard lock( this->m_mtx );
			this->rebuild_grid( origin );
			this->m_markers.clear( );
			this->m_marker_seen.clear( );
			this->m_snapshot_cache.clear( );
			this->m_snapshot_cache_valid = false;
			this->m_grid_ready.store( false, std::memory_order_release );

			this->m_scan_origin = origin;
			this->m_has_scan_origin = true;
			this->m_cursor.store( 0, std::memory_order_release );
		}

		this->m_phase.store( phase::grid, std::memory_order_release );
		this->m_state.store( state::scanning, std::memory_order_release );

		// Штамп старта: по нему считается пауза до следующего прохода.
		this->m_last_scan_time = this->m_elapsed;
		this->m_has_scan_time = true;
	}

	void map_scan::stop( )
	{
		if ( this->get_state( ) == state::scanning )
		{
			this->m_state.store( state::idle, std::memory_order_release );
		}
	}

	void map_scan::rebuild_grid( const math::vector3& origin )
	{
		const auto cells = static_cast< int >( ( k_radius * 2.0f ) / k_step ) + 1;
		const auto half = ( cells - 1 ) / 2;

		this->m_grid.clear( );
		this->m_grid.reserve( static_cast< std::size_t >( cells ) * static_cast< std::size_t >( cells ) );

		this->m_origin = origin;

		// Только раскладка: заполняем контейнер невалидными точками с
		// готовыми координатами. Никаких трасс здесь -- заполнение сетки
		// идёт отдельной фазой (см. m_grid_fill_cursor).
		//
		// Почему так. Сетка 17x17 -- это 289 ячеек, по две трассы на каждую,
		// то есть 578 вызовов трейсера в ОДНОМ кадре. Именно они и давали
		// фриз на входе в новую зону: раньше start() вызывал rebuild_grid
		// целиком, под мьютексом, прямо из on_render.
		for ( auto ix = 0; ix < cells; ++ix )
		{
			for ( auto iy = 0; iy < cells; ++iy )
			{
				sample s{};
				s.point = {
					origin.x + static_cast< float >( ix - half ) * k_step,
					origin.y + static_cast< float >( iy - half ) * k_step,
					origin.z
				};
				s.valid = false;
				this->m_grid.push_back( s );
			}
		}

		this->m_grid_fill_cursor.store( 0, std::memory_order_release );
		this->m_total.store( 0, std::memory_order_release );
	}

	void map_scan::step_grid_fill( )
	{
		const std::lock_guard lock( this->m_mtx );

		const auto total = static_cast< int >( this->m_grid.size( ) );
		if ( total <= 0 )
		{
			this->m_grid_ready.store( true, std::memory_order_release );
			return;
		}

		// Бюджет трасс тот же, что и у прочих фаз: сетка не должна стоить
		// дороже, чем обработка готовой геометрии.
		const auto fps = xdraw::framerate( );
		const auto load = std::clamp( fps > 1.0f ? fps / 60.0f : 1.0f, 0.0f, 2.0f );
		const auto trace_limit = static_cast< int >( std::lerp(
			static_cast< float >( k_trace_budget_min ),
			static_cast< float >( k_trace_budget ),
			std::clamp( load, 0.0f, 1.0f ) ) );

		trace_budget budget{ trace_limit };

		auto cursor = this->m_grid_fill_cursor.load( std::memory_order_acquire );

		while ( cursor < total && !budget.spent( ) )
		{
			auto& s = this->m_grid[ static_cast< std::size_t >( cursor ) ];
			++cursor;

			// Опускаем луч, чтобы найти пол под точкой. Так сетка ложится на
			// реальную поверхность и не улетает в небо или под карту.
			const auto from = math::vector3{ s.point.x, s.point.y, s.point.z + k_eye_z };
			const auto to = math::vector3{ s.point.x, s.point.y, s.point.z - k_probe_down };

			systems::tracing::result hit{};
			this->trace( from, to, hit, &budget );

			// Точка стоит на полу, если трасса нашла поверхность и эта
			// поверхность не вертикальная стена. Нормаль с большой |z|
			// означает пол/потолок -- нам нужен пол, то есть n.z > 0.
			const auto on_floor = hit.fraction < 1.0f && hit.normal.z > 0.7f;

			if ( !on_floor )
			{
				s.valid = false;
				continue;
			}

			s.floor = hit.position;
			s.point = hit.position + eye_offset( );

			// Из найденного пола поднимаемся вверх: если там пусто -- стоять
			// можно. Если упираемся, точка в геометрии и нам не нужна.
			systems::tracing::result up{};
			this->trace( s.point, s.point + math::vector3{ 0.0f, 0.0f, k_eye_z }, up, &budget );
			s.valid = up.fraction >= k_open_fraction;
		}

		this->m_grid_fill_cursor.store( cursor, std::memory_order_release );

		if ( cursor >= total )
		{
			// Сетка заполнена: только теперь известно, сколько в ней годных
			// точек, и можно начинать обход пар.
			this->m_grid_ready.store( true, std::memory_order_release );
			this->m_total.store( total, std::memory_order_release );
		}
	}

	bool map_scan::point_standable( const math::vector3& point, math::vector3& floor_out ) const
	{
		const auto from = point + math::vector3{ 0.0f, 0.0f, k_probe_up };
		const auto to = point - math::vector3{ 0.0f, 0.0f, k_probe_down };

		systems::tracing::result hit{};
		this->trace( from, to, hit );

		if ( hit.fraction >= 1.0f || hit.normal.z <= 0.7f )
		{
			return false;
		}

		floor_out = hit.position;
		return true;
	}

	bool map_scan::cull_marker( const marker& candidate ) const
	{
		for ( const auto& existing : this->m_markers )
		{
			// Схлопываем метки, которые смотрят примерно в ту же сторону и
			// стоят рядом: это один и тот же прострел, найденный из соседних
			// ячеек сетки.
			const auto delta = candidate.position - existing.position;
			if ( delta.length_sqr( ) > k_dedupe_dist_sqr )
			{
				continue;
			}

			const auto a = candidate.normal.normalized( );
			const auto b = existing.normal.normalized( );
			if ( a.dot( b ) >= k_dedupe_dot )
			{
				return true;
			}
		}

		return false;
	}

	int map_scan::find_marker( const marker& candidate ) const
	{
		// Тот же критерий, что у cull_marker, но возвращает индекс, а не факт.
		// Нужен, чтобы обновить метку, а не отбросить её: окно, найденное
		// повторно, -- это подтверждение, и оно обязано продлить жизнь метке.
		for ( std::size_t i = 0; i < this->m_markers.size( ); ++i )
		{
			const auto delta = candidate.position - this->m_markers[ i ].position;
			if ( delta.length_sqr( ) > k_dedupe_dist_sqr )
			{
				continue;
			}

			const auto a = candidate.normal.normalized( );
			const auto b = this->m_markers[ i ].normal.normalized( );
			if ( a.dot( b ) >= k_dedupe_dot )
			{
				return static_cast< int >( i );
			}
		}

		return -1;
	}

	void map_scan::prune_markers( )
	{
		// Метки, которые давно не подтверждались, уходят. Раньше список жил
		// до конце уровня, и окно, закрытое дверью или разъехавшейся тачкой,
		// продолжало висеть в памяти: ragebot считал его существующим и
		// получал бонус за прострел, которого больше нет.
		//
		// Чистим не каждый кадр, а с запасом по возрасту: TTL -- это не
		// точное время жизни, а гарантия, что метка не умрёт между двумя
		// проходами веера.
		if ( this->m_frame < k_marker_ttl_frames )
		{
			return;
		}

		const auto cutoff = this->m_frame - k_marker_ttl_frames;

		std::size_t out{};
		for ( std::size_t i = 0; i < this->m_markers.size( ); ++i )
		{
			if ( this->m_marker_seen[ i ] < cutoff )
			{
				continue;
			}

			if ( out != i )
			{
				this->m_markers[ out ] = this->m_markers[ i ];
				this->m_marker_seen[ out ] = this->m_marker_seen[ i ];
			}

			++out;
		}

		this->m_markers.resize( out );
		this->m_marker_seen.resize( out );
	}

	void map_scan::emit_wallbang( const math::vector3& from, const math::vector3& to, trace_budget* budget )
	{
		const auto delta = to - from;
		const auto length = delta.length( );

		if ( length < k_step )
		{
			return;
		}

		const auto dir = delta / length;

		// Первый трейс: входим в геометрию.
		systems::tracing::result entry{};
		if ( !this->trace( from, to, entry, budget ) )
		{
			return;
		}

		// Прямой видимости нет -- иначе это не прострел, а обычный обзор.
		if ( entry.fraction >= k_open_fraction )
		{
			return;
		}

		// Точка входа должна быть снаружи геометрии, а не внутри стены.
		// Сравниваем в юнитах, а не в долях: иначе порог уезжает с дистанцией.
		if ( entry.fraction * length <= k_inside_epsilon )
		{
			return;
		}

		// Нормаль поверхности: если она почти параллельна лучу, пуля идёт
		// вдоль стены и прострелом это не является.
		if ( std::abs( dir.dot( entry.normal ) ) < 0.163f )
		{
			return;
		}

		// Второй трейс: выходим из геометрии наружу. Начинаем чуть за точкой
		// входа, чтобы не поймать ту же самую поверхность обратно.
		//
		// Границу поиска выхода задаёт не конец луча, а k_max_thickness:
		// всё, что толще, прострелом не является (k_min_damage = 1 отсечёт
		// такое при любой модели урона). Ограничение длины здесь не
		// косметика: свип вызывает emit_wallbang на каждом шаге в 24 юнита,
		// то есть для одной и той же стены до 60 раз, и каждый лишний
		// прогон -- это поиск выхода на всю оставшуюся длину луча.
		const auto exit_start = entry.position + dir * k_inside_epsilon;

		if ( ( to - exit_start ).dot( dir ) <= 0.0f )
		{
			return;
		}

		const auto reach = std::min( ( to - exit_start ).length( ), k_max_thickness + k_inside_epsilon );
		systems::tracing::result exit{};
		if ( !this->trace( exit_start, exit_start + dir * reach, exit, budget ) )
		{
			return;
		}

		// Если второй трейс до конца ничего не встретил -- значит за первой
		// поверхностью геометрия не кончается как минимум на k_max_thickness.
		// Либо это монолит, либо мы внутри скалы: прострела нет.
		if ( exit.fraction >= k_open_fraction )
		{
			return;
		}

		// Толщина пробитого слоя -- от входа до выхода. Это честная величина,
		// в отличие от прежней оценки по позиции неопределённого трейса.
		const auto exit_position = exit_start + dir * ( exit.fraction * reach );
		const auto thickness = ( exit_position - entry.position ).length( );

		if ( thickness <= 0.01f || thickness > k_max_thickness )
		{
			return;
		}

		// За стеной должно быть пусто: если сразу за выходом стоит ещё одна
		// поверхность, пуля умирает на ней, и метка была бы ложной. Глубина
		// тут маленькая (k_min_open_depth): проверяем именно то, что выход
		// свободен, а не то, что за ним тянется большая комната.
		systems::tracing::result beyond{};
		if ( !this->trace( exit_position + dir * k_inside_epsilon,
			exit_position + dir * k_min_open_depth, beyond, budget ) )
		{
			return;
		}

		if ( beyond.fraction < k_open_fraction )
		{
			return;
		}

		// Сколько пустоты реально нашлось за выходом: это второй сигнал о
		// качестве окна. Узкая щель (beyond.fraction чуть больше порога) --
		// окно, за которым почти сразу ещё одна поверхность: пуля его
		// пройдёт, но застрянет на следующей. Просторный выход -- наоборот.
		const auto open_depth = beyond.fraction * k_min_open_depth;

		marker m{};
		m.position = entry.position;
		m.normal = entry.normal;
		m.end = exit_position;
		m.distance = length;

		// Оценка остаточного урона по толщине. Точное число даёт trace_bullet,
		// но он требует прогретого контекста оружия, которого на рендер-потоке
		// нет. Модель линейная: каждая единица толщины съедает долю урона.
		//
		// Штраф за узкий выход: если за стеной почти сразу следующая
		// поверхность, пуля теряет энергию и там. Множитель мягкий (не ниже
		// 0.75), потому что k_min_open_depth мал по построению и разброс
		// выхода между "чуть прошла" и "прошла с запасом" невелик.
		constexpr auto k_ref_damage{ 80.0f };
		constexpr auto k_thickness_scale{ 0.012f };
		const auto open_ratio = std::clamp( open_depth / k_min_open_depth, 0.0f, 1.0f );
		const auto open_penalty = 0.75f + 0.25f * open_ratio;

		m.damage = std::max( 0.0f, k_ref_damage * ( 1.0f - thickness * k_thickness_scale ) ) * open_penalty;

		if ( m.damage < k_min_damage )
		{
			return;
		}

		// Уже найдено раньше? Тогда это подтверждение: продлеваем жизнь и
		// запоминаем более выгодные параметры (толще не надо, а вот урон
		// лучшей находки -- полезен: ragebot по нему ранжирует).
		const auto existing = this->find_marker( m );

		if ( existing >= 0 )
		{
			auto& slot = this->m_markers[ static_cast< std::size_t >( existing ) ];
			this->m_marker_seen[ static_cast< std::size_t >( existing ) ] = this->m_frame;

			if ( m.damage > slot.damage )
			{
				slot.damage = m.damage;
				slot.distance = m.distance;
				slot.end = m.end;
			}

			return;
		}

		if ( this->m_markers.size( ) >= k_max_markers )
		{
			return;
		}

		this->m_markers.push_back( m );
		this->m_marker_seen.push_back( this->m_frame );
	}

	void map_scan::sweep_ray( const math::vector3& eye, const math::vector3& dir, float length, trace_budget* budget )
	{
		// Идём вдоль луча шагами k_fan_probe и на каждом шаге пробуем окно.
		//
		// Зачем шагами, а не одним выстрелом на всю длину: окно -- это место,
		// где пуля входит в стену и выходит с другой стороны. Один длинный
		// трейс найдёт только первую стену, а нам нужно знать, что за ней
		// пусто. Шаг в 24 юнита даёт несколько сэмплов на каждую стену, и
		// emit_wallbang на каждом сэмпле сам решит, окно это или нет.
		for ( auto travelled = k_fan_probe; travelled < length; travelled += k_fan_probe )
		{
			if ( budget != nullptr && budget->spent( ) )
			{
				return;
			}

			const auto point = eye + dir * travelled;
			this->emit_wallbang( eye, point, budget );

			// Ранний выход: список полон. Дальше считать некуда.
			if ( this->m_markers.size( ) >= k_max_markers )
			{
				return;
			}
		}
	}

	void map_scan::step_fan( )
	{
		const auto local = systems::g_local.get( );
		if ( !local.is_valid( ) )
		{
			this->m_state.store( state::done, std::memory_order_release );
			return;
		}

		// В фазе веера глаз берётся из prestate -- как и везде в фиче.
		const auto& prestate = systems::g_prediction.pre( );
		const auto eye = prestate.origin + eye_offset( );

		// Веер пускается только теми лучами, которые ещё не считались из
		// этого места. Раньше блокировка была только по углу взгляда:
		// выглянул в стену, скан отработал, отошёл на 50 метров -- и
		// новых окон уже не ищет, потому что взгляд не изменился. Теперь
		// вторая ось -- позиция.
		const auto view_angles = systems::g_input.get_view_angles( );

		if ( this->m_has_fan_angles )
		{
			const auto moved = ( eye - this->m_last_fan_eye ).length_sqr( ) > k_fan_eye_move_sqr;
			const auto turned = math::helpers::angle_distance( this->m_last_fan_angles, view_angles ) >= 3.0f;

			if ( !moved && !turned )
			{
				this->m_state.store( state::done, std::memory_order_release );
				return;
			}
		}

		// Опорные векторы взгляда: forward -- куда смотрим, right/up -- куда
		// отклоняем лучи. Считаем через angle_vectors, а не через углы вручную,
		// чтобы совпасть с тем, как движок строит базис из viewangles.
		math::vector3 forward{}, right{}, up{};
		math::helpers::angle_vectors_left( view_angles, &forward, &right, &up );

		const auto half_angle = k_fan_half_angle * ( std::numbers::pi_v<float> / 180.0f );

		// Бюджет трасс на этот проход веера. Раньше веер шёл до конца без
		// ограничения: 64 луча по 62 сэмпла -- почти 4000 вызовов, и каждый
		// стоит до четырёх трасс. Именно этот блок и давал фриз в тот кадр,
		// когда фаза сетки заканчивалась и начинался веер. Теперь цена
		// ограничена явно и известна заранее.
		trace_budget budget{ k_trace_budget };

		// Веер из k_fan_rays лучей. Раскладываем каждый луч по двум углам:
		// азимут и небольшой подъём. Золотое отношение задаёт иррациональный
		// шаг, поэтому лучи не выстраиваются в кольца и не дублируют друг
		// друга при малом числе.
		//
		// Порядок обхода -- от центра к краям, а не слева направо. Это важно
		// из-за бюджета: если он кончится, обрезанными окажутся периферийные
		// лучи (боковые окна), а не те, что смотрят прямо на цель. При
		// линейном обходе бюджет мог закончиться на первых же лучах, и веер
		// молча не покрывал половину сектора.
		constexpr auto golden{ 0.6180339887498949f };

		for ( auto i = 0; i < k_fan_rays; ++i )
		{
			if ( budget.spent( ) )
			{
				break;
			}

			// i = 0 -> центр, дальше чередование влево/вправо по золотому
			// сечению. Знак чередуется, модуль растёт к краю сектора.
			const auto step = static_cast< float >( ( i + 1 ) / 2 ) / static_cast< float >( k_fan_rays );
			const auto sign = ( i % 2 == 0 ) ? 1.0f : -1.0f;
			const auto t = std::clamp( step * 2.0f * sign, -1.0f, 1.0f );

			const auto yaw_offset = t * half_angle;
			const auto pitch_offset = ( std::fmodf( step * golden, 1.0f ) * 2.0f - 1.0f ) * half_angle * 0.5f;

			const auto cos_yaw = std::cosf( yaw_offset );
			const auto sin_yaw = std::sinf( yaw_offset );
			const auto cos_pitch = std::cosf( pitch_offset );
			const auto sin_pitch = std::sinf( pitch_offset );

			// Базис вращаем на yaw вокруг up и на pitch вокруг right.
			const auto yawed = forward * cos_yaw + right * sin_yaw;
			const auto ray_dir = ( yawed * cos_pitch + up * sin_pitch ).normalized( );

			this->sweep_ray( eye, ray_dir, k_fan_length, &budget );

			if ( this->m_markers.size( ) >= k_max_markers )
			{
				break;
			}
		}

		this->m_last_fan_angles = view_angles;
		this->m_last_fan_eye = eye;
		this->m_has_fan_angles = true;
		this->m_state.store( state::done, std::memory_order_release );

		// Полный проход закончен -- самое время записать результат на диск.
		// Именно здесь, а не в on_render: веер докручивается ровно один раз
		// на проход, и повторных записей на каждый кадр не будет.
		if ( settings::g_misc.m_map_scan.persist.value )
		{
			this->persist( );
		}
	}

	void map_scan::step_scan( )
	{
		const std::lock_guard lock( this->m_mtx );

		const auto total = static_cast< int >( this->m_grid.size( ) );

		// Пустая сетка (игрок в воздухе, схемы не дали пола) -- это не ошибка,
		// просто фаза сетки ничего не нашла. Переходим к вееру: он работает от
		// глаз, и пол ему не нужен.
		if ( total <= 0 )
		{
			this->m_phase.store( phase::fan, std::memory_order_release );
			return;
		}

		// Бюджет трасс на кадр, адаптивный по частоте кадров. Фиксированные
		// 24 ячейки -- это разное время в зависимости от того, сколько лучей
		// в каждой ячейке уцелело после фильтров: на открытой площади их
		// десятки, в коридоре -- единицы. Бюджет трасс выравнивает именно
		// стоимость, а не число итераций, поэтому скан не проваливает кадр
		// там, где раньше проваливал, и наоборот использует запас на пустой
		// геометрии.
		//
		// Ориентир -- 60 к/с. При 60 fps берём полный бюджет, при 30 --
		// половину, при 120 -- полный (больше не нужно: кадры идут часто,
		// и работа успеет пройти). Ниже порога не опускаемся никогда.
		const auto fps = xdraw::framerate( );
		const auto load = std::clamp( fps > 1.0f ? fps / 60.0f : 1.0f, 0.0f, 2.0f );
		const auto trace_limit = static_cast< int >( std::lerp(
			static_cast< float >( k_trace_budget_min ),
			static_cast< float >( k_trace_budget ),
			std::clamp( load, 0.0f, 1.0f ) ) );

		const auto cell_limit = static_cast< int >( std::lerp(
			static_cast< float >( k_budget_cells_min ),
			static_cast< float >( k_budget_cells_max ),
			std::clamp( load, 0.0f, 1.0f ) ) );

		trace_budget budget{ trace_limit };

		auto cursor = this->m_cursor.load( std::memory_order_acquire );

		for ( auto processed = 0; processed < cell_limit && cursor < total; ++processed, ++cursor )
		{
			if ( budget.spent( ) )
			{
				break;
			}

			const auto& from_sample = this->m_grid[ static_cast< std::size_t >( cursor ) ];
			if ( !from_sample.valid )
			{
				continue;
			}

			// Из каждой точки сетки проверяем лучи к соседним точкам, стоящим
			// дальше по порядку обхода. Пар "туда-обратно" не плодим.
			for ( auto j = cursor + 1; j < total; ++j )
			{
				if ( budget.spent( ) )
				{
					break;
				}

				const auto& to_sample = this->m_grid[ static_cast< std::size_t >( j ) ];
				if ( !to_sample.valid )
				{
					continue;
				}

				const auto delta = to_sample.point - from_sample.point;

				// Слишком близко -- это соседние ячейки одной плоскости, не
				// прострел. Слишком далеко -- бесполезно на практике.
				const auto dist_sqr = delta.length_sqr( );
				if ( dist_sqr < k_step * k_step * 4.0f )
				{
					continue;
				}

				if ( dist_sqr > k_radius * k_radius )
				{
					continue;
				}

				this->emit_wallbang( from_sample.point, to_sample.point, &budget );
			}
		}

		this->m_cursor.store( cursor, std::memory_order_release );

		if ( cursor >= total )
		{
			// Сетка пройдена. Состояние НЕ ставим в done: дальше идёт веер по
			// прицелу, который добьёт окна, пропущенные из-за шага сетки.
			this->m_phase.store( phase::fan, std::memory_order_release );
			logging::console::print( "[map_scan] grid done, %zu markers, fan next", this->m_markers.size( ) );
		}
	}

	void map_scan::on_create_move( )
	{
		const auto local = systems::g_local.get( );
		if ( !local.is_valid( ) )
		{
			return;
		}

		const auto origin = systems::g_prediction.pre( ).origin;

		// Авто-перезапуск: игрок ушёл достаточно далеко -- прежний результат
		// ничего не говорит о нынешнем окружении.
		//
		// Порог дистанции работает в паре с паузой, и это не дублирование:
		// дистанция отвечает за АКТУАЛЬНОСТЬ результата, пауза -- за то,
		// чтобы непрерывный бег не превращался в непрерывный скан. Без
		// паузы проход стартовал на каждой новой сотне юнитов, и полный
		// сброс сетки с веером на 900 трасс попадал в кадр по нескольку
		// раз в секунду -- ровно те заикания, что были при беге.
		if ( this->get_state( ) == state::done && this->m_has_scan_origin )
		{
			if ( this->m_has_scan_time
				&& ( this->m_elapsed - this->m_last_scan_time ) < k_rescan_cooldown )
			{
				return;
			}

			if ( ( origin - this->m_scan_origin ).length_sqr( ) > k_rescan_distance * k_rescan_distance )
			{
				this->m_state.store( state::idle, std::memory_order_release );
			}
		}
	}

	void map_scan::on_render( xdraw::draw_list& draw_list )
	{
		const auto& cfg = settings::g_misc.m_map_scan;

		// Тяжёлая часть -- только если включено. Трассы дорогие, и держать их
		// в кадре при выключенной фиче нельзя.
		if ( !cfg.enabled.value )
		{
			return;
		}

		// Счётчик кадров двигается и в idle: он нужен для возраста меток, а
		// метки живут, пока фича включена, независимо от того, идёт ли
		// текущий проход.
		this->m_elapsed += xdraw::delta_time( );

		if ( this->m_frame < std::numeric_limits<std::uint32_t>::max( ) )
		{
			++this->m_frame;
		}
		else
		{
			// Переполнение: сдвигаем всё к нулю, иначе возраст меток
			// сравнивался бы с уже обёрнутым счётчиком.
			this->m_frame = 1;

			const std::lock_guard lock( this->m_mtx );
			for ( auto& seen : this->m_marker_seen )
			{
				seen = 0;
			}
		}

		// Первый кадр нового уровня: подтягиваем сохранённые прострелы, чтобы
		// ragebot работал сразу, а не ждал полного прохода. Делаем здесь, а
		// не в set_map_name: к моменту level_initialization диск ещё занят
		// загрузкой карты.
		if ( !this->m_restored )
		{
			this->m_restored = true;

			if ( cfg.autoload.value )
			{
				this->restore( );
			}
		}

		const auto state_now = this->get_state( );

		if ( state_now == state::idle )
		{
			this->start( );
		}

		if ( this->get_state( ) == state::scanning )
		{
			// Фазы идут последовательно. Сетка обрабатывается по ячейкам за
			// кадр, веер -- целиком за один вызов, потому что его лучи зависят
			// от текущего взгляда: размазать их по кадрам значит считать окна
			// для углов, которых уже нет.
			if ( this->m_phase.load( std::memory_order_acquire ) == phase::grid )
			{
				// Две ступени: пока сетка не заполнена трассами, обход пар
				// невозможен. Заполнение идёт по своему бюджету и за
				// несколько кадров.
				if ( !this->m_grid_ready.load( std::memory_order_acquire ) )
				{
					this->step_grid_fill( );
				}
				else
				{
					this->step_scan( );
				}
			}
			else
			{
				this->step_fan( );
			}
		}

		std::vector<marker> markers;
		{
			const std::lock_guard lock( this->m_mtx );

			// Чистим устаревшие метки до снимка: прострел, который перестал
			// существовать, не должен попасть ни в отрисовку, ни в ragebot.
			this->prune_markers( );
			markers = this->m_markers;
		}

		// Прогресс скана в углу экрана. Первые проходы длинные (сетка из сотен
		// ячеек по бюджету в k_budget_cells за кадр), и без индикатора фича
		// выглядит зависшей: метки появляются только ближе к концу.
		//
		// В скобках -- текущая частота кадров: бюджет трасс адаптивный, и без
		// неё непонятно, почему скан идёт быстрее или медленнее на разных
		// машинах. Формат намеренно компактный: строка висит поверх игры.
		if ( cfg.show_progress.value )
		{
			char buf[ 128 ]{};
			const auto phase_name = this->m_phase.load( std::memory_order_acquire ) == phase::grid ? "grid" : "fan";
			const auto st = this->get_state( );
			const auto fps = xdraw::framerate( );

			if ( st == state::scanning )
			{
				std::snprintf( buf, sizeof( buf ), "map scan: %s %.0f%% | %zu marks | %.0f fps",
					phase_name, this->progress( ) * 100.0f, markers.size( ), fps );
			}
			else
			{
				std::snprintf( buf, sizeof( buf ), "map scan: %s | %zu marks | %.0f fps",
					st == state::done ? "done" : "idle", markers.size( ), fps );
			}

			const auto color = xdraw::color(
				cfg.color.value.r, cfg.color.value.g, cfg.color.value.b, 230 );

			draw_list.text( 12.0f, 12.0f, buf, color );
		}

		if ( markers.empty( ) )
		{
			return;
		}

		const auto color = xdraw::color(
			cfg.color.value.r, cfg.color.value.g, cfg.color.value.b, cfg.color.value.a );

		const auto draw_entry = cfg.draw_entry.value;
		const auto draw_line = cfg.draw_line.value;
		const auto radius = cfg.radius.value;
		const auto show_damage = cfg.show_damage.value;

		for ( const auto& m : markers )
		{
			const auto screen = systems::g_view.project( m.position );
			if ( !systems::g_view.projection_valid( screen ) )
			{
				continue;
			}

			if ( draw_line )
			{
				const auto end_screen = systems::g_view.project( m.end );
				if ( systems::g_view.projection_valid( end_screen ) )
				{
					draw_list.line( screen.x, screen.y, end_screen.x, end_screen.y, color, 1.0f );
				}
			}

			if ( draw_entry )
			{
				draw_list.circle( screen.x, screen.y, radius, color, 1.5f );

				// Точка входа -- центр, чтобы метка читалась как прицел.
				constexpr auto dot{ 1.5f };
				draw_list.rect_filled( screen.x - dot, screen.y - dot, dot * 2.0f, dot * 2.0f, color );
			}

			if ( show_damage )
			{
				char buf[ 32 ]{};
				std::snprintf( buf, sizeof( buf ), "%.0f", m.damage );
				draw_list.text( screen.x + radius + 3.0f, screen.y - 6.0f, buf, color );
			}
		}
	}

	float map_scan::progress( ) const
	{
		// Две фазы в одном числе. Сетка занимает 0..0.75, веер -- терминальная
		// стадия: он идёт одним вызовом и завершается сразу, так что дробь для
		// него показывать нечего.
		//
		// Внутри сетки тоже две ступени: заполнение трассами и обход пар.
		// Первой отдано больше половины диапазона, потому что она дороже
		// (две трассы на ячейку) и раньше не показывалась вообще -- индикатор
		// стоял на нуле, пока сетка заполнялась.
		if ( this->m_phase.load( std::memory_order_acquire ) == phase::fan )
		{
			return 1.0f;
		}

		const auto total = static_cast< int >( this->m_grid.size( ) );
		if ( total <= 0 )
		{
			return 0.0f;
		}

		if ( !this->m_grid_ready.load( std::memory_order_acquire ) )
		{
			const auto filled = this->m_grid_fill_cursor.load( std::memory_order_acquire );
			const auto fraction = std::clamp( static_cast< float >( filled ) / static_cast< float >( total ), 0.0f, 1.0f );
			return fraction * 0.45f;
		}

		const auto cursor = this->m_cursor.load( std::memory_order_acquire );
		const auto fraction = std::clamp( static_cast< float >( cursor ) / static_cast< float >( total ), 0.0f, 1.0f );
		return 0.45f + fraction * 0.30f;
	}

	std::size_t map_scan::marker_count( ) const
	{
		const std::lock_guard lock( this->m_mtx );
		return this->m_markers.size( );
	}

	std::vector<map_scan::marker> map_scan::snapshot( ) const
	{
		const std::lock_guard lock( this->m_mtx );

		// ragebot зовёт это до четырёх раз за тик (current_hits, planned_hits,
		// ducked_hits и повторное планирование), а метки за тик не меняются:
		// их обновляет только update() и только раз в кадр, увеличивая m_frame.
		// Держать копию под мьютексом каждый раз -- значит несколько раз за
		// тик копировать сотни структур на горячем пути. Отдаём закэшированную
		// копию, пока m_frame тот же.
		if ( this->m_snapshot_cache_frame == this->m_frame && this->m_snapshot_cache_valid )
		{
			return this->m_snapshot_cache;
		}

		this->m_snapshot_cache = this->m_markers;
		this->m_snapshot_cache_frame = this->m_frame;
		this->m_snapshot_cache_valid = true;

		return this->m_snapshot_cache;
	}

	void map_scan::set_map_name( const std::string& name )
	{
		if ( name == this->m_map_name )
		{
			return;
		}

		this->m_map_name = name;
		this->m_restored = false;
	}

	void map_scan::persist( ) const
	{
		// Копия меток под мьютексом, запись -- уже без него. Держать
		// блокировку на время файловых операций нельзя: скан идёт с
		// render-потока, и это застопорило бы кадр.
		std::vector<map_store::entry> entries;
		{
			const std::lock_guard lock( this->m_mtx );
			entries.reserve( this->m_markers.size( ) );

			for ( const auto& m : this->m_markers )
			{
				map_store::entry e{};
				e.position = m.position;
				e.normal = m.normal;
				e.end = m.end;
				e.damage = m.damage;
				e.distance = m.distance;
				entries.push_back( e );
			}
		}

		if ( entries.empty( ) || this->m_map_name.empty( ) )
		{
			return;
		}

		if ( map_store::save( this->m_map_name, entries ) )
		{
			logging::console::print( "[map_scan] saved %zu wallbangs for %s", entries.size( ), this->m_map_name.c_str( ) );
		}
	}

	std::size_t map_scan::restore( )
	{
		if ( this->m_map_name.empty( ) )
		{
			return 0;
		}

		const auto loaded = map_store::load( this->m_map_name );

		if ( loaded.empty( ) )
		{
			return 0;
		}

		const std::lock_guard lock( this->m_mtx );

		// Не дублируем: скан мог уже что-то найти за этот уровень.
		this->m_markers.reserve( this->m_markers.size( ) + loaded.size( ) );
		this->m_marker_seen.reserve( this->m_marker_seen.size( ) + loaded.size( ) );

		auto added{ std::size_t{ 0 } };

		for ( const auto& e : loaded )
		{
			marker m{};
			m.position = e.position;
			m.normal = e.normal;
			m.end = e.end;
			m.damage = e.damage;
			m.distance = e.distance;

			if ( this->find_marker( m ) >= 0 )
			{
				continue;
			}

			// Загруженные метки получают возраст "сейчас": они только что
			// пришли с диска, и стирать их раньше, чем скан успеет их
			// подтвердить или опровергнуть, нельзя.
			this->m_markers.push_back( m );
			this->m_marker_seen.push_back( this->m_frame );
			++added;
		}

		logging::console::print( "[map_scan] restored %zu wallbangs for %s (%zu new, %zu total)",
			loaded.size( ), this->m_map_name.c_str( ), added, this->m_markers.size( ) );

		return loaded.size( );
	}

	bool map_scan::has_wallbang( const std::vector<marker>& markers, const math::vector3& eye, const math::vector3& point )
	{
		// Порог "стена поперёк луча". Косинус 0.35 -- примерно 70 градусов:
		// всё, что ближе к параллели, считается выстрелом вдоль стены, а не
		// сквозь неё.
		constexpr auto k_min_normal_dot{ 0.35f };

		// Насколько близко точка входа маркера должна лежать к луч. Метки
		// снимаются с шагом сетки ~48 юнитов, а стена -- плоскость, так что
		// запас нужен шире расстояния между соседними маркерами.
		constexpr auto k_max_offset{ 96.0f };
		constexpr auto k_max_offset_sqr{ k_max_offset * k_max_offset };

		const auto delta = point - eye;
		const auto length = delta.length( );
		if ( length < 1.0f )
		{
			return false;
		}

		const auto dir = delta / length;

		for ( const auto& m : markers )
		{
			// 1. Стена должна быть поперёк выстрела.
			const auto n = m.normal.normalized( );
			if ( std::abs( dir.dot( n ) ) < k_min_normal_dot )
			{
				continue;
			}

			// 2. Точка входа маркера -- на самом луче, а не просто рядом в
			//    пространстве. Проекция даёт параметр t вдоль луча: он должен
			//    лежать между глазом и целью (иначе окно за целью или за
			//    спиной), а перпендикулярное отклонение -- быть малым.
			const auto to_marker = m.position - eye;
			const auto t = to_marker.dot( dir );

			if ( t <= 0.0f || t >= length )
			{
				continue;
			}

			const auto closest = eye + dir * t;
			if ( ( m.position - closest ).length_sqr( ) > k_max_offset_sqr )
			{
				continue;
			}

			return true;
		}

		return false;
	}

} // namespace features::misc
