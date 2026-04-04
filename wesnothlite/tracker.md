### OPEN

- Narration:
  - `[story]` tags and its subtags are currently not handled at all. When I open a campaign, after it loads, I am immediately thrown into the game scenario. In desktop Wesnoth, before the first scenario and between scenarios I see a bunch of navigable story slides with an option to skip them.
    - Optional: I think since the `[story]` tags are describing something that is more-or-less separate from the normal gameplay, I think it'd make sense for Wesnothlite to send the whole parsed `story` tag content as one big event to the browser client, and handle all the rendering JS-side in a specialised component
    - Optional: I also think it'd be nice (if at all possible) for Wesnothlite engine to send the event containing the parsed `story` tags before the whole scenario has loaded -- this would neatly alleviate the problem of the long time to load a scenario since that loading would be happening in the background.
- Savegames:
  - The Savegames Manager does not work at all. Clicking the "Save" button does nothing, and the console has this to say: `[GameController] wl_save_to_buffer returned empty buffer`
- Navigation: After entering a scenario, the browser Back button should take you back to the campaign picker.
- Messages and Objectives: they do not render. When entering a scenario, there should appear a few "Message"s with speaker portraits as defined in the "start" event in WML; similarily the screen with scenario objectives also does not appear.
- UI:
  - Main menu bar at the top overlays the top of the sidebar on the right. This is obviously wrong; perhaps you need to use two divs (one for the menu bar, and the other for both the game and the sidebar)

### IN PROGRESS

### PENDING VERIFICATION

### DONE