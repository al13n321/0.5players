Play the original game 0PLAYER here: https://plus.cerisetalis.com/0PLAYER/

0.5players is a an unofficial interactive version of 0PLAYER. It's full of spoilers, and you shouldn't play this before finishing 0PLAYER. This is meant as post-game content.

(If you're put off by the original game being just a static image, rest assured that you would enjoy the interactive version even less than staring at the static image.)

I made this just to learn some gamedev (and because I got briefly obsessed with that game). The learning was a success! The original 2d version (commit beb5275) was made in 2 days, then it took a while to prettify it to the current state.

![screenshot](https://github.com/user-attachments/assets/d6032a86-5676-4ee1-8f13-aee8db52e8bf)

---

Game controls:
 * middle-mouse-button + drag to pan
 * mouse wheel to zoom
 * mouse click
 * wasd or arrows
 * can also drag tiles with the mouse
 * z to undo, x to redo

Janky terrible record/replay controls:
 * ctrl+digit (ctrl+0 .. ctrl+9) to start recording a macro (while recording, redo is not allowed; also, I wouldn't be surprised if undo during animation breaks recording, I haven't checked)
 * ctrl+digit again to stop recording and save all 10 macros to file "replays.bin"; it's loaded on startup automatically
 * digit to replay a previous recording; it just repeats the inputs, so if you start from a different state, it'll probably do nonsense moves and give up early
 * if there's already a macro on that digit, ctrl+digits clears it; ctrl+shift+digit to append to it instead
 * weird tech: you can replay a macro while recording another macro; this way you can concatenate multiple macros together, e.g. record a playthrough in a few segments, then merge them into one macro later
 * the default "replays.bin" file included with the game has a full playthrough as macro 0; just press 0 and have the whole game spoiled even more than it was before!

Level editor with questionable controls:
 * F10 to toggle level editor
 * click a tile in palette on the right to select it (left column is movable tiles, right column is floor/walls/triggers)
 * or press 'q' while hovering over a tile in the world to copy it
 * click or drag in the world to place selected/copied tile
 * hold right mouse button and drag to weld tiles together
 * wasd to toggle wires on the cell under the cursor
 * j for bridge wires, k for conductive surface, o for circle, p to toggle power on the circle
 * ctrl + hold right mouse button and drag to place doors
 * F5 to save the map to file "save.bin"; move it "assets/level.bin" to make it load on startup
 * F9 to load from "save.bin" (if exists) or "assets/level.bin"
 * alt+r to reset everything to a small empty map
 * alt+arrow keys to resize the map

Misc:
 * '[' for animation debug mode, where tile movement animation is stalled, and you can advance it step by step by pressing ']'
