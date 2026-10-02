#pragma once

namespace animation {

	enum class easing : std::uint8_t
	{
		linear,
		ease_in,
		ease_out,
		ease_in_out,
		ease_out_back,
		ease_out_elastic
	};

	// Кадр может прийти с большим dt: загрузка уровня, перетаскивание окна,
	// своп файла подкачки. Раньше значение ничем не ограничивалось, и любой
	// такой кадр проскакивал всю анимацию разом -- рывок вместо плавности.
	// Ограничение сверху (не снизу) оставляет поведение честным: медленнее
	// чем реальное время анимация не идёт, быстрее -- не разгоняется.
	namespace detail {
		inline float clamped_delta( )
		{
			return std::min( xdraw::delta_time( ), 0.1f );
		}
	}

	class tween
	{
	public:
		void start( float from, float to, float duration, easing ease = easing::ease_out )
		{
			this->m_from = from;
			this->m_to = to;
			this->m_value = from;
			this->m_duration = duration;
			this->m_elapsed = 0.0f;
			this->m_easing = ease;
			this->m_finished = false;
		}

		void update( )
		{
			if ( this->m_finished )
			{
				return;
			}

			// Нулевая длительность -- не деление на ноль, а "применить сразу".
			if ( this->m_duration <= 0.0f )
			{
				this->m_value = this->m_to;
				this->m_finished = true;
				return;
			}

			this->m_elapsed += detail::clamped_delta( );

			if ( this->m_elapsed >= this->m_duration )
			{
				this->m_value = this->m_to;
				this->m_finished = true;
				return;
			}

			const auto t = this->apply_easing( this->m_elapsed / this->m_duration );
			this->m_value = this->m_from + ( this->m_to - this->m_from ) * t;
		}

		[[nodiscard]] float value( ) const { return this->m_value; }
		[[nodiscard]] bool finished( ) const { return this->m_finished; }

		void reset( )
		{
			this->m_value = this->m_from;
			this->m_elapsed = 0.0f;
			this->m_finished = true;
		}

	private:
		[[nodiscard]] float apply_easing( float t ) const
		{
			switch ( this->m_easing )
			{
			case easing::ease_in:
				return t * t;

			case easing::ease_out:
				return 1.0f - ( 1.0f - t ) * ( 1.0f - t );

			case easing::ease_in_out:
				return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow( -2.0f * t + 2.0f, 2.0f ) * 0.5f;

			case easing::ease_out_back:
			{
				// Небольшой перелёт за цель и возврат. Значение выходит за
				// [0,1] -- это не ошибка, а смысл кривой: так делают
				// подскакивающие появления панелей.
				constexpr auto c1{ 1.70158f };
				constexpr auto c3{ 2.70158f };
				const auto u = t - 1.0f;
				return 1.0f + c3 * u * u * u + c1 * u * u;
			}

			case easing::ease_out_elastic:
			{
				if ( t <= 0.0f )
				{
					return 0.0f;
				}

				if ( t >= 1.0f )
				{
					return 1.0f;
				}

				// Затухающая синусоида с периодом 2pi/3: к концу кривая
				// полностью успокаивается. Для всплывающих уведомлений и
				// счётчиков -- заметно живее обычного ease_out.
				constexpr auto c4{ 2.0f * std::numbers::pi_v<float> / 3.0f };
				return std::pow( 2.0f, -10.0f * t ) * std::sinf( ( t * 10.0f - 0.75f ) * c4 ) + 1.0f;
			}

			default:
				return t;
			}
		}

		float m_from{ 0.0f };
		float m_to{ 0.0f };
		float m_value{ 0.0f };
		float m_duration{ 0.0f };
		float m_elapsed{ 0.0f };
		easing m_easing{ easing::linear };
		bool m_finished{ true };
	};

	class tween2d
	{
	public:
		void start( float from_x, float from_y, float to_x, float to_y, float duration, easing ease = easing::ease_out )
		{
			this->m_x.start( from_x, to_x, duration, ease );
			this->m_y.start( from_y, to_y, duration, ease );
		}

		void update( )
		{
			this->m_x.update( );
			this->m_y.update( );
		}

		[[nodiscard]] float x( ) const { return this->m_x.value( ); }
		[[nodiscard]] float y( ) const { return this->m_y.value( ); }
		[[nodiscard]] bool finished( ) const { return this->m_x.finished( ) && this->m_y.finished( ); }

		void reset( )
		{
			this->m_x.reset( );
			this->m_y.reset( );
		}

	private:
		tween m_x{};
		tween m_y{};
	};

	class spring
	{
	public:
		void set_target( float target )
		{
			this->m_target = target;
		}

		void update( )
		{
			// Явная схема Эйлера устойчива, только пока шаг мал относительно
			// периода колебания. При stiffness 200 период порядка 0.44 с, а
			// кадр при загрузке уровня или подгрузке карты легко доходит до
			// 0.5-1.0 с. На таком шаге интегратор не сходится, а расходится:
			// значение улетает на десятки тысяч и больше не возвращается.
			//
			// Раньше это выглядело как "анимация один раз сломалась и
			// осталась сломанной": HP-бар уезжал за пределы экрана до конца
			// матча. Два независимых предохранителя:
			//
			//   1. dt ограничен сверху (детали см. detail::clamped_delta).
			//   2. Шаг разбивается на подшаги так, чтобы каждый был не больше
			//      k_max_step. Это математически корректно: сумма подшагов
			//      даёт тот же интервал, но без потери устойчивости.
			auto remaining = detail::clamped_delta( );

			while ( remaining > 0.0f )
			{
				const auto step = std::min( remaining, k_max_step );
				remaining -= step;

				const auto diff = this->m_target - this->m_value;
				const auto accel = diff * this->m_stiffness - this->m_velocity * this->m_damping;

				this->m_velocity += accel * step;
				this->m_value += this->m_velocity * step;
			}

			// Страховка от накопленной ошибки: если значения всё же разошлись
			// (например, stiffness выставили вручную в тысячи), снапаем к
			// цели, а не тащим мусор дальше. Порог 1e4 -- заведомо больше
			// любого осмысленного слагаемого UI.
			if ( !std::isfinite( this->m_value ) || !std::isfinite( this->m_velocity )
				|| std::abs( this->m_value ) > 1.0e4f )
			{
				this->snap( this->m_target );
			}
		}

		[[nodiscard]] float value( ) const { return this->m_value; }

		[[nodiscard]] bool settled( ) const
		{
			return std::abs( this->m_target - this->m_value ) < 0.001f && std::abs( this->m_velocity ) < 0.001f;
		}

		void set_stiffness( float stiffness ) { this->m_stiffness = stiffness; }
		void set_damping( float damping ) { this->m_damping = damping; }

		void snap( float value )
		{
			this->m_value = value;
			this->m_target = value;
			this->m_velocity = 0.0f;
		}

	private:
		// Максимальный подшаг интегратора. 1/120 c -- вдвое чаще кадра при
		// 60 к/с, то есть при нормальной работе цикл делает ровно один
		// проход, а подшаги появляются только на просадках.
		static constexpr auto k_max_step{ 1.0f / 120.0f };

		float m_value{ 0.0f };
		float m_velocity{ 0.0f };
		float m_target{ 0.0f };
		float m_stiffness{ 200.0f };
		float m_damping{ 20.0f };
	};

	class spring2d
	{
	public:
		void set_target( float x, float y )
		{
			this->m_x.set_target( x );
			this->m_y.set_target( y );
		}

		void update( )
		{
			this->m_x.update( );
			this->m_y.update( );
		}

		[[nodiscard]] float x( ) const { return this->m_x.value( ); }
		[[nodiscard]] float y( ) const { return this->m_y.value( ); }
		[[nodiscard]] bool settled( ) const { return this->m_x.settled( ) && this->m_y.settled( ); }

		void set_stiffness( float stiffness )
		{
			this->m_x.set_stiffness( stiffness );
			this->m_y.set_stiffness( stiffness );
		}

		void set_damping( float damping )
		{
			this->m_x.set_damping( damping );
			this->m_y.set_damping( damping );
		}

		void snap( float x, float y )
		{
			this->m_x.snap( x );
			this->m_y.snap( y );
		}

	private:
		spring m_x{};
		spring m_y{};
	};

	class progress
	{
	public:
		void set( float target, float duration = 0.3f )
		{
			this->m_tween.start( this->m_tween.value( ), target, duration, easing::ease_out );
			this->m_target = target;
		}

		void update( )
		{
			this->m_tween.update( );
		}

		[[nodiscard]] float value( ) const { return this->m_tween.value( ); }
		[[nodiscard]] float target( ) const { return this->m_target; }
		[[nodiscard]] bool finished( ) const { return this->m_tween.finished( ); }

	private:
		tween m_tween{};
		float m_target{ 0.0f };
	};

	class fade
	{
	public:
		void fade_in( float duration = 0.2f )
		{
			this->m_tween.start( this->m_tween.value( ), 1.0f, duration, easing::ease_out );
			this->m_alpha_target = 1.0f;
		}

		void fade_out( float duration = 0.2f )
		{
			this->m_tween.start( this->m_tween.value( ), 0.0f, duration, easing::ease_out );
			this->m_alpha_target = 0.0f;
		}

		void update( )
		{
			this->m_tween.update( );
		}

		[[nodiscard]] float alpha( ) const { return this->m_tween.value( ); }
		[[nodiscard]] bool visible( ) const { return this->m_alpha_target > 0.0f || !this->m_tween.finished( ); }
		[[nodiscard]] bool finished( ) const { return this->m_tween.finished( ); }

	private:
		tween m_tween{};
		float m_alpha_target{ 0.0f };
	};

} // namespace animation