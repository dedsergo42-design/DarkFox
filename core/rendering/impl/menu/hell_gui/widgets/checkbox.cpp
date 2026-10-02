#include "checkbox.h"
#include <menu/hell_gui/colors.h>
#include <menu/hell_gui/menu_ui.h>
#include <menu/hell_gui/menu_polish.h>

namespace hell {

bool checkbox(const char* label, bool& value, std::function<void()> content, const char* tooltip) {
    return checkbox(label, &value, content, tooltip);
}

bool checkbox(const char* label, bool* value_ptr, std::function<void()> content, const char* tooltip) {
    // Implementation will be completed in next files
    return false;
}

float calc_checkbox_size() {
    return 16.0f;
}

} // namespace hell