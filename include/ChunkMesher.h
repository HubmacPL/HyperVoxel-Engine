#pragma once
#include "Chunk.h"
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
//  ChunkMesher — builds optimal geometry for a single chunk
//
//  Algorithm overview:
//  1. Face Culling: Skip any face that touches a solid opaque block → O(n)
//  2. Ambient Occlusion: Per-vertex AO using 3 neighbour samples per corner
//  3. Output: compact packed vertices + indexed quads (2 triangles per face)
//
//  Input: raw uint16_t block arrays for the chunk AND its 6 neighbours.
//  This avoids boundary lookups during meshing, keeping inner-loop branchless.
// ─────────────────────────────────────────────────────────────────────────────

// Neighbour order: +X, -X, +Y, -Y, +Z, -Z  (same as face index)
enum FaceDir : int { PX=0, NX=1, PY=2, NY=3, PZ=4, NZ=5 };

// Per-face data: normal, two tangent axes, uv mapping
struct FaceInfo {
    glm::ivec3 normal;
    glm::ivec3 tangentU;
    glm::ivec3 tangentV;
};

inline constexpr std::array<FaceInfo, 6> kFaces = {{
    // +X
    {{ 1,0,0}, {0,1,0}, {0,0,1}},
    // -X
    {{-1,0,0}, {0,1,0}, {0,0,-1}},
    // +Y
    {{ 0,1,0}, {1,0,0}, {0,0,1}},
    // -Y
    {{ 0,-1,0},{1,0,0}, {0,0,-1}},
    // +Z
    {{ 0,0,1}, {1,0,0}, {0,1,0}},
    // -Z
    {{ 0,0,-1},{-1,0,0},{0,1,0}},
}};

// ─────────────────────────────────────────────────────────────────────────────
//  MeshContext — everything the mesher needs, avoiding scattered pointer chasing
// ─────────────────────────────────────────────────────────────────────────────
struct MeshContext {
    const uint16_t* center;      // [CHUNK_VOL]
    const uint16_t* px;          // +X neighbour, nullptr if not loaded
    const uint16_t* nx;          // -X
    const uint16_t* py;          // +Y (unused if below Y=255)
    const uint16_t* ny;          // -Y
    const uint16_t* pz;          // +Z
    const uint16_t* nz;          // -Z
    // Corresponding per-voxel light arrays (1 byte per voxel) — same layout as blocks
    const uint8_t* centerLight;
    const uint8_t* pxLight;
    const uint8_t* nxLight;
    const uint8_t* pyLight;
    const uint8_t* nyLight;
    const uint8_t* pzLight;
    const uint8_t* nzLight;
};

class ChunkMesher {
public:
    // Build mesh for one chunk given its neighbours.
    // Safe to call from any thread — no GL calls, pure CPU work.
    static ChunkMesh buildMesh(const MeshContext& ctx,
                               const glm::ivec2& chunkPos);

private:
    // ── Fast block lookup across chunk boundaries ─────────────────────────────
    // x,y,z may be in [-1, CHUNK_W/D] range
    static BlockType sampleBlock(const MeshContext& ctx, int x, int y, int z) noexcept;

    // ── Ambient Occlusion ─────────────────────────────────────────────────────
    // Returns AO value [0-3] for one vertex corner of a face.
    // side1, side2: the two edge neighbours; corner: the diagonal neighbour.
    static uint8_t computeAO(bool side1, bool side2, bool corner) noexcept {
        if (side1 && side2) return 0;           // fully occluded
        return 3 - static_cast<uint8_t>(side1 + side2 + corner);
    }

    // ── Emit one quad (4 vertices, 6 indices) ─────────────────────────────────
    static void emitFace(ChunkMesh& mesh,
                         int x, int y, int z,
                         FaceDir dir,
                         BlockType bt,
                         const std::array<uint8_t, 4>& ao,
                         const std::array<uint8_t, 4>& light);
};
