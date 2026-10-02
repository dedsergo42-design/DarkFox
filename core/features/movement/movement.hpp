#pragma once

namespace features::movement {

	class bhop
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class airstrafe
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		void store_angles( );

		// Симметрично valve_strafer/test_strafer. Нужен финальному блоку
		// сабтиков в cheat.cpp: без него ход airstrafe (32 сабтика с
		// analog_forward/left_delta) занулялся базовым forwardmove/leftmove=0,
		// и стрейф в игре пропадал, хотя код его считал.
		[[nodiscard]] bool handled_this_tick( ) const { return this->m_handled_this_tick; }

	private:
		void check_button( std::uintptr_t current_buttons, std::uintptr_t button );
		void rotate_movement( proto::base_usercmd_pb* base, float target_yaw, float view_yaw ) const;
		void rotate_to_stop( proto::base_usercmd_pb* base, const math::vector3& velocity ) const;

		std::uintptr_t m_last_buttons{};
		std::uintptr_t m_last_pressed{};
		bool m_side_switch{};
		bool m_handled_this_tick{};
		math::vector3 m_angles{};
	};

	class jumpbug
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		[[nodiscard]] bool active_this_tick( ) const { return this->m_active_this_tick; }
		[[nodiscard]] float landing_fraction( ) const { return this->m_landing_fraction; }

	private:
		[[nodiscard]] float get_impulse_mul( std::uintptr_t local_pawn ) const;

		float m_landing_fraction{ 1.0f };
		bool m_active_this_tick{ false };
	};

	class fastladder
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class edgejump
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class edgestop
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class edgebug
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		void on_render( xdraw::draw_list& draw_list );

		[[nodiscard]] bool active_this_tick( ) const { return this->m_active_this_tick; }

	private:
		bool m_active_this_tick{ false };
	};

	class slowwalk
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class test_strafer
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		void store_real_angles( );
		[[nodiscard]] bool is_active( ) const;
		[[nodiscard]] bool handled_this_tick( ) const { return this->m_handled_this_tick; }

	private:
		void quantized_path( systems::input::usercmd* cmd );
		[[nodiscard]] bool apply_yaw_subtick( proto::base_usercmd_pb* base, float when, float yaw_delta ) const;
		void check_button( std::uintptr_t current_buttons, std::uintptr_t button );
		[[nodiscard]] static math::vector2 movement_from_buttons( std::uintptr_t pressed );

		std::uintptr_t m_last_buttons{};
		std::uintptr_t m_last_pressed{};
		int m_substep_counter{};
		bool m_handled_this_tick{};
		math::vector3 m_real_angles{};
	};

	// Separate from test_strafer on purpose: it owns its own button and substep
	// state, and dispatches on sv_quantize_movement_input rather than being gated
	// by it, so the same toggle covers both server settings.
	class valve_strafer
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		void store_real_angles( );
		[[nodiscard]] bool is_active( ) const;
		[[nodiscard]] bool handled_this_tick( ) const { return this->m_handled_this_tick; }

	private:
		void quantized_path( systems::input::usercmd* cmd );
		void analog_path( systems::input::usercmd* cmd );
		[[nodiscard]] bool apply_yaw_subtick( proto::base_usercmd_pb* base, float when, float yaw_delta ) const;
		void check_button( std::uintptr_t current_buttons, std::uintptr_t button );
		[[nodiscard]] static math::vector2 movement_from_buttons( std::uintptr_t pressed );

		std::uintptr_t m_last_buttons{};
		std::uintptr_t m_last_pressed{};
		int m_substep_counter{};
		bool m_handled_this_tick{};
		math::vector3 m_real_angles{};
	};

} // namespace features::movement