// The vertex shader of Renderer2D: the positions arrive already in clip space (normalized -1..1, see Renderer2D.cpp), so it passes
// everything through. The 16:9 game rectangle of the back buffer is the viewport (DirectXSystem::GetGameRect).
struct VS_INPUT
{
    float3 position : POSITION;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD;
};

struct VS_OUTPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD;
};

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;
    output.position = float4(input.position, 1.0f);
    output.color = input.color;
    output.texcoord = input.texcoord;
    return output;
}
