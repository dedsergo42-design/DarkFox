#pragma once

#include <core/systems/systems.hpp>
#include <utilities/cstypes.hpp>

namespace features::combat
{
	// Сканер открытой головы по МОДЕЛИ цели.
	//
	// Зачем отдельный модуль, а не ещё десяток мультиточек.
	//
	// generate_multipoints строит точки чисто геометрически: центр капсулы
	// плюс смещения right/up/diagonals. Он ничего не знает о том, что
	// реально торчит из-за укрытия. В ситуации "враг за колонной, видно
	// только пол-лица" все девять точек строятся симметрично вокруг центра
	// капсулы, и семь из них сидят внутри колонны -- их отбрасывает
	// penetration-трасса, и до скоринга доживает одна-две. Хитчасть на них
	// низкая, выстрела нет, хотя открытая половина головы была.
	//
	// Здесь делается обратное: сначала трассируется САМА КАПСУЛА головы --
	// сетка лучей из точки стрельбы по поверхности хитбокса, -- и
	// отмечается, какие участки поверхности дают чистую линию (или
	// пробиваемую стену). Потом точки строятся только на открытых участках,
	// причём в порядке убывания "открытости": угол между нормалью
	// поверхности и направлением на стрелка. Это и есть поиск открытой
	// головы по модели.
	//
	// Всё состояние -- на один вызов: сканер не хранит ничего между тиками,
	// кроме кэша последнего результата, который нужен только для отрисовки
	// диагностики в меню.
	class head_model
	{
	public:
		// Один прощупанный участок поверхности головы.
		struct sample
		{
			math::vector3 position{};      // точка на поверхности капсулы (в мире)
			math::vector3 surface_normal{}; // нормаль поверхности в этой точке
			float openness{};              // 0..1, насколько участок открыт
			bool clean{};                  // чистая прямая видимость, без стены
			bool penetrable{};             // за стеной, но стена пробивается
		};

		// Результат анализа одной головы.
		struct analysis
		{
			std::vector<sample> samples{};   // только открытые участки, по убыванию openness
			math::vector3 aim_point{};       // лучшая точка прицеливания
			float best_openness{};
			float coverage{};                // доля открытой поверхности 0..1
			bool has_open_head{};            // есть ли вообще открытый участок
			bool any_visible{};              // видна ли голова хоть как-то
		};

		// Прощупать голову. Все параметры -- про конкретный выстрел:
		// откуда стреляем, каким оружием (для пробиваемости), и какой
		// масштаб точки использовать.
		//
		// eye         -- позиция глаза стрелка (источник лучей)
		// head_center -- центр капсулы головы
		// head_a/b    -- концы капсулы головы (мировая система)
		// radius      -- радиус капсулы
		// scale       -- масштаб точек 0..1 (как в generate_multipoints)
		// out         -- куда сложить точки прицеливания (по убыванию качества)
		//
		// Возвращает анализ; out заполняется только открытыми точками.
		[[nodiscard]] analysis run( const math::vector3& eye,
			const math::vector3& head_center,
			const math::vector3& head_a,
			const math::vector3& head_b,
			float radius,
			float scale,
			std::vector<math::vector3>& out ) const;

		// Дешёвая проверка "есть ли вообще смысл запускать полный прощуп".
		// Считает, покрывает ли конус разброса хотя бы край капсулы.
		[[nodiscard]] static bool worth_probing( const math::vector3& eye,
			const math::vector3& head_center,
			float radius,
			float inaccuracy,
			float spread );

		// Последний результат -- для отрисовки в меню/оверлее.
		[[nodiscard]] const analysis& last( ) const;
		void clear_last( );

	private:
		// Лучи по сфере вокруг головы. Возвращает нормаль поверхности,
		// куда луч упёрся, и результат трассы до стрелка.
		struct probe_result
		{
			math::vector3 surface{};
			math::vector3 normal{};
			bool clean{};
			bool penetrable{};
			bool valid{};
		};

		[[nodiscard]] probe_result probe_direction( const math::vector3& eye,
			const math::vector3& center,
			const math::vector3& capsule_a,
			const math::vector3& capsule_b,
			float radius,
			const math::vector3& dir ) const;

		// Сортировка точек: сначала самые открытые, потом ближние к глазу.
		static void sort_points( std::vector<sample>& samples );

		mutable analysis m_last{};
	};
}
