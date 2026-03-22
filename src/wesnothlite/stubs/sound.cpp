/* Headless stub: sound.cpp */
#include <chrono>
#include "sound.hpp"
#include "config.hpp"
#include "log.hpp"
#include <iostream>

static lg::log_domain log_audio("audio");
#define LOG_AUDIO LOG_STREAM(info, log_audio)

namespace sound {

bool init_sound() { return true; }
void close_sound() {}
void reset_sound() {}

void stop_music() {}
void stop_sound() {}
void stop_UI_sound() {}
void stop_bell() {}

void play_music_config(const config&, bool, int) {}
void commit_music_changes() {}
void empty_playlist() {}
void play_music() {}
void play_music_once(const std::string&) {}

void play_sound(const std::string& files, channel_group, unsigned int) {
    LOG_AUDIO << "[SOUND] " << files;
}

void play_UI_sound(const std::string& files) {
    LOG_AUDIO << "[SOUND:UI] " << files;
}

void play_bell(const std::string&) {}

void play_timer(const std::string& /*files*/,
    const std::chrono::milliseconds& /*loop_ticks*/,
    const std::chrono::milliseconds& /*fadein_ticks*/) {}

void set_music_volume(int) {}
void set_sound_volume(int) {}
void set_bell_volume(int) {}
void set_UI_volume(int) {}

void write_music_play_list(config&) {}
void flush_cache() {}

} // namespace sound
