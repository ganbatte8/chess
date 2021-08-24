#include <stdint.h>

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int b32;
typedef float f32;
typedef double f64;

#define global_variable static
#define internal static
#define Kilobytes(Value) ((Value) * 1000LL)
#define Megabytes(Value) (Kilobytes(Value) * 1000LL)
#define Gigabytes(Value) (Megabytes(Value) * 1000LL)
#define Terabytes(Value) (Gigabytes(Value) * 1000LL)

#if DEBUG
#define InvalidCodePath {*(int*)0 = 0;}
#define Assert(Expression) if (!(Expression)) InvalidCodePath;
#define InvalidDefaultCase default: InvalidCodePath;
#else
#define Assert(Expression)
#define InvalidCodePath
#define InvalidDefaultCase default: {}
#endif

#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))
#define Align8(Count)  (((Count) + 7) & ~7)
#define Align2(Count)  (((Count) + 1) & ~1)

struct memory_arena
{
    u32 Size;
    u8 *Base;
    u32 Used;
    u32 TempCount;
};

inline void
InitializeArena(memory_arena *Arena, u32 Size, void *Base)
{
    Arena->Size = Size;
    Arena->Base = (u8 *)Base;
    Arena->Used = 0;
    Arena->TempCount = 0;
}

inline void *
PushSize(memory_arena *Arena, u32 Size)
{
    Assert((Arena->Used + Size) <= Arena->Size);
    void *Result = Arena->Base + Arena->Used;
    Arena->Used += Size;
    return Result;
}

#define PushStruct(Arena, type) (type *)PushSize(Arena, sizeof(type))
#define PushArray(Arena, Count, type) (type *)PushSize(Arena, (Count)*sizeof(type))

struct temporary_memory
{
    memory_arena *Arena;
    u32 Used;
};

inline temporary_memory
BeginTemporaryMemory(memory_arena *Arena)
{
    temporary_memory Result;
    Result.Arena = Arena;
    Result.Used = Arena->Used;
    ++Arena->TempCount;
    return Result;
}

inline void
EndTemporaryMemory(temporary_memory Temp)
{
    Assert(Temp.Arena->Used >= Temp.Used);
    Temp.Arena->Used = Temp.Used;
    Assert(Temp.Arena->TempCount > 0);
    Temp.Arena->TempCount--;
}

inline void
CommitTemporaryMemory(temporary_memory Temp)
{
    Assert(Temp.Arena->Used >= Temp.Used);
    Assert(Temp.Arena->TempCount > 0);
    Temp.Arena->TempCount--;
}

inline void
CheckArena(memory_arena *Arena)
{
    Assert(Arena->TempCount == 0);
}

internal void
SubArena(memory_arena *Result, memory_arena *Arena, u32 Size)
{
    Result->Size = Size;
    Result->Base = (u8 *)PushSize(Arena, Size);
    Result->Used = 0;
    Result->TempCount = 0;
}

internal b32
BytesAreZero(char *Buffer, u32 BytesCount)
{
    for (u32 Byte = 0; Byte < BytesCount; ++Byte)
    {
        if (Buffer[Byte])
            return false;
    }
    return true;
}

internal b32
BytesAreEqual(u8 *A, u8 *B, u32 BytesCount)
{
    b32 Result = true;
    for (u32 i = 0; i < BytesCount; ++i)
    {
        if (*A++ != *B++)
        {
            Result = false;
            break;
        }
    }
    return Result;
}
internal void
ZeroBytes(u8 *Buffer, u32 BytesCount)
{
    for (u32 Byte = 0; Byte < BytesCount; ++Byte)
        Buffer[Byte] = 0;
}

internal void
MemCopy(u8 *Source, u8 *Dest, u32 Size)
{
    for (u32 i = 0; i < Size; ++i)
        *Dest++ = *Source++;
}

#define ZeroStruct(Instance) ZeroBytes(&(Instance), (sizeof(Instance)))

#define THREAD_COUNT 8
#define BYTES_PER_PIXEL 4
#if COMPILER_MSVC
#include <intrin.h>
#elif COMPILER_GCC
#include <x86intrin.h>
//#include <xmmintrin.h>
#endif

struct game_backbuffer
{
    void *Memory;
    s32 Width;
    s32 Height;
    s32 Pitch;
};

struct game_render_commands
{
    u32 Width;
    u32 Height;
    
    // TODO(vincent): memory arena here?
    u32 MaxPushBufferSize;
    u32 PushBufferSize;
    u8 *PushBufferBase;
};

#define RenderCommandsStruct(Width, Height, MaxPushBufferSize, PushBuffer) \
{Width, Height, MaxPushBufferSize, 0, (u8 *)PushBuffer};


struct platform_work_queue;
#define PLATFORM_WORK_QUEUE_CALLBACK(name) void name(platform_work_queue *Queue, void *Data)
typedef PLATFORM_WORK_QUEUE_CALLBACK(platform_work_queue_callback);
typedef void platform_add_entry(platform_work_queue *Queue, platform_work_queue_callback *Callback, void *Data);
typedef void platform_complete_all_work(platform_work_queue *Queue);
struct platform_work_queue_entry
{
    platform_work_queue_callback *Callback;
    void *Data;
};

#define PLATFORM_WRITE_FILE(name) b32 name(char *Filename, u32 Size, void *Memory)
typedef PLATFORM_WRITE_FILE(platform_write_file);

struct string
{
    char *Base;
    u32 Size;
};

#define PLATFORM_PUSH_READ_FILE(name) string name(memory_arena *Arena, char *Filename)
typedef PLATFORM_PUSH_READ_FILE(platform_push_read_file);


struct platform_api
{
    platform_add_entry *AddEntry;
    platform_complete_all_work *CompleteAllWork;
    platform_write_file *WriteFile;
    platform_push_read_file *PushReadFile;
};

struct game_memory
{
    void *Storage;
    u32 StorageSize;
    
    platform_work_queue *Queue;
    platform_api Platform;
};


struct button_state
{
    b32 IsPressed;
    b32 WasPressed;
    b32 AutoRelease;
};

struct game_input
{
    union
    {
        struct
        {
            button_state A;
            button_state B;
            
            button_state K;
            button_state T;
            button_state N;
            button_state KeyB;
            button_state R;
            button_state P;
            
            button_state Left;
            button_state Down;
            button_state Up;
            button_state Right;
            
            button_state Back;
            button_state Forward;
        };
        button_state AllButtons[14];
    };
    
    f32 MouseX;   // [0.0f, 1.0f]. Left to right
    f32 MouseY;   // Bottom to top
    b32 MouseMoved;
    
    f32 ThumbLX;  // [-1.0f, 1.0f]. Left to right.
    f32 ThumbLY;  // Bottom to top
    
    f32 dtForFrame;
    b32 ReloadedDLL;
};

internal s32
RoundFS(f32 X)
{
    s32 Result = (s32) (X + 0.5f);
    return Result;
}

internal u32
RoundFU(f32 X)
{
    u32 Result = (u32) (X + 0.5f);
    return Result;
}

internal u16
RoundF32ToU16(f32 X)
{
    u16 Result = (u16) (X + 0.5f);
    return Result;
}

#define GAME_UPDATE(name) void name(game_memory *Memory, game_input *Input, game_render_commands *RenderCommands)
typedef GAME_UPDATE(game_update);


internal b32
IsWhitespace(char C)
{
    b32 Result = false;
    if (C == ' ' || C == '\t' || C == '\v' || C == '\f' || C == '\n' || C == '\r')
        Result = true;
    return Result;
}

#include "math.h"
#include "render_entries.h"