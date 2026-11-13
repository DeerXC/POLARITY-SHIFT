#pragma once

enum class Renderer2DBlendMode
{
    Alpha,
    Additive
};

bool Renderer2D_Initialize();
void Renderer2D_Finalize();
int Renderer2D_LoadTexture(const wchar_t* fileName);
void Renderer2D_SetBlendMode(Renderer2DBlendMode mode);
Renderer2DBlendMode Renderer2D_GetBlendMode();
void Renderer2D_SetCameraOffset(float offsetX, float offsetY);
void Renderer2D_ClearCameraOffset();

void Renderer2D_DrawQuad(float left, float top, float right, float bottom, float r, float g, float b, float a);

void Renderer2D_DrawTexture(int textureId, float left, float top, float right, float bottom, float r, float g, float b, float a,
                            float u0, float v0, float u1, float v1);

void Renderer2D_DrawTextureEx(int textureId, float centerX, float centerY, float width, float height, float rotationRadians,
                              float r, float g, float b, float a, float u0, float v0, float u1, float v1);

struct ID3D11ShaderResourceView;

// Polygons: convex, 3..32 vertices, any winding. Per-vertex colour; u / v only matter for the textured version.
struct Renderer2DVertex
{
    float x, y;       // normalized, like the other calls
    float r, g, b, a; // colour of this vertex, blended across the polygon
    float u = 0.0f;
    float v = 0.0f;
};
constexpr int RENDERER2D_EXTERNAL_TEXTURE_ID = 63; // texture id that Renderer2D_BindExternalTexture fills

void Renderer2D_DrawPolygon(const Renderer2DVertex* vertices, int count);
void Renderer2D_DrawPolygonTextured(int textureId, const Renderer2DVertex* vertices, int count);
void Renderer2D_BindExternalTexture(ID3D11ShaderResourceView* texture);

// A procedural surface (shaders/shader_pattern_2d.hlsl), described in the virtual 1280 x 720 UI space (y down).
enum class Renderer2DPatternKind
{
    Halftone, // diamonds in rows parallel to a line, shrinking with the distance from it (P5 print)
    Glass     // hairlines along and across a line and a glass slab parallel to it (P3)
};
struct Renderer2DPattern
{
    Renderer2DPatternKind kind = Renderer2DPatternKind::Halftone;
    float originX = 0.0f, originY = 0.0f; // a point on the line (virtual px)
    float alongX = 1.0f, alongY = 0.0f;   // unit vector along it
    float normalX = 0.0f, normalY = 1.0f; // unit normal into the surface
    float step = 17.0f, reach = 300.0f, radius = 4.4f; // halftone: spacing, distance at which the dots are gone, largest radius
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;      // the colour (glass: the lines) and its opacity
    float r2 = 1.0f, g2 = 1.0f, b2 = 1.0f;             // glass: the ivory edge of the slab
};
// Fills the convex polygon (normalized coordinates, like DrawPolygon) with the pattern; the alpha of the vertices is the strength of the
// whole surface. Alpha blended. Returns false when the pattern shader is not available (shader_pattern_2d.cso missing): draw it another way.
bool Renderer2D_DrawPolygonPattern(const Renderer2DVertex* vertices, int count, const Renderer2DPattern& pattern);

// Everything that is drawn (quads, textures, polygons) is collected into a batch: consecutive draws with the same texture and blend mode
// go to the GPU as ONE draw call. The batch is sent when the texture or the blend mode changes, when it is full, and by this call.
// Call it before the text is rendered and before the frame is presented.
void Renderer2D_Flush();

