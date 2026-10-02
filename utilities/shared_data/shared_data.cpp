#include <pch/pch.hpp>
#include "shared_data.hpp"

#include <Windows.h>
#include <cstdio>
#include <cstring>

namespace shared_data
{
    /* ВАЖНО: точно такая же структура должна быть в loader/shared_data.cpp!
       #pragma pack(1) убирает padding — гарантирует одинаковый layout
       между DLL и лоадером. */
    #pragma pack(push, 1)
    struct payload_v1
    {
        char     magic[8];        // "DarkFox\0"
        char     username[64];
        char     hwid[64];        // 64 (не 65!) — hex sha256 без нуль-терминатора
        uint32_t version;
        uint32_t reserved;        // резерв на будущее, чтобы структура была ровно 144 байта
    };
    #pragma pack(pop)

    static_assert(sizeof(payload_v1) == 144, "payload_v1 size must be 144 bytes");

    static info g_info{};

    /* SEH-выделенная функция без C++ объектов с деструкторами для разграничения с __try (C2712) */
    static bool read_payload_seh(payload_v1* out)
    {
        __try {
            DWORD pid = GetCurrentProcessId();

            char mappingName[64];
            sprintf_s(mappingName, sizeof(mappingName), "Global\\NGN_DATA_%lu", pid);

            HANDLE hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, mappingName);
            if (!hMap) {
                /* Fallback: пробуем без Global\ префикса (если запущено без админа) */
                sprintf_s(mappingName, sizeof(mappingName), "NGN_DATA_%lu", pid);
                hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, mappingName);
            }

            if (!hMap) {
                return false;
            }

            void* view = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, sizeof(payload_v1));
            if (!view) {
                CloseHandle(hMap);
                return false;
            }

            /* Копируем данные в локальную переменную */
            memcpy(out, view, sizeof(payload_v1));

            UnmapViewOfFile(view);
            CloseHandle(hMap);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool read()
    {
        payload_v1 local{};
        if (!read_payload_seh(&local)) {
            g_info.username = "guest";
            g_info.hwid = "";
            g_info.version = 0;
            g_info.loaded = false;
            return false;
        }

        /* Проверяем магию */
        if (memcmp(local.magic, "DarkFox", 7) != 0) {
            g_info.loaded = false;
            return false;
        }

        /* Форсим null-terminator для безопасности */
        local.username[sizeof(local.username) - 1] = '\0';
        local.hwid[sizeof(local.hwid) - 1] = '\0';

        /* Заполняем результат */
        g_info.username = local.username;
        g_info.hwid = std::string(local.hwid, sizeof(local.hwid));
        /* Убираем возможные нули в конце hwid */
        auto nullpos = g_info.hwid.find('\0');
        if (nullpos != std::string::npos) {
            g_info.hwid.resize(nullpos);
        }
        g_info.version = static_cast<int>(local.version);
        g_info.loaded = true;

        return true;
    }

    const info& get() { return g_info; }
}