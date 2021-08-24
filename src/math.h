
struct v2
{
    f32 x;
    f32 y;
};

union v3
{
    struct
    {
        f32 x;
        f32 y;
        f32 z;
    };
    struct
    {
        f32 r;
        f32 g;
        f32 b;
    };
};

union v4
{
    struct 
    {
        union
        {
            struct
            {
                f32 x;
                f32 y;
                f32 z;
            };
            v3 xyz;
        };
        f32 w;
    };
    
    struct
    {
        union
        {
            struct
            {
                f32 r;
                f32 g;
                f32 b;
            };
            v3 rgb;
        };
        f32 a;
    };
};

internal v2
V2(f32 x, f32 y)
{
    v2 Result = {x, y};
    return Result;
}

internal v2
V2i(s32 x, s32 y)
{
    v2 Result = {(f32)x, (f32)y};
    return Result;
}

internal v3
V3(f32 x, f32 y, f32 z)
{
    v3 Result = {x, y, z};
    return Result;
}

internal v4
V4(v3 Vector, f32 w)
{
    v4 Result = {Vector.x, Vector.y, Vector.z, w};
    return Result;
}

internal v4
V4(f32 x, f32 y, f32 z, f32 w)
{
    v4 Result = {x, y, z, w};
    return Result;
}

internal v4
V4i(s32 x, s32 y, s32 z, s32 w)
{
    v4 Result = {(f32)x, (f32)y, (f32)z, (f32)w};
    return Result;
}

struct rectangle2
{
    v2 Min;
    v2 Max;
};

struct rectangle2i
{
    s32 MinX;
    s32 MinY;
    s32 MaxX;
    s32 MaxY;
};

inline v2
operator+(v2 A, v2 B)
{
    v2 Result = {A.x + B.x, A.y + B.y};
    return Result;
}

inline v3
operator+(v3 A, v3 B)
{
    v3 Result = {A.x + B.x, A.y + B.y, A.z + B.z};
    return Result;
}

inline v2 &
operator+=(v2 &A, v2 B)
{
    A = A + B;
    return A;
}

inline v2
operator-(v2 A, v2 B)
{
    v2 Result = {A.x - B.x, A.y - B.y};
    return Result;
}

inline v2
operator-(v2 A)
{
    v2 Result = {-A.x, -A.y};
    return Result;
}

inline v2 &
operator-=(v2 &A, v2 B)
{
    A = A - B;
    return A;
}

inline v2
operator*(f32 Scalar, v2 V)
{
    v2 Result = {V.x * Scalar, V.y * Scalar};
    return Result;
}

inline v2
operator*(v2 V, f32 Scalar)
{
    v2 Result = {V.x * Scalar, V.y * Scalar};
    return Result;
}

inline v2 &
operator*=(v2 &V, f32 Scalar)
{
    V = Scalar * V;
    return V;
}

inline v3
operator*(v3 V, f32 Scalar)
{
    v3 Result;
    Result.x = V.x * Scalar;
    Result.y = V.y * Scalar;
    Result.z = V.z * Scalar;
    return Result;
}

inline v3
operator*(f32 Scalar, v3 V)
{
    v3 Result;
    Result.x = Scalar * V.x;
    Result.y = Scalar * V.y;
    Result.z = Scalar * V.z;
    return Result;
}

inline v3 &
operator*=(v3 &V, f32 Scalar)
{
    V = Scalar * V;
    return V;
}

inline v4
operator+(v4 A, v4 B)
{
    v4 Result;
    Result.x = A.x + B.x;
    Result.y = A.y + B.y;
    Result.z = A.z + B.z;
    Result.w = A.w + B.w;
    return Result;
}

inline v4 &
operator+=(v4 &A, v4 B)
{
    A = A + B;
    return A;
}

inline v4
operator-(v4 A, v4 B)
{
    v4 Result;
    Result.x = A.x - B.x;
    Result.y = A.y - B.y;
    Result.z = A.z - B.z;
    Result.w = A.w - B.w;
    return Result;
}


inline v4 &
operator-=(v4 &A, v4 B)
{
    A = A - B;
    return A;
}

inline v4
operator*(f32 Scalar, v4 V)
{
    v4 Result;
    Result.x = V.x * Scalar;
    Result.y = V.y * Scalar;
    Result.z = V.z * Scalar;
    Result.w = V.w * Scalar;
    return Result;
}

inline v4
operator*(v4 V, f32 Scalar)
{
    v4 Result;
    Result.x = V.x * Scalar;
    Result.y = V.y * Scalar;
    Result.z = V.z * Scalar;
    Result.w = V.w * Scalar;
    return Result;
}

inline v4 &
operator*=(v4 &V, f32 Scalar)
{
    V = Scalar * V;
    return V;
}

internal u32
PackPixel(v4 Color)
{
    u32 Result = (((u32)(Color.r * 255.0f + 0.5f) << 16) |
                  ((u32)(Color.g * 255.0f + 0.5f) << 8) |
                  ((u32)(Color.b * 255.0f + 0.5f) << 0) |
                  ((u32)(Color.a * 255.0f + 0.5f) << 24) );
    return Result;
}

internal f32
Inner(v2 A, v2 B)
{
    f32 Result = A.x * B.x + A.y * B.y;
    return Result;
}

internal rectangle2
RectCenterDim(v2 Center, v2 Dim)
{
    rectangle2 Result;
    v2 HalfDim = Dim * 0.5f;
    Result.Min = Center - HalfDim;
    Result.Max = Center + HalfDim;
    return Result;
}

internal b32
IsInRectangle(rectangle2 Rect, v2 P)
{
    b32 Result = (Rect.Min.x <= P.x && P.x < Rect.Max.x &&
                  Rect.Min.y <= P.y && P.y < Rect.Max.y);
    return Result;
}

internal b32
RectanglesIntersect(rectangle2 A, rectangle2 B)
{
    b32 Result = (A.Min.x <= B.Max.x && B.Min.x <= A.Max.x &&
                  A.Min.y <= B.Max.y && B.Min.y <= A.Max.y);
    return Result;
}

internal f32
Lerp(f32 A, f32 t, f32 B)
{
    f32 Result = (1.0f - t) * A + t*B;
    return Result;
}

internal v2
Lerp(v2 A, f32 t, v2 B)
{
    v2 Result = (1.0f - t) * A + t*B;
    return Result;
}
internal v4
Lerp(v4 A, f32 t, v4 B)
{
    v4 Result = (1.0f - t) * A + t*B;
    return Result;
}

struct random_series
{
    u32 State;
};

internal random_series
RandomSeries(u32 Seed)
{
    random_series Series;
    Series.State = Seed;
    return Series;
}

internal u32
Xorshift32(random_series *Series)
{
    // NOTE(vincent): c.f. Marsaglia, "Xorshift PRNGs"
    u32 X = Series->State;
    X ^= X << 13;
    X ^= X >> 17;
    X ^= X << 5;
    Series->State = X;
    return X;
}

internal u32
RandomU32(random_series *Series, u32 Min, u32 Max)
{
    // NOTE(vincent): Not exactly uniform distribution generally.
    // it would be if Max - Min + 1 were a power of two.
    u32 Raw = Xorshift32(Series);
    u32 Result = Raw % (Max - Min + 1);
    Result += Min;
    return Result;
}

internal s32
RandomS32(random_series *Series, s32 Min, s32 Max)
{
    u32 Raw = Xorshift32(Series);
    u32 Remainder = Raw % (Max - Min + 1);
    s32 Result = Remainder + Min;
    return Result;
}

internal f32
Random01(random_series *Series)
{
    f32 Result = (f32)((f32)Xorshift32(Series) / (f32)0xFFFFFFFF);
    Assert(0 <= Result && Result <= 1);
    return Result;
}

internal f32
Random11(random_series *Series)
{
    f32 Result = 2.0f * Random01(Series) - 1.0f;
    Assert(-1.0f <= Result && Result <= 1.0f);
    return Result;
}

internal f32
RandomF32(random_series *Series, f32 Min, f32 Max)
{
    f32 Result = Random01(Series) * (Max - Min) + Min;
    return Result;
}

struct disc
{
    v2 Center;
    f32 Radius;
};

internal b32
PointIsInDisc(disc Disc, v2 Point)
{
    f32 X = Point.x - Disc.Center.x;
    f32 Y = Point.y - Disc.Center.y;
    f32 DistanceSquared = X*X + Y*Y;
    b32 InDisc = (DistanceSquared <= Disc.Radius * Disc.Radius);
    return InDisc;
}

internal disc
DiscCenterRadius(v2 Center, f32 Radius)
{
    disc Result;
    Result.Center = Center;
    Result.Radius = Radius;
    return Result;
}

internal f32
AbsoluteValue(f32 X)
{
    if (X < 0)
        return -X;
    return X;
}

internal u32
AbsoluteDifference(u32 A, u32 B)
{
    u32 Result = (A <= B ? B - A : A - B);
    return Result;
}

// TODO(not-set): Chess probably doesn't need math.h
#include <math.h>
internal f32
Cos(f32 X)
{
    f32 Result = (f32)cosf(X);
    return Result;
}

internal f32
Sin(f32 X)
{
    f32 Result = (f32)sinf(X);
    return Result;
}

internal s32
Ceil(f32 X)
{
    s32 Result = (s32)ceilf(X);
    return Result;
}

internal f32
Sqrt(f32 X)
{
    f32 Result = (f32)sqrt(X);
    return Result;
}

internal s32
Floor(f32 X)
{
    f32 Result = (s32)floorf(X);
    return Result;
}

internal f32
Frac(f32 X)
{
    f32 Result = X - floorf(X);
    return Result;
}

internal f32
Norm(v2 Vector)
{
    f32 Result = Sqrt(Inner(Vector, Vector));
    return Result;
}

#define TAU 6.283185307179586476925286766559f
internal v2
RandomUnitVector(random_series *PRNG)
{
    f32 RandomAngle = RandomF32(PRNG, 0, TAU);
    v2 Result = V2(Cos(RandomAngle), Sin(RandomAngle));
    return Result;
}

internal f32
SafeRatio1(f32 Dividend, f32 Divisor)
{
    if (Divisor == 0.0f)
        return 1.0f;
    return Dividend / Divisor;
}

internal f32
SafeRatio0(f32 Dividend, f32 Divisor)
{
    if (Divisor == 0.0f)
        return 0.0f;
    return Dividend / Divisor;
}

internal f32
Minimum(f32 A, f32 B)
{
    if (A <= B)
        return A;
    return B;
}

internal f32
Minimum(s32 A, s32 B)
{
    if (A <= B)
        return A;
    return B;
}

internal f32
Minimum(u32 A, u32 B)
{
    if (A <= B)
        return A;
    return B;
}

internal f32
Maximum(f32 A, f32 B)
{
    if (A >= B)
        return A;
    return B;
}

internal s32
Maximum(s32 A, s32 B)
{
    if (A >= B)
        return A;
    return B;
}

internal u32
Maximum(u32 A, u32 B)
{
    if (A >= B)
        return A;
    return B;
}

internal v4
InvertColor(v4 Color)
{
    v4 Result;
    Result.r = 1-Color.r;
    Result.g = 1-Color.g;
    Result.b = 1-Color.b;
    Result.a = 1-Color.a;
    return Result;
}

internal s32
Clamp(s32 Value, s32 Min, s32 Max)
{
    Assert(Min <= Max);
    if (Value < Min)
        Value = Min;
    else if (Value > Max)
        Value = Max;
    Assert(Min <= Value && Value <= Max);
    return Value;
}

internal f32
Clamp(f32 Value, f32 Min, f32 Max)
{
    Assert(Min <= Max);
    f32 Result = Value;
    if (Result < Value)
        Result = Min;
    else if (Max < Result)
        Result = Max;
    return Result;
}


internal f32
Clamp01(f32 Value)
{
    f32 Result = Value;
    if (Result < 0.0f)
        Result = 0.0f;
    else if (Result > 1.0f)
        Result = 1.0f;
    return Result;
}

internal f32
Clamp11(f32 Value)
{
    f32 Result = Value;
    if (Result < -1.0f)
        Result = -1.0f;
    else if (Result > 1.0f)
        Result = 1.0f;
    return Result;
}

internal v2
Clamp11Vector(v2 V)
{
    v2 Result = {Clamp11(V.x), Clamp11(V.y)};
    return Result;
}

internal v2
Perp(v2 Vector)
{
    v2 Result = {-Vector.y, Vector.x};
    return Result;
}

internal s32
Sign(s32 X)
{
    s32 Result = X >= 0 ? 1 : -1;
    return Result;
}

internal rectangle2i
InvertedInfinityRectangle()
{
    rectangle2i Result;
    Result.MinX = 0x80000000;
    Result.MinY = 0x80000000;
    Result.MaxX = 0x7FFFFFFF;
    Result.MaxY = 0x7FFFFFFF;
    return Result;
}

internal rectangle2i
Intersect(rectangle2i A, rectangle2i B)
{
    rectangle2i Result;
    Result.MinX = A.MinX > B.MinX ? A.MinX : B.MinX;
    Result.MinY = A.MinY > B.MinY ? A.MinY : B.MinY;
    Result.MaxX = A.MaxX < B.MaxX ? A.MaxX : B.MaxX;
    Result.MaxY = A.MaxY < B.MaxY ? A.MaxY : B.MaxY;
    return Result;
}

internal b32
HasArea(rectangle2i R)
{
    b32 Result = R.MinX < R.MaxX && R.MinY < R.MaxY;
    return Result;
}

internal u32
BroadcastByte(u8 Byte)
{
    u32 Result = (Byte << 0) | (Byte << 8) | (Byte << 16) | (Byte << 24);
    return Result;
}

internal b32
AllBytesEqual(u32 Word)
{
    b32 Result = false;
    u8 Byte1 = Word & 0xff;
    u8 Byte2 = (Word >> 8) & 0xff;
    u8 Byte3 = (Word >> 16) & 0xff;
    u8 Byte4 = Word >> 24;
    if (Byte1 == Byte2 && Byte2 == Byte3 && Byte3 == Byte4)
        Result = true;
    return Result;
}

internal u32
WrappedIncrement(u32 X, u32 Modulo)
{
    u32 Result = X + 1;
    if (Result >= Modulo)
        Result = 0;
    return Result;
}

internal s32
WrappedDecrement(s32 X, s32 Modulo)
{
    s32 Result = X - 1;
    if (Result < 0)
        Result = Modulo - 1;
    return Result;
}

internal s32
WrappedSum(s32 A, s32 B, s32 Mod)
{
    Assert(Mod >= 1);
    s32 Result = A + B;
    // NOTE(vincent): Not the greatest implementation for general use case
    while (Result >= Mod)
        Result -= Mod;
    while (Result < 0)
        Result += Mod;
    return Result;
}

internal b32
Implies(b32 A, b32 B)
{
    b32 Result = (!A || B);
    return Result;
}

internal b32
VectorsEqual(v2 A, v2 B)
{
    b32 Result = false;
    if (A.x == B.x && A.y == B.y)
        Result = true;
    return Result;
}

struct transition_clock
{
    f32 Elapsed;
    f32 Duration;
};

internal f32
RatioFromTransitionClock(transition_clock Clock)
{
    f32 Result = Clamp01(SafeRatio1(Clock.Elapsed, Clock.Duration));
    Assert(0.0f <= Result && Result <= 1.0f);
    return Result;
}

struct moving_v2
{
    transition_clock Clock;
    v2 Start;
    v2 Current;
    v2 End;
};


internal void
InitMovingV2(moving_v2 *V, v2 Start, v2 End, f32 Duration)
{
    V->Clock.Duration = Duration;
    V->Clock.Elapsed = 0.0f;
    V->Start = Start;
    V->Current = Start;
    V->End = End;
}

internal void
InitMovingV2FromCurrent(moving_v2 *V, v2 End, f32 Duration)
{
    V->Clock.Duration = Duration;
    V->Clock.Elapsed = 0.0f;
    V->Start = V->Current;
    V->End = End;
}

internal void
AdjustMovingV2(moving_v2 *V, v2 Start, v2 End)
{
    V->Start = Start;
    V->End = End;
}

internal f32
Square(f32 t)
{
    f32 Result = t*t;
    return Result;
}

internal f32
SquareRoot(f32 t)
{
    // TODO(vincent): Prefer hardware instruction instead of full software Newton-Raphson
    Assert(t >= 0.0f);
    f32 x = t;//100.0f; 
    x = 0.5f*(x + SafeRatio0(t, x));
    x = 0.5f*(x + SafeRatio0(t, x));
    x = 0.5f*(x + SafeRatio0(t, x));
    x = 0.5f*(x + SafeRatio0(t, x));
    x = 0.5f*(x + SafeRatio0(t, x));
    x = 0.5f*(x + SafeRatio0(t, x));
    x = 0.5f*(x + SafeRatio0(t, x));
    x = 0.5f*(x + SafeRatio0(t, x));
    x = 0.5f*(x + SafeRatio0(t, x));
    x = 0.5f*(x + SafeRatio0(t, x));
    x = 0.5f*(x + SafeRatio0(t, x));
    x = 0.5f*(x + SafeRatio0(t, x));
    
    Assert(x >= 0.0f);
#if DEBUG
    f32 AbsError = AbsoluteValue(x*x - t);
    f32 RelError = SafeRatio0(AbsError, t);
    Assert(RelError <= 0.001f);
#endif
    return x;
}

internal f32
CubicHermite(f32 t)
{
    f32 Result = t*t*(3 - 2*t);
    Assert(!(0.0f <= t && t <= 1.0f) ||
           (0.0f <= Result && Result <= 1.0f)); 
    return t;
}
internal v2
HermiteInterpolation(v2 A, f32 t, v2 B)
{
    f32 LerpCoef = t*t*(3 - 2*t);
    Assert(!(0.0f <= t && t <= 1.0f) ||
           (0.0f <= LerpCoef && LerpCoef <= 1.0f)); 
    v2 Result = Lerp(A, LerpCoef, B);
    return Result;
}

internal void
AdvanceMovingV2(moving_v2 *V, f32 dt)
{
    V->Clock.Elapsed += dt;
    V->Current = HermiteInterpolation(V->Start, RatioFromTransitionClock(V->Clock), V->End);
}

internal v2
Hadamard(v2 A, v2 B)
{
    v2 Result = {A.x * B.x, A.y * B.y};
    return Result;
}

internal f32
SignOf(f32 t)
{
    f32 Result = 1.0f;
    if (t < 0.0f)
        Result = -1.0f;
    return Result;
}