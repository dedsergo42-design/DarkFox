#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

namespace features::esp::player {

	bool glow::on_is_glowing( std::uintptr_t owner_entity, std::uint32_t owner_hash ) const
	{
		if ( owner_hash != "C_CSPlayerPawn"_hash )
		{
			return false;
		}

		const auto& cfg = settings::g_esp.m_player.m_glow;
		if ( !cfg.enemy.enabled.value && !cfg.enemy_ragdoll.enabled.value && !cfg.team.enabled.value && !cfg.team_ragdoll.enabled.value && !cfg.local.enabled.value && !cfg.local_ragdoll.enabled.value )
		{
			return false;
		}

		const auto local = systems::g_local.get( );
		const auto team = memory::read<int>( owner_entity + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
		const auto health = memory::read<int>( owner_entity + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );

		const auto is_other_team = local.is_this_other_team( team );
		const auto is_local = owner_entity == local.view_pawn( );
		const auto is_dead = health <= 0;

		if ( is_dead )
		{
			if ( is_local )
			{
				return cfg.local_ragdoll.enabled.value;
			}

			if ( is_other_team )
			{
				return cfg.enemy_ragdoll.enabled.value;
			}

			return cfg.team_ragdoll.enabled.value;
		}

		if ( is_local && settings::g_misc.m_camera.thirdperson.value )
		{
			return cfg.local.enabled.value;
		}

		if ( is_other_team )
		{
			return cfg.enemy.enabled.value;
		}

		return cfg.team.enabled.value;
	}

	bool glow::on_get_glow_color( std::uintptr_t owner_entity, std::uint32_t owner_hash, float* color ) const
	{
		if ( owner_hash != "C_CSPlayerPawn"_hash )
		{
			return false;
		}

		const auto& cfg = settings::g_esp.m_player.m_glow;
		if ( !cfg.enemy.enabled.value && !cfg.enemy_ragdoll.enabled.value && !cfg.team.enabled.value && !cfg.team_ragdoll.enabled.value && !cfg.local.enabled.value && !cfg.local_ragdoll.enabled.value )
		{
			return false;
		}

		const auto local = systems::g_local.get( );
		const auto team = memory::read<int>( owner_entity + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
		const auto health = memory::read<int>( owner_entity + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );

		const auto is_other_team = local.is_this_other_team( team );
		const auto is_local = owner_entity == local.view_pawn( );
		const auto is_dead = health <= 0;

		const settings::esp::glow_target* target{ nullptr };

		if ( is_dead )
		{
			if ( is_local )
			{
				target = &cfg.local_ragdoll;
			}
			else if ( is_other_team )
			{
				target = &cfg.enemy_ragdoll;
			}
			else
			{
				target = &cfg.team_ragdoll;
			}
		}
		else
		{
			if ( is_local )
			{
				if ( settings::g_misc.m_camera.thirdperson.value )
				{
					target = &cfg.local;
				}
			}
			else if ( is_other_team )
			{
				target = &cfg.enemy;
			}
			else
			{
				target = &cfg.team;
			}
		}

		if ( !target || !target->enabled.value )
		{
			return false;
		}

		// Цвет отдаётся в линейном виде (r,g,b,a в 0..1), и альфа здесь --
		// единственный канал, которым управляет движок. strength множит её,
		// thickness задаёт, насколько ореол выходит за силуэт.
		//
		// Раньше яркость жила прямо в альфе цвета: "поярче" и "другой оттенок"
		// были одной ручкой, и поднять интенсивность означало испортить цвет.
		// Теперь это две независимые настройки.
		const auto strength = std::clamp( target->strength.value, 0.05f, 4.0f );
		const auto thickness = std::clamp( target->thickness.value, 0.25f, 12.0f );

		color[ 0 ] = target->color.value.r / 255.0f;
		color[ 1 ] = target->color.value.g / 255.0f;
		color[ 2 ] = target->color.value.b / 255.0f;

		// thickness нормализована к прежнему диапазону: 2.0f -- это ровно
		// значение по умолчанию, то есть поведение не меняется, пока ручку
		// не тронули.
		color[ 3 ] = std::clamp( ( target->color.value.a / 255.0f ) * strength * ( thickness / 2.0f ), 0.0f, 1.0f );

		return true;
	}

} // namespace features::esp::player