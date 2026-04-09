#version 450 core

// ─────────────────────────────────────────────────────────────────────────────
//  Chunk fragment shader
//
//  Samples a 16×16 tile texture atlas.
//  Applies vertex AO (baked during mesh construction) + directional diffuse.
//  Blends to sky colour at distance for atmospheric depth.
// ─────────────────────────────────────────────────────────────────────────────

in vec2  v_UV;
in vec2  v_TileCoord;
in float v_AO;
in float v_FogFactor;
in float v_NormalY;
in float v_Depth;
in float v_Lighting;
in float v_Skylight;
in float v_Blocklight;

out vec4 FragColor;

uniform sampler2D u_Atlas;       // 256×256 texture (16×16 tiles, each 16px)
uniform vec3      u_SkyColor;    // horizon/fog blend target
uniform vec3      u_SunColor;    // e.g. vec3(1.0, 0.95, 0.8) warm
uniform float     u_DayNight;    // 0=night, 1=full day

// ── Atlas sampling ────────────────────────────────────────────────────────────
// Atlas is 16 tiles wide × 16 tiles tall.
// Each tile occupies (1/16) of the atlas in each axis.
// v_TileCoord gives (col, row) in [0,15].
// v_UV gives position within the tile [0,1].
// We clamp the within-tile UV slightly to avoid bleeding between tiles.
const float kAtlasSize   = 16.0;        // tiles per row/col
const float kTileUVSize  = 1.0 / 16.0;  // UV size of one tile
const float kHalfTexel   = 0.5 / 256.0; // half-texel border to prevent bleeding

vec4 sampleAtlas(vec2 uv, vec2 tile) {
    // Clamp within-tile UV away from edges
    vec2 clamped = clamp(uv, kHalfTexel * kAtlasSize, 1.0 - kHalfTexel * kAtlasSize);
    vec2 atlasUV = (tile + clamped) * kTileUVSize;
    return texture(u_Atlas, atlasUV);
}

void main() {
    // ── Texture sample ────────────────────────────────────────────────────────
    vec4 albedo = sampleAtlas(v_UV, v_TileCoord);

    // Alpha test (for leaves, glass)
    if (albedo.a < 0.1) discard;

    // ── Lighting ──────────────────────────────────────────────────────────────
    // v_Skylight  : baked flood-fill skylight [0..1]  (0 = underground/occluded)
    // v_Blocklight: baked block light [0..1]           (no sources yet → 0)
    // v_Lighting  : sun directional + ambient (diff + 0.20) baked in vertex shader

    // Extract pure directional component (remove the pre-added ambient 0.20)
    float diff = max(v_Lighting - 0.20, 0.0);

    // Sky-driven ambient: sky open = bright, underground = dark
    float indirect = max(v_Skylight, v_Blocklight) * u_DayNight;

    // Directional boost on sun-facing surfaces (gated by skylight so caves stay dark)
    float direct = diff * v_Skylight * u_DayNight;

    // Combine, apply AO and enforce an absolute dark floor so nothing is pure black
    float lightVal = clamp((indirect * 0.75 + direct * 0.25 + 0.04) * v_AO, 0.04, 1.5);

    vec3 litColor = albedo.rgb * u_SunColor * lightVal;

    // ── Fog blend ─────────────────────────────────────────────────────────────
    vec3 finalColor = mix(litColor, u_SkyColor, v_FogFactor);

    FragColor = vec4(finalColor, albedo.a);
}
