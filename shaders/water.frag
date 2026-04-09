#version 450 core
// ── water.frag ────────────────────────────────────────────────────────────────
in vec2  v_UV;
in vec2  v_TileCoord;
in float v_FogFactor;
in float v_Depth;

out vec4 FragColor;

uniform sampler2D u_Atlas;
uniform vec3      u_SkyColor;
uniform float     u_Time;

const float kTileUVSize  = 1.0 / 16.0;
const float kHalfTexel   = 0.5 / 256.0;

vec4 sampleAtlas(vec2 uv, vec2 tile) {
    vec2 c = clamp(uv, kHalfTexel * 16.0, 1.0 - kHalfTexel * 16.0);
    return texture(u_Atlas, (tile + c) * kTileUVSize);
}

void main() {
    // Scrolling UV to simulate flow
    vec2 scrolledUV = v_UV + vec2(u_Time * 0.05, u_Time * 0.03);
    scrolledUV = fract(scrolledUV);  // wrap within tile

    vec4 waterColor = sampleAtlas(scrolledUV, v_TileCoord);

    // Tint water blue and make semi-transparent
    vec3 tinted = mix(waterColor.rgb, vec3(0.1, 0.35, 0.8), 0.5);
    float alpha  = 0.72;

    vec3 final = mix(tinted, u_SkyColor, v_FogFactor);
    FragColor = vec4(final, alpha);
}
