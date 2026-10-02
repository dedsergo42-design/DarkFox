#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <vector>

#include <core/systems/systems.hpp>

namespace features::misc {

	// Полный скан карты: ищет прострелы (wallbang / автоваллы) и ставит на них
	// метки.
	//
	// Идея. Автовалл -- это линия, которая пробивает стену: из точки A видно
	// точку B только через геометрию, и пуля туда долетает с остаточным уроном.
	// Чтобы найти такие линии, не нужен BSP: у нас есть родной трассировщик
	// (systems::g_tracing) и родной расчёт пробития
	// (PATTERN(patterns::trace_bullet)) -- то же самое, чем стреляет ragebot.
	//
	// Как ищем:
	//   1. Берём сетку точек вокруг локального игрока (шаг k_step, радиус k_radius).
	//   2. Каждую точку поднимаем на уровень глаз и проверяем трассой на
	//      проходимость: точка не должна быть внутри стены.
	//   3. Считаем, что между двумя точками стены есть прострел, если прямая
	//      между ними пересекает геометрию, но пуля доходит: трассируем и
	//      смотрим остаточный урон через trace_bullet.
	//   4. Совпавшие по сути линии (то же направление в пределах допуска)
	//      схлопываем, чтобы не залить экран метками.
	//
	// Почему инкрементально. Всё крутится на render/game-потоке: своего треда у
	// фич нет (см. dlight -- там просто мьютекс). Полный перебор сетки -- это
	// тысячи трасс, за один кадр это фриз. Поэтому работа нарезана на тики:
	// за кадр обрабатывается k_budget_cells ячеек, остальное -- в следующих.
	class map_scan
	{
	public:
		// Метка прострела. position -- точка на стене (вход пули), normal --
		// нормаль поверхности, damage -- оценка остаточного урона за стеной.
		struct marker
		{
			math::vector3 position{};
			math::vector3 normal{};
			math::vector3 end{};
			float damage{};
			float distance{};
		};

		// Состояние прохода. Нужно, чтобы UI мог показать прогресс, а логика --
		// понять, что скан завершён и результат полный.
		enum class state : std::uint8_t
		{
			idle,
			scanning,
			done
		};

		void on_render( xdraw::draw_list& draw_list );
		void on_create_move( );
		void on_level_change( );

		// Запуск/остановка полного скана. Полный скан перезапускается сам, когда
		// игрок ушёл дальше, чем k_rescan_distance от точки прошлого старта.
		// Позиция берётся из prestate предсказания (systems::g_prediction.pre().origin).
		void start( );
		void stop( );

		[[nodiscard]] state get_state( ) const { return this->m_state.load( std::memory_order_acquire ); }
		[[nodiscard]] bool active( ) const { return this->get_state( ) == state::scanning; }
		[[nodiscard]] float progress( ) const;
		[[nodiscard]] std::size_t marker_count( ) const;

		[[nodiscard]] std::vector<marker> snapshot( ) const;

		// Имя карты, к которой относятся текущие метки. Приходит из
		// level_initialization (см. cheat.cpp) -- там же, где сканер
		// сбрасывается. Именно оно задаёт, в какой файл сохранять и
		// из какого загружать (см. map_store).
		void set_map_name( const std::string& name );

		// Сбросить метки в файл карты. Вызывается один раз, когда скан
		// дошёл до конца: сохранять промежуточные результаты смысла нет,
		// они неполные, и файл от них только хуже.
		void persist( ) const;

		// Подтянуть метки из файла карты. Возвращает число загруженных
		// записей (0 -- файла нет или он битый).
		//
		// Загруженные метки помечаются как готовые: скан всё равно
		// продолжит работу по свежему взгляду (фаза веера), но ragebot
		// получает данные сразу, не дожидаясь полного прохода.
		std::size_t restore( );

		[[nodiscard]] const std::string& map_name( ) const { return this->m_map_name; }

		// Мост в ragebot. Проверяет, идёт ли луч eye -> point через уже
		// найденное окно прострела: ищем маркер, чья нормаль смотрит вдоль
		// луча (стена поперёк пути) и чья точка входа лежит на самом луче.
		//
		// Список передаётся снаружи, чтобы ragebot снял его один раз на тик,
		// а не дёргал мьютекс на каждый hit.
		//
		// Геометрия простая: пусть m -- маркер, n -- его нормаль, d -- луч.
		// Тогда |d.n| ~ 1 означает, что стена стоит поперёк выстрела (то, что
		// нужно), а расстояние от точки входа маркера до прямой луча мало --
		// что выстрел идёт через это самое место.
		[[nodiscard]] static bool has_wallbang( const std::vector<marker>& markers, const math::vector3& eye, const math::vector3& point );

	private:
		static constexpr auto k_step{ 48.0f };              // шаг сетки, юнитов
		static constexpr auto k_radius{ 384.0f };           // радиус обхода вокруг игрока
		static constexpr auto k_eye_z{ 64.0f };             // уровень глаз над полом
		static constexpr auto k_probe_down{ 128.0f };       // куда падаем, чтобы найти пол
		static constexpr auto k_probe_up{ 96.0f };          // сколько отступаем вверх
		static constexpr auto k_budget_cells{ 24 };         // ячеек за кадр (пол)
		static constexpr auto k_budget_cells_min{ 6 };      // пол бюджета при просадке кадра
		static constexpr auto k_budget_cells_max{ 96 };     // потолок при запасе
		static constexpr auto k_min_damage{ 1.0f };         // ниже -- не прострел
		static constexpr auto k_dedupe_dot{ 0.985f };       // схлопывание линий
		static constexpr auto k_dedupe_dist_sqr{ 64.0f * 64.0f };
		static constexpr auto k_max_markers{ 512 };

		// Дистанция авто-пересканирования. Результат скана -- это окна
		// прострела вокруг ТОЧКИ СТАРТА, и на 256 юнитах он протухает
		// ровно так же, как на 2048: сетка радиусом 384 перекрывает
		// 256 юнитов смещения почти целиком. Разница только в цене.
		//
		// 256 юнитов игрок проходит меньше чем за секунду бега, поэтому
		// скан перезапускался непрерывно: каждая итерация -- перестройка
		// сетки 17x17, повторный обход пар и веер на 900 трасс. Именно
		// это и читалось как постоянные микрозаикания в движении.
		//
		// 1024 юнита -- треть радиуса сетки: полный проход успевает
		// закончиться (при бюджете 24 ячейки/кадр это ~19 кадров, то есть
		// меньше трети секунды на 60 fps), а перезапуск случается раз в
		// несколько секунд бега, а не каждую.
		static constexpr auto k_rescan_distance{ 1024.0f };

		// Пауза между проходами. Даже на дистанции перезапуска полный скан
		// стоит денег, а результат между двумя последовательными проходами
		// почти не меняется: противник не переставляет карту. Держим
		// минимальный интервал, чтобы стоящий на месте игрок не сканировал
		// карту в цикле.
		static constexpr auto k_rescan_cooldown{ 2.0f };

		// Метки живут дольше интервала скана: пока фича включена, окна,
		// найденные прошлым проходом, остаются валидными.
		static constexpr auto k_marker_lifetime{ 120.0f };

		// Бюджет трасс на кадр. Бюджет ячеек ничего не говорит о цене: одна
		// ячейка -- это луч к каждой следующей ячейке, и на плотной сетке
		// 24 ячейки дают сотни трасс в одном кадре. Именно поэтому скан
		// «заикался» на входе в новую зону. Теперь цена ограничена явно,
		// а k_budget_cells работает как вторичный предохранитель.
		static constexpr auto k_trace_budget{ 900 };
		static constexpr auto k_trace_budget_min{ 220 };

		// Шаг сетки, снятый при построении: на плотной геометрии шаг можно
		// уменьшить и найти окна, которые 48 юнитов перепрыгивают.
		static constexpr auto k_step_min{ 24.0f };

		// Второй проход -- веер по прицелу.
		//
		// Сетка вокруг игрока находит окна в произвольных местах, но ragebot
		// стреляет в конкретную цель, и она почти всегда впереди игрока, а не
		// сбоку. Веер продлевает лучи от глаз по направлению взгляда: это
		// ловит окна, которые сетка пропускает из-за шага 48, и делает это
		// ровно там, где сейчас идёт бой.
		static constexpr auto k_fan_rays{ 64 };             // лучей в веере
		static constexpr auto k_fan_length{ 1500.0f };      // длина луча веера, юнитов
		static constexpr auto k_fan_half_angle{ 45.0f };    // полураствор веера, градусов
		static constexpr auto k_fan_probe{ 24.0f };         // шаг съёма окон вдоль луча

		struct sample
		{
			math::vector3 point{};
			math::vector3 floor{};
			bool valid{};
		};

		// Бюджет трасс. Каждый вызов обёртки списывает единицу; когда счётчик
		// исчерпан, обёртка возвращает "нет попадания", и вызывающий цикл
		// штатно выходит. Через него проходят ВСЕ трассы фазы сетки -- иначе
		// считать нечего.
		struct trace_budget
		{
			int remaining{};
			[[nodiscard]] bool spent( ) const { return this->remaining <= 0; }
			[[nodiscard]] bool take( ) { return --this->remaining >= 0; }
		};

		void rebuild_grid( const math::vector3& origin );
		void step_grid_fill( );
		void step_scan( );
		void step_fan( );
		void emit_wallbang( const math::vector3& from, const math::vector3& to, trace_budget* budget = nullptr );
		void sweep_ray( const math::vector3& eye, const math::vector3& dir, float length, trace_budget* budget );
		[[nodiscard]] bool point_standable( const math::vector3& point, math::vector3& floor_out ) const;
		[[nodiscard]] bool cull_marker( const marker& candidate ) const;

		// Индекс уже существующей метки, совпадающей с кандидатом по тому же
		// критерию, что и cull_marker. -1 -- такой метки нет.
		[[nodiscard]] int find_marker( const marker& candidate ) const;

		// Стереть метки, которые не подтверждались k_marker_ttl_frames кадров.
		void prune_markers( );

		[[nodiscard]] bool trace( const math::vector3& from, const math::vector3& to, systems::tracing::result& out, trace_budget* budget = nullptr ) const;

		// Возраст меток в кадрах: растёт в on_render, используется при
		// стирании.
		std::uint32_t m_frame{};

		[[nodiscard]] static math::vector3 eye_offset( )
		{
			return { 0.0f, 0.0f, k_eye_z };
		}

		mutable std::mutex m_mtx{};

		// Кэш публичного снимка меток. ragebot зовёт snapshot() до четырёх раз
		// за тик, а метки за тик не меняются (их обновляет только update() и
		// ровно раз в кадр, увеличивая m_frame), поэтому копировать сотни
		// структур под мьютексом каждый раз незачем. Ключ -- m_frame.
		mutable std::vector<marker> m_snapshot_cache{};
		mutable std::uint32_t m_snapshot_cache_frame{};
		mutable bool m_snapshot_cache_valid{ false };

		// Две фазы одного прохода: сначала сетка вокруг игрока, потом веер по
		// прицелу. Разделены, потому что веер зависит от текущего взгляда, а
		// сетка -- нет: сетку можно считать один раз и долго, веер -- каждый
		// тик заново по свежему углу.
		//
		// У сетки, в свою очередь, две ступени: раскладка (мгновенная) и
		// заполнение трассами (по бюджету за кадр). Пока fill не закончен,
		// обход пар начинать нельзя -- половина точек ещё невалидна.
		enum class phase : std::uint8_t
		{
			grid,
			fan
		};

		std::atomic<state> m_state{ state::idle };
		std::atomic<phase> m_phase{ phase::grid };
		std::atomic<int> m_cursor{ 0 };
		std::atomic<int> m_total{ 0 };

		// Заполнение сетки: сколько ячеек уже получило трассу.
		std::atomic<int> m_grid_fill_cursor{ 0 };
		std::atomic<bool> m_grid_ready{ false };

		std::vector<sample> m_grid{};
		std::vector<marker> m_markers{};

		// Метки с меткой времени, по индексу совпадает с публичными
		// снимками. Нужна отдельно от m_markers, потому что наружу (в
		// snapshot/has_wallbang) возраст не течёт: ragebot должен видеть
		// только саму геометрию.
		std::vector<std::uint32_t> m_marker_seen{};

		math::vector3 m_origin{};
		math::vector3 m_scan_origin{};
		bool m_has_scan_origin{};

		// Время старта последнего прохода. Отсчитывается от секунд аптайма
		// рендера (xdraw::delta_time аккумулируется в on_render), поэтому
		// здесь не нужен системный таймер: кд живёт ровно столько, сколько
		// идёт матч.
		float m_last_scan_time{};
		bool m_has_scan_time{};

		// Аккумулятор времени рендера. Растёт в on_render на delta_time, и
		// только через него меряется пауза между проходами: create_move
		// тикает чаще кадра, и брать время из него значило бы получить
		// разную шкалу на разном тикрейте.
		float m_elapsed{};

		// Сколько лучей веера уже выпущено в этом проходе и каким был взгляд на
		// прошлом тике: пока игрок не повернулся заметно, веер пересчитывать
		// незачем.
		int m_fan_cursor{};
		math::vector3 m_last_fan_angles{};
		math::vector3 m_last_fan_eye{};
		bool m_has_fan_angles{};

		// Насколько нужно сместиться, чтобы веер стоило пересчитать. 64 юнита
		// -- меньше, чем любое осмысленное перемещение, но больше, чем
		// болтанка позиции предсказания на месте.
		static constexpr auto k_fan_eye_move_sqr{ 64.0f * 64.0f };

		// Имя карты и признак "файл уже прочитан". Второй нужен, чтобы не
		// дёргать диск на каждом тике: restore() вызывается один раз на
		// уровень, при первом же on_render, а не при set_map_name -- к
		// моменту level_initialization файловая система может быть ещё
		// занята загрузкой карты.
		std::string m_map_name{};
		bool m_restored{};

		// Сохранение -- операция с диском, её нельзя делать под мьютексом
		// меток: скан пишет метки с render-потока, и блокировка на время
		// записи файла застопорила бы кадр. Поэтому копия снимается под
		// мьютексом, а пишется уже без него.
		std::atomic<bool> m_pending_save{ false };
	};

} // namespace features::misc
