// PostFx: the full-screen passes (bloom, grade, lens, danger, the fill round a 16:9 picture). Compiled at build time
// (one .hlsl per entry point includes this file, see GE_06.vcxproj) into postfx_*.cso next to the program.
Texture2D g_Src : register(t0);
Texture2D g_Bloom : register(t1);
SamplerState g_Linear : register(s0);

cbuffer Params : register(b0)
{
    float2 g_Texel;      // 1 / size of the texture that is read
    float g_Threshold;
    float g_Knee;
    float3 g_Tint;
    float g_BloomStrength;
    float g_Exposure;
    float g_Saturation;
    float g_Contrast;
    float g_Vignette;
    float g_Aberration;
    float g_Grain;
    float g_Danger;
    float g_Time;
    float4 g_Rect;       // the 16:9 game rectangle in the back buffer, in 0..1 (x, y, w, h)
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// one triangle that covers the screen (no vertex buffer)
VSOut VSFull(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv = uv;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float3 Tap(Texture2D t, float2 uv)
{
    return t.SampleLevel(g_Linear, uv, 0).rgb;
}

// full size -> half size, keeps only what is brighter than the threshold (with a soft knee)
float4 PSPrefilter(VSOut i) : SV_TARGET
{
    float2 h = g_Texel * 0.5;
    float3 c = Tap(g_Src, i.uv + float2(-h.x, -h.y)) + Tap(g_Src, i.uv + float2(h.x, -h.y))
             + Tap(g_Src, i.uv + float2(-h.x, h.y)) + Tap(g_Src, i.uv + float2(h.x, h.y));
    c *= 0.25;
    float brightest = max(c.r, max(c.g, c.b));
    float soft = clamp(brightest - g_Threshold + g_Knee, 0.0, 2.0 * g_Knee);
    soft = soft * soft / (4.0 * g_Knee + 1e-4);
    float contribution = max(soft, brightest - g_Threshold) / max(brightest, 1e-4);
    return float4(c * contribution, 1.0);
}

// dual filter, down: the middle four times and the four corners
float4 PSDown(VSOut i) : SV_TARGET
{
    float2 h = g_Texel;
    float3 c = Tap(g_Src, i.uv) * 4.0;
    c += Tap(g_Src, i.uv - h);
    c += Tap(g_Src, i.uv + h);
    c += Tap(g_Src, i.uv + float2(h.x, -h.y));
    c += Tap(g_Src, i.uv - float2(h.x, -h.y));
    return float4(c / 8.0, 1.0);
}

// dual filter, up (added onto the bigger target with additive blending)
float4 PSUp(VSOut i) : SV_TARGET
{
    float2 h = g_Texel;
    float3 c = Tap(g_Src, i.uv + float2(-2.0 * h.x, 0.0));
    c += Tap(g_Src, i.uv + float2(-h.x, h.y)) * 2.0;
    c += Tap(g_Src, i.uv + float2(0.0, 2.0 * h.y));
    c += Tap(g_Src, i.uv + float2(h.x, h.y)) * 2.0;
    c += Tap(g_Src, i.uv + float2(2.0 * h.x, 0.0));
    c += Tap(g_Src, i.uv + float2(h.x, -h.y)) * 2.0;
    c += Tap(g_Src, i.uv + float2(0.0, -2.0 * h.y));
    c += Tap(g_Src, i.uv + float2(-h.x, -h.y)) * 2.0;
    return float4(c / 12.0, 1.0);
}

float Hash(float2 p)
{
    p = frac(p * float2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return frac((p.x + p.y) * p.x);
}

// The bars of a screen that is not 16:9: a soft, dark extension of the picture (a wide blur of its nearest edge, falling off), and the
// seam along the edge of the picture: an ivory hairline, and a red (top / left) or blue (bottom / right) hairline outside it.
float4 PSFill(VSOut i) : SV_TARGET
{
    float2 suv = (i.uv - g_Rect.xy) / g_Rect.zw;
    float2 c = clamp(suv, 0.0, 1.0);
    float3 col = 0.0;
    [unroll] for (int k = 0; k < 12; ++k)
    {
        float a = k * 0.5235988;
        float2 o = float2(cos(a), sin(a)) * float2(0.030, 0.055);
        col += g_Src.SampleLevel(g_Linear, clamp(c * 0.94 + 0.03 + o, 0.0, 1.0), 0).rgb;
    }
    col /= 12.0;
    float2 outsidePx = (suv - c) / g_Texel; // how far outside the picture, in pixels of the picture
    float d = length(outsidePx);
    col = min(col, 1.0) * 0.30 * exp(-d * 0.012);
    float hair = saturate(1.6 - abs(d - 1.5));
    float side = (suv.y < 0.0 || suv.x < 0.0) ? 1.0 : 0.0;
    float3 seam = lerp(float3(0.20, 0.55, 1.0), float3(1.0, 0.24, 0.20), side);
    float line2 = saturate(1.4 - abs(d - 7.0));
    col = col * (1.0 - hair) + float3(0.85, 0.85, 0.80) * hair * 0.55 + seam * line2 * 0.55;
    float noise = Hash(i.uv * 1024.0 + g_Time) - 0.5;
    col += noise * g_Grain;
    return float4(saturate(col), 1.0);
}

// the picture and its light, graded, onto the back buffer
float4 PSComposite(VSOut i) : SV_TARGET
{
    float2 d = i.uv - 0.5;
    float r2 = dot(d, d); // 0 in the middle, 0.5 in a corner
    float2 fringe = d * (g_Aberration * r2 * 4.0);
    float3 c;
    c.r = g_Src.SampleLevel(g_Linear, i.uv - fringe, 0).r;
    c.g = g_Src.SampleLevel(g_Linear, i.uv, 0).g;
    c.b = g_Src.SampleLevel(g_Linear, i.uv + fringe, 0).b;
    c += g_Bloom.SampleLevel(g_Linear, i.uv, 0).rgb * g_BloomStrength;
    c *= g_Exposure;

    // soft shoulder: unchanged up to 0.8, then the light rolls off towards 1 instead of clipping
    float3 over = max(c - 0.8, 0.0);
    c = min(c, 0.8) + 0.2 * (1.0 - exp(-over / 0.2));

    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = lerp(float3(luma, luma, luma), c, g_Saturation);
    c = (c - 0.5) * g_Contrast + 0.5;
    c *= g_Tint;

    float edge = smoothstep(0.08, 0.5, r2);
    c *= 1.0 - g_Vignette * edge;
    c = lerp(c, float3(0.85, 0.06, 0.08), saturate(g_Danger * smoothstep(0.12, 0.5, r2) * 0.75));

    float noise = Hash(i.uv * 1024.0 + g_Time) - 0.5;
    c += noise * g_Grain;
    return float4(saturate(c), 1.0);
}
