struct render_group
{
    v2 ScreenDim;
    f32 PixelsPerMeter;
    
    game_render_commands *Commands;
};

inline render_group
SetRenderGroup(game_render_commands *Commands)
{
    render_group Result = {};
    f32 MonitorWidth = 0.6f;
    Result.PixelsPerMeter = Commands->Width / MonitorWidth;
    f32 MonitorHeight = Commands->Height / Result.PixelsPerMeter;
    Result.ScreenDim = V2(MonitorWidth, MonitorHeight);
    Result.Commands = Commands;
    
    return Result;
}

#define PushRenderElement(Group, type) (type *)PushRenderElement_(Group, sizeof(type), RenderEntryType_##type)

inline void *
PushRenderElement_(render_group *Group, u32 Size, render_entry_type Type)
{
    game_render_commands *Commands = Group->Commands;
    void *Result = 0;
    Size += sizeof(render_entry_header);
    Assert(Commands->PushBufferSize + Size <= Commands->MaxPushBufferSize);
    render_entry_header *Header = 
        (render_entry_header *)(Commands->PushBufferBase + Commands->PushBufferSize);
    Header->Type = Type;
    Result = (u8 *)Header + sizeof(*Header);
    Commands->PushBufferSize += Size;
    return Result;
}

inline render_entry_bitmap *
PushBitmap(render_group *Group, bitmap *Bitmap, v2 Origin, v2 XAxis, v2 YAxis, v4 Color)
{
    render_entry_bitmap *Entry = PushRenderElement(Group, render_entry_bitmap);
    if (Entry)
    {
        Entry->Origin = Origin * Group->PixelsPerMeter;
        Entry->XAxis = XAxis * Group->PixelsPerMeter;
        Entry->YAxis = YAxis * Group->PixelsPerMeter;
        Entry->Bitmap = Bitmap;
        Entry->Color = Color;
    }
    return Entry;
}

inline render_entry_bitmap_no_blend *
PushBitmapNearest(render_group *Group, bitmap *Bitmap, v2 Origin, 
                  v2 XAxis, v2 YAxis, v4 Color)
{
    render_entry_bitmap_no_blend *Entry = 
        PushRenderElement(Group, render_entry_bitmap_no_blend);
    if (Entry)
    {
        Entry->Origin = Origin * Group->PixelsPerMeter;
        Entry->XAxis = XAxis * Group->PixelsPerMeter;
        Entry->YAxis = YAxis * Group->PixelsPerMeter;
        Entry->Bitmap = Bitmap;
        Entry->Color = Color;
    }
    return Entry;
}


inline render_entry_rectangle *
PushRect(render_group *Group, v2 vMin, v2 vMax, v4 Color)
{
    render_entry_rectangle *Entry = PushRenderElement(Group, render_entry_rectangle);
    if (Entry)
    {
        Entry->Min = Group->PixelsPerMeter * vMin;
        Entry->Max = Group->PixelsPerMeter * vMax;
        Entry->Color = Color;
    }
    return Entry;
}

inline render_entry_rectangle *
PushRect(render_group *Group, rectangle2 Rect, v4 Color)
{
    render_entry_rectangle *Entry = PushRenderElement(Group, render_entry_rectangle);
    if (Entry)
    {
        Entry->Min = Group->PixelsPerMeter * Rect.Min;
        Entry->Max = Group->PixelsPerMeter * Rect.Max;
        Entry->Color = Color;
    }
    return Entry;
}

inline void
PushRectOutline(render_group *Group, v2 vMin, v2 vMax, v4 Color, f32 Thickness)
{
    v2 LeftRectangleMin = vMin - V2(Thickness, Thickness);
    v2 LeftRectangleMax = V2(vMin.x, vMax.y + Thickness);
    PushRect(Group, LeftRectangleMin, LeftRectangleMax, Color);
    
    v2 DownRectangleMin = V2(vMin.x - Thickness, vMin.y - Thickness);
    v2 DownRectangleMax = V2(vMax.x + Thickness, vMin.y);
    PushRect(Group, DownRectangleMin, DownRectangleMax, Color);
    
    v2 UpRectangleMin = V2(vMin.x - Thickness, vMax.y);
    v2 UpRectangleMax = V2(vMax.x + Thickness, vMax.y + Thickness);
    PushRect(Group, UpRectangleMin, UpRectangleMax, Color);
    
    v2 RightRectangleMin = V2(vMax.x, vMin.y - Thickness);
    v2 RightRectangleMax = V2(vMax.x + Thickness, vMax.y + Thickness);
    PushRect(Group, RightRectangleMin, RightRectangleMax, Color);
}

inline render_entry_clear *
PushClear(render_group *Group, v4 Color)
{
    render_entry_clear *Entry = PushRenderElement(Group, render_entry_clear);
    if (Entry)
    {
        Entry->Color = Color;
    }
    return Entry;
}

struct glyph
{
    bitmap Bitmap;
    s32 AdvanceWidth;
    s32 AdvanceHeight;
    s32 LeftSideBearing;
    rectangle2i Box;
};

struct font
{
    rectangle2i Box;
    f32 SF;
    s32 Ascent;
    s32 Descent;
    s32 LineGap;
#define CHAR_TABLE "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 '"
    glyph Glyphs[sizeof(CHAR_TABLE)];
    s32 KerningTable[sizeof(CHAR_TABLE) * sizeof(CHAR_TABLE)];
};

internal u32
CharToGlyphIndex(char C)
{
    u32 Result = 0;
    if ('a' <= C && C <= 'z')
        Result = C - 'a';
    else if ('A' <= C && C <= 'Z')
        Result = (C + 26) - 'A';
    else if ('0' <= C && C <= '9')
        Result = (C + 52) - '0';
    else if (C == ' ')
        Result = 62;
    else
    {
        Assert(C == '\'');
        Result = 63;
    }
    return Result;
}

inline void
PushText(render_group *RenderGroup, char *String, font *Font, f32 Scale, v2 P, v4 Color)
{
    char *C = String;
    //rectangle2i FontBox = Font->Box;
    v2 CurrentP = P;
    s32 PreviousGlyphIndex = -1;
    while (*C)
    {
        u32 GlyphIndex = CharToGlyphIndex(*C);
        glyph *Glyph = Font->Glyphs + GlyphIndex;
        
        Assert(Glyph->Bitmap.Memory);
        Assert(Glyph->Bitmap.Width <= 1000);
        Assert(Glyph->Bitmap.Height <= 1000);
        Assert(Glyph->Bitmap.Pitch <= 4*(Glyph->Bitmap.Width + 2));
        
        f32 GlyphWidth = Scale * (Glyph->Box.MaxX - Glyph->Box.MinX);
        f32 GlyphHeight = Scale * (Glyph->Box.MaxY - Glyph->Box.MinY);
        
        s32 KernAdvanceInt;
        if (PreviousGlyphIndex >= 0)
            KernAdvanceInt = Font->KerningTable[sizeof(CHAR_TABLE)*PreviousGlyphIndex + GlyphIndex];
        else
            KernAdvanceInt = 0;
        f32 KernAdvance = Scale * KernAdvanceInt;
        f32 GlyphAdvanceWidth = Scale * Glyph->AdvanceWidth + KernAdvance;
        
        v2 XAxis = V2(GlyphWidth, 0);
        v2 YAxis = V2(0, GlyphHeight);
        
        v2 AdjustedP = CurrentP + Scale * V2i(Glyph->Box.MinX, -Glyph->Box.MaxY);
        PushBitmap(RenderGroup, &Glyph->Bitmap, AdjustedP, XAxis, YAxis, Color);
        CurrentP += Font->SF * V2(GlyphAdvanceWidth, 0);
        PreviousGlyphIndex = GlyphIndex;
        C++;
    }
}

internal void
PushNumber(render_group *Group, u32 Number, font *Font, f32 Scale, v2 P, v4 Color)
{
    char Chars[11];
    char String[11];
    char *Dest = String;
    
    s32 DigitCount = 0;
    for (; DigitCount < (s32)ArrayCount(Chars) && Number; ++DigitCount)
    {
        Chars[DigitCount] = (Number % 10) + '0';
        Number /= 10;
    }
    
    if (DigitCount == 0)
    {
        *Dest++ = '0';
        *Dest = 0;
    }
    else
    {
        for (s32 i = DigitCount-1; i >= 0; --i)
        {
            *Dest++ = Chars[i];
        }
        *Dest = 0;
    }
    
    PushText(Group, String, Font, Scale, P, Color);
}
