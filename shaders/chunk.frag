#version 450 core

// ─────────────────────────────────────────────────────────────────────────────
//  Chunk fragment shader
//
//  Lighting model (linear, then gamma-corrected at output):
//    skylight curve  : geometric falloff 0.8^(15-L)   (Minecraft style)
//    direct sunlight : (face-vs-sun) × (sky-exposure) × (sun-above-horizon)
//                      — turns the sun's motion into visible side-lighting
//                        without rebuilding any BFS data.
//    night ambient   : non-zero floor for moonlight; sky tint shifts cool.
// ─────────────────────────────────────────────────────────────────────────────

in vec2  v_UV;
in vec2  v_TileCoord;
in float v_AO;
in float v_FogFactor;
in vec3  v_Normal;
in float v_Depth;
in float v_Skylight;
in float v_Blocklight;

out vec4 FragColor;

uniform sampler2D u_Atlas;       // 256×256 texture (16×16 tiles, each 16px)
uniform vec3      u_SkyColor;    // horizon/fog blend target
uniform vec3      u_SunColor;    // warm at dusk, neutral at noon (CPU-set)
uniform vec3      u_SunDir;      // normalised sun direction (CPU-set)
uniform float     u_DayNight;    // 0=night, 1=full day (smoothstep on CPU)

// ── Atlas sampling ────────────────────────────────────────────────────────────
const float kAtlasSize  = 16.0;
const float kTileUVSize = 1.0 / 16.0;
const float kHalfTexel  = 0.5 / 256.0;

vec4 sampleAtlas(vec2 uv, vec2 tile) {
    vec2 clamped = clamp(uv, kHalfTexel * kAtlasSize, 1.0 - kHalfTexel * kAtlasSize);
    vec2 atlasUV = (tile + clamped) * kTileUVSize;
    return texture(u_Atlas, atlasUV);
}

void main() {
    // ── Texture sample ────────────────────────────────────────────────────────
    vec4 albedo = sampleAtlas(v_UV, v_TileCoord);
    if (albedo.a < 0.1) discard;

    // ── Skylight / blocklight curves (linear-light space) ─────────────────────
    // v_Skylight already in [0,1] = L/15 with L the BFS value 0..15.
    // 0.8^(15-L) gives a smooth, Minecraft-like perceptual falloff.
    float L         = v_Skylight   * 15.0;
    float Lb        = v_Blocklight * 15.0;
    float skyCurve  = pow(0.8, max(15.0 - L,  0.0));
    float blockCurve= pow(0.8, max(15.0 - Lb, 0.0));

    // ── Direct sunlight term: sun-facing × sky-exposed × sun-above-horizon ──
    // sunFacing  : Lambert wrt the sun. Surfaces whose normals point away
    //              from the sun naturally drop out of "direct" light.
    // sunExposed : only highly sky-exposed vertices count as in-the-sun. A
    //              face under a 1-block overhang already reads ~0.7 and
    //              fades out; deep caves (~0) are excluded outright.
    // sunAbove   : smoothly vanishes at dusk so the world stops being lit
    //              the moment the sun dips below the horizon.
    vec3  N         = normalize(v_Normal);
    vec3  sunDirN   = normalize(u_SunDir);
    float sunFacing = max(dot(N, -sunDirN), 0.0);
    // Wider smoothstep range (0.2→1.0 instead of 0.6→1.0) so the transition
    // from "in shadow" to "in sun" is gradual — prevents the sharp brightness
    // jump between ground-level blocks (skylight≈13-14) and blocks higher up
    // (skylight=15). AO already handles the contact-shadow at the base.
    float sunExposed= smoothstep(0.20, 1.0, v_Skylight);
    float sunAbove  = smoothstep(0.0, 0.15, -sunDirN.y);
    float direct    = sunFacing * sunExposed * sunAbove;

    // ── Ambient (sky) term ────────────────────────────────────────────────────
    // Night floor very low (0.03) so midnight is genuinely dark.
    // Textures are stored in sRGB (display-ready), so NO gamma correction
    // at output — applying pow(x, 1/2.2) to sRGB values over-brightens and
    // washes out colours. Keep the pipeline in display space throughout.
    float skyBrightness = mix(0.03, 1.0, u_DayNight);
    float ambient       = max(skyCurve * skyBrightness, blockCurve);

    // Warm sun ↔ cool moon tint blend
    vec3  moonTint  = vec3(0.45, 0.55, 0.80);
    vec3  lightTint = mix(moonTint, u_SunColor, u_DayNight);

    // ── Combine, apply AO, clamp ──────────────────────────────────────────────
    // Direct term kept at 0.40 so sun-facing surfaces don't blow out.
    // Upper clamp 1.0 keeps colours in gamut without HDR.
    float lit = (ambient + direct * 0.40) * v_AO;
    lit = clamp(lit, 0.015, 1.0);

    vec3 litColor = albedo.rgb * lightTint * lit;

    // ── Fog blend ─────────────────────────────────────────────────────────────
    vec3 finalColor = mix(litColor, u_SkyColor, v_FogFactor);

    FragColor = vec4(finalColor, albedo.a);
}
