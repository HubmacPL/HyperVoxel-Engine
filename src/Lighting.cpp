#include "Lighting.h"
#include "BlockRegistry.h"
#include <deque>
#include <array>
#include <utility>
#include <cassert>

// ─────────────────────────────────────────────────────────────────────────────
// Voxel skylight flood-fill BFS.
//
// The BFS works on a region of up to 5 chunks (center + 4 cardinal neighbours).
// Each queue node carries a chunk pointer plus local coords inside that chunk,
// so propagation can hop across chunk seams seamlessly.
//
// Symmetry note: we seed from BOTH the center chunk's full-skylight cells AND
// from the 1-cell-thick border strips of every loaded neighbour. Without the
// neighbour seeding, an already-lit neighbour could not push light into a dark
// overhang on the center side, producing visible seam artefacts on chunk
// boundaries. With it, the BFS converges to the same result regardless of the
// order in which chunks were generated.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct LightNode {
    Chunk* chunk;
    int x, y, z;
};

// Encode neighbour identity as a small index so we can flag "this chunk was
// mutated" without a chain of pointer comparisons in the hot loop.
//   0 = center, 1 = px, 2 = nx, 3 = pz, 4 = nz
constexpr int kCenter = 0;
constexpr int kPx = 1;
constexpr int kNx = 2;
constexpr int kPz = 3;
constexpr int kNz = 4;

// Resolve a (chunk, dx, dz) move into another chunk in the 5-chunk region.
// Returns nullptr if the target lies outside the provided neighbour set.
// `outIdx` receives the LightingUpdate slot of the resolved target.
inline Chunk* stepChunk(Chunk* from, int fromIdx,
                        int& localX, int& localZ,
                        const NeighborChunks& nb,
                        Chunk& center, int& outIdx) noexcept
{
    // Bring localX/localZ back into [0, CHUNK_W) / [0, CHUNK_D) and remember
    // which side we crossed.
    int crossX = 0, crossZ = 0;
    if (localX < 0)            { localX += CHUNK_W; crossX = -1; }
    else if (localX >= CHUNK_W){ localX -= CHUNK_W; crossX = +1; }
    if (localZ < 0)            { localZ += CHUNK_D; crossZ = -1; }
    else if (localZ >= CHUNK_D){ localZ -= CHUNK_D; crossZ = +1; }

    if (crossX == 0 && crossZ == 0) {
        outIdx = fromIdx;
        return from;
    }

    // Translate the source chunk's "world cell" of the 5-chunk region by the
    // crossings, then map to a known chunk. Only single-step cardinal moves
    // are supported — diagonals leave the region.
    // Determine source's world cell:
    int sx = 0, sz = 0;
    switch (fromIdx) {
        case kPx: sx = +1; break;
        case kNx: sx = -1; break;
        case kPz: sz = +1; break;
        case kNz: sz = -1; break;
        default: break;
    }
    int tx = sx + crossX;
    int tz = sz + crossZ;

    if (tx ==  0 && tz ==  0) { outIdx = kCenter; return &center; }
    if (tx == +1 && tz ==  0) { outIdx = kPx; return nb.px; }
    if (tx == -1 && tz ==  0) { outIdx = kNx; return nb.nx; }
    if (tx ==  0 && tz == +1) { outIdx = kPz; return nb.pz; }
    if (tx ==  0 && tz == -1) { outIdx = kNz; return nb.nz; }
    // Diagonal or 2-step away: outside the 5-chunk region.
    outIdx = -1;
    return nullptr;
}

inline void pushIfImproves(std::deque<LightNode>& q,
                           Chunk* chunk, int idx,
                           int x, int y, int z,
                           const NeighborChunks& nb,
                           Chunk& center,
                           const BlockRegistry& reg)
{
    if (chunk == nullptr) return;
    uint8_t src = chunk->getSkylight(x, y, z);
    if (src <= 1) return; // nothing to push (a light of 1 attenuates to 0)

    // Only enqueue if any of the 6 neighbours could actually improve.
    static constexpr int dx[6] = {+1,-1,0,0,0,0};
    static constexpr int dy[6] = {0,0,+1,-1,0,0};
    static constexpr int dz[6] = {0,0,0,0,+1,-1};
    for (int d = 0; d < 6; ++d) {
        int nxL = x + dx[d];
        int nyL = y + dy[d];
        int nzL = z + dz[d];
        if (nyL < 0 || nyL >= CHUNK_H) continue;
        int outIdx = -1;
        int lx = nxL, lz = nzL;
        Chunk* tgt = stepChunk(chunk, idx, lx, lz, nb, center, outIdx);
        if (!tgt) continue;
        BlockType nbt = static_cast<BlockType>(tgt->rawBlocks()[blockIndex(lx, nyL, lz)]);
        if (!reg.isTransparent(nbt)) continue;
        int atten = (nbt == BlockType::Leaves) ? 2 : 1;
        if (static_cast<int>(src) - atten > tgt->getSkylight(lx, nyL, lz)) {
            q.push_back({ chunk, x, y, z });
            return;
        }
    }
}

} // namespace

LightingUpdate computeFloodFillSkylight(Chunk& center, const NeighborChunks& neighbors) {
    LightingUpdate touched{};
    const BlockRegistry& reg = BlockRegistry::instance();

    // ── Step 0: vertical seeding (center only) ───────────────────────────────
    // Sky-exposed transparent cells get skylight=15. We do this only for the
    // center chunk; neighbours were already vertically seeded when they ran
    // their own initial BFS pass.
    for (int x = 0; x < CHUNK_W; ++x) {
        for (int z = 0; z < CHUNK_D; ++z) {
            for (int y = CHUNK_H - 1; y >= 0; --y) {
                BlockType bt = static_cast<BlockType>(
                    center.rawBlocks()[blockIndex(x, y, z)]);
                if (!reg.isTransparent(bt)) break;
                if (center.getSkylight(x, y, z) != 15u) {
                    center.setSkylight(x, y, z, 15u);
                    touched.center = true;
                }
            }
        }
    }

    // ── Step 1: enqueue seeds ────────────────────────────────────────────────
    // Center: every cell that could push to a darker neighbour.
    std::deque<LightNode> q;
    for (int x = 0; x < CHUNK_W; ++x) {
        for (int z = 0; z < CHUNK_D; ++z) {
            for (int y = 0; y < CHUNK_H; ++y) {
                if (center.getSkylight(x, y, z) <= 1) continue;
                pushIfImproves(q, &center, kCenter, x, y, z, neighbors, center, reg);
            }
        }
    }

    // Neighbour border strips: 1-thick column on the side facing center.
    auto seedNeighbourStrip = [&](Chunk* nbChunk, int idx,
                                  int fixedX, int fixedZ,
                                  bool varyX, bool varyZ)
    {
        if (!nbChunk) return;
        for (int y = 0; y < CHUNK_H; ++y) {
            if (varyX) {
                for (int xx = 0; xx < CHUNK_W; ++xx) {
                    if (nbChunk->getSkylight(xx, y, fixedZ) > 1)
                        pushIfImproves(q, nbChunk, idx, xx, y, fixedZ,
                                       neighbors, center, reg);
                }
            } else if (varyZ) {
                for (int zz = 0; zz < CHUNK_D; ++zz) {
                    if (nbChunk->getSkylight(fixedX, y, zz) > 1)
                        pushIfImproves(q, nbChunk, idx, fixedX, y, zz,
                                       neighbors, center, reg);
                }
            }
        }
    };

    // +X neighbour: cells adjacent to center are at localX = 0
    seedNeighbourStrip(neighbors.px, kPx, /*fixedX*/0, /*fixedZ*/0,
                       /*varyX*/false, /*varyZ*/true);
    // -X neighbour: cells adjacent to center are at localX = CHUNK_W-1
    seedNeighbourStrip(neighbors.nx, kNx, /*fixedX*/CHUNK_W - 1, /*fixedZ*/0,
                       /*varyX*/false, /*varyZ*/true);
    // +Z neighbour: cells adjacent to center are at localZ = 0
    seedNeighbourStrip(neighbors.pz, kPz, /*fixedX*/0, /*fixedZ*/0,
                       /*varyX*/true,  /*varyZ*/false);
    // -Z neighbour: cells adjacent to center are at localZ = CHUNK_D-1
    seedNeighbourStrip(neighbors.nz, kNz, /*fixedX*/0, /*fixedZ*/CHUNK_D - 1,
                       /*varyX*/true,  /*varyZ*/false);

    // ── Step 2: BFS propagation ───────────────────────────────────────────────
    static constexpr int dx[6] = {+1,-1,0,0,0,0};
    static constexpr int dy[6] = {0,0,+1,-1,0,0};
    static constexpr int dz[6] = {0,0,0,0,+1,-1};

    auto idxOf = [&](Chunk* c) -> int {
        if (c == &center)        return kCenter;
        if (c == neighbors.px)   return kPx;
        if (c == neighbors.nx)   return kNx;
        if (c == neighbors.pz)   return kPz;
        if (c == neighbors.nz)   return kNz;
        return -1;
    };

    while (!q.empty()) {
        LightNode n = q.front(); q.pop_front();
        const int nodeIdx = idxOf(n.chunk);
        if (nodeIdx < 0) continue;

        const uint8_t src = n.chunk->getSkylight(n.x, n.y, n.z);
        if (src <= 1) continue;

        for (int d = 0; d < 6; ++d) {
            int nxL = n.x + dx[d];
            int nyL = n.y + dy[d];
            int nzL = n.z + dz[d];
            if (nyL < 0 || nyL >= CHUNK_H) continue;

            int outIdx = -1;
            int lx = nxL, lz = nzL;
            Chunk* tgt = stepChunk(n.chunk, nodeIdx, lx, lz,
                                   neighbors, center, outIdx);
            if (!tgt) continue;

            BlockType nbt = static_cast<BlockType>(
                tgt->rawBlocks()[blockIndex(lx, nyL, lz)]);
            if (!reg.isTransparent(nbt)) continue;

            const int atten = (nbt == BlockType::Leaves) ? 2 : 1;
            if (static_cast<int>(src) <= atten) continue;
            const uint8_t newLight = static_cast<uint8_t>(src - atten);
            if (tgt->getSkylight(lx, nyL, lz) >= newLight) continue;

            tgt->setSkylight(lx, nyL, lz, newLight);
            switch (outIdx) {
                case kCenter: touched.center = true; break;
                case kPx:     touched.px = true;     break;
                case kNx:     touched.nx = true;     break;
                case kPz:     touched.pz = true;     break;
                case kNz:     touched.nz = true;     break;
                default: break;
            }
            q.push_back({ tgt, lx, nyL, lz });
        }
    }

    return touched;
}
