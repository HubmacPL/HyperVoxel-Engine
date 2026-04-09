#version 450 core
// ── water.vert ────────────────────────────────────────────────────────────────
layout(location = 0) in uvec4 aPos;
layout(location = 1) in uvec4 aUVTile;
layout(location = 2) in vec3  aNormal;
layout(location = 3) in uint   aAO;

uniform mat4  u_MVP;
uniform vec3  u_ChunkOrigin;
uniform float u_Time;
uniform float u_FogStart;
uniform float u_FogEnd;

out vec2  v_UV;
out vec2  v_TileCoord;
out float v_FogFactor;
out float v_Depth;

void main() {
    float px = float(aPos.x);
    float py = float(aPos.y + aPos.z * 256u);
    float pz = float(aPos.w);

    vec3 worldPos = vec3(px, py, pz) + u_ChunkOrigin;

    // Gentle wave: displace Y slightly using sine of world XZ + time
    worldPos.y += sin(worldPos.x * 0.8 + u_Time * 1.2) * 0.06
                + sin(worldPos.z * 0.7 + u_Time * 1.0) * 0.06;

    vec4 clip = u_MVP * vec4(worldPos, 1.0);
    gl_Position = clip;

    v_UV        = vec2(float(aUVTile.x), float(aUVTile.y));
    v_TileCoord = vec2(float(aUVTile.z), float(aUVTile.w));
    v_Depth     = -clip.z / clip.w;
    v_FogFactor = clamp((v_Depth - u_FogStart) / (u_FogEnd - u_FogStart), 0.0, 1.0);
    v_FogFactor *= v_FogFactor;
}
