struct bitmap
{
    s32 Width;
    s32 Height;
    s32 Pitch;
    void *Memory;
    u32 Handle;
};

enum render_entry_type
{
    RenderEntryType_render_entry_clear,
    RenderEntryType_render_entry_bitmap,
    RenderEntryType_render_entry_bitmap_no_blend,
    RenderEntryType_render_entry_rectangle,
};

struct render_entry_header
{
    render_entry_type Type;
};

struct render_entry_clear
{
    v4 Color;
};

struct render_entry_bitmap
{
    bitmap *Bitmap;
    v2 Origin;
    v2 XAxis;
    v2 YAxis;
    v4 Color;
};

struct render_entry_bitmap_no_blend
{
    bitmap *Bitmap;
    v2 Origin;
    v2 XAxis;
    v2 YAxis;
    v4 Color;
};

struct render_entry_rectangle
{
    v2 Min;
    v2 Max;
    v4 Color;
};

