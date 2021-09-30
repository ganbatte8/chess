#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <semaphore.h>

#include <xcb/xcb.h>
//#include <xcb/xcb_image.h>
#include <linux/joystick.h>
//#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include <GL/glx.h>

#include "common.h"
#include "chess_opengl.cpp"

// TODO(vincent): Remove stdio
#include <stdio.h>

typedef const char * glx_swap_interval_ext(Display * dpy, GLXDrawable Drawable, int Interval);

PLATFORM_PUSH_READ_FILE(LinuxPushReadFile)
{
    string Result = {};
    
    int FileDescriptor = open(Filename, O_RDONLY);
    if (FileDescriptor < 0)
        perror("PushReadFile open failed");
    if (FileDescriptor >= 0)
    {
        printf("Got file descriptor\n");
        struct stat FileInfo = {};
        fstat(FileDescriptor, &FileInfo);
        Result.Size = FileInfo.st_size;
        u32 AvailableSize = Arena->Size - Arena->Used;
        if (Result.Size <= AvailableSize)
        {
            printf("Can fit size (need:%d available: %d)\n", Result.Size, AvailableSize);
            Result.Base = PushArray(Arena, Result.Size, char);
            u32 WrittenSize = read(FileDescriptor, Result.Base, Result.Size);
            if (WrittenSize != Result.Size)
            {
                printf("Oops, didn't load entire file\n");
                Result.Base = 0;
            }
        }
        else
        {
            printf("Cannot fit size (need:%d available:%d)\n", Result.Size, AvailableSize);
        }
    }
    close(FileDescriptor);
    
    if (Result.Base)
        printf("Base pointer should be nonzero\n");
    
    return Result;
}

PLATFORM_WRITE_FILE(LinuxWriteFile)
{
    b32 Result = false;
    
    int FileDescriptor = open(Filename, O_CREAT | O_WRONLY, 0777);
    if (FileDescriptor >= 0)
    {
        u32 WrittenSize = write(FileDescriptor, Memory, Size);
        if (WrittenSize == Size)
            Result = true;
    }
    close(FileDescriptor);
    return Result;
}

struct linux_game_code
{
#define SO_FILENAME "chess.so"
#define SO_COPY_FILENAME "chess_copy.so"
    void *DLL;
    game_update *Update;
    struct timespec LastWriteTime;
};

internal void
LinuxCopyFile(char *SourceFilename, char *DestFilename)
{
    int SourceFD = open(SourceFilename, O_RDWR);
    int DestFD = open(DestFilename, O_RDWR | O_CREAT, 0777);
    if (SourceFD == -1)
        perror("Invalid SourceFD");
    if (DestFD  == -1)
        perror("Invalid DestFD");
    char Buffer[4096];
    if (SourceFD >= 0 && DestFD >= 0)
    {
        for (;;)
        {
            int ReadCount = read(SourceFD, Buffer, sizeof(Buffer));
            if (ReadCount <= 0)
                break;
            char *Source = Buffer;
            int ToWrite = ReadCount;
            while (ToWrite > 0)
            {
                int WriteCount = write(DestFD, Source, ToWrite);
                ToWrite -= WriteCount;
                Source += WriteCount;
            }
        }
        
    }
    
    Assert(SourceFD >= 0 && DestFD >= 0);
    if (SourceFD >= 0)
        close(SourceFD);
    if (DestFD >= 0)
        close(DestFD);
}

internal void
LinuxLoadGameCode(linux_game_code *Code)
{
    LinuxCopyFile(SO_FILENAME, SO_COPY_FILENAME);
    Code->DLL = dlopen("./" SO_COPY_FILENAME, RTLD_LAZY);
    if (!Code->DLL)
        perror("dlopen failed");
    Code->Update = (game_update *)dlsym(Code->DLL, "GameUpdate");
    if (!Code->Update)
        perror("dlsym failed");
    Assert(Code->DLL && Code->Update);
    struct stat FileInfo = {};
    stat(SO_FILENAME, &FileInfo);
    Code->LastWriteTime = FileInfo.st_mtim;
}

struct platform_work_queue
{
    u32 volatile CompletionCount;
    u32 volatile CompletionGoal;
    u32 volatile WriteIndex;
    u32 volatile FetchIndex;
    sem_t SemaphoreHandle;
    platform_work_queue_entry Entries[256];
};

internal void
LinuxAddEntry(platform_work_queue *Queue, platform_work_queue_callback *Callback, void *Data)
{
    u32 NextWriteIndex = (Queue->WriteIndex + 1) % ArrayCount(Queue->Entries);
    Assert(NextWriteIndex != Queue->FetchIndex);
    platform_work_queue_entry *Entry = Queue->Entries + Queue->WriteIndex;
    Entry->Callback = Callback;
    Entry->Data = Data;
    ++Queue->CompletionGoal;
    Queue->WriteIndex = NextWriteIndex;
    sem_post(&Queue->SemaphoreHandle); // increase semaphore count so a thread can wake up
}

internal b32
LinuxDoNextWorkQueueEntry(platform_work_queue *Queue)
{
    b32 WeShouldSleep = true;
    u32 OriginalFetch = Queue->FetchIndex;
    if (OriginalFetch != Queue->WriteIndex)
    {
        WeShouldSleep = false;
        u32 NextFetch = (OriginalFetch + 1) % ArrayCount(Queue->Entries);
        u32 Index = __sync_val_compare_and_swap(&Queue->FetchIndex, OriginalFetch, NextFetch);
        if (Index == OriginalFetch)
        {
            platform_work_queue_entry Entry = Queue->Entries[Index];
            Entry.Callback(Queue, Entry.Data);
            __sync_fetch_and_add(&Queue->CompletionCount, 1);
        }
    }
    return WeShouldSleep;
}

internal void
LinuxCompleteAllWork(platform_work_queue *Queue)
{
    while (Queue->CompletionGoal != Queue->CompletionCount)
    {
        LinuxDoNextWorkQueueEntry(Queue);
    }
    Queue->CompletionCount = 0;
    Queue->CompletionGoal = 0;
}

internal void *
ThreadProc(void *Arg)
{
    platform_work_queue *Queue = (platform_work_queue *)Arg;
    for (;;)
    {
        if (LinuxDoNextWorkQueueEntry(Queue))
        {
            sem_wait(&Queue->SemaphoreHandle); // decrements semaphore count, may put thread to sleep
        }
    }
    
}

internal void
LinuxMakeQueue(platform_work_queue *Queue, u32 ThreadCount)
{
    Queue->CompletionGoal = 0;
    Queue->CompletionCount = 0;
    Queue->FetchIndex = 0;
    Queue->WriteIndex = 0;
    u32 InitialCount = 0;
    sem_init(&Queue->SemaphoreHandle, 0, InitialCount);
    for (u32 ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
    {
        pthread_t ThreadID;
        pthread_create(&ThreadID, 0, ThreadProc, Queue); 
    }
}


struct xcb_xlib_glx_context
{
    Display *XDisplay;
    int DefaultScreen;
    xcb_connection_t *Connection;
    xcb_window_t Window;
    xcb_screen_t *Screen;
    GLXDrawable Drawable;
    
    // NOTE(vincent): Since we don't support software rendering, I'm pretty sure there is no actual
    // back buffer that we allocate, write or access ourselves. OpenGL is doing it all.
    u32 BufferWidth;
    u32 BufferHeight;
    u32 WindowWidth;
    u32 WindowHeight;
    
    int JoystickFD;
    struct stat JoystickStatus;
    time_t LastJoystickCTime;
    f32 JoystickRefreshClock;
};

global_variable f32 NormalizedLX;
global_variable f32 NormalizedLY;
internal void
LinuxHandleEvents(xcb_xlib_glx_context *C, game_input *Input)
{
    xcb_generic_event_t *Event;
    while ((Event = xcb_poll_for_event(C->Connection)))
    {
        // NOTE(vincent): I've seen many examples that mask this bit out, but idk why
        //printf("Event type %d (XCB_KEY_PRESS is %d)\n", Event->response_type, XCB_KEY_PRESS);
        switch(Event->response_type & ~0x80)
        {
            //case XCB_EXPOSE: break;
#define SetPressed(Code, Button)  case Code: Input->Button.IsPressed = IsDown; break
            case XCB_BUTTON_PRESS:
            case XCB_BUTTON_RELEASE:
            {
                xcb_button_press_event_t *BP = (xcb_button_press_event_t *)Event;
                b32 IsDown = Event->response_type == XCB_BUTTON_PRESS;
                
                //xcb_button_release_event_t *BR = (xcb_button_release_event_t *)Event;
                //printf("Key release. Key button %d\n", BP->detail);
                
                switch (BP->detail)
                {
                    SetPressed(1, A);       // mouse left
                    SetPressed(3, B);       // mouse right
                    SetPressed(4, Forward); // mouse up
                    SetPressed(5, Back);    // mouse down
                }
            } break;
            
            
            case XCB_KEY_PRESS:
            case XCB_KEY_RELEASE:
            {
                b32 IsDown = Event->response_type == XCB_KEY_PRESS;
                xcb_key_press_event_t *KP = (xcb_key_press_event_t *)Event;
                //printf("Key press. Keycode %d\n", KP->detail);
                
                switch (KP->detail)
                {
                    SetPressed(113, Left);
                    SetPressed(116, Down);
                    SetPressed(111, Up);
                    SetPressed(114, Right);
                    SetPressed(22, Back);    // backspace
                    SetPressed(36, Forward); // enter/return
                    SetPressed(52, A);       // ';'
                    SetPressed(53, B);       // 'q'
                    
                    SetPressed(27, P);
                    SetPressed(32, R);
                    SetPressed(46, N);
                    SetPressed(57, KeyB);
                    SetPressed(55, K);
                    SetPressed(45, T);
                }
            } break;
            
            case XCB_MOTION_NOTIFY:
            {
                Input->MouseMoved = true; 
                
                // In full screen 1920*1080 with border size 0 on i3, I get the following intervals
                // of values inside of the client area:
                // [0, 1079] for y (top to bottom)
                // [0, 1919] for x (left to right)
                // Values can get out of these bounds when the mouse cursor is outside 
                // of the client area.
                xcb_motion_notify_event_t *Motion = (xcb_motion_notify_event_t *)Event;
                u32 ClientWidth = C->WindowWidth;
                u32 ClientHeight = C->WindowHeight;
                Input->MouseX = Clamp01(SafeRatio0((f32)Motion->event_x, (f32)(ClientWidth-1)));
                Input->MouseY = Clamp01(1.0f - SafeRatio0((f32)Motion->event_y, (f32)(ClientHeight-1)));
                //printf("Mouse move x: %d y: %d (UV: %f %f)\n", Motion->event_x, Motion->event_y,
                //Input->MouseX, Input->MouseY);
            } break;
            
            default:
            {
                //printf("Unhandled event: %d\n", Event->response_type);
                // NOTE(vincent): Event 14 is firing a lot (once every frame it looks like), but why?
            }
        }
        free(Event);
    }
    
    // NOTE(vincent): Refresh gamepad.
    // - Problem statement: we want to let the controller disconnect and reconnect
    // - It seems more sane to verify every second (as we do) than every frame, but I'm not sure
    // - Closing and reopening the JoystickFD every time causes huge stalls, especially when
    // the controller is plugged in. Looking at the CTime with fstat() mitigates a lot of the cost.
    if (C->JoystickRefreshClock >= 1.0f)
    {
        C->JoystickRefreshClock -= 1.0f;
        if (C->JoystickFD >= 0)
        {
            fstat(C->JoystickFD, &C->JoystickStatus);
            if (C->JoystickStatus.st_ctime != C->LastJoystickCTime)
            {
                close(C->JoystickFD);
                C->LastJoystickCTime = C->JoystickStatus.st_ctime;
                C->JoystickFD = -1;
            }
        }
        else
        {
            C->JoystickFD = open("/dev/input/js0", O_RDONLY | O_NONBLOCK);
            if (C->JoystickFD >= 0)
            {
                fstat(C->JoystickFD, &C->JoystickStatus);
                C->LastJoystickCTime = C->JoystickStatus.st_ctime;
            }
        }
    }
    C->JoystickRefreshClock += Input->dtForFrame;
    
    if (C->JoystickFD >= 0)
    {
        struct js_event JoystickEvent;
        b32 LStickMoved = false;
        while (read(C->JoystickFD, &JoystickEvent, sizeof(js_event)) == sizeof(js_event))
        {
            JoystickEvent.type &= ~JS_EVENT_INIT;
            switch (JoystickEvent.type)
            {
                case JS_EVENT_BUTTON:
                {
                    //printf("JS_EVENT_BUTTON: %d, %d\n", JoystickEvent.number, JoystickEvent.value);
                    // value is 0 for release, 1 for press
                    b32 IsDown = JoystickEvent.value;
                    switch (JoystickEvent.number)
                    {
                        SetPressed(0, A);
                        SetPressed(1, B);
                        SetPressed(2, Back);
                        SetPressed(3, Forward);
                    }
                } break;
                
                case JS_EVENT_AXIS:
                {
                    //printf("JS_EVENT_AXIS: %d, %d\n", JoystickEvent.number, JoystickEvent.value);
                    // Dpad left and right: number 6. Value -32767 left, 0 released, 32767 right.
                    // Dpad up and down: number 7. Value -32767 top, 0 released, 32767 bottom.
                    // Xbox Dpad values are digital, only three possible values for each axis.
                    
                    // left stick X: number 0, value -32767 (?) to 32767 (?) left to right 
                    // left stick Y: number 1, value -32767
                    
                    switch (JoystickEvent.number)
                    {
                        case 6: 
                        {
                            Input->Left.IsPressed = (JoystickEvent.value < 0);
                            Input->Right.IsPressed = (JoystickEvent.value > 0);
                        } break;
                        case 7:
                        {
                            Input->Up.IsPressed = (JoystickEvent.value < 0);
                            Input->Down.IsPressed = (JoystickEvent.value > 0);
                        } break;
                        case 0: 
                        {
                            NormalizedLX = JoystickEvent.value / 32767.0f; // [-1.0f, 1.0f] 
                            LStickMoved = true;
                        } break;
                        case 1:
                        {
                            NormalizedLY = -JoystickEvent.value / 32767.0f; // [-1.0f, 1.0f]
                            LStickMoved = true;
                        } break;
                        
                    }
                    
                } break;
            }
        }
        // NOTE(vincent): I sort of expect Magnitude to be <= 1.0f, but it might not be.
        // I can imagine that some joysticks will allow (NormalizedLX, NormalizedLY) 
        // to be outside of the unit circle. For this game I don't care too much.
        // Worst case, Magnitude can go up to sqrt(2).
        if (LStickMoved)
        {
            f32 Magnitude = SquareRoot(Square(NormalizedLX) + Square(NormalizedLY));
            f32 Deadzone = 0.25f;
            f32 ProcessedMagnitude = 0.0f;
            if (Magnitude > Deadzone)
                ProcessedMagnitude = (Magnitude - Deadzone) / (1.0f - Deadzone);
            f32 MagnitudeRatio = SafeRatio0(ProcessedMagnitude, Magnitude);
            Input->ThumbLX = MagnitudeRatio * NormalizedLX;
            Input->ThumbLY = MagnitudeRatio * NormalizedLY;
            
            if (Input->ThumbLX <= -.5f)
                Input->Left.IsPressed = true;
            if (Input->ThumbLX >= .5f)
                Input->Right.IsPressed = true;
            if (Input->ThumbLY <= -.5f)
                Input->Down.IsPressed = true;
            if (Input->ThumbLY >= .5f)
                Input->Up.IsPressed = true;
            
        }
    }
}

internal void
LinuxInitWindowAndGLX(xcb_xlib_glx_context *C)
{
    // NOTE(vincent): First, create XCB/Xlib connection and retrieve screen info
    C->XDisplay = XOpenDisplay(0);
    Assert(C->XDisplay);
#undef DefaultScreen
    C->DefaultScreen = XDefaultScreen(C->XDisplay);
    C->Connection = XGetXCBConnection(C->XDisplay);
    Assert(C->Connection);
    XSetEventQueueOwner(C->XDisplay, XCBOwnsEventQueue);
    
    xcb_screen_iterator_t ScreenIter = xcb_setup_roots_iterator(xcb_get_setup(C->Connection));
    for (int ScreenNumber = C->DefaultScreen; ScreenIter.rem && ScreenNumber > 0; --ScreenNumber)
    {
        xcb_screen_next(&ScreenIter);
    }
    C->Screen = ScreenIter.data;
    
    C->Window = xcb_generate_id(C->Connection);
    
    C->BufferWidth = 960;
    C->BufferHeight = 540;
    
    // NOTE(vincent): GLX stuff starts here; we also create a window
    int VisualAttribs[] = 
    {
        GLX_X_RENDERABLE, true,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        GLX_DEPTH_SIZE, 8,
        GLX_STENCIL_SIZE, 8,
        GLX_DOUBLEBUFFER, true,
        None  // attribs list must end with this value
    };
    
    int VisualID = 0;
    int FBConfigsCount = 0;
    GLXFBConfig *FBConfigs = 
        glXChooseFBConfig(C->XDisplay, C->DefaultScreen, VisualAttribs, &FBConfigsCount);
    Assert(FBConfigs && FBConfigsCount);
    
    GLXFBConfig FBConfig = FBConfigs[0];
    glXGetFBConfigAttrib(C->XDisplay, FBConfig, GLX_VISUAL_ID, &VisualID);
    GLXContext Context = glXCreateNewContext(C->XDisplay, FBConfig, GLX_RGBA_TYPE, 0, true);
    Assert(Context);
    
    xcb_colormap_t Colormap = xcb_generate_id(C->Connection);
    xcb_create_colormap(C->Connection, XCB_COLORMAP_ALLOC_NONE, Colormap, C->Screen->root, VisualID);
    
    // NOTE(vincent): Specify list of X events to subscribe to for our window
    u32 EventMask =
        XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
        XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE;
    u32 ValueList[] = {EventMask, Colormap, 0};
    u32 ValueMask = XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;
    
    // TODO(vincent): Not sure whether we should put buffer or window width and height in this call.
    xcb_create_window(C->Connection, XCB_COPY_FROM_PARENT, C->Window, C->Screen->root,
                      0, 0, C->BufferWidth, C->BufferHeight, 0, // x y w h borderwidth
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, VisualID, ValueMask, ValueList);
    xcb_map_window(C->Connection, C->Window);
    GLXWindow glXWindow = glXCreateWindow(C->XDisplay, FBConfig, C->Window, 0);
    Assert(C->Window);
    
    C->Drawable = glXWindow;
    if (glXMakeContextCurrent(C->XDisplay, C->Drawable, C->Drawable, Context))
    {
        // TODO(vincent): Should we do context escalation?
        // TODO(vincent): Check for available extensions with
        // const char* glXQueryExtensionsString(Display *dpy, int Screen)
        // (or maybe glGetString() ?)
        // to check more rigorously whether glXSwapIntervalEXT() is present.
        
        glx_swap_interval_ext *glXSwapIntervalEXT = 
            (glx_swap_interval_ext *)glXGetProcAddress((GLubyte*)"glXSwapIntervalEXT");
        if (glXSwapIntervalEXT)
            glXSwapIntervalEXT(C->XDisplay, C->Drawable, 1);
    }
    
}

internal void
LinuxDisplayBufferInWindow(xcb_xlib_glx_context *C, game_render_commands *Commands)
{
    // NOTE(vincent): All this code is identical on Windows (except for GLXDrawable and glXSwapBuffers)
    
    xcb_get_geometry_cookie_t GeometryCookie = xcb_get_geometry(C->Connection, C->Window);
    xcb_get_geometry_reply_t *Reply = xcb_get_geometry_reply(C->Connection, GeometryCookie, 0);
    if (Reply)
    {
        //if (C->WindowWidth != Reply->width && C->WindowHeight != Reply->height)
        //printf("Updating WindowWidth to %d and WindowHeight to %d\n", Reply->width, Reply->height);
        
        C->WindowWidth = Reply->width;
        C->WindowHeight = Reply->height;
        free(Reply);
    }
    
    OpenGLRenderToOutput(Commands, C->WindowWidth, C->WindowHeight);
    glXSwapBuffers(C->XDisplay, C->Drawable);
    Commands->PushBufferSize = 0;
}


internal struct timespec
LinuxGetWallClock()
{
    struct timespec Result = {};
    clock_gettime(CLOCK_MONOTONIC, &Result);
    return Result;
}

int main(void)
{
    
    xcb_xlib_glx_context C = {};
    
    LinuxInitWindowAndGLX(&C);
    
    platform_work_queue Queue = {};
    u32 ThreadCount = THREAD_COUNT; // TODO(vincent): Query for number of cores
    LinuxMakeQueue(&Queue, ThreadCount-1);
    
    game_input Input = {};
    
    f32 GameUpdateHz = 60.0f;
    Input.dtForFrame = 1.0f / GameUpdateHz;
    u32 TargetNSPerFrame = (1000*1000*1000) / GameUpdateHz;
    struct timespec LastWallClock = LinuxGetWallClock();
    
    game_memory GameMemory = {};
    GameMemory.StorageSize = Gigabytes(1);
    
    GameMemory.Storage = mmap(0, GameMemory.StorageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((uintptr_t)GameMemory.Storage == (uintptr_t)-1)
        perror("mmap failed");
    Assert((uintptr_t)GameMemory.Storage != (uintptr_t)-1);
    GameMemory.Platform.AddEntry = LinuxAddEntry;
    GameMemory.Platform.CompleteAllWork = LinuxCompleteAllWork;
    GameMemory.Queue = &Queue;
    GameMemory.Platform.WriteFile = LinuxWriteFile;
    GameMemory.Platform.PushReadFile = LinuxPushReadFile;
    
    linux_game_code GameCode = {};
    LinuxLoadGameCode(&GameCode);
    
    u32 PushBufferSize = Megabytes(4);
    void *PushBuffer = mmap(0, GameMemory.StorageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); 
    Assert((uintptr_t)PushBuffer != (uintptr_t)-1);
    game_render_commands RenderCommands = 
        RenderCommandsStruct((u32)C.BufferWidth, (u32)C.BufferHeight, PushBufferSize, PushBuffer);
    
    RenderCommands.Width = C.BufferWidth;
    RenderCommands.Height = C.BufferHeight;
    RenderCommands.MaxPushBufferSize = PushBufferSize;
    RenderCommands.PushBufferSize = 0;
    RenderCommands.PushBufferBase = (u8 *)PushBuffer;
    
    C.JoystickFD = -1;
    
    b32 Running = true;
    while (Running)
    {
        for (u32 i = 0; i < ArrayCount(Input.AllButtons); ++i)
        {
            Input.AllButtons[i].WasPressed = Input.AllButtons[i].IsPressed;
            // NOTE(vincent): AutoRelease is not necessary for Linux because XCB gives us 
            // more flexibility regarding input handling than Win32 does.
        }
        Input.MouseMoved = false;
        
        LinuxHandleEvents(&C, &Input);
        
        // NOTE(vincent): Check SO write time and reload if it's new
        Input.ReloadedDLL = false;
        struct stat FileInfo = {};
        stat(SO_FILENAME, &FileInfo);
        if (GameCode.LastWriteTime.tv_sec != FileInfo.st_mtim.tv_sec ||
            GameCode.LastWriteTime.tv_nsec != FileInfo.st_mtim.tv_nsec)
        {
            dlclose(GameCode.DLL);
            LinuxLoadGameCode(&GameCode);
            Input.ReloadedDLL = true;
        }
        
        GameCode.Update(&GameMemory, &Input, &RenderCommands);
        
        LinuxDisplayBufferInWindow(&C,  &RenderCommands);
        
        // NOTE(vincent): nanosleep() and update LastWallClock
        // TODO(vincent): See if this is consistent with new win32 approach
        struct timespec TargetWallClock;
        TargetWallClock.tv_sec = LastWallClock.tv_sec;
        TargetWallClock.tv_nsec = LastWallClock.tv_nsec + TargetNSPerFrame;
        if (TargetWallClock.tv_nsec > 1000*1000*1000)
        {
            TargetWallClock.tv_nsec -= 1000*1000*1000;
            Assert(TargetWallClock.tv_nsec < 1000*1000*1000);
            TargetWallClock.tv_sec++;
        }
        
        struct timespec ClockAfterWork = LinuxGetWallClock();
        b32 ShouldSleep = (ClockAfterWork.tv_sec < TargetWallClock.tv_sec)
            || (ClockAfterWork.tv_sec == TargetWallClock.tv_sec &&
                ClockAfterWork.tv_nsec < TargetWallClock.tv_nsec);
        
        if (ShouldSleep)
        {
            timespec SleepAmount;
            SleepAmount.tv_sec = 0;
            SleepAmount.tv_nsec = TargetWallClock.tv_nsec - ClockAfterWork.tv_nsec;
            if (SleepAmount.tv_nsec < 0)
            {
                SleepAmount.tv_nsec += 1000*1000*1000;
            }
            //printf("Sleep amount tv_sec: %ld tv_nsec: %ld\n", SleepAmount.tv_sec, SleepAmount.tv_nsec);
            nanosleep(&SleepAmount, 0);
        }
        LastWallClock = LinuxGetWallClock();
    }
}