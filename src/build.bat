@echo off
set CompilerFlags= -nologo -MTd -Gm- -GR- -EHa- -O2 -Oi -FC -Z7 -WX -W4 -DDEBUG=0 -DINTERNAL=1 -DCOMPILER_MSVC=1 -wd4201 -wd4996 -wd4100 -wd4244 -wd4456 -wd4457 -wd4245 -wd4505 -wd4701 -wd4189
if not exist ..\build mkdir ..\build
pushd ..\build
del *.pdb
cl %CompilerFlags% ../src/chess_asset_packer.cpp /link -incremental:no -opt:ref
cl %CompilerFlags% ../src/chess.cpp -LD /link -incremental:no -opt:ref -PDB:dll%random%.pdb -EXPORT:GameUpdate
cl %CompilerFlags% ../src/win32_chess.cpp /link -incremental:no -opt:ref user32.lib gdi32.lib winmm.lib opengl32.lib
REM cl %CompilerFlags% ../src/png.cpp /link -incremental:no -opt:ref
popd