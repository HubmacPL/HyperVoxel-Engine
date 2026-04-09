#include "ChunkMesher.h"
#include "BlockRegistry.h"
#include <cassert>

// ─────────────────────────────────────────────────────────────────────────────
//  Atlas layout: 16 columns × 16 rows of 16×16 px tiles.
//  Given a BlockType and face direction, returns (tileX, tileY).
// ─────────────────────────────────────────────────────────────────────────────
static std::pair<uint8_t,uint8_t> getTileCoords(BlockType bt, FaceDir face) noexcept {
    const auto& def = BlockRegistry::instance().get(bt);
    uint8_t tile = def.faceTextures[static_cast<int>(face)];
    // tile % 16 = column, tile / 16 = row (from top of PNG)
    // stb_image flips vertically so UV y=0 = bottom of PNG.
    // Invert row so row 0 in BlockRegistry = top row of PNG = correct tile.
    return { static_cast<uint8_t>(tile % 16),
             static_cast<uint8_t>(15 - tile / 16) };
}

// ─────────────────────────────────────────────────────────────────────────────
//  sampleBlock — fast cross-chunk boundary lookup
//  Offsets: x ∈ [-1, CHUNK_W], y ∈ [0, CHUNK_H-1], z ∈ [-1, CHUNK_D]
// ─────────────────────────────────────────────────────────────────────────────
BlockType ChunkMesher::sampleBlock(const MeshContext& ctx, int x, int y, int z) noexcept {
    if (y < 0 || y >= CHUNK_H) return BlockType::Air;

    // Determine which chunk owns this coordinate
    const uint16_t* data = ctx.center;

    if (x < 0) {
        if (!ctx.nx) return BlockType::Stone;  // assume solid at unloaded edge
        data = ctx.nx;
        x += CHUNK_W;
    } else if (x >= CHUNK_W) {
        if (!ctx.px) return BlockType::Stone;
        data = ctx.px;
        x -= CHUNK_W;
    }

    if (z < 0) {
        if (!ctx.nz) return BlockType::Stone;
        // Note: if also x was shifted, we'd need a corner chunk.
        // For simplicity, corners inherit from the Z-neighbour.
        data = ctx.nz;
        z += CHUNK_D;
    } else if (z >= CHUNK_D) {
        if (!ctx.pz) return BlockType::Stone;
        data = ctx.pz;
        z -= CHUNK_D;
    }

    return static_cast<BlockType>(data[blockIndex(x, y, z)]);
}

// ─────────────────────────────────────────────────────────────────────────────
// sampleLight — read 4-bit skylight from corresponding light arrays
// Offsets: x ∈ [-1, CHUNK_W], y ∈ [0, CHUNK_H-1], z ∈ [-1, CHUNK_D]
// Returns skylight in [0,15], or -1 when the sample falls inside an opaque
// block (or outside the loaded region). Per-vertex smooth-lighting must skip
// opaque samples — including them as zero used to drag corner light values
// down and produce the visible dark rim/streak around standalone blocks.
// ─────────────────────────────────────────────────────────────────────────────
static int sampleLight(const MeshContext& ctx, int x, int y, int z) noexcept {
    if (y < 0 || y >= CHUNK_H) return -1;

    const uint8_t*  light  = ctx.centerLight;
    const uint16_t* blocks = ctx.center;

    if (x < 0) {
        if (!ctx.nxLight || !ctx.nx) return -1;
        light  = ctx.nxLight;
        blocks = ctx.nx;
        x += CHUNK_W;
    } else if (x >= CHUNK_W) {
        if (!ctx.pxLight || !ctx.px) return -1;
        light  = ctx.pxLight;
        blocks = ctx.px;
        x -= CHUNK_W;
    }

    if (z < 0) {
        if (!ctx.nzLight || !ctx.nz) return -1;
        light  = ctx.nzLight;
        blocks = ctx.nz;
        z += CHUNK_D;
    } else if (z >= CHUNK_D) {
        if (!ctx.pzLight || !ctx.pz) return -1;
        light  = ctx.pzLight;
        blocks = ctx.pz;
        z -= CHUNK_D;
    }

    const int idx = blockIndex(x, y, z);
    BlockType bt = static_cast<BlockType>(blocks[idx]);
    if (!BlockRegistry::instance().isTransparent(bt)) return -1;
    return static_cast<int>(light[idx] & 0x0Fu);
}

// ─────────────────────────────────────────────────────────────────────────────
//  emitFace — push 4 vertices and 6 indices for one quad
//
//  AO-based vertex darkening: flips diagonal to prevent anisotropy artefact.
//  Technique from Mikael Christofilos' "Ambient Occlusion for Minecraft":
//  if ao[0]+ao[3] > ao[1]+ao[2], flip the quad diagonal.
// ─────────────────────────────────────────────────────────────────────────────
void ChunkMesher::emitFace(ChunkMesh& mesh,
                            int x, int y, int z,
                            FaceDir dir,
                            BlockType bt,
                            const std::array<uint8_t,4>& ao,
                            const std::array<uint8_t,4>& light)
{
    auto [tileX, tileY] = getTileCoords(bt, dir);

    // Corner offsets for each face direction
    // Each face has 4 corners; u/v vary in {0,1}
    static constexpr std::array<std::array<uint8_t,3>, 4> kCornerOffsets[6] = {
        // +X face (x+1, varying y, z)
        {{ {1,0,0}, {1,1,0}, {1,1,1}, {1,0,1} }},
        // -X face (x, varying y, z)
        {{ {0,0,1}, {0,1,1}, {0,1,0}, {0,0,0} }},
        // +Y face (varying x, y+1, z)
        {{ {0,1,0}, {0,1,1}, {1,1,1}, {1,1,0} }},
        // -Y face (varying x, y, z)
        {{ {1,0,0}, {1,0,1}, {0,0,1}, {0,0,0} }},
        // +Z face (varying x, y, z+1)
        {{ {1,0,1}, {1,1,1}, {0,1,1}, {0,0,1} }},
        // -Z face (varying x, y, z)
        {{ {0,0,0}, {0,1,0}, {1,1,0}, {1,0,0} }},
    };

    static constexpr uint8_t kUV[4][2] = { {0,0},{0,1},{1,1},{1,0} };

    const int d = static_cast<int>(dir);
    const uint32_t baseIdx = static_cast<uint32_t>(mesh.vertices.size());

    for (int c = 0; c < 4; ++c) {
        ChunkVertex v{};
        v.x    = static_cast<uint8_t>(x + kCornerOffsets[d][c][0]);
        v.y_lo = static_cast<uint8_t>((y + kCornerOffsets[d][c][1]) & 0xFF);
        v.y_hi = static_cast<uint8_t>((y + kCornerOffsets[d][c][1]) >> 8);
        v.z    = static_cast<uint8_t>(z + kCornerOffsets[d][c][2]);
        v.u     = kUV[c][0];
        v.v     = kUV[c][1];
        v.tileX = tileX;
        v.tileY = tileY;
        v.ao    = ao[c];
        // Pack skylight (low nibble) and blocklight (high nibble = 0 for now)
        uint8_t packed = static_cast<uint8_t>(light[c] & 0x0Fu);
        v.pad[0] = packed;
        // Write explicit normal vector for this face (as floats)
        v.normal = glm::vec3(kFaces[d].normal);
        mesh.vertices.push_back(v);
    }

    // Flip quad diagonal to reduce AO anisotropy
    if (ao[0] + ao[3] > ao[1] + ao[2]) {
        // 0,1,2 / 0,2,3  (standard)
        mesh.indices.insert(mesh.indices.end(),
            { baseIdx, baseIdx+1, baseIdx+2,
              baseIdx, baseIdx+2, baseIdx+3 });
    } else {
        // 1,2,3 / 0,1,3  (flipped)
        mesh.indices.insert(mesh.indices.end(),
            { baseIdx+1, baseIdx+2, baseIdx+3,
              baseIdx,   baseIdx+1, baseIdx+3 });
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  buildMesh — main entry point
//
//  Inner loop order: Y outer, Z middle, X inner
//  → Y-slice iteration matches rawData_ layout [y*W*D + z*W + x]
//  → Hot blocks (same column) stay in L1 cache
// ─────────────────────────────────────────────────────────────────────────────
ChunkMesh ChunkMesher::buildMesh(const MeshContext& ctx, const glm::ivec2& /*chunkPos*/)
{
    ChunkMesh mesh;
    // Reserve conservatively: average ~2000 visible faces per chunk
    mesh.vertices.reserve(8000);
    mesh.indices.reserve(12000);

    const BlockRegistry& reg = BlockRegistry::instance();

    // Static direction offsets: matches FaceDir enum order PX NX PY NY PZ NZ
    static constexpr int kDX[6] = { 1,-1, 0, 0, 0, 0};
    static constexpr int kDY[6] = { 0, 0, 1,-1, 0, 0};
    static constexpr int kDZ[6] = { 0, 0, 0, 0, 1,-1};

    for (int y = 0; y < CHUNK_H; ++y) {
        for (int z = 0; z < CHUNK_D; ++z) {
            for (int x = 0; x < CHUNK_W; ++x) {

                BlockType bt = static_cast<BlockType>(
                    ctx.center[blockIndex(x, y, z)]);

                if (bt == BlockType::Air) continue;  // fast path: ~60% of blocks

                const bool srcTransparent = reg.isTransparent(bt);

                // ── Check each of the 6 faces ─────────────────────────────
                for (int f = 0; f < 6; ++f) {
                    int nx = x + kDX[f];
                    int ny = y + kDY[f];
                    int nz = z + kDZ[f];

                    BlockType nb = sampleBlock(ctx, nx, ny, nz);

                    // Cull rule:
                    //  • source opaque  → cull if neighbour is opaque
                    //  • source transparent → cull if neighbour is same type
                    bool cull;
                    if (srcTransparent) {
                        cull = (nb == bt);
                    } else {
                        cull = !reg.isTransparent(nb);
                    }
                    if (cull) continue;

                    // ── Compute per-vertex AO (4 corners of this face) ────
                    // For face F, the 4 corners each need 3 neighbour lookups.
                    // We sample the plane perpendicular to the face normal.

                    std::array<uint8_t,4> ao{};

                    // Tangent axes for this face (for AO neighbour sampling)
                    const auto& fi = kFaces[f];
                    const glm::ivec3 n  = fi.normal;
                    const glm::ivec3 tU = fi.tangentU;
                    const glm::ivec3 tV = fi.tangentV;

                    // 4 corner positions (in tangent space)
                    static constexpr int corners[4][2] = {{0,0},{0,1},{1,1},{1,0}};

                    for (int c = 0; c < 4; ++c) {
                        int cu = corners[c][0] * 2 - 1;  // -1 or +1
                        int cv = corners[c][1] * 2 - 1;

                        int bx = x + n.x + tU.x*cu + tV.x*cv;
                        int by = y + n.y + tU.y*cu + tV.y*cv;
                        int bz = z + n.z + tU.z*cu + tV.z*cv;

                        // side1: along tangentU
                        bool s1 = reg.isSolid(sampleBlock(ctx,
                            x + n.x + tU.x*cu,
                            y + n.y + tU.y*cu,
                            z + n.z + tU.z*cu));
                        // side2: along tangentV
                        bool s2 = reg.isSolid(sampleBlock(ctx,
                            x + n.x + tV.x*cv,
                            y + n.y + tV.y*cv,
                            z + n.z + tV.z*cv));
                        // corner: diagonal
                        bool sc = reg.isSolid(sampleBlock(ctx, bx, by, bz));

                        ao[c] = computeAO(s1, s2, sc);
                    }

                    // Compute per-corner skylight using smooth sampling in the
                    // air layer in front of the face. sampleLight returns -1
                    // for opaque or out-of-region samples; we average only the
                    // valid ones so an adjacent solid block can't drag the
                    // corner average toward zero. The face-center sample (s0)
                    // is in clear air (we already culled occluded faces) so it
                    // is always valid and acts as the fallback divisor.
                    std::array<uint8_t,4> light{};
                    for (int c = 0; c < 4; ++c) {
                        int cu = corners[c][0] * 2 - 1;  // -1 or +1
                        int cv = corners[c][1] * 2 - 1;

                        int samples[4] = {
                            sampleLight(ctx, x + n.x,                     y + n.y,                     z + n.z),
                            sampleLight(ctx, x + n.x + tU.x*cu,           y + n.y + tU.y*cu,           z + n.z + tU.z*cu),
                            sampleLight(ctx, x + n.x + tV.x*cv,           y + n.y + tV.y*cv,           z + n.z + tV.z*cv),
                            sampleLight(ctx, x + n.x + tU.x*cu + tV.x*cv, y + n.y + tU.y*cu + tV.y*cv, z + n.z + tU.z*cu + tV.z*cv),
                        };

                        int sum = 0, count = 0;
                        for (int s : samples) {
                            if (s >= 0) { sum += s; ++count; }
                        }
                        if (count == 0) {
                            // Defensive fallback: face-center is normally valid.
                            light[c] = 0;
                        } else {
                            light[c] = static_cast<uint8_t>((sum + count / 2) / count);
                        }
                    }

                    emitFace(mesh, x, y, z, static_cast<FaceDir>(f), bt, ao, light);
                }
            }
        }
    }

    mesh.dirty = true;
    return mesh;
}
