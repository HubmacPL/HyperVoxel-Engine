#version 450 core

// ─────────────────────────────────────────────────────────────────────────────
//  Chunk vertex shader
//
//  All attributes are packed into uint8 to minimise VRAM bandwidth.
//  Unpacking happens here on the GPU — cheap integer ops, no float ALU waste.
// ─────────────────────────────────────────────────────────────────────────────

// layout(location=0): uvec4 → (x, y_lo, y_hi, z)  position within chunk
// layout(location=1): uvec4 → (u, v, tileX, tileY) UV + atlas tile
// layout(location=2): uvec2 → (ao, normalIndex)

layout(location = 0) in uvec4 aPos;      // packed position
layout(location = 1) in uvec4 aUVTile;   // uv + atlas tile coords
layout(location = 2) in uvec2 aAONorm;   // ao [0-3], normal [0-5]

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
out float v_NormalY;    // used for sky-light approximation
out float v_Depth;      // view-space depth

// ── Face normal lookup ────────────────────────────────────────────────────────
// Order matches FaceDir: PX=0 NX=1 PY=2 NY=3 PZ=4 NZ=5
const vec3 kNormals[6] = vec3[6](
    vec3( 1, 0, 0),
    vec3(-1, 0, 0),
    vec3( 0, 1, 0),
    vec3( 0,-1, 0),
    vec3( 0, 0, 1),
    vec3( 0, 0,-1)
);

// Face brightness multipliers — cheap directional diffuse without real lighting
const float kFaceBrightness[6] = float[6](
    0.80,  // +X east
    0.80,  // -X west
    1.00,  // +Y top    (full bright)
    0.50,  // -Y bottom (dark underside)
    0.90,  // +Z south
    0.90   // -Z north
);

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

    // ── AO: map [0,3] → [0.25, 1.0] ──────────────────────────────────────────
    // AO=0 → full shadow (0.25), AO=3 → full light (1.0)
    float aoRaw = float(aAONorm.x);
    float faceBright = kFaceBrightness[aAONorm.y];
    v_AO = (0.25 + aoRaw * 0.25) * faceBright;

    // ── Normal Y for sky-light gradient ───────────────────────────────────────
    v_NormalY = kNormals[aAONorm.y].y;

    // ── View-space depth for fog ──────────────────────────────────────────────
    v_Depth = -clipPos.z / clipPos.w;
    v_FogFactor = clamp((v_Depth - u_FogStart) / (u_FogEnd - u_FogStart), 0.0, 1.0);
    v_FogFactor = v_FogFactor * v_FogFactor;  // quadratic ramp (softer horizon)
}
