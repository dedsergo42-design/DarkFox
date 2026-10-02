#pragma once

#include <core/systems/systems.hpp>
#include <utilities/cstypes.hpp>

namespace features::combat {

	constexpr int k_max_skeleton_bones = 128;

	class shared
	{
	public:
		class lagcomp
		{
		public:
			struct record
			{
				std::uintptr_t pawn{};
				std::uintptr_t game_scene_node{};
				std::uintptr_t bone_cache{};
				int bone_count{};

			systems::bones::data bones[ k_max_skeleton_bones ]{};
			systems::bones::data bones_backup[ k_max_skeleton_bones ]{};

				math::vector3 origin{};
				math::vector3 rotation{};

				float simulation_time{};
				int tick{};
				bool valid{};
				bool was_valid{};
				bool is_applied{};
				bool extrapolated{};
				std::uint32_t flags{};

				bool setup( std::uintptr_t pawn );
				[[nodiscard]] bool is_valid( ) const;

				void apply( );
				void restore( );
			};

			struct visual_record
			{
				math::vector3 origin{};
				std::array<systems::bones::data, 27> bones{};
			};

			struct extrapolation_data
			{
				math::vector3 origin{};
				math::vector3 velocity{};
				math::vector3 obb_mins{};
				math::vector3 obb_maxs{};
				std::uint32_t flags{};
				float surface_friction{ 1.0f };
				float sv_gravity{ 800.0f };
				float sv_friction{ 4.0f };
				float sv_stopspeed{ 2.0f };
				float sim_time{};
				float direction{};
			};

			void run( );

			[[nodiscard]] record* get_oldest_valid( std::uintptr_t pawn );
			[[nodiscard]] record* get_oldest_was_valid( std::uintptr_t pawn );
			[[nodiscard]] std::optional<visual_record> get_oldest_was_valid_visual( std::uintptr_t pawn ) const;
			[[nodiscard]] std::vector<record*> get_valid_records( std::uintptr_t pawn );
			[[nodiscard]] std::array<systems::bones::data, 27> get_skeleton( const record& record ) const;

			[[nodiscard]] std::optional<record> extrapolate( std::uintptr_t pawn );

			[[nodiscard]] math::vector3 predict_position_physics( std::uintptr_t pawn, const math::vector3& start_origin, const math::vector3& velocity, std::uint32_t flags, const math::vector3& obb_mins, const math::vector3& obb_maxs, int ticks ) const;

		private:
			void predict_movement( extrapolation_data& data, std::uintptr_t skip_entity ) const;

			std::unordered_map<std::uintptr_t, std::deque<record>> m_records{};
			mutable std::shared_mutex m_records_mtx{};
		};

		// resolver - reconstructed from impl/shared.cpp (original combat.hpp declaration was lost when the file was overwritten)
		class resolver
		{
		public:
			enum class yaw_side : std::uint8_t
			{
				none,
				real,
				left,
				right
			};

			struct resolve_data
			{
				float last_shot_time{};
				float last_update_time{};
				int last_shot_hitbox{};
				int hits{};
				int misses{};
				int head_miss_streak{};
				bool force_body_aim{};
				float force_body_until{};
				bool has_angle_data{};
				float last_eye_yaw{};
				float last_body_yaw{};
				float detected_desync{};
				int history_index{};
				int history_count{};
				float desync_history[ 8 ]{};
				yaw_side brute_side{ yaw_side::real };
			};

			void on_shot_fired( std::uintptr_t target_pawn );
			void on_shot_target_hitbox( std::uintptr_t target_pawn, int hitbox_index );
			void on_shot_hit( std::uintptr_t target_pawn );
			void on_shot_missed( std::uintptr_t target_pawn );
			[[nodiscard]] bool should_force_body_aim( std::uintptr_t target_pawn ) const;
			[[nodiscard]] float resolve_yaw( std::uintptr_t target_pawn, float original_yaw ) const;
			[[nodiscard]] bool is_prone( std::uintptr_t target_pawn, const lagcomp::record* record ) const;
			[[nodiscard]] yaw_side current_side( std::uintptr_t target_pawn ) const;
			void update_target_data( std::uintptr_t target_pawn );
			[[nodiscard]] float get_detected_desync( std::uintptr_t target_pawn ) const;
			[[nodiscard]] float read_eye_yaw( std::uintptr_t pawn ) const;
			[[nodiscard]] float read_body_yaw( std::uintptr_t pawn ) const;
			[[nodiscard]] float compute_smoothed_desync( const resolve_data& data ) const;

		private:
			std::unordered_map<std::uintptr_t, resolve_data> m_entries{};
			mutable std::shared_mutex m_mtx{};
		};

		resolver m_resolver{};

		class penetration
		{
		public:
			// Verbose trace logging costs three Win32 calls plus a vformat and a
			// WriteFile per run(), and run() is called hundreds of times per frame
			// from eight worker threads. Under DEV the old compile-time guard left
			// it on unconditionally, which alone accounted for ~700 ms per frame in
			// live logs (354k [pen] lines in one five-second window). Kept as a
			// runtime switch so a development build stays playable and the trace
			// can still be turned on when actually diagnosing penetration.
			inline static bool debug_log{};

			struct weapon_data
			{
				float damage;
				float penetration;
				float range_modifier;
				float range;
				float armor_ratio;
				float headshot_multiplier;
			};

			struct damage_scales
			{
				float ct_head;
				float t_head;
				float ct_body;
				float t_body;
			};

			struct run_context
			{
				std::uintptr_t target_pawn{};
				int target_armor{};
				int target_team{};
				bool has_helmet{};
				damage_scales scales{};
				float armor_ratio{};
				float headshot_multiplier{};
				systems::hitboxes::set hitboxes{};
				lagcomp::record* record{};
			};

			struct result
			{
				float damage{};
				int hitbox{ -1 };
				int hitgroup{ -1 };
				bool penetrated{};
			};

			void prepare( std::uintptr_t weapon_vdata, std::uintptr_t weapon );

			[[nodiscard]] run_context prepare_target( std::uintptr_t target_pawn, lagcomp::record* record ) const;
			[[nodiscard]] bool run( const math::vector3& start, const math::vector3& end, const run_context& ctx, std::uintptr_t local_pawn, int local_team, result& out ) const;
			[[nodiscard]] bool can( const math::vector3& start, const math::vector3& direction, float& out_damage, const systems::local::snapshot& local ) const;
			[[nodiscard]] float get_max_damage( int hitgroup, int target_armor, bool has_helmet, int target_team ) const;
			[[nodiscard]] const weapon_data& get_weapon_data( ) const { return this->m_weapon_data; }

		private:
			void scale_damage( int hitgroup, int armor, bool has_helmet, int team, float armor_ratio, float headshot_multiplier, const damage_scales& scales, float& damage ) const;
			weapon_data m_weapon_data{};
		};

		class shoot_history
		{
		public:
			struct ring_entry
			{
				int tick{};
				float fraction{};
				math::vector3 position{};
			};

			struct eye_candidate
			{
				math::vector3 position{};
				int player_tick{};
				float player_frac{};
				int lerp_ticks_int{};
				float lerp_ticks_frac{};
				bool is_uninterpolated{};
			};

			struct eye_candidates
			{
				eye_candidate entries[ 2 ]{};
				int count{};
			};

			void snapshot( std::uintptr_t local_pawn, std::uintptr_t weapon_services );
			[[nodiscard]] eye_candidates get_candidates( ) const;
			[[nodiscard]] bool has_data( ) const { return this->m_count > 0; }
			[[nodiscard]] int client_tick( ) const { return this->m_client_tick; }
			[[nodiscard]] float client_tick_frac( ) const { return this->m_client_tick_frac; }
			[[nodiscard]] int server_tick( ) const { return this->m_server_tick; }
			[[nodiscard]] int lerp_ticks_int( ) const { return this->m_lerp_ticks_int; }
			[[nodiscard]] float lerp_ticks_frac( ) const { return this->m_lerp_ticks_frac; }
			[[nodiscard]] int count( ) const { return this->m_count; }
			[[nodiscard]] int oldest_tick( ) const { return this->m_count > 0 ? this->m_entries[ 0 ].tick : -1; }
			[[nodiscard]] int newest_tick( ) const { return this->m_count > 0 ? this->m_entries[ this->m_count - 1 ].tick : -1; }

		private:
			ring_entry m_entries[ 32 ]{};
			int m_count{};
			int m_client_tick{};
			float m_client_tick_frac{};
			int m_server_tick{};
			int m_lerp_ticks_int{};
			float m_lerp_ticks_frac{};
		};

		struct context
		{
			std::uintptr_t weapon{};
			std::uintptr_t weapon_services{};
			std::uintptr_t weapon_vdata{};
			std::uint32_t weapon_type{};
			std::uint16_t item_def_idx{};
			int num_bullets{};
			float recoil_index{};
			int current_tick{};
			int ticks_since_land{};
			float current_time{};
			float weapon_max_speed{};
			float range{};
			bool is_jump_scouting{};
			bool is_scoped{};
			bool valid{};

			float inaccuracy{};
			float spread{};

			// Damage scales, read once per tick rather than per candidate.
			//
			// prepare_target used to pull four convars every time it was called,
			// and it is called once per record per player -- twenty-odd hash
			// lookups and indirect calls a tick, on worker threads, for values that
			// cannot change within a tick. They are also read here with a floor,
			// because a server that has not set them hands back zero and multiplying
			// the damage by that silently disables the whole aimbot.
			penetration::damage_scales scales{ 1.0f, 1.0f, 1.0f, 1.0f };
		};

		void update( );
		void invalidate_if_needed( );

		[[nodiscard]] context& ctx( ) { return this->m_ctx; }
		[[nodiscard]] penetration& pen( ) { return this->m_pen; }
		[[nodiscard]] lagcomp& lc( ) { return this->m_lc; }
		[[nodiscard]] shoot_history& sh( ) { return this->m_sh; }
		[[nodiscard]] resolver& res( ) { return this->m_resolver; }
		[[nodiscard]] const resolver& res( ) const { return this->m_resolver; }
		[[nodiscard]] float get_bullet_speed( std::uint16_t item_def_idx ) const;
		[[nodiscard]] int calculate_bullet_travel_ticks( const math::vector3& shoot_pos, const math::vector3& target_pos, std::uint16_t item_def_idx ) const;

		[[nodiscard]] bool autowalling( ) const { return this->m_autowalling; }
		[[nodiscard]] lagcomp::record* current_autowall_record( ) const { return this->m_current_autowall_record; }

		[[nodiscard]] int& last_shoot_tick( ) { return this->m_last_shoot_tick; }

		[[nodiscard]] std::uint32_t get_spread_seed( const math::vector3& angles, int tick ) const;
		[[nodiscard]] math::vector2 calculate_spread( int seed, float accuracy, float spread, float recoil_index, int item_def_idx, int num_bullets ) const;
		[[nodiscard]] math::vector3 get_aim_punch( std::uintptr_t local_pawn ) const;
		[[nodiscard]] float calculate_hitchance( const math::vector3& shoot_position, const math::vector3& aim_angle, const systems::hitboxes::entry& hitbox, const systems::bones::data& bone, float inaccuracy, float spread, int samples = 256 ) const;
		[[nodiscard]] math::vector3 find_spread_correction( const math::vector3& aim_angle, int tick ) const;
		[[nodiscard]] math::vector3 get_eye_position( std::uintptr_t local_pawn ) const;
		[[nodiscard]] math::vector3 get_shoot_position( ) const;
		[[nodiscard]] math::vector3 get_interpolated_shoot_position( std::uintptr_t local_pawn, bool newest = false ) const;
		[[nodiscard]] int calculate_stop_ticks( const math::vector3& velocity, float max_speed, std::uintptr_t local_pawn ) const;
		[[nodiscard]] float get_spread( ) const;
		[[nodiscard]] float get_inaccuracy( bool update_accuracy_penalty ) const;
		[[nodiscard]] float get_inaccuracy_at_velocity( std::uintptr_t local_pawn, const math::vector3& velocity ) const;
		[[nodiscard]] float get_air_inaccuracy( float vertical_speed, float jump_initial, float jump_apex ) const;
		[[nodiscard]] bool can_shoot( systems::input::usercmd* cmd, std::uintptr_t local_controller, bool check_next_attack = true ) const;
		[[nodiscard]] bool is_max_accuracy( float inaccuracy ) const;

		// The revolver's quick shot fires with a wider cone than the one the weapon
		// reports while idle, and the extra amount is applied by the game at fire
		// time -- so reading the weapon can never see it, and no_spread solves for a
		// cone the server does not use.
		//
		// Rather than hard-code a guess, learn it. The engine's own GetInaccuracy is
		// hooked, and when it is called from inside FireGuns the value it returns is
		// exactly what the shot used. Compare that against what we predicted for the
		// same shot and carry the ratio forward.
		// Our own calls to the engine's GetInaccuracy have to be distinguishable
		// from the one it makes for a real shot, otherwise the calibration below
		// measures itself.
		[[nodiscard]] static bool in_self_inaccuracy_call( ) { return m_in_self_inaccuracy_call; }


		void note_pending_shot( float predicted_inaccuracy, bool quick_revolver, int command_tick );
		void on_engine_fire_inaccuracy( std::uintptr_t weapon, float actual ) const;
		[[nodiscard]] bool quick_revolver_active( ) const;
		[[nodiscard]] math::vector3 simulate_aim_punch( int recoil_index ) const;

		bool ray_vs_capsule( const math::vector3& ray_origin, const math::vector3& ray_dir, const math::vector3& capsule_a, const math::vector3& capsule_b, float radius, float& out_fraction ) const;

	private:
		context m_ctx{};
		penetration m_pen{};
		lagcomp m_lc{};
		shoot_history m_sh{};

		// Parallel rage workers must not overwrite each other's trace record.
		inline static thread_local bool m_autowalling{};
		inline static thread_local lagcomp::record* m_current_autowall_record{ nullptr };

		int m_last_shoot_tick{};

		inline static thread_local bool m_in_self_inaccuracy_call{};
		inline static std::atomic<float> m_pending_shot_inaccuracy{ 0.0f };
		inline static std::atomic<bool> m_pending_shot_quick_revolver{ false };
		inline static std::atomic<int> m_pending_shot_tick{ 0 };
	};

	// TODO: verify against original - reconstructed blindly (AA yaw resolver helpers; side_offset magnitude is a guess)
	inline shared::resolver::yaw_side next_side( shared::resolver::yaw_side side )
	{
		if ( side == shared::resolver::yaw_side::left ) return shared::resolver::yaw_side::right;
		if ( side == shared::resolver::yaw_side::right ) return shared::resolver::yaw_side::left;
		return side;
	}

	inline float side_offset( shared::resolver::yaw_side side )
	{
		switch ( side )
		{
			case shared::resolver::yaw_side::left:  return -58.0f;
			case shared::resolver::yaw_side::right: return 58.0f;
			case shared::resolver::yaw_side::real:
			default:                               return 0.0f;
		}
	}

	class misc
	{
	private:
		class antiaim
		{
		public:
			void on_create_move( systems::input::usercmd* cmd );
			void on_render( xdraw::draw_list& draw_list ) const;

			[[nodiscard]] bool has_modified_angles( ) const { return this->m_should_correct || this->m_modified_angles.y != this->m_old_angles.y; }
			[[nodiscard]] const math::vector3& get_modified_angles( ) const { return this->m_modified_angles; }

		private:
			[[nodiscard]] float get_pitch( float view_pitch );
			[[nodiscard]] float get_yaw( const math::vector3& view_angles, const systems::local::snapshot& local );
			void correct_movement( systems::input::usercmd* cmd );
			[[nodiscard]] float advance_spin( );

			float m_spin_angle{};
			float m_spin_sign{ 1.0f };
			float m_spin_switch_timer{};
			[[nodiscard]] bool is_near_ladder( std::uintptr_t local_pawn ) const;

			math::vector3 m_old_angles{};
			math::vector3 m_modified_angles{};

			int m_yaw_side{};
			bool m_should_correct{};

			bool m_antiaim_active{};
			float m_indicator_yaw{};
		};

		class duckpeek
		{
		public:
			void on_create_move( systems::input::usercmd* cmd );
			void on_override_view( std::uintptr_t view_setup );

		private:
			bool m_was_active{};
			bool m_fake_stand_active{};
		};

		class quickpeek
		{
		public:
			void on_create_move( systems::input::usercmd* cmd );
			void reset_if_needed( );

		private:
			static constexpr std::uint32_t invalid_effect_index{ static_cast<std::uint32_t>( -1 ) };

			void create_particle( );
			void update_particle( );
			void release_particle( );
			void reset( );

			math::vector3 m_saved_origin{};
			bool m_should_retrack{};
			bool m_fired{};
			bool m_active{};
			std::uint32_t m_particle_effect{ invalid_effect_index };
			bool m_particle_loaded{};
			std::uintptr_t m_prev_movement_bits{};
		};

		class autostop
		{
		public:
			void on_create_move( systems::input::usercmd* cmd );

		private:
			[[nodiscard]] float get_effective_accel_base( std::uintptr_t local_pawn, std::uintptr_t movement_services, std::uint32_t flags, float max_weapon_speed ) const;
		};

		antiaim m_antiaim{};
		duckpeek m_duckpeek{};
		quickpeek m_quickpeek{};
		autostop m_autostop{};

	public:
		[[nodiscard]] antiaim& antiaim( ) { return this->m_antiaim; }
		[[nodiscard]] duckpeek& duckpeek( ) { return this->m_duckpeek; }
		[[nodiscard]] quickpeek& quickpeek( ) { return this->m_quickpeek; }
		[[nodiscard]] autostop& autostop( ) { return this->m_autostop; }
	};

	class rage
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		void on_render( xdraw::draw_list& draw_list );

		[[nodiscard]] bool should_stop( ) const noexcept { return this->m_should_stop; }
		[[nodiscard]] bool is_firing_this_tick( ) const noexcept { return this->m_firing_this_tick; }
		[[nodiscard]] bool is_cocking_revolver( ) const noexcept { return this->m_revolver_cock_ticks > 0; }
		[[nodiscard]] bool should_release_duck_for_shot( ) const noexcept { return this->m_release_duck_for_shot; }
		[[nodiscard]] bool duckpeek_wants_reduck( ) const noexcept { return this->m_duckpeek_reduck; }
		void clear_duckpeek_reduck( ) noexcept { this->m_duckpeek_reduck = false; }

		static constexpr auto k_max_lagcomp_records{ 16 };
		// Scanning the newest and oldest valid records covers the useful lag-comp
		// extremes without multiplying every penetration and hitchance test.
		static constexpr auto k_max_scan_records{ 3 };

	private:
		struct aim_context
		{
			math::vector3 view_angles{};
			math::vector3 velocity{};

			float predicted_inaccuracy{};
			float spread{};

			float weapon_max_speed{};
			float accurate_threshold{};
			bool on_ground{};
			bool is_scoped{};
		};

		struct stop_prediction
		{
			math::vector3 eye{};
			float inaccuracy{};
		};

		struct candidate
		{
			std::uintptr_t pawn{};
			int health{};
			int armor{};
			float min_damage{};
			// Цель в воздухе. Нужно для baim_air: в прыжке голова гуляет по
			// вертикали, и корпус попадает заметно надёжнее.
			bool on_air{};
			std::array<shared::lagcomp::record*, k_max_lagcomp_records> records{};
			int record_count{};
		};

		struct scan_hit
		{
			math::vector3 position{};
			math::vector3 aim_angle{};
			float damage{};
			float score{};
			float fov{};
			int hitbox_index{};
			int hitgroup{};
			int bone_index{};
			systems::hitboxes::entry hitbox{};
			bool is_center{};
			bool penetrated{};
			bool is_backstab{};
			int attack_type{};

			// Решение "целимся в корпус", посчитанное в scan_player. Несёт
			// приоритет №2 компаратора: когда включён body-aim и обе точки
			// прошли пороги, предпочитается точка на туловище. В движке это
			// тройка ctx+0x51 / ctx+0xA8 / ctx+0xBC == 1.
			bool body_aim{};
			shared::shoot_history::eye_candidate source_eye{};

			std::uintptr_t pawn{};
			int health{};
			shared::lagcomp::record* record{};

			// Контекст урона: набор хитбоксов и параметры брони цели.
			//
			// Заполняется ОДИН раз в scan_player (там prepare_target уже
			// вызывается) и переиспользуется в evaluate_point_metrics. Без
			// этого оценка точки звала бы prepare_target -- а это полный
			// query хитбоксов, полсотни чтений, -- на КАЖДУЮ оцениваемую
			// точку, в цикле, который крутится до четырёх раз за тик.
			systems::hitboxes::set hitboxes{};
			int target_armor{};
			bool target_helmet{};
			int target_team{};

			// Максимальный урон группы точки (без падения по дистанции).
			// Отношение hit.damage к нему -- доля, которую сохранил выстрел;
			// ею масштабируется урон любого направления, чтобы затухание и
			// потеря на простреле учитывались ровно один раз.
			float point_max_damage{};
		};

		// Четыре показателя точки -- аналог того, что движковый ragebot пишет
		// в выходной буфер из 4 float по 64 направлениям (см. таблицу
		// восстановления). Это НЕ доля попаданий в один хитбокс: каждое
		// направление оценивается по урону, и счётчики складываются по всем
		// направлениям с весом 1/64.
		//
		//   hit_min_damage  -- доля направлений с уроном >= минимального порога
		//                      (target+0x18). Отвечает на "выстрел вообще имеет
		//                      смысл".
		//   lethal          -- доля направлений с уроном выше порога летальности
		//                      (target+0x14, обычно hp цели). Отвечает на
		//                      "убью ли я его этим выстрелом".
		//   body            -- доля контактов с группами 2/3 (грудь/живот), либо
		//                      ноль, если выставлен флаг "считать только
		//                      летальные". Отвечает на "попаду ли я в корпус,
		//                      если голова закрыта".
		//   average_damage  -- средний оценочный урон по всем направлениям,
		//                      промахи дают нулевой вклад.
		struct point_metrics
		{
			float hit_min_damage{};
			float lethal{};
			float body{};
			float average_damage{};

			// Полный проход -- 64 направления. Знаменатель вынесен в константу,
			// чтобы не путать его с числом реально проверенных направлений:
			// движок умеет завершать проход рано и ДОоценивать остаток, поэтому
			// делить на фактическое число проверок нельзя.
			static constexpr float k_directions{ 64.0f };

			[[nodiscard]] bool valid( ) const noexcept
			{
				// Отсев нечисловых значений: NaN в любом из показателей означает,
				// что направление выродилось (нулевая длина, битый указатель), и
				// такую точку нельзя пускать в сравнение -- она случайно
				// выигрывает любое ">".
				return std::isfinite( this->hit_min_damage )
					&& std::isfinite( this->lethal )
					&& std::isfinite( this->body )
					&& std::isfinite( this->average_damage );
			}
		};

		struct target
		{
			scan_hit hit{};
			float hitchance{};
			float score{};
			point_metrics metrics{};

			// Вторая четвёрка показателей. Движок пишет её в point+0x44..+0x50
			// (second_eval, 0x525830) уже ПОСЛЕ выбора точки, и компаратор
			// использует её предпоследним рубежом -- перед свежестью записи.
			//
			// Отличие от metrics не в наборе полей, а в способе счёта:
			// metrics считается по доле урона точки (приближение, ни одной
			// трассировки мира), re -- полным расчётом урона на каждое из 64
			// направлений. Поэтому re дороже и живёт только у победителя.
			point_metrics re{};

			// Точка прошла ОБА порога одновременно: урон не ниже минимума и
			// первый показатель не ниже своего порога. Это приоритет №1
			// движкового компаратора (первое сравнение с ранним возвратом),
			// а не слагаемое взвешенной суммы.
			bool ready{};

			// Приоритет №2: предпочесть точку на туловище. В движке это
			// тройка условий ctx+0x51 (prefer_body), ctx+0xA8 (flag) и
			// ctx+0xBC == 1, и срабатывает она ТОЛЬКО когда обе точки прошли
			// пороги. У нас роль тройки играет решение force_body, уже
			// посчитанное в scan_player: оно и есть "целимся в корпус".
			bool body_aim{};

			// Пороги точки. В движке это target+0x18 (min_damage, настройка) и
			// target+0x14 (lethal_damage, здоровье цели с поправкой на броню).
			// Компаратору они нужны для приоритета 6: число выстрелов до
			// убийства считается как round(lethal_damage / damage), и без
			// обоих порогов его не получить.
			float min_damage{};
			float lethal_damage{};

			bool valid{};

			[[nodiscard]] bool is_lethal( ) const noexcept
			{
				return this->hit.damage >= static_cast< float >( this->hit.health );
			}
		};

		struct knife_info
		{
			bool can_slash{};
			bool can_stab{};
			bool charged{};
			float armor_ratio{};
		};

		[[nodiscard]] aim_context build_context( systems::input::usercmd* cmd, const systems::local::snapshot& local ) const;
		[[nodiscard]] std::optional<stop_prediction> predict_stop( const aim_context& ctx, const math::vector3& current_eye, const systems::local::snapshot& local ) const;
		[[nodiscard]] std::vector<candidate> gather_candidates( const systems::local::snapshot& local, float max_distance_sq = 0.0f ) const;

		void run_gun( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local, bool allow_fire = true );
		void run_taser( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local );
		void run_knife( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local );
		void auto_revolver( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local );

		[[nodiscard]] std::vector<scan_hit> scan_players( const math::vector3& eye, float inaccuracy, const aim_context& ctx, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const;
		[[nodiscard]] std::vector<scan_hit> scan_player( const math::vector3& eye, float inaccuracy, const aim_context& ctx, candidate& cand, shared::lagcomp::record* record, const systems::local::snapshot& local ) const;
		[[nodiscard]] target select_best( const aim_context& aim_ctx, const std::vector<scan_hit>& hits, float eval_inaccuracy, const systems::local::snapshot& local ) const;

		// Оценка точки по 64 направлениям -- замена "доля попаданий в хитбокс".
		//
		// Направления -- это НЕ геометрический конус, построенный по углу
		// разброса, а те же самые смещения, которые движок применит к пуле:
		// evaluate_point_metrics получает их готовыми (см. engine_directions).
		// Разница принципиальная. Аналитический конус tan(spread)*distance
		// требует ещё и пола радиуса, иначе на близкой дистанции кольцо
		// вырождается; любой пол раздувает кольцо сверх настоящего разброса и
		// завышает все четыре показателя. Движковые направления пола не
		// требуют: они уже лежат внутри реального конуса, и доля прошедших
		// -- честная вероятность.
		//
		// Поэтому же здесь нет трассировки мира: направление проверяется
		// геометрией цели (direction_hits_hitbox), а урон берётся из скана.
		[[nodiscard]] point_metrics evaluate_point_metrics( const scan_hit& hit, float min_damage_threshold, const aim_context& ctx, float inaccuracy ) const;

		// Вторая оценка четырёх показателей -- second_eval (0x525830).
		//
		// Отличие от evaluate_point_metrics ровно одно, но принципиальное:
		// там урон направления берётся из доли урона точки (приближение,
		// ноль трассировок мира), здесь каждое из 64 направлений прогоняется
		// через ПОЛНЫЙ расчёт урона с трассировкой мира и пробитием.
		//
		// Поэтому вызывается она один раз на тик -- для уже выбранной точки,
		// а не для каждого кандидата. Кроме четырёх чисел она заполняет
		// побочные данные (маска попаданий, маска туловища, урон по
		// направлениям), которыми потом пользуется refine_point.
		[[nodiscard]] point_metrics second_eval( const scan_hit& hit, float min_damage_threshold, const aim_context& ctx, float inaccuracy ) const;

		// Уточнение позиции точки -- refine_point (0x51C9C0).
		//
		// Точка сдвигается к центру масс тех направлений разброса, которые
		// попали по цели. Вес направления -- его урон, зажатый сверху порогом
		//   lethal * (1 + 2.5 * max(lethal / min_damage - 1, 0)),
		// то есть далёкие попадания не утягивают точку сильнее ближних.
		//
		// Новая позиция = глаз + нормированная сумма (направление * вес).
		// Возвращает true, когда сдвигать НЕКУДА: попаданий нет, суммарный вес
		// нулевой, либо доля попаданий уже достигла единицы.
		[[nodiscard]] bool refine_point( const scan_hit& hit, math::vector3& pos, float min_damage_threshold, const aim_context& ctx, float inaccuracy ) const;

		// Компаратор точек: последовательные сравнения с ранними возвратами.
		//
		// Порядок НЕ произвольный и не является взвешенной суммой -- это
		// существо алгоритма движка (0x51BEA0). Девять приоритетов, каждый с
		// ранним выходом, и почти каждый -- с ДОПУСКОМ: движок сравнивает
		// показатели через approx, а не на строгое неравенство. Без допуска
		// две точки, отличающиеся на 1e-7, разворачивали бы выбор, и бот
		// дёргался бы между ними от тика к тику.
		//
		//   1. ready -- прошла оба порога сразу.
		//   2. туловище, но только когда обе ready и включён body-aim.
		//   3. средний урон (допуск 0.05).
		//   4. вероятность достичь min_damage (допуск 0.15).
		//   5. в режиме 2 -- точка ближе к направлению взгляда.
		//   6. меньше выстрелов до убийства (обе вероятности выше 0.6).
		//   7. вероятность летального урона (допуск 0.10, порог 0.4).
		//   8. средний урон повторной оценки (допуск 2.0).
		//   9. свежесть записи лага.
		//
		// Возвращает true, если `a` лучше `b`.
		[[nodiscard]] bool is_better_point( const target& a, const target& b ) const;

		// Порог летальности: урон, который считается достаточным, чтобы
		// закрыть цель. Движок берёт его из target+0x14 -- у нас это min_damage
		// конфига, поднятый до hp цели, когда цель можно добить одним выстрелом.
		[[nodiscard]] float lethal_threshold_for( const scan_hit& hit, float min_damage ) const;

		// В какую группу попадает направление: возвращает hitgroup лучшего
		// задетого хитбокса, либо -1 при промахе.
		//
		// Работает в мировых координатах и повторяет математику capsule-ray из
		// calculate_hitchance: капсула задана отрезком (mins..maxs в системе
		// кости) и радиусом. Отличие от полного варианта -- отсутствие трассировки
		// мира: направление проверяется против САМОЙ цели, а не против геометрии
		// уровня. Это соответствует движку, который на этом шаге тоже проверяет
		// только цель.
		//
		// Возвращается ИМЕННО группа, а не признак попадания: урон направления
		// считается по задетой группе (см. get_max_damage), и без группы все
		// 64 направления получили бы одинаковый урон -- тогда четыре показателя
		// выродились бы в один бит.
		[[nodiscard]] int direction_hits_hitbox( const math::vector3& eye, const math::vector3& angle, const scan_hit& hit, const systems::hitboxes::set& hitboxes ) const;
		[[nodiscard]] float evaluate_hitchance( const scan_hit& hit, const aim_context& ctx, float inaccuracy ) const;
		[[nodiscard]] float evaluate_hitchance_with_pen( const scan_hit& hit, const aim_context& ctx, float inaccuracy, const systems::local::snapshot& local ) const;
		[[nodiscard]] float validate_shot_cone( const target& tgt, const aim_context& ctx, float inaccuracy, const systems::local::snapshot& local ) const;

		// prefer_safe_point: подбирает точку удара, для которой коробка игрока
		// (hull ~9x9x72) проходит к цели, не задевая мировой геометрии на всём
		// пути выстрела. Обычная точка из мультипоинта -- это точка на хитбоксе,
		// и она может лежать так, что пуля пройдёт, а ствол окажется в стене;
		// тогда первый же тик стрельбы даёт "нельзя стрелять" или пулю в стену.
		// Возвращает true и перезаписывает position/aim_angle, если нашёл.
		bool find_safe_point( scan_hit& hit, const math::vector3& shoot_eye, float inaccuracy, const aim_context& ctx, const systems::local::snapshot& local ) const;
		[[nodiscard]] bool should_stop_for_target( const aim_context& ctx, const target& best_target ) const;
		[[nodiscard]] float get_standing_inaccuracy( const systems::local::snapshot& local, const aim_context& ctx ) const;

		[[nodiscard]] std::vector<scan_hit> scan_taser( const math::vector3& eye, const aim_context& ctx, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const;

		[[nodiscard]] knife_info get_knife_info( const systems::local::snapshot& local ) const;
		[[nodiscard]] std::vector<scan_hit> scan_knife( const math::vector3& eye, const aim_context& ctx, const knife_info& info, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const;

		void fire_gun( systems::input::usercmd* cmd, const target& tgt, bool was_forced, const math::vector3& shoot_eye, const systems::local::snapshot& local, bool subtick_attack );
		void fire_melee( systems::input::usercmd* cmd, const target& tgt, const systems::local::snapshot& local );
		void request_scope( systems::input::usercmd* cmd );

		template <typename Entry>
		void stamp_input_entry( Entry* entry, const cstypes::tick_fraction& record_time, const shared::shoot_history::eye_candidate& source_eye ) const;

		void generate_multipoints( const systems::hitboxes::entry& hitbox, const math::vector3& center, const math::quaternion& bone_rot, float pointscale, const math::vector3& shoot_pos, float inaccuracy, std::vector<math::vector3>& out ) const;
		[[nodiscard]] bool should_stop_movement( const aim_context& ctx ) const;
		[[nodiscard]] float get_min_damage( const settings::combat::ragebot::weapon_group& config, int target_health, bool override_active ) const;
		[[nodiscard]] float get_knife_damage( float raw, int armor, float armor_ratio ) const;
		[[nodiscard]] systems::tracing::result trace_taser_hit( const math::vector3& origin, const math::vector3& forward, float range, std::uintptr_t target_pawn, std::uintptr_t local_pawn ) const;
		[[nodiscard]] systems::tracing::result trace_knife_hit( const math::vector3& origin, const math::vector3& forward, float reach, std::uintptr_t target_pawn, std::uintptr_t local_pawn ) const;

		enum class penetration_crosshair_state : std::uint8_t
		{
			unavailable,
			blocked,
			penetrable
		};

		void update_penetration_crosshair( const systems::local::snapshot& local );
		void draw_penetration_crosshair( xdraw::draw_list& draw_list ) const;

		[[nodiscard]] bool process_doubletap( systems::input::usercmd* cmd, const systems::local::snapshot& local, bool charge_dt );

		bool m_should_stop{};
		bool m_firing_this_tick{};
		bool m_release_duck_for_shot{};
		bool m_duckpeek_reduck{};
		bool m_should_duck_for_shot{};

		std::uintptr_t m_last_target_pawn{};
		int m_last_target_hitbox{};

		// Состояние удержания выстрела (см. patience в fire-блоке run_gun).
		// Считается на конкретной цели: смена цели сбрасывает счётчик, иначе
		// бот однажды застрял бы в ожидании, переключаясь между двумя врагами.
		std::uintptr_t m_hold_target{};
		int m_hold_ticks{};

		// Состояние force body aim по истории выстрелов: сколько раз подряд
		// стреляли в голову (для baim_after_shots) и был ли последний выстрел
		// летальным (для baim_lethal).
		int m_head_shot_streak{};
		bool m_last_was_lethal{};

		std::uint8_t m_knife_attack{};
		bool m_zeus_fired{};

		int m_revolver_cock_ticks{};
		int m_duck_for_shot_ticks{};
		std::atomic<penetration_crosshair_state> m_penetration_crosshair_state{ penetration_crosshair_state::unavailable };

		std::vector<shared::lagcomp::record> m_extrapolated_records{};

		struct debug_point
		{
			math::vector3 position{};
			int hitbox_index{};
			bool is_center{};
		};

		mutable std::vector<debug_point> m_debug_points{};
		mutable std::mutex m_debug_mtx{};
	};

	class legit
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		void on_render( xdraw::draw_list& draw_list );
		void invalidate_if_needed( );

		[[nodiscard]] bool has_target( ) const noexcept { return this->m_target.has_target( ); }

	private:
		struct scan_point
		{
			math::vector3 position{};
			float damage{};
			float fov{};
			int hitgroup{};
			std::size_t cfg_index{};
			int bone_index{};
			systems::hitboxes::entry hitbox{};
			bool visible{};
			bool is_center{};
			bool valid{};
		};

		struct target_result
		{
			std::uintptr_t pawn{};
			scan_point best_point{};
			math::vector3 aim_angle{};
			float hitchance{};
			float score{};
			float fov{};
			int health{};
			shared::lagcomp::record* record{};

			[[nodiscard]] bool has_target( ) const noexcept { return this->best_point.valid; }
		};

		[[nodiscard]] target_result find_target( const math::vector3& shoot_position, const math::vector3& view_angles, const settings::combat::legitbot::weapon_group& config, const systems::local::snapshot& local ) const;
		[[nodiscard]] scan_point scan_player( std::uintptr_t pawn, shared::lagcomp::record* record, const math::vector3& shoot_position, const math::vector3& view_angles, const settings::combat::legitbot::weapon_group& config, const systems::local::snapshot& local ) const;

		void apply_aimbot( systems::input::usercmd* cmd, const target_result& tgt, const math::vector3& view_angles, const math::vector3& aim_punch, const settings::combat::legitbot::weapon_group& config, const systems::local::snapshot& local );
		void apply_autostop( systems::input::usercmd* cmd, const settings::combat::legitbot::weapon_group& config, const systems::local::snapshot& local );
		void apply_triggerbot( systems::input::usercmd* cmd, const math::vector3& shoot_position, const math::vector3& view_angles, const math::vector3& aim_punch, const settings::combat::legitbot::weapon_group& config, const systems::local::snapshot& local );
		void apply_rcs( math::vector3& aim_angle, const math::vector3& aim_punch, int rand_min, int rand_max ) const;

		void update_standalone_rcs( const math::vector3& view_angles, const math::vector3& aim_punch, int amount, int rand_min, int rand_max, bool apply, const systems::local::snapshot& local );
		[[nodiscard]] float compute_rcs_factor( int rand_min, int rand_max ) const;

		void draw_fov( xdraw::draw_list& draw_list, const math::vector3& view_angles, const math::vector3& aim_punch, float fov_degrees, const config::col& color, bool rcs_active ) const;

		[[nodiscard]] static int hitgroup_to_cfg( int hitgroup );

		target_result m_target{};
		math::vector3 m_old_punch{};
		mutable float m_last_significant_punch_time{};

		float m_remainder_x{};
		float m_remainder_y{};

		float m_trigger_delay_start{};
		float m_trigger_release_time{};
		std::uintptr_t m_trigger_pending_pawn{};

		math::vector3 m_cached_view_angles{};
		math::vector3 m_cached_aim_punch{};
		bool m_autostop_active{ false };
	};

} // namespace features::combat
