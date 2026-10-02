#include <pch/pch.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <external/stb_image.h>

#include "../rendering.hpp"
#include "waifu.hpp"
#include <core/settings.hpp>

// === Ресурсы всех вайфу ===
#include <core/resources/images/astolfo.hpp>
#include <resources/images/teto.hpp>
#include <resources/images/marin.hpp>

namespace rendering {

    // === Таблица вайфу ===
    struct waifu_entry {
        const char* name;
        const unsigned char* data;
        size_t data_size;
    };

    static const waifu_entry k_waifus [] = {
        { "Astolfo", resources::images::astolfo, sizeof (resources::images::astolfo) },
        { "Teto",    resources::images::teto,    sizeof (resources::images::teto)    },
        { "Marin",   resources::images::marin,   sizeof (resources::images::marin)   },
    };
    static constexpr int k_waifu_count = sizeof (k_waifus) / sizeof (k_waifus [0]);

    static const char* k_waifu_names [k_waifu_count] = {
        "Astolfo", "Teto", "Marin"
    };

    const char* const* waifu_renderer::get_names () {
        return k_waifu_names;
    }
    int waifu_renderer::get_count () {
        return k_waifu_count;
    }

    void waifu_renderer::draw () {
        const auto& cfg = settings::g_misc.m_hud.m_waifu;

        if (!cfg.enabled.value) return;
        if (!g_menu.is_open ()) return;

        int idx = std::clamp (cfg.selected.value, 0, k_waifu_count - 1);

        this->try_load (idx);
        auto& slot = this->m_slots [idx];

        if (!slot.resource) return;

        auto& dl = xdraw::get ();

        const auto menu_x = g_menu.get_x ();
        const auto menu_y = g_menu.get_y ();

        const auto target_h = cfg.size.value;
        const auto scale = target_h / static_cast<float>(slot.height);
        const auto draw_w = static_cast<float>(slot.width) * scale;
        const auto draw_h = target_h;

        const auto x = menu_x - draw_w * 0.3f + cfg.offset_x.value;
        const auto y = menu_y - draw_h * 0.85f + cfg.offset_y.value;

        const auto alpha = static_cast<std::uint8_t>(
            std::clamp (cfg.opacity.value * 255.0f, 0.0f, 255.0f)
        );

        dl.image (x, y, draw_w, draw_h, slot.resource.Get (),
            xdraw::color {255, 255, 255, alpha});
    }

    void waifu_renderer::try_load (int index) {
        if (index < 0 || index >= k_waifu_count) return;

        auto& slot = this->m_slots [index];
        if (slot.load_attempted) return;
        slot.load_attempted = true;

        const auto& entry = k_waifus [index];

        int width {}, height {}, channels {};
        auto* rgba = stbi_load_from_memory (
            entry.data,
            static_cast<int>(entry.data_size),
            &width, &height, &channels, 4
        );

        if (!rgba) return;

        slot.resource = xdraw::create_srv_from_rgba (rgba, width, height);
        slot.width = width;
        slot.height = height;

        stbi_image_free (rgba);
    }

} // namespace rendering