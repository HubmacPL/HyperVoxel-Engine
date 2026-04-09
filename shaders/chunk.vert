#version 450 core

// ─────────────────────────────────────────────────────────────────────────────
//  Chunk vertex shader
//
//  All attributes are packed into uint8 to minimise VRAM bandwidth.
//  Unpacking happens here on the GPU — cheap integer ops, no float ALU waste.
// ─────────────────────────────────────────────────────────────────────────────

// layout(location=0): uvec4 → (x, y_lo, y_hi, z)  position within chunk
// layout(location=1): uvec4 → (u, v, tileX, tileY) UV + atlas tile
// layout(location=2): vec3 → normal

layout(location = 0) in uvec4 aPos;      // packed position
layout(location = 1) in uvec4 aUVTile;   // uv + atlas tile coords
layout(location = 2) in vec3  aNormal;   // explicit normal (float3)
layout(location = 3) in uint   aAO;      // ambient occlusion [0-3]
layout(location = 4) in uint   aPackedLight; // packed: low nibble skylight, high nibble blocklight

// ── Uniforms ──────────────────────────────────────────────────────────────────
uniform mat4  u_MVP;          // model-view-projection
uniform vec3  u_ChunkOrigin;  // world-space chunk base position
uniform float u_Time;         // seconds, for water animation
uniform float u_FogStart;
uniform float u_FogEnd;

// ── Outputs to fragment shader ────────────────────────────────────────────────
out vec2  v_UV;         // within-tile UV [0,1]
out vec2  v_TileCoord;  // atlas tile (0-15, 0-15)
out float v_AO;         // ambient occlusion multiplier [0.25 – 1.0]
out float v_FogFactor;  // 0=no fog, 1=fully fogged
out vec3  v_Normal;     // face normal (for sun-angle lighting in fragment)
out float v_Depth;      // view-space depth
out float v_Skylight;   // baked skylight [0..1]
out float v_Blocklight; // baked block light [0..1]

// Note: normals provided per-vertex as `aNormal` (vec3) and forwarded to the
// fragment shader so the sun-angle term can be evaluated per-pixel against the
// current `u_SunDir`.

void main() {
    // ── Unpack position ───────────────────────────────────────────────────────
    float px = float(aPos.x);
    float py = float(aPos.y + aPos.z * 256u);  // y_lo + y_hi*256
    float pz = float(aPos.w);

    vec3 localPos = vec3(px, py, pz);
    vec3 worldPos = localPos + u_ChunkOrigin;

    // ── Compute clip position ─────────────────────────────────────────────────
    vec4 clipPos = u_MVP * vec4(worldPos, 1.0);
    gl_Position = clipPos;

    // ── UV within tile [0,1] ──────────────────────────────────────────────────
    v_UV = vec2(float(aUVTile.x), float(aUVTile.y));

    // ── Atlas tile coordinates ─────────────────────────────────────────────────
    v_TileCoord = vec2(float(aUVTile.z), float(aUVTile.w));


    // ── AO: map [0,3] → [0.40, 1.0] ──────────────────────────────────────────
    // Raised floor from 0.25 to 0.40: the old 0.25 made contact-corners 4×
    // darker than open air, producing heavy black blocks at the base of walls
    // where both edge neighbours are solid ground. 0.40 softens AO while
    // keeping visible depth cues at corners.
    float aoRaw = float(aAO);
    v_AO = (0.40 + aoRaw * 0.20);

    // ── Normal: pass to fragment for per-pixel sun-angle term ────────────────
    v_Normal = normalize(aNormal);

    // Unpack baked light (low nibble = skylight, high nibble = blocklight)
    v_Skylight   = float(aPackedLight & 15u) / 15.0;
    v_Blocklight = float((aPackedLight >> 4u) & 15u) / 15.0;

    // ── View-space depth for fog ──────────────────────────────────────────────
    v_Depth = -clipPos.z / clipPos.w;
    v_FogFactor = clamp((v_Depth - u_FogStart) / (u_FogEnd - u_FogStart), 0.0, 1.0);
    v_FogFactor = v_FogFactor * v_FogFactor;  // quadratic ramp (softer horizon)
}
