#pragma once

#include <external/xdraw/xdraw.hpp>
#include <array>

namespace rendering {

    class waifu_renderer {
    public:
        void draw ();

        // Для меню
        static const char* const* get_names ();
        static int get_count ();

    private:
        struct texture_slot {
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> resource {};
            int width {};
            int height {};
            bool load_attempted {};
        };

        std::array<texture_slot, 3> m_slots {};

        void try_load (int index);
    };

    inline waifu_renderer g_waifu {};

} // namespace rendering