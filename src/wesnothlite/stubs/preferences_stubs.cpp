/* Headless stub: preferences_stubs.cpp
 * Upstream unified all preferences into prefs::get() singleton in preferences.cpp.
 * The functions below cover legacy free-function symbols still referenced by
 * a few headless source files.
 */
#include "preferences/preferences.hpp"
#include <string>

namespace preferences {

void add_completed_campaign(const std::string& /*campaign_id*/, const std::string& /*difficulty_level*/) {}

void encounter_all_content(const game_board& /*gb*/) {}

bool interrupt_when_ally_sighted() { return true; }

bool is_ignored(const std::string& /*nick*/) { return false; }

bool parse_should_show_lobby_join(const std::string& /*sender*/, const std::string& /*message*/)
{
    return false;
}

bool skip_ai_moves() { return false; }

bool turn_dialog() { return false; }

} // namespace preferences
