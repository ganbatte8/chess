A simple chess game made (more or less) from scratch using OpenGL.
There is a Windows port based on the Win32 API, and a Linux port based on Xlib/XCB.


NOTE: not a chess enthusiast. This is just a programming curiosity for me, though I tried to make it somewhat robust and good.


### Build
#### Windows
Drop stb_truetype.h in the src folder (https://github.com/nothings/stb/blob/master/stb_truetype.h).
Run src/build.bat. Your shell instance needs to have the MSVC compiler activated for 64-bit compilation.
Then run the asset packer executable. It will pack the bmp files into a single file used by the game.
#### Linux
You need a ttf file to feed to the asset packer. Look for the line containing /usr/share/fonts/TTF/Inconsolata-Regular.ttf in chess_asset_packer.cpp.
Replace that path with whatever ttf file works for you. Put stb_truetype.h in the src folder. 
You will need the GCC compiler, and the packages for XCB, Xlib and OpenGL development for whatever distro you are using.
Run the src/build.sh script. 
If you are missing some package, then you will either get a linker error (ld) or some error about not being able to find a .h file to include.


### Controls
- Directional navigation: arrow keys, mouse cursor, x360 left joystick and D-pad.
- Back/forward navigation: enter/backspace keys, mouse wheel, x360 X/Y. 
This is useful for selecting a game save or navigating in the history.
- Validate/cancel: ';' / 'q' keys, left/right click, x360 A/B.
- You can also use the knbpt keys to point the cursor at pieces by their type, a little bit in the spirit of Vim, but it's not very useful imo.

### Main features
- Autosave. Applied whenever a move is executed in an ongoing game.
- Navigation through multiple game saves; duplication and deletion of saves.
- Game history navigation once current game is over.
- Multiple AI difficulties, including an iterative implementation of minimax with alpha beta pruning.
- Castling, en passant and pawn promotion rules are properly handled.
- Draw/stalemate is partially handled.
- Some nice UI and animation features.
- Keyboard, x360 controller and mouse input. Entirely playable on any of the 3.
- Some shitty art I did in less than a day after buying Aseprite.

### Main issues
- Keyboard virtual keycode mapping is only really suited for dvorak layout.
- Some ai problems, see https://github.com/ganbatte8/chess/blob/main/src/chess.cpp#L2069
- Save file corruption safety could be improved
