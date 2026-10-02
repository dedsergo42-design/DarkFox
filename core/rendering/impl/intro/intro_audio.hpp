#pragma once
#include <Windows.h>
#include <mmsystem.h>
#include <vector>
#include <chrono>

#pragma comment(lib, "winmm.lib")

namespace rendering::intro {

    class audio_player {
    public:
        // Проиграть WAV из ресурса DLL (async, loop = false)
        bool play_resource(HMODULE dll_module, const char* resource_name) {
            stop();

            // Находим ресурс
            const auto res_info = FindResourceA(dll_module, resource_name, "WAVE");
            if (!res_info) {
                return false;
            }

            const auto res_data = LoadResource(dll_module, res_info);
            if (!res_data) {
                return false;
            }

            const auto res_ptr = LockResource(res_data);
            if (!res_ptr) {
                return false;
            }

            const auto res_size = SizeofResource(dll_module, res_info);
            if (res_size == 0) {
                return false;
            }

            // Копируем в heap чтобы PlaySound был асинхронным и безопасным
            m_wav_buffer.assign(
                static_cast<const std::uint8_t*>(res_ptr),
                static_cast<const std::uint8_t*>(res_ptr) + res_size
            );

            // Играем async из памяти
            const auto ok = PlaySoundA(
                reinterpret_cast<LPCSTR>(m_wav_buffer.data()),
                nullptr,
                SND_MEMORY | SND_ASYNC | SND_NODEFAULT
            );

            if (ok) {
                m_playing = true;
                m_start_time = std::chrono::steady_clock::now();
            }

            return ok != FALSE;
        }

        void stop() {
            if (m_playing) {
                PlaySoundA(nullptr, nullptr, 0);
                m_playing = false;
                m_wav_buffer.clear();
            }
        }

        [[nodiscard]] bool is_playing() const { return m_playing; }

        // Прошло секунд с момента старта воспроизведения
        [[nodiscard]] float elapsed_seconds() const {
            if (!m_playing) return 0.0f;
            const auto now = std::chrono::steady_clock::now();
            const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_start_time).count();
            return static_cast<float>(diff) / 1000.0f;
        }

    private:
        bool m_playing = false;
        std::vector<std::uint8_t> m_wav_buffer;
        std::chrono::steady_clock::time_point m_start_time;
    };

} // namespace rendering::intro