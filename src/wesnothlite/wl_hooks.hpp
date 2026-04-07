/**
 * wl_hooks.hpp — Thin C++ hooks callable from headless stubs.
 *
 * These functions run in the game thread and post WL events through the
 * thread-local channel set up by wesnothlite.cpp.  They are no-ops when
 * called outside a game session (tl_channel == nullptr).
 */

#pragma once

#include <string>
#include <vector>

/** Post a WL_EVENT_STORY event for one story screen part. */
void wl_hook_story_part(const std::string& title,
                        const std::string& text,
                        const std::string& background);

/** Post a WL_EVENT_MUSIC_CHANGE event. */
void wl_hook_music_change(const std::string& path,
                          const std::string& title = "");

/**
 * Post a WL_EVENT_MESSAGE and block until the player dismisses the dialog.
 * For simple messages (0 or 1 trivial option) the game thread blocks via
 * await_ack(); for messages with real choices it additionally posts
 * WL_EVENT_CHOICE_NEEDED and returns the chosen option index (0-based).
 * portrait may be empty.
 */
int wl_hook_message(const std::string& speaker,
                    const std::string& portrait,
                    const std::string& text,
                    const std::vector<std::string>& options = {});
