/* Headless stub: singleplayer.cpp
 * Replaces sp::select_campaign / sp::configure_campaign with no-ops.
 * The headless engine runs scenarios directly via wl_run_scenario(),
 * bypassing the campaign-selection GUI entirely.
 */
#include "game_initialization/singleplayer.hpp"

namespace sp {

bool select_campaign(saved_game& /*state*/, ...)
{
    return false;
}

bool configure_campaign(saved_game& /*state*/, ng::create_engine& /*create_eng*/)
{
    return true;
}

} // namespace sp
