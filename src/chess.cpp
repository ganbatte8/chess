
#include "common.h"
global_variable platform_api *GlobalPlatform;
#include "chess_render_group.cpp"

struct asset_header
{
    bitmap WhitePawn;
    bitmap WhiteRook;
    bitmap WhiteKnight;
    bitmap WhiteBishop;
    bitmap WhiteKing;
    bitmap WhiteQueen;
    
    bitmap BlackPawn;
    bitmap BlackRook;
    bitmap BlackKnight;
    bitmap BlackBishop;
    bitmap BlackKing;
    bitmap BlackQueen;
    
    font Font;
};

enum chess_piece_type
{
    ChessPieceType_Empty = 0,   // captured pieces have this type
    ChessPieceType_Pawn,
    ChessPieceType_Rook,
    ChessPieceType_Knight,
    ChessPieceType_Bishop,
    ChessPieceType_Queen,
    ChessPieceType_King,
};

struct destination
{
    u8 DestCode;
    // low 3 bits:  Row      (DestCode & 7)
    // next 3 bits: Column   ((DestCode >> 3) & 7)
    // seventh bit: Indicates capture when set, useful for rendering
};

struct board
{
    moving_v2 P;  // center of board in game world space (not pixels)
    moving_v2 Dim;
};

struct chess_piece
{
    chess_piece_type Type;
    
    moving_v2 P;  
    // center of bitmap, relative to the board. (0,0) is center of board, (1,1) is top-right corner.
    
    u32 Row;
    u32 Column;
    u32 MoveCount;
    u32 DestinationsCount;
    destination *Destinations;
    u32 Index;   // 0-15.
};

struct cursor
{
    s32 Row;
    s32 Column;
    
    moving_v2 P;
    // center of bitmap, relative to the board. (0,0) is center of board, (1,1) is top-right corner.
    
    f32 BreatheT;
};


struct decoded_history_entry
{
    s32 DeltaRow;
    s32 DeltaCol;
    u32 MovingPieceIndex;
    u32 CapturedPieceIndex;
    b32 ThereIsCapture;
    chess_piece_type CapturedType;
    chess_piece_type PromotionType;
    b32 ThereIsPromotion;
};

struct history_entry
{
    u8 Delta;  // low 4 bits: delta row, high 4 bits: delta column
    
    u8 Indices;  // low 4 bits: moving piece index; high 4 bits: captured piece index.
    // Because index 12 is always the king, and the king cannot be captured,
    // the captured piece index here is actually encoded such that we have a value of 0
    // iff there is no capture.
    
    u8 Special;
    // low 2 bits for pawn promotion: 0 is rook, 1 is knight, 2 is bishop, 3 is queen.
    // lowest 3rd bit should be set iff there was a promotion (necessary for undo).
    // high 3 bits for captured piece type (necessary for undo):
    // pawn 0, rook 1, knight 2, bishop 3, queen 4.
};

internal void
DecodeHistoryEntry(decoded_history_entry *Out, history_entry In)
{
    u32 DeltaRowCode = In.Delta & 15;
    Out->DeltaRow = ((DeltaRowCode >> 3) == 1 ? -1 : 1) * (DeltaRowCode & 7);
    u32 DeltaColCode = In.Delta >> 4;
    Out->DeltaCol = ((DeltaColCode >> 3) == 1 ? -1 : 1) * (DeltaColCode & 7);
    
    Out->MovingPieceIndex = In.Indices & 15;
    Out->CapturedPieceIndex = In.Indices >> 4;
    if (1 <= Out->CapturedPieceIndex && Out->CapturedPieceIndex <= 12)
        Out->CapturedPieceIndex--;
    Out->ThereIsCapture = ((In.Indices >> 4) != 0);
    
    switch (In.Special >> 5)
    {
        case 0: Out->CapturedType = ChessPieceType_Pawn; break;
        case 1: Out->CapturedType = ChessPieceType_Rook; break;
        case 2: Out->CapturedType = ChessPieceType_Knight; break;
        case 3: Out->CapturedType = ChessPieceType_Bishop; break;
        case 4: Out->CapturedType = ChessPieceType_Queen; break;
        InvalidDefaultCase;
    }
    switch (In.Special & 3)
    {
        case 0: Out->PromotionType = ChessPieceType_Rook; break;
        case 1: Out->PromotionType = ChessPieceType_Knight; break;
        case 2: Out->PromotionType = ChessPieceType_Bishop; break;
        case 3: Out->PromotionType = ChessPieceType_Queen; break;
        InvalidDefaultCase;
    }
    Out->ThereIsPromotion = (In.Special >> 2 & 1);
}

struct history
{
    u32 EntryCount;
    history_entry Entries[1000];
};

enum chess_game_running_state
{
    ChessGameRunningState_Normal,
    ChessGameRunningState_Check,
    ChessGameRunningState_Checkmate,
    ChessGameRunningState_Stalemate,
};

struct get_piece_result
{
    chess_piece *Piece;
    b32 IsWhite;
};

struct decision
{
    chess_piece *Piece;
    destination Destination;
    chess_piece_type PromotionType;
};

struct good_decision_result
{
    decision Decision;
    s32 Value;
};

struct chess_game_state;

struct get_good_decision_params
{
    chess_game_state *Game;
    memory_arena *Arena;
    random_series *Series;
    u32 MaxDepth;
    
    b32 ShouldContinue;
    good_decision_result Result;
    b32 Finished;
};

struct ai_state
{
    s32 OldCursorRow;
    s32 OldCursorColumn;
    u32 Stage;
    f32 t;
    
    get_good_decision_params WorkParams;
};

struct chess_game_state
{
    chess_game_running_state RunningState;
    
    b32 PromotingPawn;
    ai_state AIState;
    
    board Board;
    chess_piece Blacks[16];
    chess_piece Whites[16];
    
    cursor Cursor;
    get_piece_result SelectedPiece;
    get_piece_result PieceOnCursor;
    f32 SelectedPieceT;
    f32 DestColorT;
    
    b32 BlackCanMove;
    b32 WhiteCanMove;
    b32 BlackIsPlaying;
    
    history History;
    
    u32 WhiteAI;  // 0 iff human
    u32 BlackAI;
    
    b32 GameIsOver;
    u32 CurrentEntryIndex;
    
    // NOTE(vincent): upper bound for number of destinations to push:
    // 27 possible destinations for a queen at most. There cannot be more than 18 queens. 
    // 27*18 = 486.
    // 14 for rooks and bishops, 8 * 14 = 112. 
    // 8 for kings and knights. 6 * 8 = 48.  Total: 646, actual answer is probably much less.
    u32 DestinationsCount;
    destination Destinations[646];
};

enum game_mode
{
    GameMode_StartScreen,
    GameMode_Gameplay,
    GameMode_Pause,
    GameMode_Settings
};

struct repeat_clock
{
    u32 Phase;
    f32 t;
};

struct repeat_clocks
{
    repeat_clock Left;
    repeat_clock Down;
    repeat_clock Up;
    repeat_clock Right;
    repeat_clock Forward;
    repeat_clock Back;
};

struct game_state
{
    b32 IsInitialized;
    b32 ShouldSave;
    
    memory_arena GlobalArena;
    memory_arena AIArena;
    
    random_series Series;
    
    repeat_clocks RepeatClocks;
    
    render_group RenderGroup; 
    
    game_mode PreviousMode;
    game_mode GameMode;
    s32 MenuX;
    s32 MenuY;
    u32 CurrentGameIndex;
    u32 GamesCount;
    
    b32 ShouldUpdateBoardMovingVectors;
    
    bitmap BoardBitmap;         // TODO(vincent): 
    
#define MAX_GAMES_COUNT 100
    chess_game_state Games[MAX_GAMES_COUNT];
    
    asset_header *Assets;       // TODO(vincent): 
};

internal v2
GetBoardSpaceV2(u32 Row, u32 Column)
{
    // board side length is 2 units, so 1 square is 0.25f units.
    f32 SquareSide = 0.25f;
    v2 Result = V2(-0.875f + Column*SquareSide, -0.875f + Row*SquareSide);
    return Result;
}


#define MOVE_PIECE_DURATION .3f

internal void
InitChessPiece(chess_piece *Piece, chess_piece_type Type, u32 Row, u32 Column, u32 Index)
{
    Assert(Row < 8 && Column < 8);
    Piece->Type = Type;
    Piece->Row = Row;
    Piece->Column = Column;
    Piece->Index = Index;
    InitMovingV2FromCurrent(&Piece->P, GetBoardSpaceV2(Row, Column), MOVE_PIECE_DURATION);
}

internal void
InitChessPieces(chess_game_state *Game)
{
    
    for (u32 PawnColumn = 0; PawnColumn < 8; ++PawnColumn)
    {
        InitChessPiece(Game->Blacks + PawnColumn, ChessPieceType_Pawn, 6, PawnColumn, PawnColumn);
        InitChessPiece(Game->Whites + PawnColumn, ChessPieceType_Pawn, 1, PawnColumn, PawnColumn);
    }
    
    InitChessPiece(Game->Whites + 8,  ChessPieceType_Rook,   0, 0, 8);
    InitChessPiece(Game->Whites + 9,  ChessPieceType_Knight, 0, 1, 9);
    InitChessPiece(Game->Whites + 10, ChessPieceType_Bishop, 0, 2, 10);
    InitChessPiece(Game->Whites + 11, ChessPieceType_Queen,  0, 3, 11);
    InitChessPiece(Game->Whites + 12, ChessPieceType_King,   0, 4, 12);
    InitChessPiece(Game->Whites + 13, ChessPieceType_Bishop, 0, 5, 13);
    InitChessPiece(Game->Whites + 14, ChessPieceType_Knight, 0, 6, 14);
    InitChessPiece(Game->Whites + 15, ChessPieceType_Rook,   0, 7, 15);
    
    InitChessPiece(Game->Blacks + 8,  ChessPieceType_Rook,   7, 0, 8);
    InitChessPiece(Game->Blacks + 9,  ChessPieceType_Knight, 7, 1, 9);
    InitChessPiece(Game->Blacks + 10, ChessPieceType_Bishop, 7, 2, 10);
    InitChessPiece(Game->Blacks + 11, ChessPieceType_Queen,  7, 3, 11);
    InitChessPiece(Game->Blacks + 12, ChessPieceType_King,   7, 4, 12);
    InitChessPiece(Game->Blacks + 13, ChessPieceType_Bishop, 7, 5, 13);
    InitChessPiece(Game->Blacks + 14, ChessPieceType_Knight, 7, 6, 14);
    InitChessPiece(Game->Blacks + 15, ChessPieceType_Rook,   7, 7, 15);
}

internal chess_piece *
GetWhite(chess_piece *Whites, u32 Row, u32 Column)
{
    Assert(Row < 8 && Column < 8);
    
    chess_piece *Result = 0;
    for (u32 Index = 0; Index < 16; ++Index)
    {
        if (Whites[Index].Row == Row && Whites[Index].Column == Column && Whites[Index].Type)
        {
            Result = Whites + Index;
            break;
        }
    }
    return Result;
}

internal chess_piece *
GetBlack(chess_piece *Blacks, u32 Row, u32 Column)
{
    Assert(Row < 8 && Column < 8);
    chess_piece *Result = 0;
    for (u32 Index = 0; Index < 16; ++Index)
    {
        if (Blacks[Index].Row == Row && Blacks[Index].Column == Column && Blacks[Index].Type)
        {
            Result = Blacks + Index;
            break;
        }
    }
    return Result;
}


internal get_piece_result
GetPiece(chess_piece *Blacks, chess_piece *Whites, u32 Row, u32 Column)
{
    Assert(Blacks + 16 == Whites);
    get_piece_result Result = {};
    Assert(Row < 8 && Column < 8);
    Result.Piece = GetWhite(Whites, Row, Column);
    if (Result.Piece)
        Result.IsWhite = true;
    else
        Result.Piece = GetBlack(Blacks, Row, Column);
    Assert(!Result.Piece || (Result.Piece->Type != ChessPieceType_Empty));
    return Result;
}

internal b32
HasPiece(chess_piece *Blacks, chess_piece *Whites, u32 Row, u32 Column)
{
    Assert(Blacks + 16 == Whites);
    Assert(Row < 8 && Column < 8);
    b32 Result = false;
    get_piece_result GetPieceResult = GetPiece(Blacks, Whites, Row, Column);
    if (GetPieceResult.Piece)
        Result = true;
    return Result;
}

internal b32
IsCheck_(chess_piece *Blacks, chess_piece *Whites, b32 White)
{
    b32 Result = false;
    Assert(Whites[12].Type == ChessPieceType_King);
    Assert(Blacks[12].Type == ChessPieceType_King);
    Assert(Blacks + 16 == Whites);
    
    s32 KR, KC;
    chess_piece *Capturers;
    if (White)
    {
        KR = Whites[12].Row;
        KC = Whites[12].Column;
        Capturers = Blacks;
    }
    else
    {
        KR = Blacks[12].Row;
        KC = Blacks[12].Column;
        Capturers = Whites;
    }
    
    for (u32 Index = 0; Index < 16 && !Result; ++Index)
    {
        chess_piece *Piece = Capturers + Index;
        s32 R = Piece->Row;
        s32 C = Piece->Column;
        s32 dR = KR - R;
        s32 dC = KC - C;
        switch (Piece->Type)
        {
            case ChessPieceType_Pawn:
            {
                Result = (dR == (White ? -1 : 1) && AbsoluteValue(dC) == 1);
            } break;
            
            case ChessPieceType_Rook:
            {
                if (dR == 0)
                {
                    s32 MinC = Minimum(C, KC);
                    s32 MaxC = Maximum(C, KC);
                    Result = true;
                    for (s32 CIndex = MinC+1; CIndex < MaxC; ++CIndex)
                    {
                        if (HasPiece(Blacks, Whites, R, CIndex))
                        {
                            Result = false;
                            break;
                        }
                    }
                }
                else if (dC == 0)
                {
                    s32 MinR = Minimum(R, KR);
                    s32 MaxR = Maximum(R, KR);
                    Result = true;
                    for (s32 RIndex = MinR+1; RIndex < MaxR; ++RIndex)
                    {
                        if (HasPiece(Blacks, Whites, RIndex, C))
                        {
                            Result = false;
                            break;
                        }
                    }
                }
            } break;
            
            case ChessPieceType_Knight:
            {
                Result = ((AbsoluteValue(dR) == 1 && AbsoluteValue(dC) == 2) ||
                          (AbsoluteValue(dR) == 2 && AbsoluteValue(dC) == 1));
            } break;
            
            case ChessPieceType_Bishop:
            {
                if (AbsoluteValue(dR) == AbsoluteValue(dC))
                {
                    Result = true;
                    
                    s32 RIncrement = (dR > 0 ? 1 : -1);
                    s32 CIncrement = (dC > 0 ? 1 : -1);
                    
                    s32 CurrentR = R + RIncrement;
                    s32 CurrentC = C + CIncrement;
                    while (CurrentR != KR)
                    {
                        if (HasPiece(Blacks, Whites, CurrentR, CurrentC))
                        {
                            Result = false;
                            break;
                        }
                        CurrentR += RIncrement;
                        CurrentC += CIncrement;
                    }
                }
            } break;
            
            case ChessPieceType_Queen:
            {
                // NOTE(vincent): bishop test and then rook test
                if (AbsoluteValue(dR) == AbsoluteValue(dC))
                {
                    Result = true;
                    
                    s32 RIncrement = (dR > 0 ? 1 : -1);
                    s32 CIncrement = (dC > 0 ? 1 : -1);
                    
                    s32 CurrentR = R + RIncrement;
                    s32 CurrentC = C + CIncrement;
                    while (CurrentR != KR)
                    {
                        if (HasPiece(Blacks, Whites, CurrentR, CurrentC))
                        {
                            Result = false;
                            break;
                        }
                        CurrentR += RIncrement;
                        CurrentC += CIncrement;
                    }
                }
                
                if (!Result)
                {
                    if (dR == 0)
                    {
                        s32 MinC = Minimum(C, KC);
                        s32 MaxC = Maximum(C, KC);
                        Result = true;
                        for (s32 CIndex = MinC+1; CIndex < MaxC; ++CIndex)
                        {
                            if (HasPiece(Blacks, Whites, R, CIndex))
                            {
                                Result = false;
                                break;
                            }
                        }
                    }
                    else if (dC == 0)
                    {
                        s32 MinR = Minimum(R, KR);
                        s32 MaxR = Maximum(R, KR);
                        Result = true;
                        for (s32 RIndex = MinR+1; RIndex < MaxR; ++RIndex)
                        {
                            if (HasPiece(Blacks, Whites, RIndex, C))
                            {
                                Result = false;
                                break;
                            }
                        }
                    }
                }
            } break;
            
            case ChessPieceType_King:
            {
                Result = (AbsoluteValue(dR) <= 1 && AbsoluteValue(dC) <= 1);
            } break;
            case ChessPieceType_Empty: break;
            InvalidDefaultCase;
        }
    }
    return Result;
}

internal b32
WhiteIsCheck(chess_piece *Blacks, chess_piece *Whites)
{
    b32 Result = IsCheck_(Blacks, Whites, true);
    return Result;
}

internal b32
BlackIsCheck(chess_piece *Blacks, chess_piece *Whites)
{
    b32 Result = IsCheck_(Blacks, Whites, false);
    return Result;
}

internal void
PushDest(chess_game_state *Game, chess_piece *Piece, u32 R, u32 C, b32 IsCapture)
{
    Assert(R <= 7  &&  C <= 7);
    Assert(Game->DestinationsCount < ArrayCount(Game->Destinations));
    destination *Destination = Game->Destinations + Game->DestinationsCount;
    ++Game->DestinationsCount;
    
    Destination->DestCode = R | (C << 3) | (IsCapture << 6);
    if (Piece->DestinationsCount == 0)
        Piece->Destinations = Destination;
    ++Piece->DestinationsCount;
}

internal b32
PushDestIfCapturableBlackAndNoCheck(chess_game_state *Game, chess_piece *MovingPiece, s32 R, s32 C)
{
    b32 Result = false;
    Assert(0 <= R && R <= 7  &&  0 <= C && C <= 7);
    chess_piece *CapturableBlack = GetBlack(Game->Blacks, R, C);
    if (CapturableBlack)
    {
        //Assert(CapturableBlack->Type != ChessPieceType_King);
        MovingPiece->Row = R;
        MovingPiece->Column = C;
        chess_piece_type SaveType = CapturableBlack->Type;
        CapturableBlack->Type = ChessPieceType_Empty;
        if (SaveType == ChessPieceType_King || !WhiteIsCheck(Game->Blacks, Game->Whites))
        {
            PushDest(Game, MovingPiece, R, C, true);
            Result = true;
        }
        CapturableBlack->Type = SaveType;
    }
    return Result;
}

internal b32
PushDestIfCapturableWhiteAndNoCheck(chess_game_state *Game, chess_piece *MovingPiece, s32 R, s32 C)
{
    b32 Result = false;
    Assert(0 <= R && R <= 7  &&  0 <= C && C <= 7);
    chess_piece *CapturableWhite = GetWhite(Game->Whites, R, C);
    if (CapturableWhite)
    {
        //Assert(CapturableWhite->Type != ChessPieceType_King);
        MovingPiece->Row = R;
        MovingPiece->Column = C;
        chess_piece_type SaveType = CapturableWhite->Type;
        CapturableWhite->Type = ChessPieceType_Empty;
        if (SaveType == ChessPieceType_King || !BlackIsCheck(Game->Blacks, Game->Whites))
        {
            PushDest(Game, MovingPiece, R, C, true);
            Result = true;
        }
        CapturableWhite->Type = SaveType;
    }
    return Result;
}

internal void
PushDestIfFreeOrCapturableAndNoCheck(chess_game_state *Game, chess_piece *MovingPiece, s32 R, s32 C,
                                     b32 MoverIsWhite)
{
    Assert(0 <= R && R <= 7  &&  0 <= C && C <= 7);
    
    get_piece_result PieceResult = GetPiece(Game->Blacks, Game->Whites, R, C);
    if (PieceResult.Piece)
    {
        if (PieceResult.IsWhite != MoverIsWhite)
        {
            MovingPiece->Row = R;
            MovingPiece->Column = C;
            chess_piece_type SaveType = PieceResult.Piece->Type;
            PieceResult.Piece->Type = ChessPieceType_Empty;
            if (SaveType == ChessPieceType_King || 
                !IsCheck_(Game->Blacks, Game->Whites, MoverIsWhite))
                PushDest(Game, MovingPiece, R, C, true);
            PieceResult.Piece->Type = SaveType;
        }
    }
    else
    {
        MovingPiece->Row = R;
        MovingPiece->Column = C;
        if (!IsCheck_(Game->Blacks, Game->Whites, MoverIsWhite))
            PushDest(Game, MovingPiece, R, C, false);
    }
}

#define AssertInBounds(Row, Column) \
Assert(0 <= (s32)Row && Row <= 7  &&  0 <= (s32)Column && Column <= 7)



internal void
PushDestinationsForRook(chess_game_state *Game, chess_piece *Piece, s32 R, s32 C, b32 MoverIsWhite)
{
    // NOTE(vincent): determine the unoccupied squares within the rook's range
    chess_piece *Blacks = Game->Blacks;
    chess_piece *Whites = Game->Whites;
    
    s32 MinR = R;
    s32 MaxR = R;
    s32 MinC = C;
    s32 MaxC = C;
    while (MinR > 0 && !HasPiece(Blacks, Whites, MinR-1, C))
        --MinR;
    while (MaxR < 7 && !HasPiece(Blacks, Whites, MaxR+1, C))
        ++MaxR;
    while (MinC > 0 && !HasPiece(Blacks, Whites, R, MinC-1))
        --MinC;
    while (MaxC < 7 && !HasPiece(Blacks, Whites, R, MaxC+1))
        ++MaxC;
    
    // NOTE(vincent): test for possible moves within that range
    for (s32 CurrentR = MinR; CurrentR < R; ++CurrentR)
    {
        Piece->Row = CurrentR;
        AssertInBounds(Piece->Row, Piece->Column);
        if (!IsCheck_(Blacks, Whites, MoverIsWhite))
            PushDest(Game, Piece, CurrentR, C, false);
    }
    for (s32 CurrentR = R+1; CurrentR <= MaxR; ++CurrentR)
    {
        Piece->Row = CurrentR;
        AssertInBounds(Piece->Row, Piece->Column);
        if (!IsCheck_(Blacks, Whites, MoverIsWhite))
            PushDest(Game, Piece, CurrentR, C, false);
    }
    
    Piece->Row = R;
    for (s32 CurrentC = MinC; CurrentC < C; ++CurrentC)
    {
        Piece->Column = CurrentC;
        AssertInBounds(Piece->Row, Piece->Column);
        if (!IsCheck_(Blacks, Whites, MoverIsWhite))
            PushDest(Game, Piece, R, CurrentC, false);
    }
    for (s32 CurrentC = C+1; CurrentC <= MaxC; ++CurrentC)
    {
        Piece->Column = CurrentC;
        AssertInBounds(Piece->Row, Piece->Column);
        if (!IsCheck_(Blacks, Whites, MoverIsWhite))
            PushDest(Game, Piece, R, CurrentC, false);
    }
    
    // NOTE(vincent): test for possible captures beyond that range
    if (MoverIsWhite)
    {
        if (MinR > 0)
            PushDestIfCapturableBlackAndNoCheck(Game, Piece, MinR-1, C);
        if (MaxR < 7)
            PushDestIfCapturableBlackAndNoCheck(Game, Piece, MaxR+1, C);
        if (MinC > 0)
            PushDestIfCapturableBlackAndNoCheck(Game, Piece, R, MinC-1);
        if (MaxC < 7)
            PushDestIfCapturableBlackAndNoCheck(Game, Piece, R, MaxC+1);
    }
    else
    {
        if (MinR > 0)
            PushDestIfCapturableWhiteAndNoCheck(Game, Piece, MinR-1, C);
        if (MaxR < 7)
            PushDestIfCapturableWhiteAndNoCheck(Game, Piece, MaxR+1, C);
        if (MinC > 0)
            PushDestIfCapturableWhiteAndNoCheck(Game, Piece, R, MinC-1);
        if (MaxC < 7)
            PushDestIfCapturableWhiteAndNoCheck(Game, Piece, R, MaxC+1);
    }
}

internal void
PushDestinationsForBishop(chess_game_state *Game, chess_piece *Piece, s32 R, s32 C, b32 MoverIsWhite)
{
    Assert(0 <= R && R <= 7  &&  0 <= C && C <= 7);
    // NOTE(vincent): Determine the unoccupied squares within the bishop's range.
    s32 CmR = C-R;   // constant on ascending diagonal
    s32 CpR = C+R;   // constant on descending diagonal
    s32 AscendingMinR = R;    // minimum R for ascending diagonal
    s32 AscendingMaxR = R;
    s32 DescendingMinR = R;
    s32 DescendingMaxR = R;
    chess_piece *Blacks = Game->Blacks;
    chess_piece *Whites = Game->Whites;
    while (AscendingMinR > 0 && AscendingMinR + CmR > 0 &&
           !HasPiece(Blacks, Whites, AscendingMinR-1, CmR+(AscendingMinR-1)))
        --AscendingMinR;
    while (DescendingMinR > 0 && CpR - DescendingMinR < 7 &&
           !HasPiece(Blacks, Whites, DescendingMinR-1, CpR-(DescendingMinR-1)))
        --DescendingMinR;
    while (AscendingMaxR < 7 && AscendingMaxR + CmR < 7 &&
           !HasPiece(Blacks, Whites, AscendingMaxR+1, CmR+(AscendingMaxR+1)))
        ++AscendingMaxR;
    while (DescendingMaxR < 7 && CpR - DescendingMaxR > 0 &&
           !HasPiece(Blacks, Whites, DescendingMaxR+1, CpR-(DescendingMaxR+1)))
        ++DescendingMaxR;
    
    // NOTE(vincent): test for possible moves within that range.
    
    for (s32 CurrentR = AscendingMinR; CurrentR < R; ++CurrentR)
    {
        Piece->Row = CurrentR;
        Piece->Column = CurrentR + CmR;
        AssertInBounds(Piece->Row, Piece->Column);
        if (!IsCheck_(Blacks, Whites, MoverIsWhite))
            PushDest(Game, Piece, CurrentR, Piece->Column, false);
    }
    for (s32 CurrentR = R+1; CurrentR <= AscendingMaxR; ++CurrentR)
    {
        Piece->Row = CurrentR;
        Piece->Column = CurrentR + CmR;
        AssertInBounds(Piece->Row, Piece->Column);
        if (!IsCheck_(Blacks, Whites, MoverIsWhite))
            PushDest(Game, Piece, CurrentR, Piece->Column, false);
    }
    for (s32 CurrentR = DescendingMinR; CurrentR < R; ++CurrentR)
    {
        Piece->Row = CurrentR;
        Piece->Column = CpR - CurrentR;
        AssertInBounds(Piece->Row, Piece->Column);
        if (!IsCheck_(Blacks, Whites, MoverIsWhite))
            PushDest(Game, Piece, CurrentR, Piece->Column, false);
    }
    for (s32 CurrentR = R+1; CurrentR <= DescendingMaxR; ++CurrentR)
    {
        Piece->Row = CurrentR;
        Piece->Column = CpR - CurrentR;
        AssertInBounds(Piece->Row, Piece->Column);
        if (!IsCheck_(Blacks, Whites, MoverIsWhite))
            PushDest(Game, Piece, CurrentR, Piece->Column, false);
    }
    
    // NOTE(vincent): test for possible captures beyond that range
    if (MoverIsWhite)
    {
        if (AscendingMinR > 0 && CmR + AscendingMinR > 0)
            PushDestIfCapturableBlackAndNoCheck(Game, Piece, AscendingMinR - 1, CmR + (AscendingMinR - 1));
        if (AscendingMaxR < 7 && CmR + AscendingMaxR < 7)
            PushDestIfCapturableBlackAndNoCheck(Game, Piece, AscendingMaxR + 1, CmR + (AscendingMaxR + 1));
        if (DescendingMinR > 0 && CpR - DescendingMinR < 7)
            PushDestIfCapturableBlackAndNoCheck(Game, Piece, DescendingMinR-1, CpR - (DescendingMinR - 1));
        if (DescendingMaxR < 7 && CpR - DescendingMaxR > 0)
            PushDestIfCapturableBlackAndNoCheck(Game, Piece, DescendingMaxR+1, CpR - (DescendingMaxR + 1));
    }
    else
    {
        if (AscendingMinR > 0 && CmR + AscendingMinR > 0)
            PushDestIfCapturableWhiteAndNoCheck(Game, Piece, AscendingMinR-1, CmR + (AscendingMinR - 1));
        if (AscendingMaxR < 7 && CmR + AscendingMaxR < 7)
            PushDestIfCapturableWhiteAndNoCheck(Game, Piece, AscendingMaxR+1, CmR + (AscendingMaxR + 1));
        if (DescendingMinR > 0 && CpR - DescendingMinR < 7)
            PushDestIfCapturableWhiteAndNoCheck(Game, Piece, DescendingMinR-1, CpR - (DescendingMinR - 1));
        if (DescendingMaxR < 7 && CpR - DescendingMaxR > 0)
            PushDestIfCapturableWhiteAndNoCheck(Game, Piece, DescendingMaxR+1, CpR - (DescendingMaxR + 1));
    }
}

internal void
PushDiagPawnDestIfLegal(chess_game_state *Game, chess_piece *Piece, u32 CurrentR, u32 EnPassantR, 
                        u32 DestR, u32 DestC, b32 MoverIsWhite)
{
    Assert(DestR <= 7  &&  DestC <= 7);
    
    b32 RegularCapture;
    if (MoverIsWhite)
        RegularCapture = PushDestIfCapturableBlackAndNoCheck(Game, Piece, DestR, DestC);
    else
        RegularCapture = PushDestIfCapturableWhiteAndNoCheck(Game, Piece, DestR, DestC);
    
    if (!RegularCapture)
    {
        // NOTE(vincent): test en passant
        if (CurrentR == EnPassantR)
        {
            Assert(Game->History.EntryCount > 0);
            history_entry LastEntry = Game->History.Entries[Game->History.EntryCount-1];
            chess_piece *Blacks = Game->Blacks;
            chess_piece *Whites = Game->Whites;
            Assert(Game->History.EntryCount > 0);
            u32 PreviousMovingPieceIndex = LastEntry.Indices & 15;
            
            chess_piece *Pawn = MoverIsWhite ? 
                GetBlack(Blacks, CurrentR, DestC) : GetWhite(Whites, CurrentR, DestC);
            chess_piece *PieceOnDest = MoverIsWhite ? 
                GetWhite(Whites, DestR, DestC) : GetBlack(Blacks, DestR, DestC);
            if (Pawn && Pawn->Type == ChessPieceType_Pawn && Pawn->MoveCount == 1 &&
                PreviousMovingPieceIndex == Pawn->Index && 
                PieceOnDest == 0)
            {
                Assert(GetPiece(Blacks, Whites, DestR, DestC).Piece == 0);
                Piece->Row = DestR;
                Piece->Column = DestC;
                if (MoverIsWhite)
                {
                    if (!WhiteIsCheck(Blacks, Whites))
                        PushDest(Game, Piece, DestR, DestC, true);
                }
                else
                {
                    if (!BlackIsCheck(Blacks, Whites))
                        PushDest(Game, Piece, DestR, DestC, true);
                }
            }
        }
    }
}

internal void
PushDestinationsForPiece(chess_game_state *Game, chess_piece *Piece, b32 MoverIsWhite)
{
#if DEBUG
    chess_game_state Copy = *Game;
#endif
    
    u32 R = Piece->Row;
    u32 C = Piece->Column;
    chess_piece *Blacks = Game->Blacks;
    chess_piece *Whites = Game->Whites;
    
    switch (Piece->Type)
    {
        case ChessPieceType_Pawn:
        {
            Assert(1 <= R && R < 7);
            if (MoverIsWhite)
            {
                // (R+1, C) if (R+1, C) free and white won't be in check;
                // (R+2, C) if (R+1, C) and (R+2, C) free and white won't be in check and R == 1;
                // (R+1, C+1) if C < 7 and white won't be in check
                // and [(R+1, C+1)] has a black piece or En-Passant is possible];
                // (R+1, C-1) if C > 0 and white won't be in check
                // and [(R+1, C+1)] has a black piece or En-Passant is possible];
                
                b32 ForwardFree = !HasPiece(Blacks, Whites, R+1, C);
                if (ForwardFree)
                {
                    Piece->Row = R+1;
                    if (!WhiteIsCheck(Blacks, Whites))
                        PushDest(Game, Piece, R+1, C, false);
                    if (R == 1)
                    {
                        b32 ForwardForwardFree = !HasPiece(Blacks, Whites, R+2, C);
                        if (ForwardForwardFree)
                        {
                            Piece->Row = R+2;
                            if (!WhiteIsCheck(Blacks, Whites))
                                PushDest(Game, Piece, R+2, C, false);
                        }
                    }
                }
                if (C < 7)
                    PushDiagPawnDestIfLegal(Game, Piece, R, 4, R+1, C+1, true);
                if (C > 0)
                    PushDiagPawnDestIfLegal(Game, Piece, R, 4, R+1, C-1, true);
            }
            
            else
            {
                if (C == 5)
                {
                    C += 5;
                    C -= 5;
                }
                
                // same but for moving black pawn
                b32 ForwardFree = !HasPiece(Blacks, Whites, R-1, C);
                if (ForwardFree)
                {
                    Piece->Row = R-1;
                    if (!BlackIsCheck(Blacks, Whites))
                        PushDest(Game, Piece, R-1, C, false);
                    if (R == 6)
                    {
                        b32 ForwardForwardFree = !HasPiece(Blacks, Whites, R-2, C);
                        if (ForwardForwardFree)
                        {
                            Piece->Row = R-2;
                            if (!BlackIsCheck(Blacks, Whites))
                                PushDest(Game, Piece, R-2, C, false);
                        }
                    }
                }
                if (C < 7)
                    PushDiagPawnDestIfLegal(Game, Piece, R, 3, R-1, C+1, false);
                if (C > 0)
                    PushDiagPawnDestIfLegal(Game, Piece, R, 3, R-1, C-1, false);
            }
        } break;
        
        case ChessPieceType_Rook:
        {
            PushDestinationsForRook(Game, Piece, R, C, MoverIsWhite);
        } break;
        
        case ChessPieceType_Knight:
        {
            if (R >= 2 && C >= 1)
                PushDestIfFreeOrCapturableAndNoCheck(Game, Piece, R-2, C-1, MoverIsWhite);
            if (R >= 2 && C <= 6)
                PushDestIfFreeOrCapturableAndNoCheck(Game, Piece, R-2, C+1, MoverIsWhite);
            if (R <= 5 && C >= 1)
                PushDestIfFreeOrCapturableAndNoCheck(Game, Piece, R+2, C-1, MoverIsWhite);
            if (R <= 5 && C <= 6)
                PushDestIfFreeOrCapturableAndNoCheck(Game, Piece, R+2, C+1, MoverIsWhite);
            
            if (R >= 1 && C >= 2)
                PushDestIfFreeOrCapturableAndNoCheck(Game, Piece, R-1, C-2, MoverIsWhite);
            if (R >= 1 && C <= 5)
                PushDestIfFreeOrCapturableAndNoCheck(Game, Piece, R-1, C+2, MoverIsWhite);
            if (R <= 6 && C >= 2)
                PushDestIfFreeOrCapturableAndNoCheck(Game, Piece, R+1, C-2, MoverIsWhite);
            if (R <= 6 && C <= 5)
                PushDestIfFreeOrCapturableAndNoCheck(Game, Piece, R+1, C+2, MoverIsWhite);
        } break;
        
        case ChessPieceType_Bishop:
        {
            PushDestinationsForBishop(Game, Piece, R, C, MoverIsWhite);
        } break;
        
        case ChessPieceType_King:
        {
            if (R >= 1 && C >= 1)
                PushDestIfFreeOrCapturableAndNoCheck(Game, Piece, R-1, C-1, MoverIsWhite);
            if (R >= 1)
                PushDestIfFreeOrCapturableAndNoCheck(Game, Piece, R-1, C, MoverIsWhite);
            if (R >= 1 && C < 7)
                PushDestIfFreeOrCapturableAndNoCheck(Game, Piece, R-1, C+1, MoverIsWhite);
            if (C >= 1)
                PushDestIfFreeOrCapturableAndNoCheck(Game, Piece, R, C-1, MoverIsWhite);
            if (C < 7)
                PushDestIfFreeOrCapturableAndNoCheck(Game, Piece, R, C+1, MoverIsWhite);
            if (R < 7 && C >= 1)
                PushDestIfFreeOrCapturableAndNoCheck(Game, Piece, R+1, C-1, MoverIsWhite);
            if (R < 7)
                PushDestIfFreeOrCapturableAndNoCheck(Game, Piece, R+1, C, MoverIsWhite);
            if (R < 7 && C < 7)
                PushDestIfFreeOrCapturableAndNoCheck(Game, Piece, R+1, C+1, MoverIsWhite);
            
            Piece->Row = R;
            Piece->Column = C;
            
            // NOTE(vincent): Castling
            if (Piece->MoveCount == 0)
            {
                if (MoverIsWhite)
                {
                    if (!WhiteIsCheck(Blacks, Whites))
                    {
                        // kingside
                        Piece->Row = 0;
                        if (Whites[15].MoveCount == 0 && !HasPiece(Blacks, Whites, 0, 5) && !HasPiece(Blacks,Whites, 0, 6))
                        {
                            Piece->Column = 5;
                            if (!WhiteIsCheck(Blacks, Whites))
                            {
                                Piece->Column = 6;
                                if (!WhiteIsCheck(Blacks, Whites))
                                    PushDest(Game, Piece, 0, 6, false);
                            }
                            Piece->Column = C;
                        }
                        
                        // queenside
                        if (Whites[8].MoveCount == 0 && !HasPiece(Blacks, Whites, 0, 3) &&
                            !HasPiece(Blacks, Whites, 0, 2) && !HasPiece(Blacks, Whites, 0, 1))
                        {
                            Piece->Column = 3;
                            if (!WhiteIsCheck(Blacks, Whites))
                            {
                                Piece->Column = 2;
                                if (!WhiteIsCheck(Blacks, Whites))
                                {
                                    PushDest(Game, Piece, 0, 2, false);
                                }
                            }
                            Piece->Column = C;
                        }
                    }
                }
                
                else 
                {
                    if (!BlackIsCheck(Blacks, Whites))
                    {
                        // kingside
                        Piece->Row = 7;
                        if (Blacks[15].MoveCount == 0 && !HasPiece(Blacks, Whites, 7, 5) &&
                            !HasPiece(Blacks, Whites, 7, 6))
                        {
                            Piece->Column = 5;
                            if (!BlackIsCheck(Blacks, Whites))
                            {
                                Piece->Column = 6;
                                if (!BlackIsCheck(Blacks, Whites))
                                    PushDest(Game, Piece, 7, 6, false);
                            }
                            Piece->Column = C;
                        }
                        // queenside
                        if (Blacks[8].MoveCount == 0 && !HasPiece(Blacks, Whites, 7, 3) &&
                            !HasPiece(Blacks, Whites, 7, 2) && !HasPiece(Blacks, Whites, 7, 1))
                        {
                            Piece->Column = 3;
                            if (!BlackIsCheck(Blacks, Whites))
                            {
                                Piece->Column = 2;
                                if (!BlackIsCheck(Blacks, Whites))
                                {
                                    PushDest(Game, Piece, 7, 2, false);
                                }
                            }
                            Piece->Column = C;
                        }
                    }
                }
            }
        } break;
        
        case ChessPieceType_Queen:
        {
            PushDestinationsForRook(Game, Piece, R, C, MoverIsWhite);
            PushDestinationsForBishop(Game, Piece, R, C, MoverIsWhite);
        } break;
        
        InvalidDefaultCase;
    }
    
    Piece->Row = R;
    Piece->Column = C;
    
#if DEBUG
    for (u32 i = 0; i < 16; ++i)
    {
        Assert(Copy.Blacks[i].Row == Game->Blacks[i].Row);
        Assert(Copy.Whites[i].Row == Game->Whites[i].Row);
        Assert(Copy.Blacks[i].Column == Game->Blacks[i].Column);
        Assert(Copy.Whites[i].Column == Game->Whites[i].Column);
    }
#endif
}

#if DEBUG
internal void
AssertDestPointersWithinBounds(chess_game_state *Game)
{
    for (u32 i = 0; i < 16; ++i)
    {
        Assert((uintptr_t)Game->Blacks[i].Destinations <=  
               (uintptr_t)Game + sizeof(chess_game_state));
        Assert((uintptr_t)Game->Whites[i].Destinations <= 
               (uintptr_t)Game + sizeof(chess_game_state));
        Assert((uintptr_t)Game->Blacks[i].Destinations == 0 ||
               (uintptr_t)Game->Blacks[i].Destinations >= (uintptr_t)Game);
        Assert((uintptr_t)Game->Whites[i].Destinations == 0 ||
               (uintptr_t)Game->Whites[i].Destinations >= (uintptr_t)Game);
    }
}
#endif

internal void
RecomputeDestinations(chess_game_state *Game)
{
#if DEBUG
    chess_game_state Copy = *Game;
#endif
    
    Game->DestinationsCount = 0;
    Game->WhiteCanMove = false;
    Game->BlackCanMove = false;
    for (u32 Index = 0; Index < ArrayCount(Game->Blacks); ++Index)
    {
        chess_piece *Black = Game->Blacks + Index;
        Black->DestinationsCount = 0;
        Black->Destinations = 0;
        if (Black->Type != ChessPieceType_Empty)
            PushDestinationsForPiece(Game, Black, false);
#if DEBUG
        for (u32 i = 0; i < 16; ++i)
        {
            Assert(Copy.Blacks[i].Row == Game->Blacks[i].Row);
            Assert(Copy.Whites[i].Row == Game->Whites[i].Row);
            Assert(Copy.Blacks[i].Column == Game->Blacks[i].Column);
            Assert(Copy.Whites[i].Column == Game->Whites[i].Column);
        }
#endif
        
        if (Black->DestinationsCount > 0)
            Game->BlackCanMove = true;
        
        chess_piece *White = Game->Whites + Index;
        White->DestinationsCount = 0;
        White->Destinations = 0;
        if (White->Type != ChessPieceType_Empty)
            PushDestinationsForPiece(Game, White, true);
#if DEBUG
        for (u32 i = 0; i < 16; ++i)
        {
            Assert(Copy.Blacks[i].Row == Game->Blacks[i].Row);
            Assert(Copy.Whites[i].Row == Game->Whites[i].Row);
            Assert(Copy.Blacks[i].Column == Game->Blacks[i].Column);
            Assert(Copy.Whites[i].Column == Game->Whites[i].Column);
        }
#endif
        
        if (White->DestinationsCount > 0)
            Game->WhiteCanMove = true;
        
        Assert(!(White->DestinationsCount > 0) || White->Destinations);
        Assert(!(Black->DestinationsCount > 0) || Black->Destinations);
        
#if DEBUG
        AssertDestPointersWithinBounds(Game);
#endif
    }
}

internal void
UpdateWrappedCounterOnButtonPress(s32 *Counter, button_state DecrementButton,
                                  button_state IncrementButton, s32 Modulo)
{
    Assert(0 <= *Counter && *Counter < Modulo);
    if (DecrementButton.IsPressed && !DecrementButton.WasPressed)
        --(*Counter);
    if (IncrementButton.IsPressed && !IncrementButton.WasPressed)
        ++(*Counter);
    if (*Counter < 0)
        *Counter += Modulo;
    else if (*Counter >= Modulo)
        *Counter -= Modulo;
    Assert(0 <= *Counter && *Counter < Modulo);
}

internal void
JumpCursorToNextPiece(chess_game_state *Game, chess_piece *Pieces, chess_piece_type Type)
{
    cursor *Cursor = &Game->Cursor;
    get_piece_result PieceOnCursor = Game->PieceOnCursor;
    
    u32 DestIndex = 0;
    u32 FirstPieceIndex = 0;
    b32 FoundFirstPiece = false;
    
    while (FirstPieceIndex < 16)
    {
        if (Pieces[FirstPieceIndex].Type == Type)
        {
            FoundFirstPiece = true;
            DestIndex = FirstPieceIndex;
            break;
        }
        FirstPieceIndex++;
    }
    
    if (FoundFirstPiece)
    {
        if (PieceOnCursor.Piece && PieceOnCursor.IsWhite != Game->BlackIsPlaying)
        {
            u32 PieceOnCursorIndex = PieceOnCursor.Piece - Pieces;
            Assert(PieceOnCursorIndex < 16);
            
            u32 NextPieceIndex = PieceOnCursorIndex + 1;
            b32 FoundNextPiece = false;
            while (NextPieceIndex < 16)
            {
                if (Pieces[NextPieceIndex].Type == Type)
                {
                    FoundNextPiece = true;
                    break;
                }
                ++NextPieceIndex;
            }
            
            if (FoundNextPiece)
                DestIndex = NextPieceIndex;
        }
        Cursor->Row = Pieces[DestIndex].Row;
        Cursor->Column = Pieces[DestIndex].Column;
    }
}

internal v2
GetMouseVector(game_input *Input, render_group *Group)
{
    v2 Result = Hadamard(V2(Input->MouseX, Input->MouseY), Group->ScreenDim);
    return Result;
}

internal b32
InputRepeatClockAdvance(repeat_clock *Clock, b32 IsPressed, f32 dt)
{
    b32 Repeat = false;  // will also be set to true if new press
    f32 FirstRepeat = .3f;
    f32 RepeatInterval = .1f;
    if (!IsPressed)
    {
        Clock->Phase = 0;
        Clock->t = 0;
    }
    else
    {
        Clock->t += dt;
        switch (Clock->Phase)
        {
            case 0: 
            if (Clock->t >= FirstRepeat) 
            {
                Clock->t -= FirstRepeat; 
                Clock->Phase = 1; 
                Repeat = true;
            } 
            else if (Clock->t == dt)
                Repeat = true;
            break;
            case 1: 
            if (Clock->t >= RepeatInterval) 
            {
                Clock->t -= RepeatInterval; 
                Repeat = true;
            } break;
            InvalidDefaultCase;
        }
    }
    return Repeat;
}

internal void
UpdateCursor(game_input *Input, chess_game_state *Game, render_group *RenderGroup,
             repeat_clocks *RepeatClocks)
{
    // NOTE(vincent): Look at user input, set Cursor->Column, Cursor->Row and Cursor->P.
    cursor *Cursor = &Game->Cursor;
    v2 StickVector = V2(Input->ThumbLX, Input->ThumbLY);
    
    if (StickVector.x != 0.0f || StickVector.y != 0.0f)
    {
        f32 Speed = 2.5f;
        v2 DeltaP = Speed * Input->dtForFrame * StickVector;
        v2 NewP = Cursor->P.Current + DeltaP;
        
        NewP = Clamp11Vector(NewP);
        
        Cursor->P.Current = NewP;
        Cursor->P.Clock.Elapsed = 0.0f;
        Cursor->P.Clock.Duration = 0.0f;
        Cursor->P.Start = NewP;
        Cursor->P.End = NewP;
        
        v2 CursorPIn07Space = 4*(NewP + V2(0.875f, 0.875f));
        Cursor->Row = Clamp((s32)(CursorPIn07Space.y + 0.5f), 0, 7);
        Cursor->Column = Clamp((s32)(CursorPIn07Space.x + 0.5f), 0, 7);
    }
    else
    {
        board *Board = &Game->Board;
        if (Input->MouseMoved)
        {
            v2 MouseVector = GetMouseVector(Input, RenderGroup);
            
            v2 BoardDim = Board->Dim.Current;
            v2 BoardP = Board->P.Current;
            
            v2 BoardMin = BoardP - 0.5f*BoardDim;
            v2 BoardMax = BoardP + 0.5f*BoardDim;
            
            v2 MouseBoardOffset = MouseVector - BoardMin;
            v2 MouseBoardMaxOffset = MouseVector - BoardMax;
            
            if (MouseBoardOffset.x >= 0.0f &&
                MouseBoardOffset.y >= 0.0f &&
                MouseBoardMaxOffset.x <= 0.0f &&
                MouseBoardMaxOffset.y <= 0.0f)
            {
                v2 SquareDim = 0.125f * BoardDim;
                v2 InvSquareDim = V2(1.0f / SquareDim.x, 1.0f / SquareDim.y);
                u32 MouseRow = (u32)(MouseBoardOffset.y * InvSquareDim.y);
                u32 MouseColumn = (u32)(MouseBoardOffset.x * InvSquareDim.x);
                Assert(MouseRow < 8);
                Assert(MouseColumn < 8);
                Cursor->Row = MouseRow;
                Cursor->Column = MouseColumn;
            }
        }
        
        b32 LeftRepeat = InputRepeatClockAdvance(&RepeatClocks->Left, 
                                                 Input->Left.IsPressed, Input->dtForFrame);
        b32 DownRepeat = InputRepeatClockAdvance(&RepeatClocks->Down, 
                                                 Input->Down.IsPressed, Input->dtForFrame);
        b32 UpRepeat = InputRepeatClockAdvance(&RepeatClocks->Up, 
                                               Input->Up.IsPressed, Input->dtForFrame);
        b32 RightRepeat = InputRepeatClockAdvance(&RepeatClocks->Right, 
                                                  Input->Right.IsPressed, Input->dtForFrame);
        
        s32 XAxisAdd = RightRepeat - LeftRepeat;
        s32 YAxisAdd = UpRepeat - DownRepeat;
        Cursor->Column = Clamp(Cursor->Column + XAxisAdd, 0, 7);
        Cursor->Row = Clamp(Cursor->Row + YAxisAdd, 0, 7);
        
        chess_piece *Pieces = Game->BlackIsPlaying ? Game->Blacks : Game->Whites;
        
        // NOTE(vincent): We almost always use a generic function for jumping the cursor 
        // to a certain piece type because pawn promotion and captures
        // make it unpredictable which type is at which index.
        if (Input->K.IsPressed && !Input->K.WasPressed)
        {
            Cursor->Row = Pieces[12].Row;
            Cursor->Column = Pieces[12].Column;
        }
        if (Input->T.IsPressed && !Input->T.WasPressed)
            JumpCursorToNextPiece(Game, Pieces, ChessPieceType_Queen);
        if (Input->N.IsPressed && !Input->N.WasPressed)
            JumpCursorToNextPiece(Game, Pieces, ChessPieceType_Knight);
        if (Input->KeyB.IsPressed && !Input->KeyB.WasPressed)
            JumpCursorToNextPiece(Game, Pieces, ChessPieceType_Bishop);
        if (Input->R.IsPressed && !Input->R.WasPressed)
            JumpCursorToNextPiece(Game, Pieces, ChessPieceType_Rook);
        if (Input->P.IsPressed && !Input->P.WasPressed)
            JumpCursorToNextPiece(Game, Pieces, ChessPieceType_Pawn);
        
        v2 NewEndP = GetBoardSpaceV2(Cursor->Row, Cursor->Column);
#define MOVE_CURSOR_DURATION .1f
        if (!VectorsEqual(Cursor->P.End, NewEndP))
            InitMovingV2FromCurrent(&Cursor->P, NewEndP, MOVE_CURSOR_DURATION);
    }
}


internal u8
SelectedPieceToCursorDestCodeIfLegal(chess_game_state *Game)
{
    Assert(Game->SelectedPiece.Piece);
    u8 Result = 0xff;
    u8 CursorDestCode = Game->Cursor.Row | (Game->Cursor.Column << 3);
    if (Game->BlackIsPlaying == !Game->SelectedPiece.IsWhite)
    {
        for (u32 DestIndex = 0; 
             DestIndex < Game->SelectedPiece.Piece->DestinationsCount;
             ++DestIndex)
        {
            u8 DestCode = Game->SelectedPiece.Piece->Destinations[DestIndex].DestCode;
            if ((DestCode & 63) == CursorDestCode)
            {
                Result = CursorDestCode;
                break;
            }
        }
    }
    return Result;
}

internal void
MovePieceAfterwork(chess_game_state *Game)
{
    RecomputeDestinations(Game);
    
    Game->RunningState = ChessGameRunningState_Normal;
    b32 OpponentIsCheck = IsCheck_(Game->Blacks, Game->Whites, 
                                   Game->BlackIsPlaying);
    b32 OpponentCanMove = (Game->BlackIsPlaying ? Game->WhiteCanMove : Game->BlackCanMove);
    
    if (OpponentIsCheck)
    {
        if (OpponentCanMove)
            Game->RunningState = ChessGameRunningState_Check;
        else
            Game->RunningState = ChessGameRunningState_Checkmate;
    }
    else if (!OpponentCanMove)
        Game->RunningState = ChessGameRunningState_Stalemate;
    else
    {
        b32 TwoKingsStalemate = true;
        for (u32 i = 0; i < ArrayCount(Game->Blacks); ++i)
        {
            if ((Game->Blacks[i].Type != ChessPieceType_Empty && 
                 Game->Blacks[i].Type != ChessPieceType_King    ) ||
                (Game->Whites[i].Type != ChessPieceType_Empty && 
                 Game->Whites[i].Type != ChessPieceType_King))
            {
                TwoKingsStalemate = false;
                break;
            }
        }
        if (TwoKingsStalemate)
            Game->RunningState = ChessGameRunningState_Stalemate;
    }
    
    if (Game->RunningState != ChessGameRunningState_Checkmate && 
        ArrayCount(Game->History.Entries) == Game->CurrentEntryIndex)
    {
        Game->RunningState = ChessGameRunningState_Stalemate;
    }
    
    if (!Game->GameIsOver &&
        (Game->RunningState == ChessGameRunningState_Stalemate ||
         Game->RunningState == ChessGameRunningState_Checkmate))
    {
        Game->GameIsOver = true;
        Game->CurrentEntryIndex = Game->History.EntryCount;
        // pushed first: white was playing, odd number
        Assert(!(Game->CurrentEntryIndex & 1) || !Game->BlackIsPlaying);
        Assert((Game->CurrentEntryIndex & 1) || Game->BlackIsPlaying);
    }
    
    Game->BlackIsPlaying = !Game->BlackIsPlaying;
    Game->SelectedPiece.Piece = 0;
}

internal void
SetCapturedPieceBits(history_entry *Entry, chess_piece *Piece)
{
    Assert(Piece->Index != 12);
    u32 IndexCode = Piece->Index;
    if (Piece->Index < 12)
        ++IndexCode;
    Entry->Indices |= IndexCode << 4;
    u32 Type;
    switch (Piece->Type)
    {
        case ChessPieceType_Pawn:   Type = 0; break;
        case ChessPieceType_Rook:   Type = 1; break;
        case ChessPieceType_Knight: Type = 2; break;
        case ChessPieceType_Bishop: Type = 3; break;
        case ChessPieceType_Queen:  Type = 4; break;
        InvalidDefaultCase;
    }
    Entry->Special |= Type << 5;
}

internal void
SetPromotionBits(history_entry *Entry, chess_piece_type Type)
{
    Entry->Special = Entry->Special & 0xf8; // zero out the low 3 bits
    Entry->Special |= (1 << 2);
    switch (Type)
    {
        case ChessPieceType_Rook: Entry->Special |= 0; break;
        case ChessPieceType_Knight: Entry->Special |= 1; break;
        case ChessPieceType_Bishop: Entry->Special |= 2; break;
        case ChessPieceType_Queen: Entry->Special |= 3; break;
        InvalidDefaultCase;
    }
}

internal void
SetPromotionBits(history_entry *Entry, u32 PromotionCode)
{
    Entry->Special = Entry->Special & 0xf8; // zero out the low 3 bits
    Assert(PromotionCode <= 3);
    Entry->Special |= (1 << 2) | PromotionCode;
}

#if DEBUG
internal void
TestEncodedEntry(history_entry Entry, chess_game_state *Game)
{
    decoded_history_entry Decoded;
    DecodeHistoryEntry(&Decoded, Entry);
    if ((Decoded.DeltaCol == 2 || Decoded.DeltaCol == -2) &&
        Decoded.MovingPieceIndex == 12)
    {
        if (Game->BlackIsPlaying)
        {
            Assert(Game->Blacks[12].MoveCount == 0 && Game->Blacks[12].Column == 5 &&
                   Game->Blacks[12].Row == 7);
            if (Decoded.DeltaCol == 2)
            {
                Assert(Game->Blacks[15].Row == 7 && Game->Blacks[15].Column == 7 &&
                       Game->Blacks[15].MoveCount == 0);
                
            }
            else
            {
                Assert(Game->Blacks[8].Row == 7 && Game->Blacks[8].Column == 0 &&
                       Game->Blacks[8].MoveCount == 0);
            }
        }
        else
        {
            Assert(Game->Whites[12].MoveCount == 0 && Game->Whites[12].Column == 5 &&
                   Game->Whites[12].Row == 0);
            if (Decoded.DeltaCol == 2)
            {
                Assert(Game->Whites[15].Row == 0 && Game->Whites[15].Column == 7 &&
                       Game->Whites[15].MoveCount == 0);
                
            }
            else
            {
                Assert(Game->Whites[8].Row == 0 && Game->Whites[8].Column == 0 &&
                       Game->Whites[8].MoveCount == 0);
            }
        }
    }
}
#endif

internal void
MovePieceToCursor(chess_game_state *Game, chess_piece *MovingPiece, u8 DestCode)
{
    // This function also pushes a new history entry. Some of the history entry information 
    // may have to be set later (pawn promotion type).
    Assert(!Game->GameIsOver);
    
    history_entry *Entry = Game->History.Entries + Game->History.EntryCount;
    ZeroBytes((u8 *)Entry, sizeof(history_entry));
    
    u8 DestRow = DestCode & 7;
    u8 DestCol = (DestCode >> 3) & 7;
    u8 AbsDeltaRow = AbsoluteDifference(DestRow, MovingPiece->Row);
    u8 AbsDeltaCol = AbsoluteDifference(DestCol, MovingPiece->Column);
    u8 DeltaRowCode = ((MovingPiece->Row <= DestRow ? 0 : 1) << 3) | AbsDeltaRow;
    u8 DeltaColCode = ((MovingPiece->Column <= DestCol ? 0 : 1) << 3) | AbsDeltaCol;
    Entry->Delta = DeltaRowCode | (DeltaColCode << 4);
    
#if DEBUG
    if ((AbsDeltaRow == 2 || AbsDeltaCol == 2) && MovingPiece->Type == ChessPieceType_King)
    {
        Assert(MovingPiece->Column == 4);
        Assert(MovingPiece->Row == (u32)(Game->BlackIsPlaying ? 7 : 0));
        Assert(MovingPiece->MoveCount == 0);
    }
    TestEncodedEntry(*Entry, Game);
#endif
    
    Entry->Indices = MovingPiece->Index;
    ++Game->History.EntryCount;
    ++Game->CurrentEntryIndex;
    
    chess_piece *Blacks = Game->Blacks;
    chess_piece *Whites = Game->Whites;
    
    // NOTE(vincent): Check if a piece was captured, and if so, set its type to empty.
    if (Game->PieceOnCursor.Piece)
    {
        Assert(Game->PieceOnCursor.IsWhite == Game->BlackIsPlaying);
        Assert(Game->PieceOnCursor.Piece->Type != ChessPieceType_Empty);
        Assert(Game->PieceOnCursor.Piece->Type != ChessPieceType_King);
        
        SetCapturedPieceBits(Entry, Game->PieceOnCursor.Piece);
        Game->PieceOnCursor.Piece->Type = ChessPieceType_Empty;
    }
    
    // NOTE(vincent): "en passant"
    else if (MovingPiece->Type == ChessPieceType_Pawn)
    {
        s32 R = MovingPiece->Row;
        s32 C = MovingPiece->Column;
        if (Game->BlackIsPlaying)
        {
            if (R == 3)
            {
                Assert(Game->History.EntryCount > 0);
                u32 PreviousMovingPieceIndex = Entry[-1].Indices & 15;
                if (C+1 == Game->Cursor.Column)
                {
                    chess_piece *Pawn = GetWhite(Whites, R, C+1);
                    if (Pawn && Pawn->Type == ChessPieceType_Pawn && Pawn->MoveCount == 1
                        && PreviousMovingPieceIndex == Pawn->Index)
                    {
                        Assert(GetPiece(Blacks, Whites, R-1, C+1).Piece == 0);
                        SetCapturedPieceBits(Entry, Pawn);
                        Pawn->Type = ChessPieceType_Empty;
                    }
                }
                else if (C-1 == Game->Cursor.Column)
                {
                    chess_piece *Pawn = GetWhite(Whites, R, C-1);
                    if (Pawn && Pawn->Type == ChessPieceType_Pawn && Pawn->MoveCount == 1
                        && PreviousMovingPieceIndex == Pawn->Index)
                    {
                        Assert(GetPiece(Blacks, Whites, R-1, C-1).Piece == 0);
                        SetCapturedPieceBits(Entry, Pawn);
                        Pawn->Type = ChessPieceType_Empty;
                    }
                }
            }
        }
        
        else
        {
            if (R == 4)
            {
                Assert(Game->History.EntryCount > 0);
                u32 PreviousMovingPieceIndex = Entry[-1].Indices & 15;
                if (C+1 == Game->Cursor.Column)
                {
                    chess_piece *Pawn = GetBlack(Blacks, R, C+1);
                    if (Pawn && Pawn->Type == ChessPieceType_Pawn && Pawn->MoveCount == 1
                        && PreviousMovingPieceIndex == Pawn->Index)
                    {
                        Assert(GetPiece(Blacks, Whites, R+1, C+1).Piece == 0);
                        SetCapturedPieceBits(Entry, Pawn);
                        Pawn->Type = ChessPieceType_Empty;
                    }
                }
                else if (C-1 == Game->Cursor.Column)
                {
                    chess_piece *Pawn = GetBlack(Blacks, R, C-1);
                    if (Pawn && Pawn->Type == ChessPieceType_Pawn && Pawn->MoveCount == 1
                        && PreviousMovingPieceIndex == Pawn->Index)
                    {
                        Assert(GetPiece(Blacks, Whites, R+1, C-1).Piece == 0);
                        SetCapturedPieceBits(Entry, Pawn);
                        Pawn->Type = ChessPieceType_Empty;
                    }
                }
            }
        }
    }
    
    // NOTE(vincent): if there is castling, move the rook as well.
    else if (MovingPiece->Type == ChessPieceType_King)
    {
        s32 C = MovingPiece->Column;
        s32 dC = Game->Cursor.Column - C;
        
        chess_piece *Rook = 0;
        s32 RookColumn;
        if (dC == 2)
        {
            Rook = Game->BlackIsPlaying ? Game->Blacks + 15 : Game->Whites + 15;
            RookColumn = 5;
        }
        else if (dC == -2)
        {
            Rook = Game->BlackIsPlaying ? Game->Blacks + 8 : Game->Whites + 8; 
            RookColumn = 3;
        }
        
        if (Rook)
        {
            Rook->Column = RookColumn;
            Rook->MoveCount++;
            InitMovingV2FromCurrent(&Rook->P, GetBoardSpaceV2(Rook->Row, RookColumn), 
                                    MOVE_PIECE_DURATION);
        }
    }
    
    // NOTE(vincent): actually move the piece
    MovingPiece->Row = Game->Cursor.Row;
    MovingPiece->Column = Game->Cursor.Column;
    MovingPiece->MoveCount++;
    Assert(0 < MovingPiece->MoveCount && MovingPiece->MoveCount < 1000);
    InitMovingV2FromCurrent(&MovingPiece->P, GetBoardSpaceV2(MovingPiece->Row, MovingPiece->Column), 
                            MOVE_PIECE_DURATION);
    
    // NOTE(vincent): detect pawn promotion
    if (MovingPiece->Type == ChessPieceType_Pawn && 
        (MovingPiece->Row == 0 || MovingPiece->Row == 7))
        Game->PromotingPawn = true;
    else
        MovePieceAfterwork(Game);
    
}

internal b32
MoveSelectedPieceToCursorIfLegal(chess_game_state *Game)
{
    b32 MoveIsLegal = false;
    u8 DestCode = SelectedPieceToCursorDestCodeIfLegal(Game);
    
    if (DestCode != 0xff)
    {
        MoveIsLegal = true;
        MovePieceToCursor(Game, Game->SelectedPiece.Piece, DestCode);
    }
    return MoveIsLegal;
}

internal decision
GetRandomDecision(chess_game_state *Game, random_series *Series)
{
    Assert(Game->DestinationsCount > 0);
    chess_piece *Pieces = Game->BlackIsPlaying ? Game->Blacks : Game->Whites;
    u32 TotalDestCount = 0;
    for (u32 i = 0; i < 16; ++i)
    {
        TotalDestCount += Pieces[i].DestinationsCount;
    }
    Assert(TotalDestCount > 0);
    
    u32 RandomDestIndex = RandomU32(Series, 0, TotalDestCount-1);
    destination RandomDestination = {0xff};
    chess_piece *RandomPiece = 0;
    
    TotalDestCount = 0;
    for (u32 i = 0; i < 16; ++i)
    {
        u32 NextTotal = TotalDestCount + Pieces[i].DestinationsCount;
        if (NextTotal > RandomDestIndex)
        {
            RandomPiece = Pieces + i;
            u32 RandomDestIndexOfPiece = RandomDestIndex - TotalDestCount;
            RandomDestination = Pieces[i].Destinations[RandomDestIndexOfPiece];
            break;
        }
        TotalDestCount = NextTotal;
    }
    
    Assert(RandomPiece && RandomDestination.DestCode != 0xff);
    u32 DestRow = RandomDestination.DestCode & 7 ;
    chess_piece_type PromotionType = ChessPieceType_Empty;
    if (RandomPiece->Type == ChessPieceType_Pawn && 
        (DestRow == 7 || DestRow == 0))
    {
        u32 RandomTypeIndex = RandomU32(Series, 0, 3);
        switch (RandomTypeIndex)
        {
            case 0: PromotionType = ChessPieceType_Rook; break;
            case 1: PromotionType = ChessPieceType_Knight; break;
            case 2: PromotionType = ChessPieceType_Bishop; break;
            case 3: PromotionType = ChessPieceType_Queen; break;
            InvalidDefaultCase;
        }
    }
    
    decision Result = {RandomPiece, RandomDestination, PromotionType};
    return Result;
}

internal f32
HeuristicEvaluation(chess_game_state *Game, random_series *Series)
{
    s32 Result = 0;
    
    Assert(Game->RunningState != ChessGameRunningState_Checkmate &&
           Game->RunningState != ChessGameRunningState_Stalemate);
    for (u32 i = 0; i < 16; ++i)
    {
        switch (Game->Blacks[i].Type)
        {
            case ChessPieceType_Pawn:   Result -= 10; break;
            case ChessPieceType_Knight: Result -= 30; break;
            case ChessPieceType_Bishop: Result -= 30; break;
            case ChessPieceType_Rook:   Result -= 50; break;
            case ChessPieceType_Queen:  Result -= 90; break;
        }
        switch (Game->Whites[i].Type)
        {
            case ChessPieceType_Pawn:   Result += 10; break;
            case ChessPieceType_Knight: Result += 30; break;
            case ChessPieceType_Bishop: Result += 30; break;
            case ChessPieceType_Rook:   Result += 50; break;
            case ChessPieceType_Queen:  Result += 90; break;
        }
    }
    
    Result += RandomS32(Series, -1, 1);
    //Result *= (1.0f - 0.001f * Game->History.EntryCount);
    
    return Result;
}




internal void
GameHistoryMoveForward(chess_game_state *Game)
{
    if (Game->CurrentEntryIndex < Game->History.EntryCount)
    {
        // "play" whatever the current entry index is
        Assert((Game->CurrentEntryIndex & 1) || !Game->BlackIsPlaying);
        Assert(!(Game->CurrentEntryIndex & 1) || Game->BlackIsPlaying);
        
        history_entry Entry = Game->History.Entries[Game->CurrentEntryIndex];
        Game->CurrentEntryIndex++;
        
        decoded_history_entry Decoded;
        DecodeHistoryEntry(&Decoded, Entry);
        
        chess_piece *PlayerPieces = (Game->BlackIsPlaying ? Game->Blacks : Game->Whites);
        chess_piece *OpponentPieces = (Game->BlackIsPlaying ? Game->Whites : Game->Blacks);
        
        Assert(!(Game->CurrentEntryIndex & 1) || PlayerPieces == Game->Whites);
        Assert((Game->CurrentEntryIndex & 1) || PlayerPieces == Game->Blacks);
        
        // select the piece
        chess_piece *MovingPiece = PlayerPieces + Decoded.MovingPieceIndex;
        Assert(MovingPiece->Type != ChessPieceType_Empty);
        
        // "delete" the captured piece if there is one
        if (Decoded.ThereIsCapture)
        {
            OpponentPieces[Decoded.CapturedPieceIndex].Type = ChessPieceType_Empty;
        }
        
        // if there is castling, move the rook as well
        if (MovingPiece->Type == ChessPieceType_King)
        {
            chess_piece *Rook = 0;
            s32 RookColumn;
            if (Decoded.DeltaCol == 2)
            {
                Rook = Game->BlackIsPlaying ? Game->Blacks + 15 : Game->Whites + 15;
                RookColumn = 5;
            }
            else if (Decoded.DeltaCol == -2)
            {
                Rook = Game->BlackIsPlaying ? Game->Blacks + 8 : Game->Whites + 8; 
                RookColumn = 3;
            }
            
            if (Rook)
            {
                Rook->Column = RookColumn;
                Rook->MoveCount++;
                InitMovingV2FromCurrent(&Rook->P, GetBoardSpaceV2(Rook->Row, RookColumn), 
                                        MOVE_PIECE_DURATION);
            }
        }
        
        // moving piece row, column and vector stuff
        MovingPiece->Row += Decoded.DeltaRow;
        MovingPiece->Column += Decoded.DeltaCol;
        MovingPiece->MoveCount++;
        InitMovingV2FromCurrent(&MovingPiece->P, 
                                GetBoardSpaceV2(MovingPiece->Row, MovingPiece->Column),
                                MOVE_PIECE_DURATION);
        
        // handle pawn promotion "ourselves"
        if (Decoded.ThereIsPromotion)
        {
            MovingPiece->Type = Decoded.PromotionType;
        }
        MovePieceAfterwork(Game);
    }
}

internal void
GameHistoryMoveBack(chess_game_state *Game)
{
    if (Game->CurrentEntryIndex > 0)
    {
        Assert((Game->CurrentEntryIndex & 1) || !Game->BlackIsPlaying);
        Assert(!(Game->CurrentEntryIndex & 1) || Game->BlackIsPlaying);
        
        Game->CurrentEntryIndex--;
        history_entry Entry = Game->History.Entries[Game->CurrentEntryIndex];
        
        decoded_history_entry Decoded;
        DecodeHistoryEntry(&Decoded, Entry);
        
        chess_piece *PlayerPieces = (!Game->BlackIsPlaying ? Game->Blacks : Game->Whites);
        chess_piece *OpponentPieces = (!Game->BlackIsPlaying ? Game->Whites : Game->Blacks);
        
        Assert((Game->CurrentEntryIndex & 1) || PlayerPieces == Game->Whites);
        Assert(!(Game->CurrentEntryIndex & 1) || PlayerPieces == Game->Blacks);
        
        // select the piece
        chess_piece *MovingPiece = PlayerPieces + Decoded.MovingPieceIndex;
        Assert(MovingPiece->MoveCount > 0);
        Assert(MovingPiece->Type != ChessPieceType_Empty);
        
        // restore captured piece if there is one
        if (Decoded.ThereIsCapture)
        {
            OpponentPieces[Decoded.CapturedPieceIndex].Type = Decoded.CapturedType;
        }
        
        // if there is castling, move the rook as well
        if (MovingPiece->Type == ChessPieceType_King)
        {
            s32 RookColumn = 0xffff;
            if (Decoded.DeltaCol == 2)
            {
                RookColumn = 7;
            }
            else if (Decoded.DeltaCol == -2)
            {
                RookColumn = 0;
            }
            
            if (RookColumn != 0xffff)
            {
                chess_piece *Rook = PlayerPieces + 15;
                Rook->Column = RookColumn;
                Assert(Rook->MoveCount > 0);
                Rook->MoveCount--;
                Assert(Rook->MoveCount == 0);
                Assert(MovingPiece->MoveCount == 1);
                InitMovingV2FromCurrent(&Rook->P, GetBoardSpaceV2(Rook->Row, RookColumn), 
                                        MOVE_PIECE_DURATION);
            }
        }
        
        // cancel pawn promotion
        if (Decoded.ThereIsPromotion)
        {
            MovingPiece->Type = ChessPieceType_Pawn;
        }
        
        // moving piece row, column and vector animation setup
        MovingPiece->Row -= Decoded.DeltaRow;
        MovingPiece->Column -= Decoded.DeltaCol;
        MovingPiece->MoveCount--;
        Assert(MovingPiece->MoveCount < 1000);
        InitMovingV2FromCurrent(&MovingPiece->P, 
                                GetBoardSpaceV2(MovingPiece->Row, MovingPiece->Column),
                                MOVE_PIECE_DURATION);
        MovePieceAfterwork(Game);
        
    }
}


internal decision
GetDecisionAndApply(chess_game_state *Game, chess_piece *Pieces, u32 PieceIndex, u32 DestIndex)
{
    Assert(Pieces[PieceIndex].Destinations && Pieces[PieceIndex].DestinationsCount);
    // NOTE(vincent): Get decision data 
    decision Decision = {};
    Decision.Piece = Pieces + PieceIndex;
    u32 DestCode = Pieces[PieceIndex].Destinations[DestIndex].DestCode;
    Decision.Destination.DestCode = DestCode;
    s32 DestRow = DestCode & 7;
    s32 DestCol = (DestCode >> 3) & 7;
    if (Pieces[PieceIndex].Type == ChessPieceType_Pawn && (DestRow == 7 || DestRow == 0))
    {
        Decision.PromotionType = ChessPieceType_Queen;
    }
    
    // NOTE(vincent): Apply move
    Game->Cursor.Row = DestRow;
    Game->Cursor.Column = DestCol;
    Game->PieceOnCursor = GetPiece(Game->Blacks, Game->Whites,
                                   Game->Cursor.Row, Game->Cursor.Column);
    MovePieceToCursor(Game, Pieces + PieceIndex, DestCode);
    if (Decision.PromotionType != ChessPieceType_Empty)
    {
        Decision.Piece->Type = Decision.PromotionType;
        history_entry *Entry = Game->History.Entries + Game->History.EntryCount-1;
        SetPromotionBits(Entry, Decision.PromotionType);
        MovePieceAfterwork(Game);
    }
    
    return Decision;
}

struct minimax_stage
{
    f32 Alpha;
    f32 Beta;
    chess_game_state GameCopy;
    u32 DecisionIndex;
    u32 DecisionsCount;
    decision *Decisions;
};

struct minimax_context
{
    minimax_stage *Stages;
    u32 CurrentDepth;
};


internal void
CopyGame(chess_game_state *Source, chess_game_state *Dest)
{
    Dest->RunningState = Source->RunningState;
    Dest->PromotingPawn = Source->PromotingPawn;
    
    for (u32 i = 0; i < 16; ++i)
    {
        Dest->Blacks[i] = Source->Blacks[i];
        Dest->Whites[i] = Source->Whites[i];
    }
    
    Dest->Cursor.Row = Source->Cursor.Row;
    Dest->Cursor.Column = Source->Cursor.Column;
    Dest->SelectedPiece = Source->SelectedPiece;
    Dest->PieceOnCursor = Source->PieceOnCursor;
    Dest->BlackCanMove = Source->WhiteCanMove;
    Dest->BlackIsPlaying = Source->BlackIsPlaying;
    Dest->History = Source->History;
    Dest->GameIsOver = Source->GameIsOver;
    Dest->CurrentEntryIndex = Source->CurrentEntryIndex;
    Dest->DestinationsCount = Source->DestinationsCount;
    for (u32 i = 0; i < ArrayCount(Source->Destinations); ++i)
    {
        Dest->Destinations[i] = Source->Destinations[i];
    }
}


PLATFORM_WORK_QUEUE_CALLBACK(GetGoodDecision)
{
    // NOTE(vincent): Minimax algorithm implementation with alpha-beta pruning.
    // Some known issues:
    // - chess_game_state and the functions that work with it are not really tailored 
    // for the performance of this function. Some of the data is irrelevant 
    // (e.g. vectors and animation data),
    // and some of the reused code for game simulation involves unnecessary work.
    // - This is single-threaded scalar code, 
    // yet it's the most performance critical part of the program.
    // - Behavioral problems: Although a higher MaxDepth will always win against a lower one,
    // sometimes a higher MaxDepth leads to more strange "conservative" plays, 
    // when you could in fact win in one move or a few moves. 
    // This means the code is busted in some way, as I don't think that's supposed to happen.
    // Additionally, AI vs AI games can often get stuck in a loop, or have little variety
    // in them (we avoid exploring nodes that we know are going to have equal values or worse,
    // but it becomes impossible to properly choose a random decision 
    // among several ones that are in a tie).
    // The RandomS32() call in HeuristicEvaluation() makes the AI behave a little differently
    // sometimes, and that comes at a noticeable speed cost, 
    // but the behavior is still not great; there is a strong bias for the AI to move pieces 
    // on the left side of the board at the beginning of the game because of pruning order.
    
    get_good_decision_params *Params = (get_good_decision_params *)Data;
    chess_game_state *Game_ = Params->Game;
    memory_arena *Arena = Params->Arena;
    CheckArena(Arena);
    random_series *Series = Params->Series;
    u32 MaxDepth = Params->MaxDepth;
    good_decision_result *Result = &Params->Result;
    
    Assert(MaxDepth > 0);
    
    chess_game_state Game = *Game_;
    for (u32 i = 0; i < 16; ++i)
    {
        if (Game.Blacks[i].Destinations)
        {
            Game.Blacks[i].Destinations = 
                (destination *)((u8 *)&Game + 
                                ((u8 *)Game_->Blacks[i].Destinations - (u8 *)Game_));
        }
        if (Game.Whites[i].Destinations)
        {
            Game.Whites[i].Destinations = 
                (destination *)((u8 *)&Game + 
                                ((u8 *)Game_->Whites[i].Destinations - (u8 *)Game_));
        }
    }
    Game.SelectedPiece.Piece =
        (chess_piece *)((u8 *)&Game + ((u8 *)Game_->SelectedPiece.Piece - (u8 *)Game_));
    Game.PieceOnCursor.Piece = 
        (chess_piece *)((u8 *)&Game + ((u8 *)Game_->PieceOnCursor.Piece - (u8 *)Game_));
    
    
    
    
#if DEBUG
    b32 RootPlayerIsBlack = Game.BlackIsPlaying;
#endif
    Assert(Game.DestinationsCount > 0);
    
    temporary_memory StagesMemory = BeginTemporaryMemory(Arena);
    
    decision LastTopDecision;
    
    minimax_context Context;
    Context.CurrentDepth = 0;
    Context.Stages = PushArray(Arena, MaxDepth, minimax_stage);
    
    ZeroBytes((u8 *)Context.Stages, sizeof(minimax_stage) * MaxDepth);
    
    for (u32 DepthIndex = 0; DepthIndex < MaxDepth; ++DepthIndex)
    {
        Context.Stages[DepthIndex].Decisions = PushArray(Arena, 500, decision);
        Context.Stages[DepthIndex].DecisionsCount = 0;
    }
    
    Context.Stages[0].Alpha = -10000;
    Context.Stages[0].Beta = 10000;
    CopyGame(&Game, &Context.Stages[0].GameCopy);
    
    Result->Value = Game.BlackIsPlaying ? 10000.0f : -10000.0f;
    
    Goto_StageExploration:
    
    if (!Params->ShouldContinue)
        goto Goto_EndExploration;
    
    {
        Assert(Context.CurrentDepth < MaxDepth);
        minimax_stage *Stage = Context.Stages + Context.CurrentDepth;
        chess_piece *Pieces = Game.BlackIsPlaying ? Game.Blacks : Game.Whites;
        
        if (Stage->DecisionsCount == 0)
        {
            // NOTE(vincent): Push decisions in two passes: those that involve a capture on
            // the first pass, and then those that don't on the second pass.
            // This is the cheapest/simplest way to reorder nodes to get some decent pruning.
            
            for (u32 PieceIndex = 0; PieceIndex < 16; ++PieceIndex)
            {
                chess_piece *Piece = Pieces + PieceIndex;
                u32 PieceDestCount = Piece->DestinationsCount;
                for (u32 DestIndex = 0; DestIndex < PieceDestCount; ++DestIndex)
                {
                    if (Piece->Destinations[DestIndex].DestCode >> 6)
                    {
                        decision *Dec = Stage->Decisions + Stage->DecisionsCount;
                        Dec->Piece = Piece;
                        Dec->Destination.DestCode = Piece->Destinations[DestIndex].DestCode;
                        ++Stage->DecisionsCount;
                    }
                }
            }
            
            for (u32 PieceIndex = 0; PieceIndex < 16; ++PieceIndex)
            {
                chess_piece *Piece = Pieces + PieceIndex;
                u32 PieceDestCount = Piece->DestinationsCount;
                for (u32 DestIndex = 0; DestIndex < PieceDestCount; ++DestIndex)
                {
                    if ((Piece->Destinations[DestIndex].DestCode >> 6) == 0)
                    {
                        decision *Dec = Stage->Decisions + Stage->DecisionsCount;
                        Dec->Piece = Piece;
                        Dec->Destination.DestCode = Piece->Destinations[DestIndex].DestCode;
                        ++Stage->DecisionsCount;
                    }
                }
            }
        }
        
        for (; Stage->DecisionIndex < Stage->DecisionsCount; ++Stage->DecisionIndex)
        {
            Assert(Game.BlackIsPlaying == (b32)(RootPlayerIsBlack ^ (Context.CurrentDepth & 1)));
            
            // NOTE(vincent): Apply move
            decision Decision = Stage->Decisions[Stage->DecisionIndex];
            s32 DestRow = Decision.Destination.DestCode & 7;
            s32 DestCol = (Decision.Destination.DestCode >> 3) & 7;
            Game.Cursor.Row = DestRow;
            Game.Cursor.Column = DestCol;
            Game.PieceOnCursor = GetPiece(Game.Blacks, Game.Whites,
                                          Game.Cursor.Row, Game.Cursor.Column);
            Assert(!Game.GameIsOver);
            MovePieceToCursor(&Game, Decision.Piece, Decision.Destination.DestCode);
            if (Game.PromotingPawn)
            {
                Game.PromotingPawn = false;
                Decision.Piece->Type = ChessPieceType_Queen;
                history_entry *Entry = Game.History.Entries + Game.History.EntryCount-1;
                Decision.PromotionType = ChessPieceType_Queen;
                Entry->Special |= 7;
                MovePieceAfterwork(&Game);
            }
            else
                Decision.PromotionType = ChessPieceType_Empty;
            Assert(!Game.BlackIsPlaying == (b32)(RootPlayerIsBlack ^ (Context.CurrentDepth & 1)));
            
            if (Context.CurrentDepth == 0)
                LastTopDecision = Decision;
            
            f32 Value = 99999.0f;
            
            if (Game.RunningState == ChessGameRunningState_Checkmate)
                Value = Game.BlackIsPlaying ? 5000 : -5000;
            else if (Game.RunningState == ChessGameRunningState_Stalemate)
                Value = 0;
            else if (Context.CurrentDepth == MaxDepth - 1)
                Value = HeuristicEvaluation(&Game, Series);
            
            if (Value != 99999.0f)
            {
                if (Game.BlackIsPlaying && Value > Stage->Alpha)
                {
                    if (Value >= Stage->Beta)
                    {
                        // Pruning.
                        // We had explored another path indicating that MIN (black) player
                        // could guarantee a max utility of Stage->Beta.
                        // But from the current node, MAX (white) can get more than that
                        // (at least Value, which a candidate for the new alpha here).
                        // This means that MIN's last decision is not rational and we should
                        // stop exploring the current node. We do not propagate the alpha.
                        Assert(Context.CurrentDepth > 0);
                        Stage[-1].DecisionIndex++;
                        Context.CurrentDepth--;
                        CopyGame(&Stage[-1].GameCopy, &Game);
                        goto Goto_StageExploration;
                    }
                    Stage->Alpha = Value;
                    if (Context.CurrentDepth == 0)
                    {
                        Result->Decision = Decision;
                        Assert(AbsoluteValue(Result->Value) != 5000);
                        Assert(Result->Value < Value);
                        Result->Value = Value;
                    }
                }
                else if (!Game.BlackIsPlaying && Value < Stage->Beta)
                {
                    if (Stage->Alpha >= Value)
                    {
                        // Pruning.
                        Assert(Context.CurrentDepth > 0);
                        Stage[-1].DecisionIndex++;
                        Context.CurrentDepth--;
                        CopyGame(&Stage[-1].GameCopy, &Game);
                        goto Goto_StageExploration;
                    }
                    Stage->Beta = Value;
                    if (Context.CurrentDepth == 0)
                    {
                        Result->Decision = Decision;
                        Assert(AbsoluteValue(Result->Value) != 5000);
                        Assert(Result->Value > Value);
                        Result->Value = Value;
                    }
                }
            }
            else
            {
                Assert(AbsoluteValue(Value) != 5000);
                Context.CurrentDepth++;
                Stage[1].DecisionIndex = 0;
                Stage[1].DecisionsCount = 0;
                Stage[1].Alpha = Stage[0].Alpha;
                Stage[1].Beta = Stage[0].Beta;
                CopyGame(&Game, &Stage[1].GameCopy);
                //Stage[1].GameCopy = *Game;
                goto Goto_StageExploration;
            }
            
            CopyGame(&Stage->GameCopy, &Game);
        }
        
        Goto_PruningParent:
        
        
        // NOTE(vincent): Value of current stage has been fully evaluated...
        Assert(Stage->Alpha <= Stage->Beta);
        if (Context.CurrentDepth > 0)
        {
            // ...propagate it up to the parent stage if it's better.
            CopyGame(&Stage[-1].GameCopy, &Game);
            
            if (Game.BlackIsPlaying && (Stage->Alpha < Stage[-1].Beta))
            {
                Stage[-1].Beta = Stage->Alpha;
                if (Context.CurrentDepth == 1)
                {
                    Result->Decision = LastTopDecision;
                    Assert(Result->Value > Stage->Alpha);
                    Result->Value = Stage->Alpha;
                }
                else if (Stage[-1].Beta == Stage[-1].Alpha)
                {
                    Context.CurrentDepth--;
                    Stage--;
                    goto Goto_PruningParent;
                }
                
            }
            else if (!Game.BlackIsPlaying && (Stage->Beta > Stage[-1].Alpha))
            {
                Stage[-1].Alpha = Stage->Beta;
                if (Context.CurrentDepth == 1)
                {
                    Result->Decision = LastTopDecision;
                    Assert(Result->Value < Stage->Beta);
                    Result->Value = Stage->Beta;
                }
                else if (Stage[-1].Beta == Stage[-1].Alpha)
                {
                    Context.CurrentDepth--;
                    Stage--;
                    goto Goto_PruningParent;
                }
            }
            
            Stage[-1].DecisionIndex++;
            Context.CurrentDepth--;
            
            goto Goto_StageExploration;
        }
    }
    Goto_EndExploration:
    
    EndTemporaryMemory(StagesMemory);
    CheckArena(Arena);
    CopyGame(&Context.Stages[0].GameCopy, &Game);
    Assert(Result->Decision.Piece->Destinations && 
           Result->Decision.Piece->DestinationsCount);
    Result->Decision.Piece =
        (chess_piece *)((u8 *)Game_ + ((u8 *)Result->Decision.Piece - (u8 *)&Game));
    Assert(Result->Decision.Piece->Destinations && 
           Result->Decision.Piece->DestinationsCount);
    
    CompilerWriteBarrier;
    Params->Finished = true;
}


internal void
AdvanceAIAction(game_state *State, chess_game_state *Game, f32 dt, random_series *Series,
                memory_arena *Arena, platform_work_queue *Queue)
{
    cursor *Cursor = &Game->Cursor;
    ai_state *AIState = &Game->AIState;
    
    switch(AIState->Stage)
    {
        case 0:
        {
            u32 AIType = Game->BlackIsPlaying ? Game->BlackAI : Game->WhiteAI;
            if (AIType == 1)
            {
                AIState->WorkParams.Result.Decision = GetRandomDecision(Game, Series);
                AIState->WorkParams.Finished = true;
            }
            else
            {
#if 0
                // iterative max depth (outdated code, sorry)
                for (u32 Depth = 1; Depth < AIType; ++Depth)
                {
                    good_decision_result DecResult = 
                        GetGoodDecisionTEST(Game, Arena, Series, Depth); 
                    Decision = DecResult.Decision;
                    if (AbsoluteValue(DecResult.Value) == 5000)
                        break;
                }
#else
                // fixed max depth
                AIState->WorkParams.Game = Game;
                AIState->WorkParams.Arena = Arena;
                AIState->WorkParams.Series = Series;
                AIState->WorkParams.MaxDepth = AIType - 1;
                AIState->WorkParams.Finished = false;
                AIState->WorkParams.ShouldContinue = true;
                GlobalPlatform->AddEntry(Queue, GetGoodDecision, &AIState->WorkParams);
                //GetGoodDecision(Queue, &AIState->WorkParams);
#endif
            }
            AIState->Stage++;
            
        } break;
        
        case 1:
        {
            if (AIState->WorkParams.Finished)
            {
                Assert(AIState->WorkParams.Result.Decision.Piece->Destinations &&
                       AIState->WorkParams.Result.Decision.Piece->DestinationsCount);
                AIState->OldCursorRow = Cursor->Row;
                AIState->OldCursorColumn = Cursor->Column;
                
                // NOTE(vincent): Update cursor to point at chosen piece
                Cursor->Row = AIState->WorkParams.Result.Decision.Piece->Row;
                Cursor->Column = 
                    AIState->WorkParams.Result.Decision.Piece->Column;
                v2 NewCursorP = GetBoardSpaceV2(Cursor->Row, Cursor->Column);
                InitMovingV2FromCurrent(&Cursor->P, NewCursorP, MOVE_CURSOR_DURATION);
                
                AIState->t = 0.0f;
                AIState->Stage++;
            }
        } break;
        
        case 2:
        {
            if (AIState->t > MOVE_CURSOR_DURATION)
            {
                decision Decision = AIState->WorkParams.Result.Decision;
                // NOTE(vincent): Select the piece
                Game->SelectedPiece.Piece = Decision.Piece;
                Game->SelectedPiece.IsWhite = !Game->BlackIsPlaying;
                
                // NOTE(vincent): Move cursor to destination
                Cursor->Row = Decision.Destination.DestCode & 7;
                Cursor->Column = (Decision.Destination.DestCode >> 3) & 7;
                v2 NewCursorP = GetBoardSpaceV2(Cursor->Row, Cursor->Column);
                InitMovingV2FromCurrent(&Cursor->P, NewCursorP, MOVE_CURSOR_DURATION);
                Game->PieceOnCursor = GetPiece(Game->Blacks, Game->Whites,
                                               Game->Cursor.Row, Game->Cursor.Column);
                
                // NOTE(vincent): Actually move the piece to the destination
                b32 ActuallyMoved = MoveSelectedPieceToCursorIfLegal(Game);
                Assert(ActuallyMoved);
                
                // NOTE(vincent): handle pawn promotion "ourselves" in this code path:
                Game->PromotingPawn = false;
                if (Decision.PromotionType != ChessPieceType_Empty)
                {
                    Decision.Piece->Type = Decision.PromotionType;
                    history_entry *Entry = Game->History.Entries + Game->History.EntryCount-1;
                    SetPromotionBits(Entry, Decision.PromotionType);
                    MovePieceAfterwork(Game);
                }
                
                // NOTE(vincent): MovePieceAfterwork flips the variable BlackIsPlaying.
                // This may give back control to the other player, but we don't really want
                // to do that before finishing stage 2 of the animation, so we temporarily
                // flip BlackIsPlaying back to let stage 2 execute itself.
                // However, MovePieceAfterwork() could result in a game over, in which case
                // stage 2 will never execute. If we flipped BlackIsPlaying here
                // in that case, BlackIsPlaying will be erroneous, which leads to some bugs
                // in the history navigation system.
                if (Game->GameIsOver)
                {
                    AIState->Stage = 0;
                }
                else
                {
                    Game->BlackIsPlaying = !Game->BlackIsPlaying;
                    AIState->Stage++;
                }
                
                AIState->t = 0.0f;
            }
            else
                AIState->t += dt;
        } break;
        
        case 3:
        {
            if (AIState->t > MOVE_PIECE_DURATION)
            {
                // NOTE(vincent): reestablish older cursor position for the other player
                // if the other player is human. When the other player is also an AI,
                // if we did execute this path the game will run a frame with the wrong
                // vector animation endpoints.
                if (!Game->WhiteAI || !Game->BlackAI)
                {
                    Cursor->Row = Game->AIState.OldCursorRow;
                    Cursor->Column = Game->AIState.OldCursorColumn;
                    v2 NewCursorP = GetBoardSpaceV2(Cursor->Row, Cursor->Column);
                    InitMovingV2FromCurrent(&Cursor->P, NewCursorP, MOVE_CURSOR_DURATION);
                }
                AIState->Stage = 0;
                State->ShouldSave = true;
                Game->BlackIsPlaying = !Game->BlackIsPlaying;
            }
            else
                AIState->t += dt;
        } break;
        
        InvalidDefaultCase;
    }
}

internal void
LoadSaveFile(game_state *State, memory_arena *FileArena)
{
    // NOTE(vincent): We only want to run this path once during the lifetime of the process.
    Assert(FileArena->Used == 0);
    temporary_memory FileTempMemory = BeginTemporaryMemory(FileArena);
    
    char *SaveFilename = "chess_save";
    
    string ReadResult = GlobalPlatform->PushReadFile(FileArena, SaveFilename);
    memory_arena CopyArena = *FileArena;
    
    if (ReadResult.Base)
    {
        if (ReadResult.Size == sizeof(game_state))
        {
            
            u8 *Source = (u8 *)ReadResult.Base;
            u8 *Dest = (u8 *)State;
            for (u32 Byte = 0; Byte < sizeof(game_state); ++Byte)
            {
                *Dest++ = *Source++;
            }
            
            
            State->GlobalArena = CopyArena;;
            
            
            for (u32 i = 0; i < State->GamesCount; ++i)
            {
                chess_game_state *Game = State->Games + i;
                if (Game->SelectedPiece.Piece)
                {
                    Game->SelectedPiece.Piece = 
                        (chess_piece *)((u8 *)Game + (uintptr_t)Game->SelectedPiece.Piece);
                }
                if (Game->PieceOnCursor.Piece)
                {
                    Game->PieceOnCursor.Piece = 
                        (chess_piece *)((u8 *)Game + (uintptr_t)Game->PieceOnCursor.Piece);
                }
                for (u32 PieceIndex = 0; PieceIndex < 16; ++PieceIndex)
                {
                    if (Game->Blacks[PieceIndex].Destinations)
                    {
                        Game->Blacks[PieceIndex].Destinations =
                            (destination *) ((u8 *)Game + 
                                             (uintptr_t)Game->Blacks[PieceIndex].Destinations);
                    }
                    if (Game->Whites[PieceIndex].Destinations)
                    {
                        Game->Whites[PieceIndex].Destinations = 
                            (destination *) ((u8 *)Game + 
                                             (uintptr_t)Game->Whites[PieceIndex].Destinations);
                    }
                    Assert((uintptr_t)Game->Blacks[PieceIndex].Destinations == 0 ||
                           (uintptr_t)Game->Blacks[PieceIndex].Destinations <= 
                           (uintptr_t)Game + sizeof(chess_game_state));
                    Assert((uintptr_t)Game->Whites[PieceIndex].Destinations == 0 ||
                           (uintptr_t)Game->Whites[PieceIndex].Destinations <= 
                           (uintptr_t)Game + sizeof(chess_game_state));
                }
            }
        }
    }
    
    EndTemporaryMemory(FileTempMemory);
}

internal void
TransitionToStartScreen(game_state *State)
{
    State->PreviousMode = State->GameMode;
    State->GameMode = GameMode_StartScreen;
    State->MenuX = State->CurrentGameIndex;
    State->MenuY = 1;
    State->ShouldUpdateBoardMovingVectors = true;
    
    Assert(State->GamesCount > 0);
    State->Games[State->CurrentGameIndex].AIState.WorkParams.ShouldContinue = false;
    if (State->Games[State->CurrentGameIndex].AIState.Stage == 1)
        State->Games[State->CurrentGameIndex].AIState.Stage = 0;
    State->Games[State->CurrentGameIndex].AIState.t = 0.0f;
}

internal void
TransitionToGameplay(game_state *State, chess_game_state *Game)
{
    State->PreviousMode = State->GameMode;
    State->GameMode = GameMode_Gameplay;
    
    v2 NewBoardDim = 0.98f*V2(State->RenderGroup.ScreenDim.y, State->RenderGroup.ScreenDim.y);
    InitMovingV2FromCurrent(&Game->Board.P, 0.5f * State->RenderGroup.ScreenDim, .5f);
    InitMovingV2FromCurrent(&Game->Board.Dim, NewBoardDim, .5f);
}

internal void
TransitionToSettings(game_state *State, chess_game_state *Game)
{
    State->PreviousMode = State->GameMode;
    State->GameMode = GameMode_Settings;
    State->MenuY = 0;
}

internal void
TransitionToPause(game_state *State, chess_game_state *Game)
{
    State->PreviousMode = State->GameMode;
    State->GameMode = GameMode_Pause;
    State->MenuY = 0;
}

internal void
StartNewGame(game_state *State)
{
    if (State->GamesCount < MAX_GAMES_COUNT)
    {
        chess_game_state *Game = State->Games + State->GamesCount;
        
        ZeroBytes((u8 *)Game, sizeof(chess_game_state));
        
        State->CurrentGameIndex = State->GamesCount;
        ++State->GamesCount;
        
        // NOTE(vincent): Setting the positions and dims of the boards from start screen
        // so that the newly created board is focused on when we go back to the start screen.
        f32 BoardAnimDuration = .3f;
        for (s32 GameIndex = 0; GameIndex < (s32)State->GamesCount-1; ++GameIndex)
        {
            InitMovingV2FromCurrent(&State->Games[GameIndex].Board.Dim, V2(0.1f, 0.1f),
                                    BoardAnimDuration);
            InitMovingV2FromCurrent(&State->Games[GameIndex].Board.P,
                                    V2(0.3f + 0.12f * (GameIndex - State->GamesCount),
                                       0.27f), BoardAnimDuration);
        }
        
        TransitionToSettings(State, Game);
        
        Game->RunningState = ChessGameRunningState_Normal;
        Game->GameIsOver = false;
        InitChessPieces(Game);
        
        Game->Cursor.Row = 0;
        Game->Cursor.Column = 0;
        InitMovingV2(&Game->Cursor.P, V2(0,0), GetBoardSpaceV2(Game->Cursor.Row, Game->Cursor.Column), 0.0f);
        InitMovingV2(&Game->Board.P, 0.5f*State->RenderGroup.ScreenDim, 
                     0.5f*State->RenderGroup.ScreenDim, 0.0f);
        
        v2 NewBoardDim = 0.98f*V2(State->RenderGroup.ScreenDim.y, State->RenderGroup.ScreenDim.y);
        InitMovingV2(&Game->Board.Dim, V2(0,0), NewBoardDim, 1.0f);
        
        Game->SelectedPiece.Piece = 0;
        Game->BlackIsPlaying = false;
        Game->History.EntryCount = 0;
        
        RecomputeDestinations(Game);
        
        //AssertDestPointersWithinBounds(Game);
        State->MenuY = 0;
    }
}

internal void
LoadGame(game_state *State, u32 GameIndex)
{
    if (GameIndex < State->GamesCount)
    {
        TransitionToGameplay(State, State->Games + GameIndex);
    }
}

internal void
DuplicateGame(game_state *State, u32 GameIndex)
{
    if (GameIndex < State->GamesCount && State->GamesCount < MAX_GAMES_COUNT)
    {
        chess_game_state *Games = State->Games;
        for (u32 DestGameIndex = State->GamesCount; DestGameIndex >= GameIndex+1; --DestGameIndex)
        {
            chess_game_state *DestGame = Games + DestGameIndex;
            chess_game_state *SourceGame = Games + DestGameIndex - 1;
            *DestGame = *SourceGame;
            
            if (DestGame->SelectedPiece.Piece)
            {
                DestGame->SelectedPiece.Piece =
                    (chess_piece *)((u8 *)DestGame->SelectedPiece.Piece + sizeof(chess_game_state));
            }
            if (DestGame->PieceOnCursor.Piece)
            {
                DestGame->PieceOnCursor.Piece =
                    (chess_piece *)((u8 *)DestGame->PieceOnCursor.Piece + sizeof(chess_game_state));
            }
            for (u32 i = 0; i < 16; ++i)
            {
                if (DestGame->Blacks[i].Destinations)
                {
                    DestGame->Blacks[i].Destinations =
                        (destination *)((u8 *)DestGame->Blacks[i].Destinations + 
                                        sizeof(chess_game_state));
                }
                if (DestGame->Whites[i].Destinations)
                {
                    DestGame->Whites[i].Destinations =
                        (destination *)((u8 *)DestGame->Whites[i].Destinations + 
                                        sizeof(chess_game_state));
                }
            }
            //AssertDestPointersWithinBounds(DestGame);
        }
        State->ShouldUpdateBoardMovingVectors = true;
        ++State->GamesCount;
    }
}

internal void
DeleteGame(game_state *State, u32 GameIndex)
{
    if (GameIndex < State->GamesCount)
    {
        chess_game_state *Games = State->Games;
        for (u32 DestGameIndex = GameIndex; DestGameIndex < State->GamesCount-1; ++DestGameIndex)
        {
            chess_game_state *DestGame = Games + DestGameIndex;
            chess_game_state *SourceGame = Games + DestGameIndex + 1;
            *DestGame = *SourceGame;
            
            if (DestGame->SelectedPiece.Piece)
            {
                DestGame->SelectedPiece.Piece =
                    (chess_piece *)((u8 *)DestGame->SelectedPiece.Piece - sizeof(chess_game_state));
            }
            if (DestGame->PieceOnCursor.Piece)
            {
                DestGame->PieceOnCursor.Piece =
                    (chess_piece *)((u8 *)DestGame->PieceOnCursor.Piece - sizeof(chess_game_state));
            }
            for (u32 i = 0; i < 16; ++i)
            {
                if (DestGame->Blacks[i].Destinations)
                {
                    DestGame->Blacks[i].Destinations =
                        (destination *)((u8 *)DestGame->Blacks[i].Destinations - 
                                        sizeof(chess_game_state));
                }
                if (DestGame->Whites[i].Destinations)
                {
                    DestGame->Whites[i].Destinations =
                        (destination *)((u8 *)DestGame->Whites[i].Destinations - 
                                        sizeof(chess_game_state));
                }
            }
            //AssertDestPointersWithinBounds(DestGame);
        }
        --State->GamesCount;
        State->ShouldUpdateBoardMovingVectors = true;
    }
}

internal asset_header *
LoadAssetFile(memory_arena *Arena, char *Filename)
{
    string File = GlobalPlatform->PushReadFile(Arena, Filename);
    asset_header *Result = 0;
    if (File.Base)
    {
        Result = (asset_header *)File.Base;
        
#define AdjustBitmapOffset(Bitmap) \
Result->Bitmap.Memory =  (void *)((uintptr_t)Result->Bitmap.Memory + (u8 *)Result); \
Assert(Result->Bitmap.Memory); \
Assert(Result->Bitmap.Width == 64); \
Assert(Result->Bitmap.Height == 64); \
Assert(Result->Bitmap.Pitch == 4*(Result->Bitmap.Width)); \
Assert(Result->Bitmap.Handle == 0);
        
        AdjustBitmapOffset(WhitePawn);
        AdjustBitmapOffset(WhiteRook);
        AdjustBitmapOffset(WhiteKnight);
        AdjustBitmapOffset(WhiteBishop);
        AdjustBitmapOffset(WhiteKing);
        AdjustBitmapOffset(WhiteQueen);
        
        AdjustBitmapOffset(BlackPawn);
        AdjustBitmapOffset(BlackRook);
        AdjustBitmapOffset(BlackKnight);
        AdjustBitmapOffset(BlackBishop);
        AdjustBitmapOffset(BlackKing);
        AdjustBitmapOffset(BlackQueen);
        
#undef AdjustBitmapOffset
        for (u32 GlyphIndex = 0; GlyphIndex < ArrayCount(Result->Font.Glyphs); ++GlyphIndex)
        {
            bitmap *Bitmap = &Result->Font.Glyphs[GlyphIndex].Bitmap;
            Bitmap->Memory = (void *)((uintptr_t)Bitmap->Memory + (u8 *)Result);
            
            Assert(Bitmap->Memory);
            Assert(Bitmap->Width <= 1000);
            Assert(Bitmap->Height <= 1000);
            Assert(Bitmap->Pitch == 4*(Bitmap->Width));
            Assert(Bitmap->Handle == 0);
        }
    }
    return Result;
}

internal void
DrawGameBoard(render_group *Group, chess_game_state *Game, bitmap *BoardBitmap, 
              asset_header *Assets)
{
    board *Board = &Game->Board;
    
    v2 SquareDim = 0.125f * Board->Dim.Current;
    v2 BottomLeftSquareP = Board->P.Current - 3.5f*SquareDim;
    
    // NOTE(vincent): Drawing the game board as a single 8*8 bitmap instead of multiple
    // rectangle entries turns out to be faster.
    PushBitmapNearest(Group, BoardBitmap, Board->P.Current - 0.5f*Board->Dim.Current, V2(Board->Dim.Current.x, 0),
                      V2(0, Board->Dim.Current.y), V4(1,1,1,1));
    
    
    // NOTE(vincent): highlighting dest squares when a piece is selected
    if (Game->SelectedPiece.Piece)
    {
        chess_piece *SPiece = Game->SelectedPiece.Piece;
        f32 DestSqDimRatio = 8.0f * Game->SelectedPieceT;
        if (DestSqDimRatio > 1.0f)
            DestSqDimRatio = 1.0f;
        v2 DestSquareDim = SquareDim * DestSqDimRatio;
#define DestColorTPeriod 1.0f
        
        f32 NormalizedDestColorT = Game->DestColorT / DestColorTPeriod;
        f32 DestColorT = CubicHermite(NormalizedDestColorT);
        f32 CenteredDestColorT = 2.0f*DestColorT - 1.0f;
        f32 RecenteredDestColorT = 2.0f*AbsoluteValue(CenteredDestColorT) - 1.0f;
        
        v4 DestColor = V4(0.55f * (1.0f + 0.1f*RecenteredDestColorT), 
                          0.6f * (1.0f + 0.1f*RecenteredDestColorT), 
                          0.95f * (1.0f + 0.1f*RecenteredDestColorT),
                          0.95f);
        
        v4 CapDestColor = V4(0.95f * (1.0f + 0.1f*RecenteredDestColorT), 
                             0.6f * (1.0f + 0.1f*RecenteredDestColorT), 
                             0.55f * (1.0f + 0.1f*RecenteredDestColorT),
                             0.95f);
        
        for (u32 DestIndex = 0; DestIndex < SPiece->DestinationsCount; ++DestIndex)
        {
            u32 DestCode = SPiece->Destinations[DestIndex].DestCode;
            u32 Row = DestCode & 7;
            u32 Column = (DestCode >> 3) & 7;
            b32 IsCapture = DestCode >> 6;
            
            v2 RectMin = BottomLeftSquareP - 0.5f*DestSquareDim + 
                V2(Column * SquareDim.x, Row * SquareDim.y);
            v2 RectMax = RectMin + DestSquareDim;
            
            PushRect(Group, RectMin, RectMax, IsCapture ? CapDestColor : DestColor);
        }
    }
    
    
    v2 BiggerSquareDim = 1.2f*SquareDim;
    v2 AvgSquareDim = 0.5f*(BiggerSquareDim + SquareDim);
    for (u32 i = 0; i < ArrayCount(Game->Blacks); ++i)
    {
        v2 BlackSquareP = BottomLeftSquareP - AvgSquareDim + 
            V2((0.5f + 0.5f*Game->Blacks[i].P.Current.x)*Board->Dim.Current.x,
               (0.5f + 0.5f*Game->Blacks[i].P.Current.y)*Board->Dim.Current.y);
        
        v2 WhiteSquareP = BottomLeftSquareP - AvgSquareDim +
            V2((0.5f + 0.5f*Game->Whites[i].P.Current.x)*Board->Dim.Current.x,
               (0.5f + 0.5f*Game->Whites[i].P.Current.y)*Board->Dim.Current.y);
        v4 Color = V4(1,1,1,1);
        
        switch (Game->Blacks[i].Type)
        {
            case ChessPieceType_Pawn: 
            PushBitmap(Group, &Assets->BlackPawn, BlackSquareP, V2(BiggerSquareDim.x, 0),
                       V2(0, BiggerSquareDim.y), Color);
            break;
            
            case ChessPieceType_Rook: 
            PushBitmap(Group, &Assets->BlackRook, BlackSquareP, V2(BiggerSquareDim.x, 0),
                       V2(0, BiggerSquareDim.y), Color);
            break;
            
            case ChessPieceType_Knight: 
            PushBitmap(Group, &Assets->BlackKnight, BlackSquareP, V2(BiggerSquareDim.x, 0),
                       V2(0, BiggerSquareDim.y), Color);
            break;
            
            case ChessPieceType_Bishop:
            PushBitmap(Group, &Assets->BlackBishop, BlackSquareP, V2(BiggerSquareDim.x, 0),
                       V2(0, BiggerSquareDim.y), Color);
            break;
            
            case ChessPieceType_Queen:
            PushBitmap(Group, &Assets->BlackQueen, BlackSquareP, V2(BiggerSquareDim.x, 0),
                       V2(0, BiggerSquareDim.y), Color);
            break;
            
            case ChessPieceType_King:
            PushBitmap(Group, &Assets->BlackKing, BlackSquareP, V2(BiggerSquareDim.x, 0),
                       V2(0, BiggerSquareDim.y), Color);
            break;
            case ChessPieceType_Empty: break;
            InvalidDefaultCase;
        }
        
        switch (Game->Whites[i].Type)
        {
            case ChessPieceType_Pawn: 
            PushBitmap(Group, &Assets->WhitePawn, WhiteSquareP, V2(BiggerSquareDim.x, 0),
                       V2(0, BiggerSquareDim.y), Color);
            break;
            
            case ChessPieceType_Rook: 
            PushBitmap(Group, &Assets->WhiteRook, WhiteSquareP, V2(BiggerSquareDim.x, 0),
                       V2(0, BiggerSquareDim.y), Color);
            break;
            
            case ChessPieceType_Knight: 
            PushBitmap(Group, &Assets->WhiteKnight, WhiteSquareP, V2(BiggerSquareDim.x, 0),
                       V2(0, BiggerSquareDim.y), Color);
            break;
            
            case ChessPieceType_Bishop:
            PushBitmap(Group, &Assets->WhiteBishop, WhiteSquareP, V2(BiggerSquareDim.x, 0),
                       V2(0, BiggerSquareDim.y), Color);
            break;
            
            case ChessPieceType_Queen:
            PushBitmap(Group, &Assets->WhiteQueen, WhiteSquareP, V2(BiggerSquareDim.x, 0),
                       V2(0, BiggerSquareDim.y), Color);
            break;
            
            case ChessPieceType_King:
            PushBitmap(Group, &Assets->WhiteKing, WhiteSquareP, V2(BiggerSquareDim.x, 0),
                       V2(0, BiggerSquareDim.y), Color);
            break;
            case ChessPieceType_Empty: break;
            InvalidDefaultCase;
        }
    }
    
    // NOTE(vincent): Drawing the cursor brackets.
    // The inflating cursor kind of looks bad because rectangle positions are not AA'd :(
#define CursorBreatheTPeriod 100.0f
    f32 NormalizedCursorBreatheT = Game->Cursor.BreatheT / CursorBreatheTPeriod;
    f32 CenteredCursorBreatheT = 2.0f*NormalizedCursorBreatheT - 1.0f;
    f32 AbsCenteredCursorT = AbsoluteValue(CenteredCursorBreatheT);
    f32 HermiteCenteredCursorT = CubicHermite(AbsCenteredCursorT);
    
    v4 CursorColor = V4(0.98f + 0.02f*AbsCenteredCursorT, 
                        0.9f + 0.025f*HermiteCenteredCursorT,
                        0.4f + 0.15f*HermiteCenteredCursorT, 
                        0.9f);
    
    v2 CursorSquareDim = SquareDim * (1.0f + 0.07f*HermiteCenteredCursorT);
    
    v2 CursorSquareMin = BottomLeftSquareP 
        - 0.5f*(SquareDim + CursorSquareDim) 
        + V2((0.5f + 0.5f*Game->Cursor.P.Current.x) * Board->Dim.Current.x,
             (0.5f + 0.5f*Game->Cursor.P.Current.y) * Board->Dim.Current.y);
    v2 CursorSquareMax = CursorSquareMin + CursorSquareDim;
    
    f32 SideLengthPercentage = 0.333f;
    f32 BracketThicknessPercentage = 0.4f;
    v2 BracketDim = SideLengthPercentage*(CursorSquareMax - CursorSquareMin);
    v2 BracketThickness = BracketThicknessPercentage * BracketDim;
    
    v2 LowerLeftBracketMin = CursorSquareMin;
    v2 LowerLeftBracketMax = CursorSquareMin + BracketDim;
    v2 LowerLeftJunction = LowerLeftBracketMin + BracketThickness;
    v2 LowerLeftRect1Max = V2(LowerLeftJunction.x, LowerLeftBracketMax.y);
    v2 LowerLeftRect2Low = V2(LowerLeftJunction.x, LowerLeftBracketMin.y);
    v2 LowerLeftRect2Max = V2(LowerLeftBracketMax.x, LowerLeftJunction.y);
    PushRect(Group, LowerLeftBracketMin, LowerLeftRect1Max, CursorColor);
    PushRect(Group, LowerLeftRect2Low, LowerLeftRect2Max, CursorColor);
    
    v2 LowerRightBracketMin = V2(CursorSquareMax.x - BracketDim.x, CursorSquareMin.y);
    v2 LowerRightBracketMax = LowerRightBracketMin + BracketDim;
    v2 LowerRightJunction =
        V2(LowerRightBracketMax.x - BracketThickness.x,
           LowerRightBracketMin.y + BracketThickness.y);
    v2 LowerRightRect2Min = V2(LowerRightJunction.x, LowerRightBracketMin.y);
    PushRect(Group, LowerRightBracketMin, LowerRightJunction, CursorColor);
    PushRect(Group, LowerRightRect2Min, LowerRightBracketMax, CursorColor);
    
    v2 UpperLeftBracketMin = V2(CursorSquareMin.x, CursorSquareMax.y - BracketDim.y);
    v2 UpperLeftBracketMax = UpperLeftBracketMin + BracketDim;
    v2 UpperLeftJunction =
        V2(UpperLeftBracketMin.x + BracketThickness.x, 
           UpperLeftBracketMax.y - BracketThickness.y);
    v2 UpperLeftRect2Min = V2(UpperLeftBracketMin.x, UpperLeftJunction.y);
    PushRect(Group, UpperLeftBracketMin, UpperLeftJunction, CursorColor);
    PushRect(Group, UpperLeftRect2Min, UpperLeftBracketMax, CursorColor);
    
    v2 UpperRightBracketMax = CursorSquareMax;
    v2 UpperRightBracketMin = UpperRightBracketMax - BracketDim;
    v2 UpperRightRect1Min = 
        V2(UpperRightBracketMin.x, UpperRightBracketMax.y - BracketThickness.y);
    v2 UpperRightRect2Min = 
        V2(UpperRightBracketMax.x - BracketThickness.x, UpperRightBracketMin.y);
    v2 UpperRightRect2Max = 
        V2(UpperRightBracketMax.x, UpperRightBracketMax.y - BracketThickness.y);
    PushRect(Group, UpperRightRect1Min, UpperRightBracketMax, CursorColor);
    PushRect(Group, UpperRightRect2Min, UpperRightRect2Max, CursorColor);
    
}

internal b32
NewButtonPress(button_state Button)
{
    b32 Result = false;
    if (Button.IsPressed && !Button.WasPressed)
        Result = true;
    return Result;
}

internal void
DrawGameStrings(render_group *Group, font *Font, chess_game_state *Game, f32 RelX, f32 RelY)
{
    
    char *StringWhitesTurn = "White's turn";
    char *StringBlacksTurn = "Black's turn";
    char *StringDraw = "Draw";
    char *StringBlackCheck = "Check black";
    char *StringWhiteCheck = "Check white";
    char *StringBlackCheckmate = "White wins";
    char *StringWhiteCheckmate = "Black wins";
    
    char *StringToDisplay = 0;
    
    v4 TextColor = V4(1,1,1,1);
    v4 NumberColor = Game->GameIsOver ? V4(0.7f, 0.7f, 1.0f, 1.0f) : TextColor;
    switch(Game->RunningState)
    {
        case ChessGameRunningState_Check: 
        TextColor = V4(0.9f, 0.9f, 0.5f, 1.0f);
        StringToDisplay = (Game->CurrentEntryIndex & 1) ? StringBlackCheck : StringWhiteCheck;
        break;
        case ChessGameRunningState_Checkmate: 
        TextColor = V4(0.7f, 0.7f, 1.0f, 1.0f);
        StringToDisplay = (Game->CurrentEntryIndex & 1) ? StringBlackCheckmate : StringWhiteCheckmate; 
        break;
        case ChessGameRunningState_Stalemate: 
        TextColor = V4(0.7f, 0.7f, 1.0f, 1.0f);
        StringToDisplay = StringDraw; 
        break;
        case ChessGameRunningState_Normal: 
        StringToDisplay = (Game->CurrentEntryIndex & 1) ? StringBlacksTurn : StringWhitesTurn; 
        break;
        InvalidDefaultCase;
    }
    Assert(StringToDisplay);
    PushText(Group, StringToDisplay, Font, 0.0007f, 
             V2(RelX, RelY + 0.02f), TextColor);
    PushNumber(Group, Game->CurrentEntryIndex, Font, 0.0007f, V2(RelX, RelY), NumberColor);
    PushNumber(Group, Game->History.EntryCount, Font, 0.0007f, V2(RelX + 0.04f, RelY), NumberColor);
}

extern "C"
GAME_UPDATE(GameUpdate)
{
    game_state *State = (game_state *)Memory->Storage;
    chess_game_state *Games = State->Games;
    GlobalPlatform = &Memory->Platform;
    
    if (!State->IsInitialized)
    {
        Assert(Memory->StorageSize >= 2*sizeof(game_state));
        InitializeArena(&State->GlobalArena, Memory->StorageSize - sizeof(game_state),
                        (u8 *)Memory->Storage + sizeof(game_state));
        LoadSaveFile(State, &State->GlobalArena);
        
        State->RenderGroup = SetRenderGroup(RenderCommands);
        
        State->Assets = LoadAssetFile(&State->GlobalArena, "chess_asset_file");
        State->Series = RandomSeries(41);
        
        SubArena(&State->AIArena, &State->GlobalArena, Megabytes(1));
        
        u32 BlackSquareColor = PackPixel(V4(0,0,0,0));
        u32 WhiteSquareColor = PackPixel(V4(.8f, .8f, .8f, 1));  
        State->BoardBitmap.Width = 8;
        State->BoardBitmap.Height = 8;
        State->BoardBitmap.Pitch = 4 * 8;
        State->BoardBitmap.Memory = PushArray(&State->GlobalArena, 64, u32);
        u32 *DestPixel = (u32 *)State->BoardBitmap.Memory;
        for (u32 Y = 0; Y < 8; ++Y)
        {
            for (u32 X = 0; X < 8; ++X)
            {
                *DestPixel++ = (X & 1) == (Y & 1) ? BlackSquareColor : WhiteSquareColor;
            }
        }
        
        // NOTE(vincent): Bitmap OpenGL handle needs to be reset so OpenGL can actually
        // give it a proper one when we draw it for the first time. 
        // Otherwise OpenGL will eventually draw something else instead of the board bitmap.
        State->BoardBitmap.Handle = 0;
        
        State->IsInitialized = true;
    }
    else
    {
        // NOTE(vincent): Advancing all moving vectors and clocks.
#if 1
        for (u32 GameIndex = 0; GameIndex < State->GamesCount; ++GameIndex)
        {
            chess_game_state *Game = Games + GameIndex;
            AdvanceMovingV2(&Game->Board.P, Input->dtForFrame);
            AdvanceMovingV2(&Game->Board.Dim, Input->dtForFrame);
            for (u32 Index = 0; Index < ArrayCount(Game->Blacks); ++Index)
            {
                AdvanceMovingV2(&Game->Blacks[Index].P, Input->dtForFrame);
                AdvanceMovingV2(&Game->Whites[Index].P, Input->dtForFrame);
            }
            AdvanceMovingV2(&Game->Cursor.P, Input->dtForFrame);
            Game->Cursor.BreatheT += Input->dtForFrame;
            if (Game->Cursor.BreatheT > CursorBreatheTPeriod)
                Game->Cursor.BreatheT -= CursorBreatheTPeriod;
            Game->SelectedPieceT += Input->dtForFrame;
            Game->DestColorT += Input->dtForFrame;
            if (Game->DestColorT > DestColorTPeriod)
                Game->DestColorT -= DestColorTPeriod;
        }
#endif
    }
    
    render_group *Group = &State->RenderGroup;
    PushClear(Group, V4(0,0,0,1));
    
    switch (State->GameMode)
    {
        case GameMode_StartScreen:
        {
            
            if (State->GamesCount < (u32)State->MenuX + 1)
                State->MenuX = (State->GamesCount == 0 ? 0 : State->GamesCount - 1);
            Assert(0 <= State->MenuX);
            s32 OldX = State->MenuX;
            
            if (State->GamesCount)
            {
                s32 XAxisAdd = NewButtonPress(Input->Right) + 
                    NewButtonPress(Input->Forward) - NewButtonPress(Input->Left) -
                    NewButtonPress(Input->Back);
                State->MenuX = Clamp(State->MenuX + XAxisAdd, 0, State->GamesCount-1);
            }
            
            UpdateWrappedCounterOnButtonPress(&State->MenuY, Input->Up, Input->Down, 4);
            
            State->CurrentGameIndex = State->MenuX;
            
            f32 BoardAnimDuration = .2f;
            if (State->ShouldUpdateBoardMovingVectors)
            {
                for (s32 GameIndex = 0; (u32)GameIndex < State->GamesCount; ++GameIndex)
                {
                    InitMovingV2FromCurrent(&Games[GameIndex].Board.Dim, 
                                            V2(0.1f, 0.1f), 
                                            BoardAnimDuration);
                }
            }
            State->ShouldUpdateBoardMovingVectors |= (OldX != State->MenuX);
            
            if (State->ShouldUpdateBoardMovingVectors)
            {
                for (s32 GameIndex = 0; (u32)GameIndex < State->GamesCount; ++GameIndex)
                {
                    InitMovingV2FromCurrent(&Games[GameIndex].Board.P, 
                                            V2(0.3f + 0.12f * (GameIndex - State->MenuX), 0.27f), 
                                            BoardAnimDuration);
                }
                InitMovingV2FromCurrent(&Games[OldX].Board.Dim,
                                        V2(0.1f, 0.1f), BoardAnimDuration);
                InitMovingV2FromCurrent(&Games[State->MenuX].Board.Dim,
                                        V2(0.2f, 0.2f), BoardAnimDuration);
                InitMovingV2FromCurrent(&Games[State->MenuX].Board.P,
                                        V2(0.3f, 0.11f), BoardAnimDuration);
                State->ShouldUpdateBoardMovingVectors = false;
            }
            
            v2 RectMin = V2(0.0f, 0.0f);
            v2 RectMax = V2(0.18f, 0.18f);
            f32 MarginLeft = 0.03f;
            f32 MenuStringX = MarginLeft + RectMin.x;
            f32 MenuStringYDim = 0.25f * (RectMax.y - RectMin.y);
            f32 MenuStringY[4] = 
            {
                RectMin.y + 3.4f*MenuStringYDim,
                RectMin.y + 2.4f*MenuStringYDim,
                RectMin.y + 1.4f*MenuStringYDim,
                RectMin.y + 0.4f*MenuStringYDim,
            };
            
            for (u32 GameIndex = 0; GameIndex < State->GamesCount; ++GameIndex)
            {
                //AssertDestPointersWithinBounds(Games + GameIndex);
                DrawGameBoard(Group, Games + GameIndex, &State->BoardBitmap, State->Assets);
            }
            
            PushRect(Group, RectMin, RectMax, V4(0.1f, 0.1f, 0.2f, 1.0f));
            
            if (State->GamesCount > 0)
            {
                PushNumber(Group, State->MenuX, &State->Assets->Font, 0.0007f, V2(0.48f, 0.1f), 
                           V4(1.0f, 1.0f, 1.0f, 1.0f));
                DrawGameStrings(Group, &State->Assets->Font, Games + State->MenuX, 0.48f, 0.12f);
            }
            
            if (Input->MouseMoved)
            {
                v2 MouseVector = GetMouseVector(Input, Group);
                f32 HackOffset = 0.018f;
                if (MouseVector.y >= MenuStringY[0] - HackOffset)
                    State->MenuY = 0;
                else if (MouseVector.y >= MenuStringY[1] - HackOffset)
                    State->MenuY = 1;
                else if (MouseVector.y >= MenuStringY[2] - HackOffset)
                    State->MenuY = 2;
                else
                    State->MenuY = 3;
            }
            
#define PushMenuItem(String, Index) \
PushText(Group, String, &State->Assets->Font, 0.0007f, V2(MenuStringX, MenuStringY[Index]), State->MenuY == Index ? V4(0.5f, 1.0f, 0.4f, 1.0f) : V4(1.0f, 1.0f, 1.0f, 1.0f))
            PushMenuItem("New game", 0);
            PushMenuItem("Load game", 1);
            PushMenuItem("Duplicate game", 2);
            PushMenuItem("Delete game", 3);
#undef PushMenuItem
            
            if (Input->A.IsPressed && !Input->A.WasPressed)
            {
                switch (State->MenuY)
                {
                    case 0: StartNewGame(State); break;
                    case 1: LoadGame(State, State->MenuX); break;
                    case 2: DuplicateGame(State, State->MenuX); break;
                    case 3: DeleteGame(State, State->MenuX); break;
                    InvalidDefaultCase;
                }
            }
            
            
            
        } break;
        
        case GameMode_Settings:
        {
            UpdateWrappedCounterOnButtonPress(&State->MenuY, Input->Up, Input->Down, 3);
            
            Assert(State->CurrentGameIndex < State->GamesCount);
            chess_game_state *Game = State->Games + State->CurrentGameIndex;
            
            s32 PlayerTypesCount = 10;
            switch (State->MenuY)
            {
                case 0:
                {
                    if (NewButtonPress(Input->Left)) 
                        Game->BlackAI = WrappedDecrement(Game->BlackAI, PlayerTypesCount);
                    if (NewButtonPress(Input->Right) || NewButtonPress(Input->A)) 
                        Game->BlackAI = WrappedIncrement(Game->BlackAI, PlayerTypesCount);
                } break;
                case 1:
                {
                    if (NewButtonPress(Input->Left)) 
                        Game->WhiteAI = WrappedDecrement(Game->WhiteAI, PlayerTypesCount);
                    if (NewButtonPress(Input->Right) || NewButtonPress(Input->A)) 
                        Game->WhiteAI = WrappedIncrement(Game->WhiteAI, PlayerTypesCount);
                } break;
                case 2:
                {
                    if (Input->A.IsPressed && !Input->A.WasPressed)
                        TransitionToGameplay(State, Game);
                } break;
                InvalidDefaultCase;
            }
            
            if (Input->B.IsPressed && !Input->B.WasPressed)
            {
                switch (State->PreviousMode)
                {
                    case GameMode_Pause: TransitionToPause(State, Game); break;
                    case GameMode_StartScreen: TransitionToStartScreen(State); break;
                    InvalidDefaultCase;
                }
            }
            
            DrawGameBoard(Group, State->Games + State->CurrentGameIndex, &State->BoardBitmap, State->Assets);
            PushRect(Group, V2(0,0), Group->ScreenDim, V4(0.1f, 0.1f, 0.1f, 0.8f));
            
            PushText(Group, "Settings", &State->Assets->Font, 0.0012f, V2(0.25f, 0.25f), 
                     V4(1.0f, 1.0f, 1.0f, 1.0f));
            f32 Scale = 0.0008f;
            f32 MarginLeft = 0.1f;
            f32 MarginLeft2 = 0.5f*Group->ScreenDim.x + MarginLeft;
            
            v4 TextColor = V4(1,1,1,1);
            v4 HoveredTextColor = V4(0.5f, 1.0f, 0.4f, 1.0f);
            
            if (Input->MouseMoved)
            {
                f32 TopY = 0.25f;
                v2 MouseVector = GetMouseVector(Input, Group);
                if (MouseVector.y >= TopY - 0.072f)
                    State->MenuY = 0;
                else if (MouseVector.y >= TopY - 0.122f)
                    State->MenuY = 1;
                else
                    State->MenuY = 2;
            }
            
            
            char *BlackAIString = 0;
            switch (Game->BlackAI)
            {
                case 0: BlackAIString = "Human"; break;
                case 1: BlackAIString = "AI 0 random"; break;
                case 2: BlackAIString = "AI 1"; break;
                case 3: BlackAIString = "AI 2"; break;
                case 4: BlackAIString = "AI 3"; break;
                case 5: BlackAIString = "AI 4"; break;
                case 6: BlackAIString = "AI 5"; break;
                case 7: BlackAIString = "AI 6"; break;
                case 8: BlackAIString = "AI 7"; break;
                case 9: BlackAIString = "AI 8"; break;
                InvalidDefaultCase;
            }
            char *WhiteAIString = 0;
            switch (Game->WhiteAI)
            {
                case 0: WhiteAIString = "Human"; break;
                case 1: WhiteAIString = "AI 0 random"; break;
                case 2: WhiteAIString = "AI 1"; break;
                case 3: WhiteAIString = "AI 2"; break;
                case 4: WhiteAIString = "AI 3"; break;
                case 5: WhiteAIString = "AI 4"; break;
                case 6: WhiteAIString = "AI 5"; break;
                case 7: WhiteAIString = "AI 6"; break;
                case 8: WhiteAIString = "AI 7"; break;
                case 9: WhiteAIString = "AI 8"; break;
                InvalidDefaultCase;
            }
            
            PushText(Group, "Black player type", &State->Assets->Font, Scale,
                     V2(MarginLeft, 0.2f), 
                     State->MenuY == 0 ? HoveredTextColor : TextColor);
            
            PushText(Group, BlackAIString, &State->Assets->Font, Scale,
                     V2(MarginLeft2, 0.2f), 
                     State->MenuY == 0 ? HoveredTextColor : TextColor);
            
            PushText(Group, "White player type", &State->Assets->Font, Scale,
                     V2(MarginLeft, 0.15f), 
                     State->MenuY == 1 ? HoveredTextColor : TextColor);
            
            PushText(Group, WhiteAIString, &State->Assets->Font, Scale,
                     V2(MarginLeft2, 0.15f), 
                     State->MenuY == 1 ? HoveredTextColor : TextColor);
            
            PushText(Group, "Done", &State->Assets->Font, Scale,
                     V2(0.27f, 0.1f), 
                     State->MenuY == 2 ? HoveredTextColor : TextColor);
            
            //AssertDestPointersWithinBounds(Game);
        } break;
        
        case GameMode_Pause:
        {
            UpdateWrappedCounterOnButtonPress(&State->MenuY, Input->Up, Input->Down, 3);
            
            DrawGameBoard(Group, State->Games + State->CurrentGameIndex, &State->BoardBitmap, State->Assets);
            PushRect(Group, V2(0,0), Group->ScreenDim, V4(0.1f, 0.1f, 0.1f, 0.8f));
            
            f32 TopY = 0.25f;
            PushText(Group, "Pause", &State->Assets->Font, 0.0012f, V2(0.25f, TopY),
                     V4(1,1,1,1));
            
            f32 Scale = 0.0008f;
            f32 MarginLeft = 0.215f;
            
            v4 TextColor = V4(1,1,1,1);
            v4 HoveredTextColor = V4(0.5f, 1.0f, 0.4f, 1.0f);
            
            if (Input->MouseMoved)
            {
                v2 MouseVector = GetMouseVector(Input, Group);
                if (MouseVector.y >= TopY - 0.072f)
                    State->MenuY = 0;
                else if (MouseVector.y >= TopY - 0.122f)
                    State->MenuY = 1;
                else 
                    State->MenuY = 2;
            }
            
            PushText(Group, "Resume", &State->Assets->Font, Scale, 
                     V2(MarginLeft, TopY - 0.05f),
                     State->MenuY == 0 ? HoveredTextColor : TextColor);
            PushText(Group, "Game settings", &State->Assets->Font, Scale,
                     V2(MarginLeft, TopY - 0.1f), 
                     State->MenuY == 1 ? HoveredTextColor : TextColor);
            PushText(Group, "Back to start screen", &State->Assets->Font, Scale,
                     V2(MarginLeft, TopY - 0.15f), 
                     State->MenuY == 2 ? HoveredTextColor : TextColor);
            
            Assert(State->CurrentGameIndex < State->GamesCount);
            chess_game_state *CurrentGame = State->Games + State->CurrentGameIndex;
            if (Input->B.IsPressed && !Input->B.WasPressed)
            {
                TransitionToGameplay(State, CurrentGame);
            }
            else if (Input->A.IsPressed && !Input->A.WasPressed)
            {
                switch (State->MenuY)
                {
                    case 0: TransitionToGameplay(State, CurrentGame); break;
                    case 1: TransitionToSettings(State, CurrentGame); break;
                    case 2: TransitionToStartScreen(State); break;
                    InvalidDefaultCase;
                }
            }
            
        } break;
        
        case GameMode_Gameplay:
        {
            Assert(State->GamesCount > 0);
            Assert(State->CurrentGameIndex < State->GamesCount);
            
            chess_game_state *Game = State->Games + State->CurrentGameIndex;
            
            Assert((s32)(Game->CurrentEntryIndex & 1) == Game->BlackIsPlaying ||
                   Game->AIState.Stage == 3);
            
            chess_piece *Blacks = Game->Blacks;
            chess_piece *Whites = Game->Whites;
            
            b32 PlayerIsHuman = Game->BlackIsPlaying ? (!Game->BlackAI) : (!Game->WhiteAI);
            
            Game->PieceOnCursor = 
                GetPiece(Blacks, Whites, Game->Cursor.Row, Game->Cursor.Column);
            
            if (PlayerIsHuman || Game->GameIsOver)
            {
                if (Game->PromotingPawn)
                {
                    UpdateWrappedCounterOnButtonPress(&State->MenuX, Input->Left,
                                                      Input->Right, 4);
                    UpdateWrappedCounterOnButtonPress(&State->MenuX, Input->Back,
                                                      Input->Forward, 4);
                    chess_piece_type ChosenType;
                    switch (State->MenuX)
                    {
                        case 0: ChosenType = ChessPieceType_Rook; break;
                        case 1: ChosenType = ChessPieceType_Knight; break;
                        case 2: ChosenType = ChessPieceType_Bishop; break;
                        case 3: ChosenType = ChessPieceType_Queen; break;
                        InvalidDefaultCase;
                    }
                    Game->PieceOnCursor.Piece->Type = ChosenType;
                }
                else
                    UpdateCursor(Input, Game, Group, &State->RepeatClocks);
                
                if (Game->GameIsOver)
                {
                    if (InputRepeatClockAdvance(&State->RepeatClocks.Forward, 
                                                Input->Forward.IsPressed, Input->dtForFrame))
                        GameHistoryMoveForward(Game);
                    if (InputRepeatClockAdvance(&State->RepeatClocks.Back, 
                                                Input->Back.IsPressed, Input->dtForFrame))
                        GameHistoryMoveBack(Game);
                }
            }
            else
            {
                AdvanceAIAction(State, Game, Input->dtForFrame, &State->Series, &State->AIArena,
                                Memory->Queue);
            }
            
            chess_piece *OldSelectedPiece = Game->SelectedPiece.Piece;
            
            if (PlayerIsHuman || Game->GameIsOver)
            {
                if (Input->A.IsPressed && !Input->A.WasPressed)
                {
                    if (Game->PromotingPawn)
                    {
                        // Finish setting history entry for pawn promotion type
                        history_entry *Entry = 
                            Game->History.Entries + Game->History.EntryCount-1;
                        SetPromotionBits(Entry, State->MenuX);
                        Game->PromotingPawn = false;
                        MovePieceAfterwork(Game);
                        State->ShouldSave = true;
                    }
                    else
                    {
                        if (!Game->GameIsOver && Game->SelectedPiece.Piece)
                        {
                            b32 ActuallyMoved = MoveSelectedPieceToCursorIfLegal(Game);
                            if (ActuallyMoved)
                                State->ShouldSave = true;
                        }
                        Game->SelectedPiece = Game->PieceOnCursor;
                    }
                }
                
                if (Input->B.IsPressed && !Input->B.WasPressed)
                {
                    if (!Game->PromotingPawn && Game->SelectedPiece.Piece)
                        Game->SelectedPiece.Piece = 0;
                    else
                        TransitionToPause(State, Game);
                }
            }
            else
            {
                if (Input->B.IsPressed && !Input->B.WasPressed)
                {
                    TransitionToPause(State, Game);
                }
            }
            
            if (OldSelectedPiece != Game->SelectedPiece.Piece)
                Game->SelectedPieceT = 0.0f;
            
            DrawGameBoard(Group, Game, &State->BoardBitmap, State->Assets);
            
#define PushNum(Number, Vector) \
PushNumber(Group, Number, &State->Assets->Font, 0.0007f, Vector, V4(1,1,1,1))
            
            // NOTE(vincent): Turn this on for visualizing joystick values, mouse values and more!
#if 0
            u32 BlacksDestCount = 0;
            for (u32 i = 0; i < ArrayCount(Game->Blacks); ++i)
            {
                BlacksDestCount += Game->Blacks[i].DestinationsCount;
            }
            u32 WhitesDestCount = Game->DestinationsCount - BlacksDestCount;
            PushNum(Game->DestinationsCount, V2(0.05f, 0.08f));
            PushNum(BlacksDestCount, V2(0.08f, 0.08f)); 
            PushNum(WhitesDestCount, V2(0.11f, 0.08f)); 
            
            if (Game->PieceOnCursor.Piece)
            {
                PushNum(Game->PieceOnCursor.Piece->Row, V2(0.05f, 0.1f));
                PushNum(Game->PieceOnCursor.Piece->Column, V2(0.07f, 0.1f));
            }
            if (Game->SelectedPiece.Piece)
            {
                chess_piece *SPiece = Game->SelectedPiece.Piece;
                PushNum(SPiece->Row, V2(0.05f, 0.12f));
                PushNum(SPiece->Column, V2(0.07f, 0.12f));
                PushNum(SPiece->DestinationsCount, V2(0.07f, 0.14f));
            }
            
            if (Input->MouseMoved)
            {
                PushText(Group, "Mouse moved", &State->Assets->Font, 0.0007f, V2(0.03f, 0.2f),
                         V4(1,1,1,1));
            }
            
            PushNum(Input->MouseX*1000.0f, V2(0.03f, 0.22f));
            PushNum(Input->MouseY*1000.0f, V2(0.06f, 0.22f));
            PushNum(AbsoluteValue(Input->ThumbLX*1000.0f),
                    V2(0.00f, 0.24f + 0.01f*(Input->ThumbLX >= 0.0f)));
            PushNum(AbsoluteValue(Input->ThumbLY*1000.0f), 
                    V2(0.06f, 0.24f + 0.01f*(Input->ThumbLY >= 0.0f)));
            
            v2 RectStickBGP = V2(0.025f, 0.31f);
            v2 RectStickBGDim = V2(0.045f, 0.045f);
            
            v2 LStickVector = V2(Input->ThumbLX, Input->ThumbLY);
            v2 RectStickP = RectStickBGP + Hadamard(LStickVector, 0.5f*RectStickBGDim);
            v2 RectStickDim = V2(0.01f, 0.01f);
            PushRect(Group, RectCenterDim(RectStickBGP, RectStickBGDim), 
                     V4(0.2f, 0.2f, 0.2f, 0.9f));
            PushRect(Group, RectCenterDim(RectStickP, RectStickDim), 
                     V4(0.8f, 0.8f, 0.8f, 0.9f));
            
#endif
            DrawGameStrings(Group, &State->Assets->Font, Game, 0.01f, 0.15f);
        } break;
        InvalidDefaultCase;
    }
    
    
    if (State->ShouldSave)
    {
        State->ShouldSave = false;
        temporary_memory SaveTempMemory = BeginTemporaryMemory(&State->GlobalArena);
        game_state *CopyGameState = PushStruct(&State->GlobalArena, game_state);
        *CopyGameState = *State;
        
        // NOTE(vincent): Transform absolute nonzero pointers to pointer offsets,
        // so that they can be reloaded in another instance of the process where
        // the base address of the game's memory is different.
        
        for (u32 i = 0; i < CopyGameState->GamesCount; ++i)
        {
            chess_game_state *Game = CopyGameState->Games + i;
            Assert(Game->AIState.Stage != 1);
            chess_game_state *OriginalGame = State->Games + i;
            if (Game->SelectedPiece.Piece)
            {
                Game->SelectedPiece.Piece =
                    (chess_piece *)((u8*)Game->SelectedPiece.Piece - (u8*)OriginalGame);
            }
            if (Game->PieceOnCursor.Piece)
            {
                Game->PieceOnCursor.Piece =
                    (chess_piece *)((u8*)Game->PieceOnCursor.Piece - (u8*)OriginalGame);
            }
            for (u32 PieceIndex = 0; PieceIndex < 16; ++PieceIndex)
            {
                if (Game->Blacks[PieceIndex].Destinations)
                {
                    Game->Blacks[PieceIndex].Destinations =
                        (destination *)((u8 *)Game->Blacks[PieceIndex].Destinations - (u8 *)OriginalGame);
                }
                if (Game->Whites[PieceIndex].Destinations)
                {
                    Game->Whites[PieceIndex].Destinations =
                        (destination *)((u8 *)Game->Whites[PieceIndex].Destinations - (u8 *)OriginalGame);
                }
            }
        }
        CopyGameState->IsInitialized = false;
        
        GlobalPlatform->WriteFile("chess_save", sizeof(game_state), CopyGameState);
        
        EndTemporaryMemory(SaveTempMemory);
    }
}
