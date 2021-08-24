#define GL_SHADING_LANGUAGE_VERSION 0x8B8C

// NOTE(vincent): Windows-specific #defines
#define WGL_CONTEXT_MAJOR_VERSION_ARB          0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB          0x2092
#define WGL_CONTEXT_LAYER_PLANE_ARB            0x2093
#define WGL_CONTEXT_FLAGS_ARB                  0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB           0x9126
#define WGL_CONTEXT_DEBUG_BIT_ARB              0x0001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x0002
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB             0x00000001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB    0x00000001

struct opengl_info
{
    b32 ModernContext;
    
    char *Vendor;
    char *Renderer;
    char *Version;
    char *ShadingLanguageVersion;
    char *Extensions;
};

internal opengl_info
OpenGLGetInfo(b32 ModernContext)
{
    opengl_info Result = {};
    Result.ModernContext = ModernContext;
    Result.Vendor = (char *)glGetString(GL_VENDOR);
    Result.Renderer = (char *)glGetString(GL_RENDERER);
    Result.Version = (char *)glGetString(GL_VERSION);
    // NOTE(vincent): GL_SHADING_LANGUAGE_VERSION is an OpenGL 2.0 extension
    if (Result.ModernContext)
        Result.ShadingLanguageVersion = (char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    Result.Extensions = (char *)glGetString(GL_EXTENSIONS);;
    
    char *At = Result.Extensions;
    while (*At)
    {
        while (IsWhitespace(*At)) ++At;
        char *End = At;
        while (*End && !IsWhitespace(*End)) ++End;
        
        // TODO(vincent): Normaly you'd test for extensions you want to fetch here 
        // and set Result.stuff = true;
        //StringEquals(char *Literal, char *First, char *LastPlusOne);
        At = End;
    }
    
    return Result;
}

internal void
OpenGLInit(b32 ModernContext)
{
    opengl_info OpenGLInfo = OpenGLGetInfo(ModernContext);
}

inline void
OpenGLSetScreenSpace(s32 Width, s32 Height)
{
    // NOTE(vincent): The game sends vectors to the OpenGL renderer in pixel space 
    // (they were converted from meter space or internal game world space when we pushed the rendering
    // commands). But OpenGL expects us to feed it vertices that are in unit cube space, where
    // coordinates go from -1 to 1 for both the x and y axis.
    // So we need to apply a linear transformation. 
    // In OpenGL there is this concept of MVP matrix 
    // (product of model, view, and projection matrices) which is a 4*4 matrix that transforms
    // the incoming vectors. They will transform a (x, y, z, w) vector.
    // We can specify each matrix individually but we will only set up the projection matrix
    // to do the actual transformation.
    // The w coordinate is used by the hardware to do a non-linear transformation afterwards 
    // (dividing every other component, and maybe other things), which has to do with 3D perspective stuff 
    // I'm not very familiar with.
    // Since we are a simple 2D game we are only really concerned with transforming the x and y
    // coordinates. The z and w will be preserved, so the last two rows of our matrix will be 
    // like the identity matrix.
    
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    glMatrixMode(GL_PROJECTION);
    f32 a = SafeRatio1(2.0f, (f32)Width);
    f32 b = SafeRatio1(2.0f, (f32)Height);
    f32 ProjectionMatrix[] = 
    {
        a,   0, 0,  0,
        0,   b, 0,  0,
        0,   0, 1,  0,
        -1, -1, 0,  1,
    };
    // The matrix specified in C layout that OpenGL wants appears as the transposed matrix 
    // of the mathematical layout. i.e. rows are (a, 0, 0, -1), (0, b, 0, -1), etc.
    
    glLoadMatrixf(ProjectionMatrix);
}

inline void
OpenGLRectangle(v2 MinP, v2 MaxP, v4 Color)
{
    glBegin(GL_TRIANGLES);
    glColor4f(Color.r, Color.g, Color.b, Color.a);
    
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(MinP.x, MinP.y);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(MaxP.x, MinP.y);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(MaxP.x, MaxP.y);
    
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(MinP.x, MinP.y);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(MaxP.x, MaxP.y);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(MinP.x, MaxP.y);
    
    glEnd();
}

global_variable u32 TextureBindCount = 0; // TODO(vincent): remove this at some point
internal void
OpenGLRenderToOutput(game_render_commands *Commands, s32 WindowWidth, s32 WindowHeight)
{
    // NOTE(vincent): glViewport sets the dimensions of the destination window on screen.
    // It tells OpenGL where to map the unit cube to. 
    // The larger the Viewport is, the more pixels we write.
    
    // NOTE(vincent): The pipeline stages of GPUs after the fragment shader are typically not very
    // programmable. So you won't be able to program shaders for the blending.
    // Instead, you are provided glBlendFunc(), where you can pass a few enumerated values for (a, b)
    // where the final result pixel is calculated as such: NewDest = a*Dest + b*Source.
    // To get premultiplied alpha blending, we use a = 1 and b = 1 - SourceAlpha.
    glViewport(0, 0, WindowWidth, WindowHeight);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    
    OpenGLSetScreenSpace(Commands->Width, Commands->Height);
    
    for (u32 Address = 0; Address < Commands->PushBufferSize;)
    {
        render_entry_header *Header = (render_entry_header *) (Commands->PushBufferBase + Address);
        Address += sizeof(*Header);
        
        void *Data = (u8 *)Header + sizeof(*Header);
        switch (Header->Type)
        {
            case RenderEntryType_render_entry_clear:
            {
                render_entry_clear *Entry = (render_entry_clear *)Data;
                glClearColor(Entry->Color.r, Entry->Color.g, Entry->Color.b, Entry->Color.a);
                glClear(GL_COLOR_BUFFER_BIT);
                Address += sizeof(*Entry);
            } break;
            case RenderEntryType_render_entry_bitmap:
            {
                render_entry_bitmap *Entry = (render_entry_bitmap *)Data;
                
                Assert(Entry->Bitmap);
                // Origin, XAxis, YAxis
                
                v2 XAxis = {1, 0};
                v2 YAxis = {0, 1};
                v2 MinP = Entry->Origin;
                v2 MaxP = MinP + Entry->XAxis + Entry->YAxis;
                
                if (Entry->Bitmap->Handle)
                {
                    glBindTexture(GL_TEXTURE_2D, Entry->Bitmap->Handle);
                }
                else
                {
                    Entry->Bitmap->Handle = ++TextureBindCount;
                    glBindTexture(GL_TEXTURE_2D, Entry->Bitmap->Handle);
                    
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Entry->Bitmap->Width, Entry->Bitmap->Height,
                                 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, Entry->Bitmap->Memory);
                    
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                }
                OpenGLRectangle(MinP, MaxP, Entry->Color);
                Address += sizeof(*Entry);
            } break;
            case RenderEntryType_render_entry_bitmap_no_blend:
            {
                render_entry_bitmap *Entry = (render_entry_bitmap *)Data;
                
                Assert(Entry->Bitmap);
                // Origin, XAxis, YAxis
                
                v2 XAxis = {1, 0};
                v2 YAxis = {0, 1};
                v2 MinP = Entry->Origin;
                v2 MaxP = MinP + Entry->XAxis + Entry->YAxis;
                
                if (Entry->Bitmap->Handle)
                {
                    glBindTexture(GL_TEXTURE_2D, Entry->Bitmap->Handle);
                }
                else
                {
                    Entry->Bitmap->Handle = ++TextureBindCount;
                    glBindTexture(GL_TEXTURE_2D, Entry->Bitmap->Handle);
                    
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Entry->Bitmap->Width, Entry->Bitmap->Height,
                                 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, Entry->Bitmap->Memory);
                    
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                }
                OpenGLRectangle(MinP, MaxP, Entry->Color);
                Address += sizeof(*Entry);
            } break;
            case RenderEntryType_render_entry_rectangle:
            {
                render_entry_rectangle *Entry = (render_entry_rectangle *)Data;
                glDisable(GL_TEXTURE_2D);
                OpenGLRectangle(Entry->Min, Entry->Max, Entry->Color);
                glEnable(GL_TEXTURE_2D);
                Address += sizeof(*Entry);
            } break;
            
            InvalidDefaultCase;
        }
    }
}