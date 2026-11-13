// The pixel shader of Renderer2D: every sprite, quad and polygon of the game.
// Textures are stored with PREMULTIPLIED alpha (core/TextureLoader); the vertex colour is a straight tint (r, g, b) and an opacity (a).
// The output is premultiplied too, so one formula serves both blend states of Renderer2D:
//   Alpha    : ONE, INV_SRC_ALPHA  ->  colour over what is there
//   Additive : ONE, ONE            ->  light added (colour x opacity), the alpha of the output is not used
// (Premultiplied textures filter without the dark / bright fringe that straight alpha gives at the edge of a sprite.)
Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD;
};

float4 PSMain(PS_INPUT input) : SV_TARGET
{
    const float4 texel = g_Texture.Sample(g_Sampler, input.texcoord);
    const float4 tint = float4(input.color.rgb * input.color.a, input.color.a);
    return texel * tint;
}
