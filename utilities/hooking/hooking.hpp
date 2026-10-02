#pragma once

namespace hooking {

	class jmp
	{
	public:
		bool create( void* target, void* hook_function );
		bool enable( );
		bool disable( );
		void reset( );

		template <typename T, typename... args_t>
		T call( args_t... args ) const
		{
			if ( !this->m_trampoline ) [[unlikely]]
			{
				if constexpr (std::is_same_v<T, void>)
					return;
				else
					return T {};
			}

			return reinterpret_cast< T( * )( args_t... ) >( this->m_trampoline )( args... );
		}

		template <typename T>
		T original( ) const
		{
			return reinterpret_cast< T >( this->m_trampoline );
		}

		explicit operator bool( ) const { return this->is_valid( ); }
		bool is_valid( ) const { return this->m_target && this->m_trampoline; }
		bool is_enabled( ) const { return this->m_enabled; }
		void* get_target( ) const { return this->m_target; }
		void* get_trampoline( ) const { return this->m_trampoline; }
		const std::uint8_t* get_original_bytes( ) const { return this->m_original_bytes; }
		std::size_t get_original_length( ) const { return this->m_original_length; }

	private:
		void* m_target{};
		void* m_hook{};
		void* m_trampoline{};
		std::uint8_t m_original_bytes[ 32 ]{};
		std::uint8_t m_hook_bytes[ 16 ]{};
		std::size_t m_original_length{};
		std::size_t m_patch_size{};
		bool m_enabled{};
	};

	namespace allocator {

		void* allocate( std::size_t size, void* near_ );
		void free( void* address );

	} // namespace allocator

	namespace manager {

		struct entry
		{
			jmp* hook;
			void* detour;

			// Имя хука -- ВЛАДЕЮЩАЯ строка, а не `const char*`.
			//
			// Таблица хуков заполняется через `xs("create_move")`, а это
			// `xorstr(str).crypt_get()`: временный объект xorstr, чей буфер
			// расшифровывается на время вызова. Указатель на этот буфер живёт
			// ровно до конца полного выражения -- то есть до закрывающей
			// скобки инициализатора. В `const char*` он превращался в висячий
			// СРАЗУ при построении таблицы.
			//
			// Дальше этот висячий указатель копировался в
			// hooks::g_unavailable_hooks, а оттуда попадал в меню
			// (menu::draw_misc), где делается `std::string += name`. operator+=
			// от `const char*` зовёт strlen по освобождённой памяти -- и
			// процесс падал в strlen, прочитав до неотображённой страницы.
			// Воспроизводилось ровно тогда, когда какие-то хуки не встали
			// (то есть почти всегда после апдейта игры) и была открыта
			// вкладка MISC.
			//
			// std::string копирует содержимое немедленно, пока временный
			// xorstr ещё жив, -- поэтому висячего указателя не возникает.
			std::string name;
			std::uintptr_t address;
		};

		bool create( const std::initializer_list<entry>& entries );

	} // namespace manager

} // namespace hooking
