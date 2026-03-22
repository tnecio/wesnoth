#pragma once
/* Headless stub: gui/dialogs/modal_dialog.hpp
 * Provides a minimal modal_dialog that does NOT define gui2::widget/window
 * (those come from the real widget headers). Found BEFORE the real header
 * due to -I stubs/ ordering in the headless build.
 */

#include <functional>
#include <memory>
#include <string>
#include <vector>
// Restore transitive includes that the real modal_dialog.hpp provided:
#include "hotkey/hotkey_command.hpp"
#include "lexical_cast.hpp"
#include "sdl/rect.hpp"
#include "utils/general.hpp"
#include <SDL2/SDL_timer.h>

// Forward-declare real gui2 types used as reference params — no redefinition
namespace gui2 {
class window;
class label;
class drawing;
class button;
class styled_widget;
class container_base;
class text_box_base;
class menu_button;
class listbox;
namespace dialogs { class message; }
} // namespace gui2

// ---------------------------------------------------------------------------
// Macros — no-op versions of REGISTER_WINDOW / REGISTER_DIALOG
// ---------------------------------------------------------------------------
#define REGISTER_WINDOW(id)
#define REGISTER_DIALOG2(type, id)                                            \
    const std::string& type::window_id() const {                             \
        static const std::string result(#id);                                \
        return result;                                                       \
    }
#define REGISTER_DIALOG(window_id) REGISTER_DIALOG2(window_id, window_id)

#define DEFINE_SIMPLE_DISPLAY_WRAPPER(dialog)                                \
    template<typename... T>                                                  \
    static void display(T&&... args)                                         \
    {                                                                        \
        dialog(std::forward<T>(args)...).show();                             \
    }

#define DEFINE_SIMPLE_EXECUTE_WRAPPER(dialog)                                \
    template<typename... T>                                                  \
    static bool execute(T&&... args)                                         \
    {                                                                        \
        return dialog(std::forward<T>(args)...).show();                      \
    }

// ---------------------------------------------------------------------------
// Minimal modal_dialog stub — standalone (no widget/window inheritance)
// Provides virtual layout() so loading_screen::layout() override compiles.
// ---------------------------------------------------------------------------
namespace gui2::dialogs {

class modal_dialog {
public:
    explicit modal_dialog(const std::string& /*window_id*/) {}
    virtual ~modal_dialog() = default;

    bool show(unsigned /*auto_close_time*/ = 0) { return false; }
    int get_retval() const { return 0; }

    void set_always_save_fields(bool) {}
    void set_allow_plugin_skip(bool) {}
    void set_show_even_without_video(bool) {}

    // TLD-style virtual interface — loading_screen (and others) override these
    virtual void update() {}
    virtual void layout() {}
    virtual void render() {}
    virtual bool expose(const rect& /*region*/) { return false; }
    virtual rect screen_location() { return {}; }

protected:
    virtual const std::string& window_id() const = 0;
    virtual void pre_show() {}
    virtual void post_show() {}
    virtual void init_fields() {}
    virtual void finalize_fields(bool) {}

    // Null pointer helpers used by dialog constructors that register fields
    using field_bool    = void;
    using field_integer = void;
    using field_text    = void;
    using field_label   = void;
};

} // namespace gui2::dialogs
