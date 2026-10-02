#include <pch/pch.hpp>

#include <core/features/combat/impl/head_model.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>

namespace features::combat
{
	namespace
	{
		// Сетка прощупа по поверхности головы.
		//
		// Голова -- капсула, и её силуэт со стороны стрелка меняется слабо
		// (десяток градусов поворота не меняет, какие участки открыты), но
		// эффект укрытия меняется скачками: колонна перекрывает сектор
		// целиком. Поэтому сетка нужна не очень плотная, но обязательно
		// равномерная по сфере, иначе полюсные участки (макушка) получат
		// втрое больше отсчётов, чем бока.
		//
		// 3 кольца по широте x 16 по долготе = 48 направлений плюс два
		// полюса. Это 50 трасс на голову, и они идут только тогда, когда
		// стоит worth_probing -- то есть когда голова вообще в конусе.
		constexpr auto k_rings{ 3 };
		constexpr auto k_per_ring{ 16 };
		constexpr auto k_probe_budget{ 64 };

		// Сколько из открытых участков отдавать в выборку точек прицеливания.
		// Больше десяти смысла нет: предфильтр в select_best всё равно
		// оставляет четыре на хитбокс.
		constexpr auto k_max_points{ 12 };

		// Участок считается "открытым", если линия до стрелка не упирается
		// в стену раньше, чем в саму голову. Порог по fraction, а не по
		// геометрии: трасса возвращает fraction=1.0, когда не встретила
		// вообще ничего, и тогда голова открыта по определению.
		constexpr auto k_open_fraction{ 0.985f };

		// Насколько открытым должен быть участок, чтобы считаться реально
		// пригодным. Ниже -- это уже скользящий луч по касательной, по
		// такой поверхности пуля уйдёт в сторону.
		constexpr auto k_min_openness{ 0.15f };

		// Максимальная толщина стены, которую считаем пробиваемой для
		// целей выбора точки. Совпадает с логикой wallbang-скана.
		constexpr auto k_max_pen_thickness{ 60.0f };

		float openness_of( const math::vector3& normal, const math::vector3& to_eye )
		{
			// Нормаль поверхности против направления на стрелка: 1.0 --
			// участок смотрит прямо на нас, 0.0 -- строго по касательной
			// или отвёрнут. Это ровно косинус угла, под которым мы видим
			// участок, то есть его видимая площадь.
			return std::clamp( normal.normalized( ).dot( to_eye.normalized( ) ), 0.0f, 1.0f );
		}
	}

	head_model::probe_result head_model::probe_direction( const math::vector3& eye,
		const math::vector3& center,
		const math::vector3& capsule_a,
		const math::vector3& capsule_b,
		float radius,
		const math::vector3& dir ) const
	{
		probe_result out{};
		const auto d = dir.normalized( );

		// Куда луч из центра упирается в поверхность капсулы. Считаем
		// аналитически, а не трассой: нам нужна точка И нормаль, а трасса
		// по хитбоксу даёт только позицию, и то если повезёт с фильтром.
		const auto mid = ( capsule_a + capsule_b ) * 0.5f;
		const auto axis = capsule_b - capsule_a;
		const auto axis_len = axis.length( );

		if ( axis_len > 0.001f && radius > 0.001f )
		{
			const auto axis_dir = axis / axis_len;
			const auto from_a = d * radius + ( mid - capsule_a );

			// Проекция на ось: точка на боковой поверхности или на полусфере.
			const auto along = from_a.dot( axis_dir );
			math::vector3 surface{};

			if ( along > axis_len )
			{
				surface = capsule_b + d * radius;
			}
			else if ( along < 0.0f )
			{
				surface = capsule_a + d * radius;
			}
			else
			{
				// Боковая поверхность: точка оси на высоте along плюс
				// перпендикулярная составляющая направления, растянутая
				// до радиуса.
				const auto on_axis = capsule_a + axis_dir * along;
				const auto perp = d - axis_dir * d.dot( axis_dir );
				const auto perp_len = perp.length( );

				surface = perp_len > 0.0001f
					? on_axis + ( perp / perp_len ) * radius
					: on_axis + d * radius;
			}

			out.surface = surface;
			out.normal = ( surface - mid ).normalized( );
		}
		else
		{
			out.surface = center + d * std::max( radius, 1.0f );
			out.normal = d;
		}

		// Основной тест: видна ли эта точка поверхности из глаза.
		const auto to_surface = out.surface - eye;
		const auto distance = to_surface.length( );

		if ( distance < 1.0f )
		{
			out.valid = true;
			out.clean = true;
			return out;
		}

		// Пропускаем локальную пешку, а не ноль: make_filter превращает entity
		// в handle, и нулевой entity даёт handle -1, на котором трассировка
		// падает внутри игры. Без пешки (экран выбора команды) тест не делаем.
		const auto local_pawn = systems::g_local.get( ).pawn;
		if ( !local_pawn )
		{
			out.valid = false;
			return out;
		}

		// Фильтр пешки не меняется за время прощупа: он зависит только от
		// (пашка, маска, слой). Готовим его один раз на запись, а не на каждый
		// из 64 прощупов. См. filter_cache в tracing.
		thread_local std::uintptr_t cached_pawn{ 0 };
		thread_local systems::tracing::filter cached_vision_filter{};

		if ( cached_pawn != local_pawn || cached_vision_filter.vtable == 0 )
		{
			cached_vision_filter = systems::g_tracing.make_filter_cached( local_pawn, 0x1c3003, 4 );
			cached_pawn = local_pawn;
		}

		const auto vision = systems::g_tracing.trace_fast( eye, out.surface + ( out.surface - eye ).normalized( ) * -0.5f, cached_vision_filter );

		if ( vision.fraction >= k_open_fraction )
		{
			out.valid = true;
			out.clean = true;
			return out;
		}

		// Стена. Смотрим, пробиваема ли она до этой точки поверхности:
		// если да -- участок тоже годен, просто с меньшим весом.
		const auto wall_thickness = ( out.surface - vision.position ).length( );

		if ( wall_thickness <= k_max_pen_thickness && wall_thickness > 0.01f )
		{
			const auto beyond = systems::g_tracing.trace_fast( vision.position + ( out.surface - eye ).normalized( ) * 1.0f, out.surface, cached_vision_filter );

			if ( beyond.fraction >= k_open_fraction )
			{
				out.valid = true;
				out.penetrable = true;
			}
		}

		return out;
	}

	bool head_model::worth_probing( const math::vector3& eye,
		const math::vector3& head_center,
		float radius,
		float inaccuracy,
		float spread )
	{
		const auto delta = head_center - eye;
		const auto distance = delta.length( );

		if ( distance < 1.0f )
		{
			return true;
		}

		// Угловой радиус головы и угловая ширина конуса. Если голова
		// целиком шире конуса -- прощуп заведомо бессмыслен, стрелять
		// всё равно в центр.
		const auto angular_radius = std::atan2f( radius, distance );
		const auto cone = std::max( inaccuracy + spread, 0.0f );

		return cone < angular_radius * 3.0f;
	}

	void head_model::sort_points( std::vector<sample>& samples )
	{
		std::stable_sort( samples.begin( ), samples.end( ), []( const sample& a, const sample& b )
			{
				// Сначала чистые (не через стену), потом по открытости.
				if ( a.clean != b.clean )
				{
					return a.clean;
				}

				return a.openness > b.openness;
			} );
	}

	head_model::analysis head_model::run( const math::vector3& eye,
		const math::vector3& head_center,
		const math::vector3& head_a,
		const math::vector3& head_b,
		float radius,
		float scale,
		std::vector<math::vector3>& out ) const
	{
		analysis result{};
		out.clear( );

		if ( radius <= 0.001f )
		{
			result.aim_point = head_center;
			return result;
		}

		const auto to_eye = ( eye - head_center ).normalized( );
		auto probes{ 0 };

		std::vector<sample> opened;
		opened.reserve( k_probe_budget );

		// Строим базис вокруг направления на стрелка: он задаёт "лицо"
		// головы с нашей точки зрения, и именно на нём нужна наибольшая
		// плотность отсчётов.
		math::vector3 axis_x{}, axis_y{};
		{
			// Любой вектор, не параллельный to_eye.
			const auto seed = std::fabs( to_eye.z ) < 0.9f
				? math::vector3{ 0.0f, 0.0f, 1.0f }
				: math::vector3{ 1.0f, 0.0f, 0.0f };

			axis_x = to_eye.cross( seed ).normalized( );
			axis_y = to_eye.cross( axis_x ).normalized( );
		}

		// Угловая сетка вокруг направления на стрелка. Полюс (+to_eye) --
		// это точка, смотрящая прямо на нас: главный кандидат. Дальше
		// кольца по 16 направлений с шагом 30 градусов.
		for ( auto ring = 0; ring <= k_rings; ++ring )
		{
			const auto polar = static_cast< float >( ring ) * 30.0f;
			const auto polar_rad = math::helpers::deg_to_rad( polar );
			const auto sin_p = std::sinf( polar_rad );
			const auto cos_p = std::cosf( polar_rad );

			const auto count = ring == 0 ? 1 : k_per_ring;

			for ( auto i = 0; i < count; ++i )
			{
				// Бюджет прощупов проверяем ДО трассы, а не после неё.
				//
				// Раньше ++probes и break стояли на разных концах тела цикла:
				// бюджет исчерпывался, но `break` срабатывал только если
				// предыдущий прощуп оказался валидным и открытым. Закрытая
				// голова (типичный случай -- цель за укрытием) не проходила
				// эту ветку НИ РАЗУ, и вместо 64 прощупов сетка
				// отрабатывала все 49 (4 кольца: 1+16+16+16) -- а на каждое,
				// найдись стена, шло по две трассы.
				if ( probes >= k_probe_budget )
				{
					break;
				}

				const auto azimuth = static_cast< float >( i ) * ( 360.0f / static_cast< float >( count ) );
				const auto az_rad = math::helpers::deg_to_rad( azimuth );
				const auto cos_a = std::cosf( az_rad );
				const auto sin_a = std::sinf( az_rad );

				const auto dir = ( to_eye * cos_p + axis_x * ( sin_p * cos_a ) + axis_y * ( sin_p * sin_a ) ).normalized( );

				const auto probe = this->probe_direction( eye, head_center, head_a, head_b, radius, dir );
				++probes;

				if ( !probe.valid )
				{
					continue;
				}

				sample s{};
				s.position = probe.surface;
				s.surface_normal = probe.normal;
				s.clean = probe.clean;
				s.penetrable = probe.penetrable;
				s.openness = openness_of( probe.normal, to_eye );

				// Участки через стену засчитываются с пониженным весом:
				// они годятся, но прострел ослабляет урон и зависит от
				// оружия.
				if ( s.penetrable )
				{
					s.openness *= 0.5f;
				}

				if ( s.openness < k_min_openness )
				{
					continue;
				}

				opened.push_back( s );
			}

			if ( probes >= k_probe_budget )
			{
				break;
			}
		}

		result.any_visible = !opened.empty( );

		if ( opened.empty( ) )
		{
			// Голова либо полностью закрыта, либо видна строго по
			// касательной. Отдаём центр -- пусть решает обычная логика.
			result.aim_point = head_center;
			result.has_open_head = false;
			m_last = result;
			return result;
		}

		sort_points( opened );

		result.samples = opened;
		result.best_openness = opened.front( ).openness;

		// Доля открытой поверхности от общего числа прощупов. Это не
		// вероятность попадания, но хороший индикатор "торчит ли голова".
		result.coverage = std::clamp(
			static_cast< float >( opened.size( ) ) / static_cast< float >( std::max( probes, 1 ) ),
			0.0f, 1.0f );

		// Точки прицеливания -- открытые участки, стянутые к центру капсулы
		// на заданный масштаб. При scale=1 точки лежат на поверхности;
		// при меньшем -- внутри, что и нужно для узкого конуса: чем меньше
		// конус, тем точнее надо целиться, и тем меньше смысла в крайних
		// точках.
		const auto point_scale = std::clamp( scale, 0.0f, 1.0f );

		for ( const auto& s : result.samples )
		{
			if ( static_cast< int >( out.size( ) ) >= k_max_points )
			{
				break;
			}

			out.push_back( head_center + ( s.position - head_center ) * point_scale );
		}

		if ( out.empty( ) )
		{
			result.aim_point = head_center;
			result.has_open_head = false;
			m_last = result;
			return result;
		}

		// Лучшая точка -- первая после сортировки, но с обязательным
		// условием: она должна быть чистой. Если все открытые участки
		// только через стену, прицел всё равно берём, но "открытой
		// головы" не объявляем -- это прострел, не чистый выстрел.
		result.aim_point = out.front( );
		result.has_open_head = result.samples.front( ).clean;

		m_last = result;
		return result;
	}

	const head_model::analysis& head_model::last( ) const
	{
		return m_last;
	}

	void head_model::clear_last( )
	{
		m_last = {};
	}
}
