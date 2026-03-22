/* Headless stub: playmp_stub.cpp
 * Provides no-op implementations for playmp_controller methods.
 * playmp_controller inherits from playsingle_controller and syncmp_handler.
 */
#include "playmp_controller.hpp"
#include "config.hpp"
#include "saved_game.hpp"
#include "display.hpp"

// Helper to get a display reference; safe only when display singleton exists.
static display& get_display_ref()
{
    return *display::get_singleton();
}

playmp_controller::playmp_controller(const config& level, saved_game& state_of_game,
    mp_game_metadata* mp_info)
    : playsingle_controller(level, state_of_game)
    , network_processing_stopped_(false)
    , network_reader_([this](config& cfg) { return receive_from_wesnothd(cfg); })
    , blindfold_(get_display_ref(), /*lock=*/false)
    , mp_info_(mp_info)
{}

playmp_controller::~playmp_controller() {}

void playmp_controller::after_human_turn() {}
void playmp_controller::do_idle_notification() {}

void playmp_controller::handle_generic_event(const std::string& /*name*/) {}

bool playmp_controller::is_host() const { return true; }
bool playmp_controller::is_networked_mp() const { return false; }

void playmp_controller::maybe_linger() {}
void playmp_controller::on_not_observer() {}
void playmp_controller::play_human_turn() {}
void playmp_controller::play_idle_loop() {}
void playmp_controller::play_network_turn() {}

void playmp_controller::play_slice() {}

void playmp_controller::process_oos(const std::string& /*err_msg*/) const {}

bool playmp_controller::receive_from_wesnothd(config& /*cfg*/) const { return false; }

void playmp_controller::send_to_wesnothd(const config& /*cfg*/,
    const std::string& /*packet_type*/) const {}

void playmp_controller::surrender(int /*side_number*/) {}
bool playmp_controller::end_linger() { return true; }
void playmp_controller::receive_actions() {}
void playmp_controller::send_actions() {}
