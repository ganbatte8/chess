#include "common.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"


#if COMPILER_MSVC
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

PLATFORM_PUSH_READ_FILE(PushReadFile)
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

PLATFORM_WRITE_FILE(WriteFile)
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

#else
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>

PLATFORM_PUSH_READ_FILE(PushReadFile)
{
    string Result = {};
    
    int FileDescriptor = open(Filename, O_RDONLY);
    if (FileDescriptor >= 0)
    {
        struct stat FileInfo = {};
        fstat(FileDescriptor, &FileInfo);
        Result.Size = FileInfo.st_size;
        u32 AvailableSize = Arena->Size - Arena->Used;
        if (Result.Size <= AvailableSize)
        {
            Result.Base = PushArray(Arena, Result.Size, char);
            u32 WrittenSize = read(FileDescriptor, Result.Base, Result.Size);
            if (WrittenSize != Result.Size)
                Result.Base = 0;
        }
    }
    else
    {
        perror("open failed");
    }
    close(FileDescriptor);
    
    return Result;
}

PLATFORM_WRITE_FILE(WriteFile)
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

#endif


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

struct asset_file_header
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

struct bit_scan_result
{
    b32 Found;
    u32 Index;
};

internal bit_scan_result
FindLeastSignificantBit(u32 Number)
{
    bit_scan_result Result = {};
    for (u32 BitIndex = 0; BitIndex < 32; ++BitIndex)
    {
        if ((Number >> BitIndex) & 1)
        {
            Result.Found = true;
            Result.Index = BitIndex;
            break;
        }
    }
    
    return Result;
}

#pragma pack(push, 1)
struct bitmap_header
{
    u16 FileType;
    u32 FileSize;
    u16 Reserved1;
    u16 Reserved2;
    u32 BitmapOffset;
    u32 Size;
    s32 Width;
    s32 Height;
    u16 Planes;
    u16 BitsPerPixel;
    u32 Compression;
    u32 SizeOfBitmap;
    s32 HorzResolution;
    s32 VertResolution;
    u32 ColorsUsed;
    u32 ColorsImportant;
    
#if 0
    u32 RedMask;
    u32 GreenMask;
    u32 BlueMask;
#endif
};
#pragma pack(pop)


internal asset_file_header *
TestLoadAssetFile(memory_arena *Arena, char *Filename)
{
    string File = PushReadFile(Arena, Filename);
    asset_file_header *Result = 0;
    
    while (!File.Base)
    {
        File = PushReadFile(Arena, Filename);
    }
    
    if (File.Base)
    {
        Result = (asset_file_header *)File.Base;
        
#define AdjustBitmapOffset(Bitmap) \
Result->Bitmap.Memory =  (void *)((uintptr_t)Result->Bitmap.Memory + (u8 *)File.Base); \
Assert(Result->Bitmap.Memory); \
Assert(Result->Bitmap.Width <= 1000); \
Assert(Result->Bitmap.Height <= 1000); \
Assert(Result->Bitmap.Pitch <= 4*(Result->Bitmap.Width)); \
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
            Bitmap->Memory = (void *)((uintptr_t)Bitmap->Memory + (u8 *)File.Base);
            
            Assert(Bitmap->Memory);
            Assert(Bitmap->Width <= 1000);
            Assert(Bitmap->Height <= 1000);
            Assert(Bitmap->Pitch <= 4*(Bitmap->Width));
            Assert(Bitmap->Handle == 0);
        }
    }
    return Result;
}

internal void
LoadBitmapData(memory_arena *PackfileArena, memory_arena *ReadArena, bitmap *Bitmap, char* Filename)
{
    temporary_memory ReadMem = BeginTemporaryMemory(ReadArena);
    string ReadFile = PushReadFile(ReadArena, Filename);
    Assert(ReadFile.Base);
    
    
    if (ReadFile.Base)
    {
        bitmap_header *Header = (bitmap_header *)ReadFile.Base;
        
        Bitmap->Width = Header->Width;
        Bitmap->Height = Header->Height;
        
        //Assert(Header->Compression == 3);
#if 0
        u32 RedMask = Header->RedMask;
        u32 GreenMask = Header->GreenMask;
        u32 BlueMask = Header->BlueMask;
        u32 AlphaMask = ~(RedMask | GreenMask | BlueMask);
        
        bit_scan_result RedScan = FindLeastSignificantBit(RedMask);
        bit_scan_result GreenScan = FindLeastSignificantBit(GreenMask);
        bit_scan_result BlueScan = FindLeastSignificantBit(BlueMask);
        bit_scan_result AlphaScan = FindLeastSignificantBit(AlphaMask);
        
        Assert(RedScan.Found);
        Assert(GreenScan.Found);
        Assert(BlueScan.Found);
        Assert(AlphaScan.Found);
        
        s32 RedShiftDown = (s32) RedScan.Index;
        s32 GreenShiftDown = (s32) GreenScan.Index;
        s32 BlueShiftDown = (s32) BlueScan.Index;
        s32 AlphaShiftDown = (s32) AlphaScan.Index;
        f32 Inv255 = 1.0f / 255.0f;
        
        u32 *Pixels = (u32 *)((u8 *)ReadFile.Base + Header->BitmapOffset);
        u32 *SourceDest = Pixels;
        for (s32 Y = 0; Y < Header->Height; ++Y)
        {
            for (s32 X = 0; X < Header->Width; ++X)
            {
                u32 C = *SourceDest;
                v4 Texel = V4((f32)((C & RedMask) >> RedShiftDown),
                              (f32)((C & GreenMask) >> GreenShiftDown),
                              (f32)((C & BlueMask) >> BlueShiftDown),
                              (f32)((C & AlphaMask) >> AlphaShiftDown));
                
                // NOTE(vincent): Alpha premultiplication. we need to convert to [0,1] temporarily for it.
                Texel *= Inv255;
                Texel.rgb *= Texel.a;
                Texel *= 255.0f;
                
                *SourceDest++ = (((u32)(Texel.a + 0.5f) << 24) |
                                 ((u32)(Texel.r + 0.5f) << 16) |
                                 ((u32)(Texel.g + 0.5f) << 8) |
                                 ((u32)(Texel.b + 0.5f) << 0));
            }
        }
#else
        u32 *Source = (u32 *)((u8 *)Header + Header->BitmapOffset);
        u32 PixelCount = Bitmap->Width * Bitmap->Height;
        u32 *Dest = PushArray(PackfileArena, PixelCount, u32);
        Bitmap->Memory = (u8 *)Dest;
        
        for (s32 Y = 0; Y < Bitmap->Height; ++Y)
        {
            for (s32 X = 0; X < Bitmap->Width; ++X)
            {
                *Dest++ = *Source++;
            }
        }
#endif
    }
    
    Bitmap->Pitch = 4*Bitmap->Width;
    Bitmap->Memory = (void *)((u8 *)Bitmap->Memory - (u8 *)PackfileArena->Base);
    
    EndTemporaryMemory(ReadMem);
}


internal void
LoadFont(memory_arena *PackfileArena, memory_arena *ReadArena, font *Font, char *Filename, 
         f32 PixelHeight = 32.0f)
{
    temporary_memory ReadMem = BeginTemporaryMemory(ReadArena);
    string FontFile = PushReadFile(ReadArena, Filename);
    
    stbtt_fontinfo Info;
    
    Assert(FontFile.Base);
    
    
    if (FontFile.Base)
    {
        printf("Successfully read font file...\n");
        stbtt_InitFont(&Info, (u8 *)FontFile.Base,
                       stbtt_GetFontOffsetForIndex((u8 *)FontFile.Base, 0));
        
        stbtt_GetFontVMetrics(&Info, &Font->Ascent, &Font->Descent, &Font->LineGap);
        Font->SF = SafeRatio0(PixelHeight, (f32)(Font->Ascent - Font->Descent));
        stbtt_GetFontBoundingBox(&Info, &Font->Box.MinX, &Font->Box.MinY,
                                 &Font->Box.MaxX, &Font->Box.MaxY);
        
        printf("About to loop through code points\n");
        // NOTE(vincent): ascii values should coincide with codepoint values
        char *CodepointTable = CHAR_TABLE;  
        
        glyph *Glyph = Font->Glyphs;
        for (u32 TableIndex = 0; TableIndex < sizeof(CHAR_TABLE); ++TableIndex)
        {
            s32 Codepoint = CodepointTable[TableIndex];
            stbtt_GetCodepointBitmapBox(&Info, Codepoint, Font->SF, Font->SF,
                                        &Glyph->Box.MinX, &Glyph->Box.MinY,
                                        &Glyph->Box.MaxX, &Glyph->Box.MaxY);
            Glyph->Bitmap.Width = Glyph->Box.MaxX - Glyph->Box.MinX;
            Glyph->Bitmap.Height = Glyph->Box.MaxY - Glyph->Box.MinY;
            Glyph->Bitmap.Pitch = Glyph->Bitmap.Width * 4;
            Glyph->Bitmap.Handle = 0;
            Assert(Glyph->Bitmap.Width <= 1000);
            Assert(Glyph->Bitmap.Height <= 1000);
            Assert(Glyph->Bitmap.Pitch <= 1000);
            
            // TODO(not-set): Set Glyph->AdvanceHeight as well ??
            
            stbtt_GetCodepointHMetrics(&Info, Codepoint, &Glyph->AdvanceWidth, &Glyph->LeftSideBearing);
            u32 *Bitmap = PushArray(PackfileArena, Glyph->Bitmap.Width * Glyph->Bitmap.Height, u32);
            Glyph->Bitmap.Memory = (void *)((u8 *)Bitmap - (u8*)PackfileArena->Base);
            
            temporary_memory ScratchBitmapMemory = BeginTemporaryMemory(ReadArena);
            u8 *ScratchBitmapBase = (u8 *)PushSize(ReadArena, Glyph->Bitmap.Width * Glyph->Bitmap.Height);
            
            u32 ScratchPitch = Glyph->Bitmap.Width;
            stbtt_MakeCodepointBitmap(&Info, ScratchBitmapBase,
                                      Glyph->Bitmap.Width, Glyph->Bitmap.Height, ScratchPitch,
                                      Font->SF, Font->SF, Codepoint);
            
            u32 *Dest = Bitmap;
            for (s32 Row = Glyph->Bitmap.Height - 1; Row >= 0; --Row)
            {
                u8 *Source = ScratchBitmapBase + ScratchPitch * Row;
                for (s32 Col = 0; Col < Glyph->Bitmap.Width; ++Col)
                {
                    *Dest++ = BroadcastByte(*Source);
                    Source++;
                }
            }
            
            EndTemporaryMemory(ScratchBitmapMemory);
            
            for (u32 Index = 0; Index < sizeof(CHAR_TABLE); ++Index)
            {
                char PreviousChar = CHAR_TABLE[TableIndex];
                char Char = CHAR_TABLE[Index];
                s32 KernAdvanceInt = stbtt_GetCodepointKernAdvance(&Info, PreviousChar, Char);
                u32 KernTableIndex = sizeof(CHAR_TABLE) * TableIndex + Index;
                Font->KerningTable[KernTableIndex] = KernAdvanceInt;
                // NOTE(vincent): usage code:
                // Font->KerningTable[sizeof(CHAR_TABLE) * PreviousGlyphIndex + GlyphIndex];
            }
            
            ++Glyph;
            printf("TableIndex %d / %lu done \n", TableIndex, sizeof(CHAR_TABLE)-1);
        }
    }
    EndTemporaryMemory(ReadMem);
}

int main()
{
#if COMPILER_MSVC
    void *Memory = VirtualAlloc(0, Gigabytes(1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    void *Memory = 
        mmap(0, Gigabytes(1), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    Assert(Memory);
    
    memory_arena Arena;
    memory_arena PackfileArena;
    InitializeArena(&Arena, Gigabytes(1), Memory);
    SubArena(&PackfileArena, &Arena, Megabytes(64));
    
    asset_file_header *Header = PushStruct(&PackfileArena, asset_file_header);
    
#define LoadBMP(Bitmap, Filename) LoadBitmapData(&PackfileArena, &Arena, &Header->Bitmap, Filename);
    LoadBMP(WhitePawn, "assets/white_pawn.bmp");
    printf("loaded white_pawn.bmp\n");
    LoadBMP(WhiteRook, "assets/white_rook.bmp");
    printf("loaded white_rook.bmp\n");
    LoadBMP(WhiteKnight, "assets/white_knight.bmp");
    printf("loaded white_knight.bmp\n");
    LoadBMP(WhiteBishop, "assets/white_bishop.bmp");
    printf("loaded white_bishop.bmp\n");
    LoadBMP(WhiteKing, "assets/white_king.bmp");
    printf("loaded white_king.bmp\n");
    LoadBMP(WhiteQueen, "assets/white_queen.bmp");
    printf("loaded white_queen.bmp\n");
    
    LoadBMP(BlackPawn, "assets/black_pawn.bmp");
    printf("loaded black_pawn.bmp\n");
    LoadBMP(BlackRook, "assets/black_rook.bmp");
    printf("loaded black_rook.bmp\n");
    LoadBMP(BlackKnight, "assets/black_knight.bmp");
    printf("loaded black_knight.bmp\n");
    LoadBMP(BlackBishop, "assets/black_bishop.bmp");
    printf("loaded black_bishop.bmp\n");
    LoadBMP(BlackKing, "assets/black_king.bmp");
    printf("loaded black_king.bmp\n");
    LoadBMP(BlackQueen, "assets/black_queen.bmp");
    printf("loaded black_queen.bmp\n");
    
    
#if COMPILER_MSVC
    LoadFont(&PackfileArena, &Arena, &Header->Font, "C:\\Windows\\Fonts\\segoeui.ttf");
#else
    LoadFont(&PackfileArena, &Arena, &Header->Font, "/usr/share/fonts/TTF/Inconsolata-Regular.ttf`");
#endif
    
    printf("LoadFont done\n");
    
    WriteFile("chess_asset_file", PackfileArena.Used, PackfileArena.Base);
    
    printf("WriteFile done\n");
    
    TestLoadAssetFile(&Arena, "chess_asset_file");
    printf("Test done, your finished\n");
    
    return 0;
}