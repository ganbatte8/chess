#include <windows.h>
#include <intrin.h>
#include "common.h"
#include <gl/gl.h>
#include "chess_opengl.cpp"

#include <xinput.h>


struct win32_backbuffer
{
    BITMAPINFO Info;
    void *Memory;
    s32 Width;
    s32 Height;
    s32 Pitch;
};

typedef BOOL WINAPI wgl_swap_interval_ext(int Interval);
global_variable wgl_swap_interval_ext *wglSwapInterval;

typedef HGLRC WINAPI wgl_create_context_attribs_arb(HDC hDC, HGLRC hShareContext, const int *AttribList);

global_variable win32_backbuffer GlobalBackbuffer;
global_variable b32 GlobalRunning;
global_variable WINDOWPLACEMENT GlobalWindowPosition = {sizeof(GlobalWindowPosition)};
global_variable LARGE_INTEGER GlobalCountsPerSecond;
global_variable game_input GlobalGameInput;

// NOTE(vincent): XInput  (SetState and GetState. Not sure we'll use SetState)
#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE *pState)
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(XInputGetStateStub)
{
    return(ERROR_DEVICE_NOT_CONNECTED);
}
global_variable x_input_get_state *XInputGetState_ = XInputGetStateStub;
#define XInputGetState XInputGetState_

#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(XInputSetStateStub)
{
    return(ERROR_DEVICE_NOT_CONNECTED);
}
global_variable x_input_set_state *XInputSetState_ = XInputSetStateStub;
#define XInputSetState XInputSetState_

internal void
Win32LoadXInput(void)
{
    HMODULE XInputLibrary = LoadLibraryA("xinput_1_4.dll");
    if (!XInputLibrary)
        XInputLibrary = LoadLibraryA("xinput9_1_0.dll");
    if (!XInputLibrary)
        XInputLibrary = LoadLibraryA("xinput1_3.dll");
    if (XInputLibrary)
    {
        XInputGetState = (x_input_get_state *)GetProcAddress(XInputLibrary, "XInputGetState");
        if (!XInputGetState) XInputGetState = XInputGetStateStub;
        XInputSetState = (x_input_set_state *)GetProcAddress(XInputLibrary, "XInputSetState");
        if(!XInputSetState) {XInputSetState = XInputSetStateStub;}
    }
}


PLATFORM_PUSH_READ_FILE(Win32PushReadFile)
{
    string Result = {};
    
#if 1
    HANDLE CreateFileHandle =  
        CreateFileA(Filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (CreateFileHandle != INVALID_HANDLE_VALUE)
    {
        Result.Size = GetFileSize(CreateFileHandle, 0);
        u32 AvailableSize = Arena->Size - Arena->Used;
        if (Result.Size <= AvailableSize)
        {
            Result.Base = PushArray(Arena, Result.Size, char);
            DWORD WrittenSize = 0;
            ReadFile(CreateFileHandle, Result.Base, Result.Size, &WrittenSize, 0);
            
            if (WrittenSize != Result.Size)
                Result.Base = 0;
        }
    }
    CloseHandle(CreateFileHandle);
#else
    FILE *File = fopen(Filename, "rb");
    if (File)
    {
        fseek(File, 0, SEEK_END);
        Result.Size = ftell(File);
        fseek(File, 0, SEEK_SET);
        u32 AvailableSize = Arena->Size - Arena->Used;
        if (Result.Size <= AvailableSize)
        {
            Result.Base = PushArray(Arena, (u32)Result.Size, char);
            size_t BytesWritten = fread(Result.Base, 1, Result.Size, File);
            if (BytesWritten != Result.Size)
                Result.Base = 0;
        }
        fclose(File);
    }
#endif
    return Result;
}

PLATFORM_WRITE_FILE(Win32WriteFile)
{
    b32 Result = false;
    
#if 1
    HANDLE CreateFileHandle = CreateFileA(Filename, GENERIC_WRITE, FILE_SHARE_WRITE, 0, 
                                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (CreateFileHandle != INVALID_HANDLE_VALUE)
    {
        DWORD WrittenSize = 0;
        WriteFile(CreateFileHandle, Memory, Size, &WrittenSize, 0);
        if (WrittenSize == Size)
            Result = true;
    }
    CloseHandle(CreateFileHandle);
#else
    
    FILE *File = fopen(Filename, "wb");
    if (File)
    {
        size_t BytesWritten = fwrite(Memory, 1, Size, File);
        fclose(File);
        if (BytesWritten == Size)
            Result = true;
    }
    
#endif
    return Result;
}

struct win32_game_code
{
#define DLL_FILENAME "chess.dll"
#define DLL_COPY_FILENAME "chess_copy.dll"
    game_update *Update;
    HMODULE DLL;
    FILETIME DLLWriteTime;
    WIN32_FILE_ATTRIBUTE_DATA DLLFileData;
};

internal void
Win32LoadGameCode(win32_game_code *Code)
{
    CopyFile(DLL_FILENAME, DLL_COPY_FILENAME, FALSE); // FALSE to overwrite
    Code->DLL = LoadLibraryA(DLL_COPY_FILENAME);
    Code->Update = (game_update *)GetProcAddress(Code->DLL, "GameUpdate");
    Assert(Code->DLL && Code->Update);
    if (GetFileAttributesExA(DLL_FILENAME, GetFileExInfoStandard, &Code->DLLFileData))
        Code->DLLWriteTime = Code->DLLFileData.ftLastWriteTime;
}

internal void
ToggleFullScreen(HWND Window)
{
    // NOTE(vincent): Raymond Chen's blog article on fullscreen:
    // https://blogs.msdn.com/b/oldnewthing/archive/2010/04/12/9994016.aspx
    DWORD Style = GetWindowLong(Window, GWL_STYLE);
    if (Style & WS_OVERLAPPEDWINDOW)
    {
        MONITORINFO MonitorInfo = { sizeof(MonitorInfo) };
        if (GetWindowPlacement(Window, &GlobalWindowPosition) &&
            GetMonitorInfo(MonitorFromWindow(Window, MONITOR_DEFAULTTOPRIMARY), &MonitorInfo))
        {
            SetWindowLong(Window, GWL_STYLE, Style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(Window, HWND_TOP,
                         MonitorInfo.rcMonitor.left, MonitorInfo.rcMonitor.top,
                         MonitorInfo.rcMonitor.right - MonitorInfo.rcMonitor.left,
                         MonitorInfo.rcMonitor.bottom - MonitorInfo.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    }
    else
    {
        SetWindowLong(Window, GWL_STYLE, Style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(Window, &GlobalWindowPosition);
        SetWindowPos(Window, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
}

global_variable GLuint GlobalBlitTextureHandle;
internal void
Win32DisplayBufferInWindow(platform_work_queue *Queue, game_render_commands *Commands, HWND Window,
                           game_memory *Memory)
{
    RECT ClientRect;
    GetClientRect(Window, &ClientRect);
    HDC DeviceContext = GetDC(Window);
    s32 WindowWidth = ClientRect.right - ClientRect.left;
    s32 WindowHeight = ClientRect.bottom - ClientRect.top;
    b32 InHardware = true;
    if (InHardware)
    {
        OpenGLRenderToOutput(Commands, WindowWidth, WindowHeight);
        SwapBuffers(DeviceContext);
    }
    
    ReleaseDC(Window, DeviceContext);
    Commands->PushBufferSize = 0;
}


#include <Windowsx.h> // For GET_X_LPARAM()

LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    switch (Message)
    {
        case WM_MOUSEMOVE:
        {
            GlobalGameInput.MouseMoved = true;
            
            // NOTE(vincent): GetClientRect is not the same as GetWindowRect. 
            RECT ClientRect;
            GetClientRect(Window, &ClientRect);
            
            u32 ClientWidth = ClientRect.right - ClientRect.left;
            u32 ClientHeight = ClientRect.bottom - ClientRect.top;
            
            s32 MouseClientX = GET_X_LPARAM(lParam);
            s32 MouseClientY = GET_Y_LPARAM(lParam);
            // NOTE(vincent): GET_X_LPARAM() returns some value in client space.
            // On my 1920*1080 monitor, in full screen mode, this maps to a range
            // of X values between 0 and 1919 inclusive.
            // Since the game is not aware about this client space, we want to remap
            // that to resolution space.
            
            // NOTE(vincent): Made these fire once, so I'm clamping.
            //Assert((s32)ClientWidth > MouseClientX);
            //Assert((s32)ClientHeight > MouseClientY);
            GlobalGameInput.MouseX = Clamp01(SafeRatio0((f32)MouseClientX, 
                                                        (f32)(ClientWidth-1)));
            GlobalGameInput.MouseY = Clamp01(1.0f - SafeRatio0((f32)MouseClientY, 
                                                               (f32)(ClientHeight-1)));
        } break;
        case WM_LBUTTONDOWN: GlobalGameInput.A.IsPressed = true; 
        GlobalGameInput.A.AutoRelease = false; break;
        case WM_LBUTTONUP: GlobalGameInput.A.IsPressed = false; 
        GlobalGameInput.A.AutoRelease = false; break;
        case WM_RBUTTONDOWN: GlobalGameInput.B.IsPressed = true; 
        GlobalGameInput.B.AutoRelease = false; break;
        case WM_RBUTTONUP: GlobalGameInput.B.IsPressed = false; 
        GlobalGameInput.B.AutoRelease = false; break;
        case WM_MOUSEWHEEL:
        {
            s32 MouseWheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (MouseWheelDelta < 0)
            {
                GlobalGameInput.Back.IsPressed = true;
                GlobalGameInput.Back.AutoRelease = true;
            }
            else if (MouseWheelDelta > 0)
            {
                GlobalGameInput.Forward.IsPressed = true;
                GlobalGameInput.Forward.AutoRelease = true;
            }
        } break;
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
            u32 VKCode = (u32)wParam;
            
            b32 WasDown = ((lParam & (1 << 30)) != 0);
            b32 IsDown = ((lParam & (1 << 31)) == 0);
            b32 AltKeyWasDown = (lParam & (1 << 29));
            if (VKCode == VK_F4 && AltKeyWasDown) GlobalRunning = false;
            
            if (WasDown != IsDown)
            {
                switch (VKCode)
                {
#define SetPressed(KeyCode, Button) \
case KeyCode: GlobalGameInput.Button.IsPressed = IsDown; GlobalGameInput.Button.AutoRelease = false; break;
                    SetPressed(VK_OEM_1, A);
                    SetPressed(0x51, B);
                    
                    SetPressed(0x4B, K);
                    SetPressed(0x54, T);
                    SetPressed(0x4E, N);
                    SetPressed(0x42, KeyB);
                    SetPressed(0x52, R);
                    SetPressed(0x50, P);
                    
                    SetPressed(VK_LEFT,  Left);
                    SetPressed(VK_DOWN,  Down);
                    SetPressed(VK_UP,    Up);
                    SetPressed(VK_RIGHT, Right);
                    
                    SetPressed(VK_RETURN, Forward);
                    SetPressed(VK_BACK, Back);
#undef SetPressed
                    case VK_F11: if(IsDown) ToggleFullScreen(Window); break;
                }
            }
        } break;
        
        case WM_QUIT: GlobalRunning = false; break;
        case WM_DESTROY: PostQuitMessage(0); GlobalRunning = false; break;
        case WM_ACTIVATEAPP: break;
        
        case WM_PAINT:
        {
#if 1
            PAINTSTRUCT Paint;
            BeginPaint(Window, &Paint);
            EndPaint(Window, &Paint);
#endif
        } break;
        
        default: return DefWindowProc(Window, Message, wParam, lParam);
    }
    return 0;
}

internal void
Win32PrepareBackbuffer(win32_backbuffer *Buffer, s32 Width, s32 Height)
{
    Buffer->Width = Width;
    Buffer->Height = Height;
    Buffer->Pitch = Width*4;
    Buffer->Memory = VirtualAlloc(0, Width*Height*4, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
    Buffer->Info.bmiHeader.biWidth = Buffer->Width;
    Buffer->Info.bmiHeader.biHeight = Buffer->Height;
    Buffer->Info.bmiHeader.biPlanes = 1;
    Buffer->Info.bmiHeader.biBitCount = 32;
    Buffer->Info.bmiHeader.biCompression = BI_RGB;
}

internal LARGE_INTEGER
Win32GetWallClock()
{
    LARGE_INTEGER Timestamp;
    QueryPerformanceCounter(&Timestamp);
    return Timestamp;
}

internal f32
Win32GetSecondsElapsed(LARGE_INTEGER StartClock, LARGE_INTEGER EndClock)
{
    f32 Result = (f32)(EndClock.QuadPart - StartClock.QuadPart) / (f32)GlobalCountsPerSecond.QuadPart;
    return Result;
}

internal f32
Win32GetSecondsElapsedF64(LARGE_INTEGER StartClock, LARGE_INTEGER EndClock)
{
    f64 Result = (f64)(EndClock.QuadPart - StartClock.QuadPart) / (f64)GlobalCountsPerSecond.QuadPart;
    return Result;
}

struct platform_work_queue
{
    u32 volatile CompletionCount;
    u32 volatile CompletionGoal;
    u32 volatile WriteIndex;
    u32 volatile FetchIndex;
    HANDLE SemaphoreHandle;
    platform_work_queue_entry Entries[256];
};

internal void
Win32AddEntry(platform_work_queue *Queue, platform_work_queue_callback *Callback, void *Data)
{
    u32 NextWriteIndex = (Queue->WriteIndex + 1) % ArrayCount(Queue->Entries);
    Assert(NextWriteIndex != Queue->FetchIndex);
    platform_work_queue_entry *Entry = Queue->Entries + Queue->WriteIndex;
    Entry->Callback = Callback;
    Entry->Data = Data;
    ++Queue->CompletionGoal;
    // _WriteBarrier();
    Queue->WriteIndex = NextWriteIndex;
    ReleaseSemaphore(Queue->SemaphoreHandle, 1, 0); // increase semaphore count so a thread can wake up
}

internal b32
Win32DoNextWorkQueueEntry(platform_work_queue *Queue)
{
    b32 WeShouldSleep = true;
    u32 OriginalFetch = Queue->FetchIndex;
    if (OriginalFetch != Queue->WriteIndex)
    {
        WeShouldSleep = false;
        u32 NextFetch = (OriginalFetch + 1) % ArrayCount(Queue->Entries);
        u32 Index = InterlockedCompareExchange((LONG volatile *)&Queue->FetchIndex,
                                               NextFetch, OriginalFetch);
        if (Index == OriginalFetch)
        {
            platform_work_queue_entry Entry = Queue->Entries[Index];
            Entry.Callback(Queue, Entry.Data);
            InterlockedIncrement((LONG volatile *) &Queue->CompletionCount);
        }
    }
    return WeShouldSleep;
}

internal void
Win32CompleteAllWork(platform_work_queue *Queue)
{
    while (Queue->CompletionGoal != Queue->CompletionCount)
        Win32DoNextWorkQueueEntry(Queue);
    Queue->CompletionCount = 0;
    Queue->CompletionGoal = 0;
}

DWORD WINAPI
ThreadProc(LPVOID lpParameter)
{
    platform_work_queue *Queue = (platform_work_queue *)lpParameter;
    for (;;)
    {
        if (Win32DoNextWorkQueueEntry(Queue))
        {
            WaitForSingleObjectEx(Queue->SemaphoreHandle, INFINITE, FALSE);
        }
    }
}

internal void
Win32MakeQueue(platform_work_queue *Queue, u32 ThreadCount)
{
    Queue->CompletionGoal = 0;
    Queue->CompletionCount = 0;
    Queue->FetchIndex = 0;
    Queue->WriteIndex = 0;
    u32 InitialCount = 0;
    Queue->SemaphoreHandle = CreateSemaphoreEx(0, InitialCount, ThreadCount, 0, 0, SEMAPHORE_ALL_ACCESS);
    for (u32 ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
    {
        DWORD ThreadID;
        HANDLE ThreadHandle = CreateThread(0, 0, ThreadProc, Queue, 0, &ThreadID);
        CloseHandle(ThreadHandle);
    }
}

internal void
Win32InitOpenGL(HWND Window)
{
    HDC WindowDC = GetDC(Window);
    PIXELFORMATDESCRIPTOR DesiredPixelFormat = {};
    DesiredPixelFormat.nSize = sizeof(DesiredPixelFormat);
    DesiredPixelFormat.nVersion = 1;
    DesiredPixelFormat.dwFlags = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER;
    DesiredPixelFormat.cColorBits = 32;
    DesiredPixelFormat.cAlphaBits = 8;
    DesiredPixelFormat.iLayerType = PFD_MAIN_PLANE;
    DesiredPixelFormat.iPixelType = PFD_TYPE_RGBA;
    
    int SuggestedPixelFormatIndex = ChoosePixelFormat(WindowDC, &DesiredPixelFormat);
    PIXELFORMATDESCRIPTOR SuggestedPixelFormat;
    DescribePixelFormat(WindowDC, SuggestedPixelFormatIndex, sizeof(SuggestedPixelFormat),
                        &SuggestedPixelFormat);
    SetPixelFormat(WindowDC, SuggestedPixelFormatIndex, &SuggestedPixelFormat);
    
    HGLRC OpenGLRC = wglCreateContext(WindowDC);
    if (wglMakeCurrent(WindowDC, OpenGLRC))
    {
        b32 ModernContext = false;
        // NOTE(vincent): Successfully set OpenGL context
        
        wgl_create_context_attribs_arb *wglCreateContextAttribsARB =
        (wgl_create_context_attribs_arb *)wglGetProcAddress("wglCreateContextAttribsARB");
        
        if (wglCreateContextAttribsARB)
        {
            // NOTE(vincent): This is a modern version of OpenGL
            
            int Attribs[] = 
            {
                WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
                WGL_CONTEXT_MINOR_VERSION_ARB, 0,
                WGL_CONTEXT_FLAGS_ARB, 0
#if DEBUG
                | WGL_CONTEXT_DEBUG_BIT_ARB
#endif
                ,
                // NOTE(vincent): Use compatibility profile bit arb in order to keep using the old fixed render functions pipeline: glTexCoord2f() glVertex2f() etc.
                // Modern OpenGL would otherwise require you to use shaders.
                // This is not guaranteed to succeed because implementations 
                // don't have to support the old things.
                WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
                0, // terminate the attribs list with a zero like this.
            };
            
            HGLRC ShareContext = 0;
            HGLRC ModernGLRC = wglCreateContextAttribsARB(WindowDC, ShareContext, Attribs);
            if (ModernGLRC)
            {
                if (wglMakeCurrent(WindowDC, ModernGLRC))
                {
                    wglDeleteContext(OpenGLRC);
                    OpenGLRC = ModernGLRC;
                    ModernContext = true;
                }
            }
        }
        else
        {
            
        }
        
        OpenGLInit(ModernContext);
        
        // NOTE(vincent): The proper way to load OpenGL extensions is to 
        // 1) check whether the OpenGL implementation has it with glGetString()
        // 2) use wglGetProcAddress() to load the function pointers we want
        
        wglSwapInterval = (wgl_swap_interval_ext *)wglGetProcAddress("wglSwapIntervalEXT");
        if (wglSwapInterval)
            wglSwapInterval(1); // set VSync for every frame
    }
    else
        InvalidCodePath;
    
    ReleaseDC(Window, WindowDC);
}

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    WNDCLASSA WindowClass = {0};
    WindowClass.style = CS_HREDRAW | CS_VREDRAW;  // redraw window on resize
    
    
    WindowClass.lpfnWndProc = WindowProc;
    WindowClass.hInstance = hInstance;
    WindowClass.hCursor = LoadCursorA(0, IDC_ARROW);
    WindowClass.lpszClassName = "ChessWindowClassName";
    
    ATOM Atom = RegisterClassA(&WindowClass);
    Assert(Atom && "Couldn't register window class");
    
    HWND Window = CreateWindowExA(WS_EX_LEFT,
                                  WindowClass.lpszClassName, "Chess",
                                  WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT,   // initial X initial Y
                                  960, 540,                      // width height
                                  0, 0, hInstance, 0);
    
    Assert(Window && "Couldn't create the window");
    Win32InitOpenGL(Window);
    
    
    Win32PrepareBackbuffer(&GlobalBackbuffer, 960, 540);
    
    Win32LoadXInput();
    
    platform_work_queue Queue = {};
    u32 ThreadCount = THREAD_COUNT;  // TODO(vincent): query for number of cores
    Win32MakeQueue(&Queue, ThreadCount-1);
    
    f32 GameUpdateHz = 60.0f;
    f32 TargetSecondsPerFrame = 1.0f / (f32)GameUpdateHz;
    GlobalGameInput.dtForFrame = TargetSecondsPerFrame;
    QueryPerformanceFrequency(&GlobalCountsPerSecond);
    LARGE_INTEGER InitialWallClock = Win32GetWallClock();
    LARGE_INTEGER LastWallClock = InitialWallClock;
    //u64 LastProcessorClock = __rdstc();
    UINT DesiredSchedulerMS = 1;
    b32 SleepIsGranular = (timeBeginPeriod(DesiredSchedulerMS) == TIMERR_NOERROR);
    
    game_memory GameMemory = {};
    GameMemory.StorageSize = Gigabytes(1);
    LPVOID BaseAddress = (LPVOID) Terabytes(2);
    GameMemory.Storage = VirtualAlloc(BaseAddress, GameMemory.StorageSize,
                                      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Assert(GameMemory.Storage);
    GameMemory.Platform.AddEntry = Win32AddEntry;
    GameMemory.Platform.CompleteAllWork = Win32CompleteAllWork;
    GameMemory.Queue = &Queue;
    GameMemory.Platform.WriteFile = Win32WriteFile;
    GameMemory.Platform.PushReadFile = Win32PushReadFile;
    
    win32_game_code GameCode = {};
    Win32LoadGameCode(&GameCode);
    
    u32 PushBufferSize = Megabytes(4);
    void *PushBuffer =
        VirtualAlloc(0, PushBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    game_render_commands RenderCommands = 
        RenderCommandsStruct((u32)GlobalBackbuffer.Width, (u32)GlobalBackbuffer.Height,
                             PushBufferSize, PushBuffer);
    
    ShowWindow(Window, SW_SHOWDEFAULT);
    GlobalRunning = true;
    while(GlobalRunning)
    {
        for (int i = 0; i < ArrayCount(GlobalGameInput.AllButtons); ++i)
        {
            GlobalGameInput.AllButtons[i].WasPressed = GlobalGameInput.AllButtons[i].IsPressed;
            if (GlobalGameInput.AllButtons[i].AutoRelease)
                GlobalGameInput.AllButtons[i].IsPressed = false;
        }
        GlobalGameInput.MouseMoved = false;
        
        MSG Message;
        while (PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessage(&Message);
        }
        
        // NOTE(vincent): XInput
        XINPUT_STATE ControllerState;
        if (XInputGetState(0, &ControllerState) == ERROR_SUCCESS)
        {
            XINPUT_GAMEPAD *Pad = &ControllerState.Gamepad;
            // Pad->sThumbLX is in [-32768.0f, 32767.0f], left to right.
            // Same for Pad->SThumbLY, bottom to top.
            
            f32 NormalizedLX = (Pad->sThumbLX + 0.5f) / 32767.5f; // [-1.0f, 1.0f]
            f32 NormalizedLY = (Pad->sThumbLY + 0.5f) / 32767.5f;
            
            f32 Magnitude = SquareRoot(Square(NormalizedLX) + Square(NormalizedLY));
            
            f32 Deadzone = 0.25f;
            f32 ProcessedMagnitude;
            if (Magnitude <= Deadzone)
                ProcessedMagnitude = 0.0f;
            else
                ProcessedMagnitude = (Magnitude - Deadzone) / (1.0f - Deadzone);
            
            f32 MagnitudeRatio = SafeRatio0(ProcessedMagnitude, Magnitude);
            GlobalGameInput.ThumbLX = MagnitudeRatio * NormalizedLX;
            GlobalGameInput.ThumbLY = MagnitudeRatio * NormalizedLY;
            
            GlobalGameInput.A.IsPressed |= (Pad->wButtons & XINPUT_GAMEPAD_A);
            GlobalGameInput.B.IsPressed |= (Pad->wButtons & XINPUT_GAMEPAD_B);
            
            if (!GlobalGameInput.Left.IsPressed)
            {
                GlobalGameInput.Left.IsPressed = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT) || 
                (GlobalGameInput.ThumbLX <= -.5f);
                GlobalGameInput.Left.AutoRelease = true;
            }
            
            if (!GlobalGameInput.Down.IsPressed)
            {
                GlobalGameInput.Down.IsPressed = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN) || 
                (GlobalGameInput.ThumbLY <= -.5f);
                GlobalGameInput.Down.AutoRelease = true;
            }
            
            if (!GlobalGameInput.Up.IsPressed)
            {
                GlobalGameInput.Up.IsPressed = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_UP) || 
                (GlobalGameInput.ThumbLY >= .5f);
                GlobalGameInput.Up.AutoRelease = true;
            }
            
            if (!GlobalGameInput.Right.IsPressed)
            {
                GlobalGameInput.Right.IsPressed = 
                (Pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) || 
                (GlobalGameInput.ThumbLX >= .5f);
                GlobalGameInput.Right.AutoRelease = true;
            }
            
            if (!GlobalGameInput.A.IsPressed)
            {
                GlobalGameInput.A.IsPressed = ((Pad->wButtons & XINPUT_GAMEPAD_A) != 0);
                GlobalGameInput.A.AutoRelease = true;
            }
            
            if (!GlobalGameInput.B.IsPressed)
            {
                GlobalGameInput.B.IsPressed = ((Pad->wButtons & XINPUT_GAMEPAD_B) != 0);
                GlobalGameInput.B.AutoRelease = true;
            }
            
            if (!GlobalGameInput.Back.IsPressed)
            {
                GlobalGameInput.Back.IsPressed = ((Pad->wButtons & XINPUT_GAMEPAD_X) != 0);
                GlobalGameInput.Back.AutoRelease = true;
            }
            if (!GlobalGameInput.Forward.IsPressed)
            {
                GlobalGameInput.Forward.IsPressed = ((Pad->wButtons & XINPUT_GAMEPAD_Y) != 0);
                GlobalGameInput.Forward.AutoRelease = true;
            }
            
        }
        
        // NOTE(vincent): Check DLL write time and reload it if it's new
        GetFileAttributesExA(DLL_FILENAME, GetFileExInfoStandard, &GameCode.DLLFileData);
        GlobalGameInput.ReloadedDLL = 0;
        if (CompareFileTime(&GameCode.DLLFileData.ftLastWriteTime, &GameCode.DLLWriteTime))
        {
            FreeLibrary(GameCode.DLL);
            Win32LoadGameCode(&GameCode);
            GlobalGameInput.ReloadedDLL = 1;
        }
        
        GameCode.Update(&GameMemory, &GlobalGameInput, &RenderCommands);
        
        Win32DisplayBufferInWindow(&Queue, &RenderCommands, Window, &GameMemory);
        RenderCommands.PushBufferSize = 0;
        
        // NOTE(vincent): wait for frame time to finish
        LARGE_INTEGER WorkCounter = Win32GetWallClock();
        f32 WorkSecondsElapsed = Win32GetSecondsElapsed(LastWallClock, WorkCounter);
        f32 SecondsElapsedForFrame = WorkSecondsElapsed;
        if (SecondsElapsedForFrame < TargetSecondsPerFrame)
        {
            if (SleepIsGranular)
            {
                DWORD SleepMS = (DWORD) (1000.0f * (TargetSecondsPerFrame - SecondsElapsedForFrame));
                if (SleepMS > 0)
                    Sleep(SleepMS);
            }
            SecondsElapsedForFrame = Win32GetSecondsElapsed(LastWallClock, Win32GetWallClock());
            
            while (SecondsElapsedForFrame < TargetSecondsPerFrame)
                SecondsElapsedForFrame = Win32GetSecondsElapsed(LastWallClock, Win32GetWallClock());
        }
    }
}