// ==========================================
// FILE: bunnyhop.cpp
// ==========================================


#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>


#include "../movement.hpp"
#include <protection/game_addresses.hpp>


namespace features::movement {


	namespace {


		[[nodiscard]] std::optional<float> predict_landing_fraction(
			std::uintptr_t local_pawn,
			std::uintptr_t movement_services,
			const systems::prediction::state& prestate,
			bool holding_duck )
		{
			if ( prestate.networked_velocity.z > 0.0f )
			{
				return std::nullopt;
			}


			const auto duck_amount = memory::read<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flDuckAmount"_hash ) );
			const auto mins = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash ) + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ) );
			auto maxs = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash ) + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ) );


			auto trace_origin = prestate.networked_origin;
			if ( holding_duck && duck_amount > 0.0f )
			{
				const auto standing_height{ 72.0f };
				const auto duck_hull_diff = standing_height - maxs.z;
				trace_origin.z -= duck_hull_diff * 0.5f;
				maxs.z = standing_height;
			}


			auto trace_mask{ 0ull };
			{
				const auto pawn_ptr = memory::read<std::uintptr_t>( movement_services + 56 );
				trace_mask = memory::read<std::uintptr_t>( pawn_ptr + 0xd48 );


				if ( !pawn_ptr || ( memory::read<std::uint32_t>( pawn_ptr + 0x3f8 ) & 0x10 ) )
				{
					trace_mask |= 0x20;
				}
			}


			const auto filter = systems::g_tracing.make_player_movement_filter( local_pawn, trace_mask, 11 );
			const auto sv_gravity = CONVAR ("sv_gravity")->get<float>( );
			const auto sv_standable_normal = CONVAR ("sv_standable_normal")->get<float>( );
			const auto gravity_scale = memory::read<float>( local_pawn + SCHEMA( "C_BaseEntity", "m_flGravityScale"_hash ) );


			auto velocity = prestate.networked_velocity;
			velocity.z -= ( gravity_scale * sv_gravity * cstypes::tick_interval ) * 0.5f;


			const math::vector3 trace_start = trace_origin;
			math::vector3 trace_end{};


			trace_end.x = trace_origin.x + velocity.x * cstypes::tick_interval;
			trace_end.y = trace_origin.y + velocity.y * cstypes::tick_interval;
			trace_end.z = trace_origin.z + velocity.z * cstypes::tick_interval;
			trace_end.z -= 2.0f;


			const auto result = systems::g_tracing.trace_player_bbox( trace_start, trace_end, { mins, maxs }, filter, movement_services );
			if ( result.fraction <= 0.0f || result.fraction >= 1.0f || result.normal.z < sv_standable_normal )
			{
				return std::nullopt;
			}


			return std::clamp( std::round( result.fraction * 64.0f ) / 64.0f, 1.0f / 64.0f, 63.0f / 64.0f );
		}


		void apply_landing_jump( proto::base_usercmd_pb* base, float when )
		{
			const auto subtick_moves = base->mutable_subtick_moves( );
			const auto release_when = std::fmaxf( 0.0f, when - 0.005f );


			if ( release_when < when )
			{
				if ( const auto jump_up = systems::g_input.acquire_subtick_step( subtick_moves ) )
				{
					jump_up->set_button( cstypes::command_buttons::in_jump );
					jump_up->set_pressed( false );
					jump_up->set_when( release_when );
					jump_up->set_analog_forward_delta( 0.0f );
					jump_up->set_analog_left_delta( 0.0f );
				}
			}


			if ( const auto jump_down = systems::g_input.acquire_subtick_step( subtick_moves ) )
			{
				jump_down->set_button( cstypes::command_buttons::in_jump );
				jump_down->set_pressed( true );
				jump_down->set_when( when );
				jump_down->set_analog_forward_delta( 0.0f );
				jump_down->set_analog_left_delta( 0.0f );
			}
		}


	} // namespace


	void bhop::on_create_move( systems::input::usercmd* cmd ) const
	{
		if ( !settings::g_movement.bhop.value )
		{
			return;
		}

		// sv_autobunnyhopping: сервер прыгает сам, наша работа была бы
		// дублирующей и только портила бы тайминг сабтиков.
		if ( CONVAR( "sv_autobunnyhopping" )->get<bool>( ) )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.pawn )
		{
			return;
		}

		const auto move_type = memory::read<std::uint8_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_nActualMoveType"_hash ) );
		if ( move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip )
		{
			return;
		}

		// jumpbug снимает/подменяет прыжок своим импульсом -- не мешаем ему.
		if ( features::movement::g_jumpbug.active_this_tick( ) )
		{
			return;
		}

		const auto& prestate = systems::g_prediction.pre( );

		// Прежний код требовал, чтобы игрок САМ держал jump, и иначе выходил.
		// Это превращало bhop в "исправитель тайминга" для тех, кто и так
		// прыгает руками. Авто-bhop должен прыгать за игрока: пока тот держит
		// jump (или включён режим "всегда"), мы ведём прыжки сами.
		const auto holding_jump = ( cmd->buttons.value & cstypes::command_buttons::in_jump ) != 0;
		const auto auto_mode = settings::g_movement.bhop_auto.value;

		if ( !holding_jump && !auto_mode )
		{
			return;
		}

		// Мы уже на земле? Тогда прыжок делает движок по нашему биту, и
		// предсказывать нечего: важно только не снять бит раньше времени.
		if ( prestate.flags & cstypes::entity_flags::on_ground )
		{
			// Убеждаемся, что бит стоит -- на земле он и должен быть, чтобы
			// прыжок сработал в первом же тике.
			cmd->buttons.value |= cstypes::command_buttons::in_jump;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_jump;
			return;
		}

		// В воздухе: снимаем бит, иначе следующий прыжок не засчитается
		// (движок требует фронт "не зажат -> зажат").
		cmd->buttons.value &= ~cstypes::command_buttons::in_jump;
		cmd->buttons.value_changed &= ~cstypes::command_buttons::in_jump;

		const auto holding_duck = ( cmd->buttons.value & cstypes::command_buttons::in_duck ) != 0;
		if ( holding_duck )
		{
			cmd->buttons.value &= ~cstypes::command_buttons::in_duck;
			cmd->buttons.value_changed &= ~cstypes::command_buttons::in_duck;
		}

		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		if ( !movement_services )
		{
			return;
		}

		// Предсказание приземления: ищем долю тика, в которой игрок коснётся
		// пола, и ставим сабтик jump ровно туда. Раньше прыжка пропускались,
		// потому что бит восстанавливался до конца тика -- здесь он приходит
		// точно в момент контакта.
		const auto landing = predict_landing_fraction( local.pawn, movement_services, prestate, holding_duck );
		if ( !landing )
		{
			// Пол не найден в пределах тика. Если мы всё равно близко к нему,
			// жмём прыжок у конца тика: лучше чуть рано, чем потерять тик.
			const auto base = cmd->csgo_user_cmd.mutable_base( );
			if ( base && prestate.networked_velocity.z <= 0.0f )
			{
				apply_landing_jump( base, 0.99f );
			}
			return;
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}

		apply_landing_jump( base, *landing );
	}


} // namespace features::movement
