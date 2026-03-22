/* Headless stub: cursor.cpp — cursor.hpp is already SDL-free; this provides the symbols */
#include "cursor.hpp"

namespace cursor {
static CURSOR_TYPE current_cursor = NORMAL;
manager::manager() {}
manager::~manager() {}
void set(CURSOR_TYPE type) { current_cursor = (type == NUM_CURSORS) ? NORMAL : type; }
void set_dragging(bool) {}
CURSOR_TYPE get() { return current_cursor; }
void set_focus(bool) {}
setter::setter(CURSOR_TYPE type) : old_(get()) { set(type); }
setter::~setter() { set(old_); }
} // namespace cursor
