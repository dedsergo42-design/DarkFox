// Маркер подлинности DLL.
//
// Лоадер инжектит файл по имени, но имя -- ничто: любой может положить рядом
// что угодно под названием DarkFox.dll, и лоадер вльёт это в игру. Здесь
// публикуется то, что можно проверить БЕЗ загрузки библиотеки в процесс:
// экспорт-функция с известным именем и структура-маркер в .rdata со сборкой,
// версией и magic-константой.
//
// Лоадер парсит PE и сверяет: файл -- x64 DLL, экспортирует darkfox_signature,
// в .rdata лежит k_magic и совпадает имя сборки. Этого достаточно, чтобы
// отсеять чужой/битый/32-битный файл, не выполняя ни одной инструкции из него.

#pragma once

#include <cstdint>

namespace darkfox::signature
{
	// Сигнатура структуры-маркера. Меняем при несовместимом изменении layout.
	inline constexpr std::uint32_t k_magic{ 0x4B464458 }; // 'XDFK' little-endian

	// Формат текущей версии маркера.
	inline constexpr std::uint32_t k_format{ 1 };

	// Имя экспортируемой функции-маркера. Лоадер ищет его в таблице экспорта.
	inline constexpr char k_export_name[] = "darkfox_signature";

	// Сборка, которой принадлежит файл. Лоадер сверяет: дев-лоадер обязан
	// инжектить только dev-сборку, релизный -- только релиз.
#if defined(DEV)
	inline constexpr char k_build[] = "dev";
#else
	inline constexpr char k_build[] = "ship";
#endif

	struct info
	{
		std::uint32_t magic;      // обязано быть равно k_magic
		std::uint32_t format;     // k_format на момент сборки
		std::uint32_t build_id;   // произвольный отпечаток сборки
		std::uint32_t checksum;   // XOR-контроль остальных полей
		char          build[16];  // "dev" / "ship"
		char          project[8]; // "DarkFox"
		char          reserved[8];
	};

	static_assert( sizeof( info ) == 48, "signature::info layout must stay stable" );
}
