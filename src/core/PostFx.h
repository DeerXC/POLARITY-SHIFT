#pragma once

// PostFx: the picture of the world is drawn into an HDR target (16-bit float per channel) and then put on the screen through a chain of
// full-screen passes:
//   BLOOM    the bright parts (bullets, beams, engines, explosions: additive light that goes above 1.0) are blurred over a chain of five
//            half-size targets (dual filter: down, then up while adding) and added back: the light glows into its surroundings
//   GRADE    exposure, a soft shoulder (bright light rolls off instead of clipping), saturation, contrast and a tint per stage of the game
//   LENS     a vignette, a slight colour fringe towards the corners (chromatic aberration) and a fine film grain
//   DANGER   a red glow at the edges of the screen when the ship is nearly dead
// SUPERSAMPLING: the world can be drawn larger than the screen (DirectXSystem::SetRenderSize, the RESOLUTION of the settings in full screen: 4K on a 1440p screen). The graded picture
// is then scaled down to the screen by two Lanczos passes in linear light (RESAMPLE: horizontal, then vertical, see DisplayMath.h and shaders/postfx.hlsli). The back buffer is the
// size of the screen, so Windows never stretches it, and the text of the UI is drawn after it at the size of the screen (Direct2D, straight onto the back buffer): always sharp.
// The shaders are compiled at build time (shaders/postfx*.hlsl -> postfx_*.cso). If anything fails, the post effects are off and the game is drawn as before;
// the reason is written to postfx_error.txt next to the program. config.ini: post_fx=0 turns them off.
namespace PostFx
{
struct Look
{
    float tint[3] = {1.0f, 1.0f, 1.0f};
    float saturation = 1.06f;
    float contrast = 1.05f;
    float exposure = 1.0f;
    float bloom = 0.55f;      // how much of the blurred light is added (was 0.85: the flat red / cyan fields of the title and the white of the logo glowed)
    float threshold = 0.86f;  // brightness from which a pixel glows (was 0.72: flat #E8141C and #10B0F0 are above that, 0.86 lets only real light through)
    float vignette = 0.32f;
    float aberration = 0.0f;  // (the style: the picture is sharp; the colour fringe only flashes up on a hit or a cut-in, see Kick)
    float grain = 0.015f;
    float danger = 0.0f;      // 0..1: the red edge
};

bool Initialize(); // after DirectXSystem and Renderer2D
void Finalize();
bool IsActive();
// The size of the picture of the world as it is drawn now (the render size, larger than the screen for a supersample); false when the post effects are off.
bool GetSceneSize(int& width, int& height);

void BeginScene(float r, float g, float b); // the world is drawn into the HDR target from here on
void EndScene();                            // ... and put on the back buffer through the passes (call after Renderer2D_Flush)

void SetLook(const Look& look); // the look of this frame (the play scene sets it every frame; the menus use the default)
// A short burst of colour fringe (chromatic aberration) on top of the look: the ship is hit, a cut-in slams in. It fades in ~0.3 s.
void Kick(float aberration);
Look DefaultLook();
}




