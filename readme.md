A simple chess game made from scratch using OpenGL and the Win32 API.

### Build
Drop stb_truetype.h in the src folder (https://github.com/nothings/stb/blob/master/stb_truetype.h).
Run src/build.bat. Your shell instance needs to have the MSVC compiler activated for 64-bit compilation.
Then run the asset packer executable. It will pack the bmp files into a single file used by the game.

### Controls
- Directional navigation: arrow keys, mouse cursor, x360 left joystick and D-pad.
- Back/forward navigation: enter/backspace keys, mouse wheel, x360 X/Y. 
This is useful for selecting a game save or navigating in the history.
- Validate/cancel: ';' / 'q' keys, left/right click, x360 A/B.
- You can also use the knbpt keys to point the cursor at pieces by their type, a little bit in the spirit of Vim, but it's not very useful I think.

### Main features
- Autosave. Applied whenever a move is executed in an ongoing game.
- navigation through multiple game saves; duplication and deletion of saves.
- game history navigation once current game is over.
- multiple ai difficulties, including an iterative implementation of minimax with alpha beta pruning.
- castling, en passant and pawn promotion rules are properly handled.
- draw/stalemate is partially handled.
- some nice UI and animation features.
- keyboard, x360 controller and mouse input. Entirely playable on any of the 3.
- some shitty art

### Main issues
- keyboard virtual keycode mapping is only really suited for dvorak layout.
- some ai problems, see https://github.com/ganbatte8/chess/blob/main/src/chess.cpp#L2092 
- save file corruption safety could be improved