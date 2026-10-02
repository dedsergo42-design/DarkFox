#pragma once
#include <string>

namespace shared_data
{
    struct info
    {
        std::string username{ "guest" };
        std::string hwid{};
        int version{ 0 };
        bool loaded{ false };
    };

    /* Читает данные из shared memory которую записал лоадер.
       Вызывать один раз при старте DLL (в context::initialize).
       Безопасно вызывать даже если лоадер не запустил маппинг —
       просто вернёт false и оставит дефолты. */
    bool read();

    /* Доступ к прочитанным данным */
    const info& get();
}