// Pattern pixel shader of Renderer2D (Renderer2D_DrawPolygonPattern): the surfaces of the menu backdrop ("THE SEAM"), computed per pixel
// instead of being drawn as thousands of small CPU-built polygons. The polygon that is drawn is only the outline of the surface; this
// shader decides what is inside it. Exact at any resolution (the coverage of every dot and line is computed for the pixel size).
//
// Coordinates: the pattern is described in the virtual 1280 x 720 UI space (y down). SV_POSITION is in render target pixels, so
// g_View maps one to the other: virtual = (pixel - viewport origin) / scale.
//
//   kind 0  HALFTONE (the red half, P5 print): rows of diamonds parallel to the cut, every other row offset by half a spacing;
//           the diamonds are largest at the cut and shrink to nothing at `reach`
//   kind 1  GLASS (the blue half, P3): hairlines along the cut every 26 px (every 4th brighter), hairlines across it every 52 px,
//           and a glass slab parallel to the cut (a pale band with an ivory and a blue edge)
//
// The vertex colour's alpha is the strength of the whole surface (the backdrop fades layouts in and out with it).
// Output is premultiplied (Renderer2D blends ONE, INV_SRC_ALPHA).

cbuffer Pattern : register(b0)
{
    float4 g_View;   // x, y: viewport origin (px); z: px per virtual px; w: kind
    float4 g_Axes;   // xy: a point on the cut (virtual px); zw: unit vector along the cut
    float4 g_Normal; // xy: unit normal into the surface; z: spacing; w: reach (halftone)
    float4 g_Color;  // rgb: colour; a: its opacity
    float4 g_Color2; // rgb: the second colour (glass: the ivory edge); a: largest dot radius (halftone)
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD;
};

// how much of a pixel of size `px` (centred `dist` from the middle of a line `width` wide) the line covers
float LineCoverage(float dist, float width, float px)
{
    const float lo = max(dist - px * 0.5, -width * 0.5);
    const float hi = min(dist + px * 0.5, width * 0.5);
    return saturate((hi - lo) / px);
}

// premultiplied "over"
float4 Over(float4 below, float3 rgb, float a)
{
    return float4(rgb * a + below.rgb * (1.0 - a), a + below.a * (1.0 - a));
}

float4 Halftone(float2 p, float px)
{
    const float2 d = p - g_Axes.xy;
    const float along = dot(d, g_Axes.zw);
    const float across = dot(d, g_Normal.xy);
    const float spacing = g_Normal.z;
    const float reach = g_Normal.w;
    const float row = max(0.0, round((across - 10.0) / spacing));
    const float shift = fmod(row, 2.0) * spacing * 0.5;
    const float col = round((along - shift) / spacing);
    const float rowOut = 10.0 + row * spacing;
    const float2 centre = g_Axes.xy + g_Axes.zw * (col * spacing + shift) + g_Normal.xy * rowOut;
    const float k = saturate(1.0 - rowOut / reach);
    const float radius = g_Color2.a * k * k;
    if (radius < 0.7) return 0.0;
    const float l1 = abs(p.x - centre.x) + abs(p.y - centre.y); // a diamond: the points within radius in L1
    const float a = g_Color.a * saturate((radius - l1) / px + 0.5);
    return float4(g_Color.rgb * a, a);
}

float4 Glass(float2 p, float px)
{
    const float2 d = p - g_Axes.xy;
    const float along = dot(d, g_Axes.zw);
    const float across = dot(d, g_Normal.xy);
    float4 c = 0.0;
    // lines along the cut, every 26 px (1..26), every 4th brighter
    const float i = round(across / 26.0);
    if (i >= 1.0 && i <= 26.0)
        c = Over(c, g_Color.rgb, (fmod(i, 4.0) == 0.0 ? 0.10 : 0.05) * LineCoverage(across - i * 26.0, 1.0, px));
    // lines across it, every 52 px
    const float j = round(along / 52.0);
    c = Over(c, g_Color.rgb, 0.035 * LineCoverage(along - j * 52.0, 1.0, px));
    // the glass slab: 120..196 px from the cut, a pale fill and two edges
    const float inSlab = LineCoverage(across - 158.0, 76.0, px);
    c = Over(c, float3(0.60, 0.85, 1.0), 0.045 * inSlab);
    c = Over(c, g_Color2.rgb, 0.22 * LineCoverage(across - 120.0, 1.0, px));
    c = Over(c, g_Color.rgb, 0.30 * LineCoverage(across - 196.0, 1.0, px));
    return c;
}

float4 PSPattern(PS_INPUT input) : SV_TARGET
{
    const float2 p = (input.position.xy - g_View.xy) / g_View.z; // virtual px
    const float px = 1.0 / g_View.z;                            // one pixel, in virtual px
    const float4 c = g_View.w < 0.5 ? Halftone(p, px) : Glass(p, px);
    return c * input.color.a;
}
