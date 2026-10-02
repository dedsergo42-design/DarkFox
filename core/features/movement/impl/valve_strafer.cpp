#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

namespace features::movement {

namespace {

    constexpr auto k_min_strafe_speed = 5.0f;

    [[nodiscard]] float get_max_subtick_when( proto::base_usercmd_pb* base )
    {
        auto max_when = 0.0f;

        for ( auto i = 0; i < base->subtick_moves_size( ); ++i )
        {
            if ( const auto step = base->mutable_subtick_moves( i ) )
            {
                max_when = std::fmaxf( max_when, step->when( ) );
            }
        }

        return max_when;
    }

    [[nodiscard]] float ref_ideal_angle( float speed, float dt, float wishspeed, float air_accel, float air_max_wishspeed )
    {
        if ( speed < 1.0f )
        {
            return 15.0f;
        }

        const auto accel_speed = wishspeed * air_accel * dt;
        float cos_theta{};

        if ( accel_speed >= air_max_wishspeed )
        {
            cos_theta = air_max_wishspeed / ( 2.0f * speed );
        }
        else
        {
            cos_theta = ( air_max_wishspeed - accel_speed ) / speed;
        }

        cos_theta = std::clamp( cos_theta, -1.0f, 1.0f );
        return std::fmaxf( std::acosf( cos_theta ) * ( 180.0f / std::numbers::pi_v<float> ), 1.0f );
    }

    [[nodiscard]] float ref_air_strafer( float vel_x, float vel_y, float target_yaw, float dt, bool side_switch, float wishspeed, float air_accel, float air_max_wishspeed )
    {
        const auto speed = std::sqrtf( vel_x * vel_x + vel_y * vel_y );
        const auto theta = ref_ideal_angle( speed, dt, wishspeed, air_accel, air_max_wishspeed );

        if ( speed < 15.0f )
        {
            return target_yaw;
        }

        const auto vel_angle = std::atan2f( vel_y, vel_x ) * ( 180.0f / std::numbers::pi_v<float> );
        auto vel_delta = target_yaw - vel_angle;
        math::helpers::normalize_angle( vel_delta );

        if ( std::fabsf( vel_delta ) > 1.0f )
        {
            if ( vel_delta > 0.0f )
            {
                auto yaw = vel_angle + theta;
                math::helpers::normalize_angle( yaw );
                return yaw;
            }

            auto yaw = vel_angle - theta;
            math::helpers::normalize_angle( yaw );
            return yaw;
        }

        if ( side_switch )
        {
            auto yaw = vel_angle + theta;
            math::helpers::normalize_angle( yaw );
            return yaw;
        }

        auto yaw = vel_angle - theta;
        math::helpers::normalize_angle( yaw );
        return yaw;
    }

    void ref_air_accel_sim( float& vel_x, float& vel_y, float wishdir_yaw, float frame_time, float friction, float wishspeed, float air_accel, float air_max_wishspeed )
    {
        const auto yaw_rad = wishdir_yaw * ( std::numbers::pi_v<float> / 180.0f );
        const auto wish_dir_x = std::cosf( yaw_rad );
        const auto wish_dir_y = std::sinf( yaw_rad );

        const auto capped = std::fminf( wishspeed, air_max_wishspeed );
        const auto dot = vel_x * wish_dir_x + vel_y * wish_dir_y;
        const auto add_speed = capped - dot;

        if ( add_speed <= 0.0f )
        {
            return;
        }

        const auto accel_speed = wishspeed * air_accel * friction * frame_time;
        const auto step = std::fminf( accel_speed, add_speed );

        vel_x += wish_dir_x * step;
        vel_y += wish_dir_y * step;
    }

} // namespace

[[nodiscard]] bool valve_strafer::is_active( ) const
{
    return settings::g_movement.valve_strafer.value;
}

math::vector2 valve_strafer::movement_from_buttons( std::uintptr_t pressed )
{
    auto forward_move = 0.0f;
    auto left_move = 0.0f;

    if ( pressed & cstypes::command_buttons::in_forward )
    {
        forward_move = 1.0f;
    }
    else if ( pressed & cstypes::command_buttons::in_back )
    {
        forward_move = -1.0f;
    }

    if ( pressed & cstypes::command_buttons::in_moveleft )
    {
        left_move = -1.0f;
    }
    else if ( pressed & cstypes::command_buttons::in_moveright )
    {
        left_move = 1.0f;
    }

    return { forward_move, left_move };
}

void valve_strafer::on_create_move( systems::input::usercmd* cmd )
{
    this->m_handled_this_tick = false;

    if ( !this->is_active( ) )
    {
        return;
    }

    if ( features::movement::g_jumpbug.active_this_tick( ) )
    {
        return;
    }

    const auto base = cmd->csgo_user_cmd.mutable_base( );
    if ( !base )
    {
        return;
    }

    const auto local = systems::g_local.get( );
    if ( !local.pawn )
    {
        return;
    }

    const auto move_type = memory::read<std::uint8_t>( local.pawn + SCHEMA( "CBaseEntity", "m_nActualMoveType"_hash ) );
    if ( move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip )
    {
        return;
    }

    const auto& prestate = systems::g_prediction.pre( );
    if ( prestate.flags & cstypes::entity_flags::on_ground )
    {
        return;
    }

    if ( features::combat::g_rage.is_firing_this_tick( ) )
    {
        return;
    }

    // A stop request outranks the strafe. Neither path here brakes -- both only
    // accelerate -- and airstrafe owns the analog counter-strafe, so holding the
    // tick meant the aimbot asked for a stop in mid-air and nothing ever slowed
    // down. Bailing hands the tick to airstrafe, which does know how to brake.
    if ( features::combat::g_rage.should_stop( )
        || features::misc::g_projectile_trajectory.should_stop( ) )
    {
        return;
    }

    if ( cmd->buttons.value & static_cast<std::uintptr_t>( cstypes::command_buttons::in_sprint ) )
    {
        return;
    }

    const bool quantized = CONVAR( "sv_quantize_movement_input" )->get<bool>( );

    if ( quantized )
        this->quantized_path( cmd );
    else
        this->analog_path( cmd );
}

bool valve_strafer::apply_yaw_subtick( proto::base_usercmd_pb* base, float when, float yaw_delta ) const
{
    math::helpers::normalize_angle( yaw_delta );

    if ( std::fabsf( yaw_delta ) <= 0.01f )
    {
        return false;
    }

    const auto subtick_moves = base->mutable_subtick_moves( );
    if ( !subtick_moves )
    {
        return false;
    }

    const auto step = systems::g_input.acquire_subtick_step( subtick_moves );
    if ( !step )
    {
        return false;
    }

    step->set_when( when );
    step->set_button( 0 );
    step->set_pressed( false );
    step->set_analog_forward_delta( 0.0f );
    step->set_analog_left_delta( 0.0f );
    step->set_yaw_delta( yaw_delta );
    step->set_pitch_delta( 0.0f );
    return true;
}

void valve_strafer::store_real_angles( )
{
    this->m_real_angles = systems::g_input.get_view_angles( );
}

void valve_strafer::quantized_path( systems::input::usercmd* cmd )
{
    const auto current_buttons = cmd->buttons.value;
    if ( current_buttons & static_cast< std::uintptr_t >( cstypes::command_buttons::in_sprint ) )
    {
        return;
    }

    const auto base = cmd->csgo_user_cmd.mutable_base( );
    if ( !base )
    {
        return;
    }

    this->check_button( current_buttons, cstypes::command_buttons::in_moveleft );
    this->check_button( current_buttons, cstypes::command_buttons::in_moveright );
    this->check_button( current_buttons, cstypes::command_buttons::in_forward );
    this->check_button( current_buttons, cstypes::command_buttons::in_back );
    this->m_last_buttons = current_buttons;

    const auto& prestate = systems::g_prediction.pre( );
    const auto velocity = prestate.networked_velocity;
    const auto speed_2d = velocity.length_2d( );

    const auto command_yaw = this->m_real_angles.y;

    const auto player_move = movement_from_buttons( this->m_last_pressed );
    if ( player_move.x == 0.0f && player_move.y == 0.0f )
    {
        return;
    }

    if ( speed_2d < k_min_strafe_speed )
    {
        return;
    }

    const auto start_when = get_max_subtick_when( base );
    if ( start_when >= 0.99f )
    {
        return;
    }

    const auto sv_airaccelerate = CONVAR( "sv_airaccelerate" )->get<float>( );
    const auto sv_maxspeed = CONVAR( "sv_maxspeed" )->get<float>( );
    const auto sv_air_max_wishspeed = CONVAR( "sv_air_max_wishspeed" )->get<float>( );
    const auto surface_friction = prestate.surface_friction;

    const auto base_yaw_offset = std::atan2f( -player_move.y, player_move.x ) * ( 180.0f / std::numbers::pi_v<float> );
    auto target_yaw = command_yaw + base_yaw_offset;
    math::helpers::normalize_angle( target_yaw );

    // max_subticks is the configured target count, not just a ceiling -- raising
    // it (e.g. to 32) gives more, finer yaw corrections per tick for a smoother,
    // faster velocity gain curve. Adaptive mode still trims it down at high speed
    // (fewer, larger corrections keep up better once already moving fast), but
    // never above whatever the user configured.
    const auto configured_max = settings::g_movement.m_test_strafer.max_subticks.value;
    auto subtick_count = configured_max;

    if ( settings::g_movement.m_test_strafer.adaptive_subticks.value )
    {
        if ( speed_2d > 400.0f )
            subtick_count = std::min( configured_max, 2 );
        else if ( speed_2d > 300.0f )
            subtick_count = std::min( configured_max, 3 );
        else
            subtick_count = std::min( configured_max, 4 );
    }

    subtick_count = std::clamp( subtick_count, 1, 32 );

    const auto sub_frame = cstypes::tick_interval / static_cast< float >( subtick_count );
    const auto when_step = ( 1.0f - start_when ) / static_cast< float >( subtick_count );

    auto acc_yaw = command_yaw;
    auto sim_vx = velocity.x;
    auto sim_vy = velocity.y;
    auto injected = 0;

    for ( auto i = 0; i < subtick_count; ++i )
    {
        const auto entry_side = ( ( this->m_substep_counter + i ) % 2 ) == 0;
        const auto wishdir_yaw = ref_air_strafer(
            sim_vx, sim_vy, target_yaw, sub_frame, entry_side,
            sv_maxspeed, sv_airaccelerate, sv_air_max_wishspeed
        );

        auto target_view_yaw = wishdir_yaw - base_yaw_offset;
        math::helpers::normalize_angle( target_view_yaw );

        auto yaw_delta = target_view_yaw - acc_yaw;
        math::helpers::normalize_angle( yaw_delta );

        const auto when_frac = start_when + static_cast< float >( i ) * when_step;

        if ( !this->apply_yaw_subtick( base, when_frac, yaw_delta ) )
        {
            continue;
        }

        acc_yaw = target_view_yaw;
        ref_air_accel_sim(
            sim_vx, sim_vy, wishdir_yaw, sub_frame, surface_friction,
            sv_maxspeed, sv_airaccelerate, sv_air_max_wishspeed
        );
        ++injected;
    }

    if ( injected > 0 )
    {
        this->m_handled_this_tick = true;
        ++this->m_substep_counter;
    }
}

void valve_strafer::analog_path( systems::input::usercmd* cmd )
{
    const auto base = cmd->csgo_user_cmd.mutable_base( );
    if ( !base )
    {
        return;
    }

    const auto subtick_moves = base->mutable_subtick_moves( );
    if ( !subtick_moves )
    {
        return;
    }

    const auto current_buttons = cmd->buttons.value;

    this->check_button( current_buttons, cstypes::command_buttons::in_moveleft );
    this->check_button( current_buttons, cstypes::command_buttons::in_moveright );
    this->check_button( current_buttons, cstypes::command_buttons::in_forward );
    this->check_button( current_buttons, cstypes::command_buttons::in_back );
    this->m_last_buttons = current_buttons;

    const auto player_move = movement_from_buttons( this->m_last_pressed );
    if ( player_move.x == 0.0f && player_move.y == 0.0f )
    {
        return;
    }

    const auto& prestate = systems::g_prediction.pre( );
    auto velocity = prestate.networked_velocity;

    if ( velocity.length_2d( ) < k_min_strafe_speed )
    {
        return;
    }

    const auto local = systems::g_local.get( );
    const auto movement_services = memory::read<std::uintptr_t>(
        local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash )
    );
    if ( !movement_services )
    {
        return;
    }

    const auto sv_airaccelerate = CONVAR( "sv_airaccelerate" )->get<float>( );
    const auto sv_air_max_wishspeed = CONVAR( "sv_air_max_wishspeed" )->get<float>( );
    const auto sv_gravity = CONVAR( "sv_gravity" )->get<float>( );
    const auto maxspeed = memory::read<float>(
        movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash )
    );
    const auto friction = prestate.surface_friction;

    const auto view_yaw = this->m_real_angles.y;

    const auto dir_offset = std::atan2f( -player_move.y, player_move.x )
        * ( 180.0f / std::numbers::pi_v<float> );
    auto target_yaw = view_yaw + dir_offset;
    math::helpers::normalize_angle( target_yaw );

    constexpr int k_subs = 32;
    constexpr float k_dt = cstypes::tick_interval / static_cast<float>( k_subs );

    auto last_fwd = prestate.last_movement_impulses.x;
    auto last_left = prestate.last_movement_impulses.y;
    auto side = ( this->m_substep_counter % 2 ) == 0;
    int injected = 0;

    for ( int i = 0; i < k_subs; ++i )
    {
        const float speed = std::sqrtf( velocity.x * velocity.x + velocity.y * velocity.y );
        if ( speed < 0.0001f )
        {
            break;
        }

        velocity.z -= sv_gravity * k_dt * 0.5f;

        const float ideal = ref_ideal_angle(
            speed, k_dt, maxspeed, sv_airaccelerate, sv_air_max_wishspeed
        );

        const float vel_yaw = std::atan2f( velocity.y, velocity.x )
            * ( 180.0f / std::numbers::pi_v<float> );

        auto delta = target_yaw - vel_yaw;
        math::helpers::normalize_angle( delta );

        float wish_yaw = 0.0f;
        float side_move = 0.0f;

        if ( std::fabsf( delta ) > 1.0f )
        {
            if ( delta > 0.0f )
            {
                wish_yaw = vel_yaw + ideal;
                side_move = -1.0f;
            }
            else
            {
                wish_yaw = vel_yaw - ideal;
                side_move = 1.0f;
            }
        }
        else if ( side )
        {
            wish_yaw = vel_yaw + ideal;
            side_move = -1.0f;
        }
        else
        {
            wish_yaw = vel_yaw - ideal;
            side_move = 1.0f;
        }
        math::helpers::normalize_angle( wish_yaw );

        const float rot = ( view_yaw - wish_yaw ) * ( std::numbers::pi_v<float> / 180.0f );
        const float out_fwd = std::clamp( -std::sinf( rot ) * side_move, -1.0f, 1.0f );
        const float out_left = std::clamp( std::cosf( rot ) * side_move * -1.0f, -1.0f, 1.0f );

        base->set_forwardmove( out_fwd );
        base->set_leftmove( out_left );

        math::vector3 f{}, r{};
        math::helpers::angle_vectors_2d( view_yaw, f, r );

        math::vector3 wish{
            f.x * out_fwd * maxspeed + r.x * out_left * maxspeed,
            f.y * out_fwd * maxspeed + r.y * out_left * maxspeed,
            0.0f
        };

        float wish_spd = std::sqrtf( wish.x * wish.x + wish.y * wish.y );
        if ( wish_spd > 0.0001f )
        {
            wish.x /= wish_spd;
            wish.y /= wish_spd;
        }
        wish_spd = std::fminf( wish_spd, maxspeed );

        const float capped = std::fminf( wish_spd, sv_air_max_wishspeed );
        const float dot = velocity.x * wish.x + velocity.y * wish.y;
        const float add = capped - dot;
        if ( add > 0.0f )
        {
            const float step_spd = std::fminf(
                sv_airaccelerate * maxspeed * friction * k_dt, add
            );
            velocity.x += wish.x * step_spd;
            velocity.y += wish.y * step_spd;
        }

        velocity.z -= sv_gravity * k_dt * 0.5f;

        const auto step = systems::g_input.acquire_subtick_step( subtick_moves );
        if ( !step )
        {
            continue;
        }

        step->set_button( 0 );
        step->set_pressed( false );
        step->set_when( static_cast<float>( i ) / static_cast<float>( k_subs ) );
        step->set_analog_forward_delta( out_fwd - last_fwd );
        step->set_analog_left_delta( out_left - last_left );
        step->set_yaw_delta( 0.0f );
        step->set_pitch_delta( 0.0f );

        last_fwd = out_fwd;
        last_left = out_left;
        side = !side;
        ++injected;
    }

    if ( injected <= 0 )
    {
        return;
    }

    this->m_handled_this_tick = true;
    ++this->m_substep_counter;

    cmd->buttons.value &= ~static_cast<std::uintptr_t>(
        cstypes::command_buttons::in_forward |
        cstypes::command_buttons::in_back |
        cstypes::command_buttons::in_moveleft |
        cstypes::command_buttons::in_moveright
    );
}

void valve_strafer::check_button( std::uintptr_t current_buttons, std::uintptr_t button )
{
    constexpr auto moveleft = static_cast< std::uintptr_t >( cstypes::command_buttons::in_moveleft );
    constexpr auto moveright = static_cast< std::uintptr_t >( cstypes::command_buttons::in_moveright );
    constexpr auto forward = static_cast< std::uintptr_t >( cstypes::command_buttons::in_forward );
    constexpr auto back = static_cast< std::uintptr_t >( cstypes::command_buttons::in_back );

    if ( current_buttons & button && ( !( this->m_last_buttons & button ) || ( button & moveleft && !( this->m_last_pressed & moveright ) ) || ( button & moveright && !( this->m_last_pressed & moveleft ) ) || ( button & forward && !( this->m_last_pressed & back ) ) || ( button & back && !( this->m_last_pressed & forward ) ) ) )
    {
        if ( button & moveleft )
        {
            this->m_last_pressed &= ~moveright;
        }
        else if ( button & moveright )
        {
            this->m_last_pressed &= ~moveleft;
        }
        else if ( button & forward )
        {
            this->m_last_pressed &= ~back;
        }
        else if ( button & back )
        {
            this->m_last_pressed &= ~forward;
        }

        this->m_last_pressed |= button;
    }
    else if ( !( current_buttons & button ) )
    {
        this->m_last_pressed &= ~button;
    }
}

} // namespace features::movement
