#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>
#include <vector>

#include <core/systems/systems.hpp>

namespace features::misc {

	// Оптимизация рендера.
	//
	// Задача: снять с GPU и CPU всё, что не участвует в геймплее, и не сломать
	// при этом стрельбу. Отсюда деление на три группы по «риску»:
	//
	//   1. Полный вырез объектов -- частицы, декали, трава, листва, тросы,
	//      мелкая пропа. Это чистая экономия: объект не рисуется, draw call не
	//      выдаётся, сортировка примитивов по нему не работает.
	//
	//   2. Упрощение материала -- плоское освещение, вырез normal/specular/
	//      detail-слоёв. Пиксельный шейдер становится короче, но геометрия и
	//      коллизия остаются на месте.
	//
	//   3. Вырез пост-обработки -- bloom, DOF, motion blur, SSAO. Каждый из них
	//      это отдельный full-screen проход, поэтому тут самая дешёвая победа
	//      по FPS на пиксель.
	//
	// Почему через set_shader_param, а не через материалы. Часть пост-эффектов
	// (bloom, DOF) движок включает не флагом на материале, а записью
	// полноэкранного шейдер-параметра -- ровно тем путём, которым уже ходят
	// gamma/bloom в scene.cpp. Перехватывать материал тут бессмысленно: до
	// материала этот эффект вообще не доходит.
	//
	// Стрельба не страдает: ни один из путей ниже не трогает
	// systems::g_tracing, оффсеты сцены не переписываются, а вырезается только
	// то, что уходит в рендер. Трава, листва и пропа не входят в world-mask
	// трассировщика и пулю не останавливают.
	class optimization
	{
	public:
		// Категория объекта, выведенная из имени его материала. Именно
		// категория, а не булев флаг: у выреза и у упрощения разные списки, и
		// один и тот же объект может попадать в оба.
		//
		// Значения совпадают с индексами в prefix-таблице, поэтому смена
		// порядка здесь обязана сопровождаться правкой таблицы в .cpp.
		enum class category : std::uint8_t
		{
			none = 0,
			grass,
			foliage,
			rope,
			particle,
			beam,
			glow_sprite,
			weather,
			prop,
			decal
		};

		// Публичный псевдоним: таблица правил живёт в анонимном namespace
		// .cpp и типизируется по нему.
		using category_kind = category;

		void on_level_change( );
		void on_frame_stage_notify( );

		// Хук m_light_scene_object: сюда приходят сами источники света, и
		// только тут их цвет вообще можно переписать.
		void on_light_scene_object( std::uintptr_t object ) const;

		// Хук m_draw_scene_object (scenesystem.dll). batch -- массив примитивов
		// по 0x70 байт, batch_count -- их число.
		void on_draw_scene_object( std::uintptr_t batch, int batch_count );

		// Хук m_generate_primitives. Вызывается до построения примитивов,
		// поэтому принудительный LOD успевает примениться в текущем кадре.
		void on_scene_object( std::uintptr_t scene_object ) const;

		// Хук m_prepare_scene_material (materialsystem2.dll). Возвращает true,
		// если вызывающий должен пропустить объект целиком.
		[[nodiscard]] bool should_skip_object( std::uintptr_t material );

		// Хук m_set_shader_param. Возвращает значение для подмены или исходный
		// указатель, если параметр нас не касается.
		[[nodiscard]] __m128i* intercept_shader_param( std::uint32_t hash, __m128i* value );

		// Хук m_setup_fog. true -- полностью подменить результат.
		[[nodiscard]] bool override_fog( __m128i* output, int* mode ) const;

		void on_render( xdraw::draw_list& draw_list );

		[[nodiscard]] bool active( ) const { return settings::g_misc.m_optimization.enabled.value; }

		// Сколько объектов реально ушло из кадра за последний кадр и сколько
		// было рассмотрено. Панель статистики показывает их отношение.
		[[nodiscard]] std::uint64_t culled( ) const { return this->m_culled_objects; }
		[[nodiscard]] std::uint64_t seen( ) const { return this->m_seen_objects; }

	private:
		[[nodiscard]] static category classify( std::string_view name );
		[[nodiscard]] static std::string_view normalize( std::string_view name );

		// Достаёт имя материала через vtable-0 и классифицирует. Имя бывает
		// недоступно для некоторых типов материалов, поэтому результат
		// отрицательной классификации тоже кэшируется.
		[[nodiscard]] category classify_material( std::uintptr_t material );

		// Отдельный от classify признак: модель игрока обязана попасть в
		// k_never_touch (её нельзя вырезать), но картон для неё строится по
		// другому шейдеру. Один флаг на оба вопроса не годится.
		[[nodiscard]] bool is_player_material( std::uintptr_t material );

		[[nodiscard]] bool should_cull( category kind ) const;

		// Гасит тени на живом материале: флаги каста/приёма и дистанцию
		// затухания. Вызывается из should_skip_object, где материал уже
		// готов и лежит в своей финальной раскладке параметров.
		void strip_shadows( std::uintptr_t material ) const;

		// Плоский материал: та же геометрия и шейдер, но выключенное
		// освещение и снятые слои. Строится из KV один раз на набор флагов,
		// затем переиспользуется -- материал неизменяем после создания.
		[[nodiscard]] std::uintptr_t prefixed_material( );

		// Картонный материал для конкретного исходного. Кэшируется по
		// (исходный материал, сигнатура настроек, признак игрока): при смене
		// цвета старая запись становится мусором, поэтому сигнатура входит в
		// ключ, а не хранится отдельным полем.
		[[nodiscard]] std::uintptr_t cardboard_material( std::uintptr_t source, bool player_skin );

		[[nodiscard]] std::uintptr_t build_flat_material( );
		[[nodiscard]] std::uintptr_t build_cardboard_material( std::uintptr_t source, bool player_skin );

		void update_stats( float frame_time );

		// Кэш решения по объекту сцены.
		//
		// Без кэша мы бы считали FNV-хеш имени материала на каждом объекте
		// каждый кадр, а это единственная реально дорогая операция в hot-path:
		// имя достаётся виртуальным вызовом. Один объект живёт кадрами, поэтому
		// кэш окупается сразу.
		//
		// Размер подобран замером, а не на глаз. Промах стоит виртуального
		// вызова плюс два safe_read (каждый из них строит diag::probe_scope),
		// поэтому промахи -- самая дорогая часть кадра в этой фиче. На наборе
		// из ~2000 материалов со разбросом адресов, характерным для кучи CS2,
		// установившийся промах на кадр составляет:
		//
		//     4096 слотов  -> 39.2%
		//    16384 слотов  -> 11.7%
		//    65536 слотов  ->  3.3%
		//
		// Слот -- три поля (uint32 ключ, uintptr значение, uint32 кадр), то
		// есть 16 байт с выравниванием, и 65536 слотов стоят ровно 1 МиБ. На
		// фоне того, что чит держит кэши текстур и материалов на порядок
		// больше, это не цена.
		//
		// Открытая адресация без вытеснения: коллизия стоит одного пересчёта на
		// следующем кадре, что безвредно. Лок не нужен -- хук живёт на
		// render-потоке, как и кэш отброшенных текстур в cheat.cpp.
		static constexpr std::size_t k_object_cache_slots{ 65536 };

		struct object_slot
		{
			std::atomic<std::uint32_t> key{};
			std::atomic<std::uintptr_t> value{};
			std::atomic<std::uint32_t> frame{};
		};

		std::array<object_slot, k_object_cache_slots> m_object_cache{};

		// Кэш картонных материалов. Ключ -- исходный материал плюс
		// сигнатура настроек; значение -- созданный материал. Живёт на
		// render-потоке, лок не нужен.
		static constexpr std::size_t k_cardboard_cache_slots{ 512 };

		struct cardboard_slot
		{
			std::uint64_t key{};
			std::uintptr_t value{};
		};

		std::array<cardboard_slot, k_cardboard_cache_slots> m_cardboard_cache{};

		std::uint32_t m_frame{};

		std::uintptr_t m_flat_material{};
		std::uint32_t m_flat_signature{};
		bool m_flat_ready{};

		std::uint32_t m_cardboard_signature{};

		// Подменённые пост-обработкой значения. Живут между вызовами, потому
		// что указатель на них отдаётся наружу и используется движком уже
		// после возврата из хука.
		static inline __m128 s_zero{};
		static inline __m128 s_color{};
		static inline __m128 s_fog_params{};
		static inline __m128 s_fog_params_2{};
		static inline __m128 s_fog_params_3{};

		// Статистика для панели.
		float m_smoothed_frame_time{};
		float m_smoothed_cull_ratio{};
		std::uint64_t m_culled_objects{};
		std::uint64_t m_seen_objects{};
	};

} // namespace features::misc
