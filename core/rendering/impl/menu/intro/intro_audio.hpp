#pragma once
#include <Windows.h>
#include <mmsystem.h>
#include <vector>
#include <chrono>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "winmm.lib")

namespace rendering::intro {

    class audio_player {
    public:
        bool play_resource(HMODULE dll_module, const char* resource_name) {
            stop();

            const auto res_info = FindResourceA(dll_module, resource_name, "WAVE");
            if (!res_info) return false;

            const auto res_data = LoadResource(dll_module, res_info);
            if (!res_data) return false;

            const auto res_ptr = LockResource(res_data);
            if (!res_ptr) return false;

            const auto res_size = SizeofResource(dll_module, res_info);
            if (res_size == 0) return false;

            m_wav_buffer.assign(
                static_cast<const std::uint8_t*>(res_ptr),
                static_cast<const std::uint8_t*>(res_ptr) + res_size
            );

            // Read the length off the header instead of keeping a second constant in
            // sync with the asset. Swapping the embedded track used to mean also
            // editing the intro's hold time by hand, and getting it wrong either cut
            // the song mid-phrase or left the screen sitting there after it ended.
            m_duration = wav_duration_seconds(m_wav_buffer.data(), m_wav_buffer.size());

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

        // Length of the loaded track in seconds, or 0 when nothing parsed. Deliberately
        // survives stop(): it describes the asset, not the playback.
        [[nodiscard]] float duration_seconds() const { return m_duration; }

        [[nodiscard]] float elapsed_seconds() const {
            if (!m_playing) return 0.0f;
            const auto now = std::chrono::steady_clock::now();
            const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_start_time).count();
            return static_cast<float>(diff) / 1000.0f;
        }

    private:
        static std::uint32_t read_u32(const std::uint8_t* p) {
            return static_cast<std::uint32_t>(p[0])
                | (static_cast<std::uint32_t>(p[1]) << 8)
                | (static_cast<std::uint32_t>(p[2]) << 16)
                | (static_cast<std::uint32_t>(p[3]) << 24);
        }

        // data_size / nAvgBytesPerSec, walked chunk by chunk. Chunks are padded to an
        // even length, hence the (size & 1) term.
        static float wav_duration_seconds(const std::uint8_t* data, std::size_t size) {
            if (!data || size < 44) return 0.0f;
            if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) return 0.0f;

            std::uint32_t byte_rate = 0;
            std::uint32_t data_size = 0;

            std::size_t offset = 12;
            while (offset + 8 <= size) {
                const auto* chunk = data + offset;
                const auto chunk_size = read_u32(chunk + 4);

                if (std::memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16 && offset + 8 + 16 <= size) {
                    byte_rate = read_u32(chunk + 8 + 8);
                } else if (std::memcmp(chunk, "data", 4) == 0) {
                    data_size = chunk_size;
                    break;
                }

                offset += 8 + chunk_size + (chunk_size & 1u);
            }

            if (byte_rate == 0 || data_size == 0) return 0.0f;

            return static_cast<float>(data_size) / static_cast<float>(byte_rate);
        }

        bool m_playing = false;
        float m_duration = 0.0f;
        std::vector<std::uint8_t> m_wav_buffer;
        std::chrono::steady_clock::time_point m_start_time;
    };

}
