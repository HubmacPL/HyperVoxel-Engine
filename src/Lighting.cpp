#include "Lighting.h"
#include "BlockRegistry.h"
#include <deque>
#include <array>
#include <utility>
#include <cassert>

// BFS node: chunk pointer + local coordinates inside that chunk
struct LightNode {
    Chunk* chunk;
    int x, y, z;
};

void computeFloodFillSkylight(Chunk& center, const NeighborChunks& neighbors) {
    using namespace std;
    const BlockRegistry& reg = BlockRegistry::instance();

    // Helper: map a known chunk pointer to its relative (cx,cz) offset from center
    auto relOffset = [&](Chunk* c) -> pair<int,int> {
        if (c == &center) return {0,0};
        if (c == neighbors.px) return {+1,0};
        if (c == neighbors.nx) return {-1,0};
        if (c == neighbors.pz) return {0,+1};
        if (c == neighbors.nz) return {0,-1};
        return {100,100};
    };

    // Convenience lambda to get block type in a given chunk (assumes coords in-range)
    auto blockAt = [&](Chunk* c, int lx, int ly, int lz) -> BlockType {
        assert(c != nullptr);
        const uint16_t* raw = c->rawBlocks();
        return static_cast<BlockType>(raw[blockIndex(lx, ly, lz)]);
    };

    // Utility to read/set skylight nibble in a chunk (coords in-range)
    auto getSkylight = [&](Chunk* c, int lx, int ly, int lz) -> uint8_t {
        assert(c != nullptr);
        return c->getSkylight(lx, ly, lz);
    };
    auto setSkylight = [&](Chunk* c, int lx, int ly, int lz, uint8_t v) {
        assert(c != nullptr);
        c->setSkylight(lx, ly, lz, v);
    };

    // --- Step 0: vertical seeding (mark sky-exposed transparent cells skylight=15)
    // For each column (x,z) in the chunk, scan down from top and set skylight=15
    for (int x = 0; x < CHUNK_W; ++x) {
        for (int z = 0; z < CHUNK_D; ++z) {
            for (int y = CHUNK_H - 1; y >= 0; --y) {
                BlockType bt = blockAt(&center, x, y, z);
                if (!reg.isTransparent(bt)) {
                    // opaque — anything below is shadowed by this block (vertical seed stops)
                    break;
                }
                // transparent: mark as full skylight
                uint8_t cur = getSkylight(&center, x, y, z);
                if (cur != 15u) setSkylight(&center, x, y, z, 15u);
            }
        }
    }

    // --- Step 1: initialize BFS queue with all skylight==15 cells that have at least one neighbour
    // that could be improved (cut down on queue size)
    deque<LightNode> q;
    for (int x = 0; x < CHUNK_W; ++x) {
        for (int z = 0; z < CHUNK_D; ++z) {
            for (int y = 0; y < CHUNK_H; ++y) {
                if (getSkylight(&center, x, y, z) != 15u) continue;
                // examine 6 neighbours
                bool push = false;
                const int dx[6] = {+1,-1,0,0,0,0};
                const int dy[6] = {0,0,+1,-1,0,0};
                const int dz[6] = {0,0,0,0,+1,-1};
                for (int d = 0; d < 6 && !push; ++d) {
                    int nx = x + dx[d];
                    int ny = y + dy[d];
                    int nz = z + dz[d];
                    if (ny < 0 || ny >= CHUNK_H) continue;

                    // Determine which chunk owns (nx,nz). Start with center rel offset 0,0.
                    int relX = 0, relZ = 0;
                    int localX = nx, localZ = nz;
                    if (localX < 0) { localX += CHUNK_W; relX = -1; }
                    else if (localX >= CHUNK_W) { localX -= CHUNK_W; relX = +1; }
                    if (localZ < 0) { localZ += CHUNK_D; relZ = -1; }
                    else if (localZ >= CHUNK_D) { localZ -= CHUNK_D; relZ = +1; }

                    Chunk* target = nullptr;
                    if (relX == 0 && relZ == 0) target = &center;
                    else if (relX == 1 && relZ == 0) target = neighbors.px;
                    else if (relX == -1 && relZ == 0) target = neighbors.nx;
                    else if (relX == 0 && relZ == 1) target = neighbors.pz;
                    else if (relX == 0 && relZ == -1) target = neighbors.nz;
                    else target = nullptr; // beyond provided neighbours

                    if (!target) continue; // stops at unloaded/unknown boundaries

                    // Only consider transparent neighbours
                    BlockType nbt = blockAt(target, localX, ny, localZ);
                    if (!reg.isTransparent(nbt)) continue;

                    uint8_t nlight = getSkylight(target, localX, ny, localZ);
                    // attenuation base 1, leaves cost extra 1
                    int atten = (nbt == BlockType::Leaves) ? 2 : 1;
                    if (nlight + atten < 15) {
                        push = true;
                    }
                }
                if (push) q.push_back({ &center, x, y, z });
            }
        }
    }

    // --- Step 2: BFS propagation
    const int dx[6] = {+1,-1,0,0,0,0};
    const int dy[6] = {0,0,+1,-1,0,0};
    const int dz[6] = {0,0,0,0,+1,-1};

    while (!q.empty()) {
        LightNode n = q.front(); q.pop_front();
        uint8_t src = getSkylight(n.chunk, n.x, n.y, n.z);
        if (src == 0) continue;

        for (int d = 0; d < 6; ++d) {
            int nx = n.x + dx[d];
            int ny = n.y + dy[d];
            int nz = n.z + dz[d];
            if (ny < 0 || ny >= CHUNK_H) continue; // vertical boundaries: stop

            // Determine relative offset of the chunk the node currently sits in
            auto rel = relOffset(n.chunk);
            int outRelX = rel.first;
            int outRelZ = rel.second;
            int localX = nx;
            int localZ = nz;

            if (localX < 0) { localX += CHUNK_W; outRelX -= 1; }
            else if (localX >= CHUNK_W) { localX -= CHUNK_W; outRelX += 1; }
            if (localZ < 0) { localZ += CHUNK_D; outRelZ -= 1; }
            else if (localZ >= CHUNK_D) { localZ -= CHUNK_D; outRelZ += 1; }

            Chunk* target = nullptr;
            if (outRelX == 0 && outRelZ == 0) target = &center;
            else if (outRelX == 1 && outRelZ == 0) target = neighbors.px;
            else if (outRelX == -1 && outRelZ == 0) target = neighbors.nx;
            else if (outRelX == 0 && outRelZ == 1) target = neighbors.pz;
            else if (outRelX == 0 && outRelZ == -1) target = neighbors.nz;
            else target = nullptr;

            if (!target) continue; // boundary: stop if neighbour not provided

            // If target is not transparent at that position, light cannot enter
            BlockType nbt = blockAt(target, localX, ny, localZ);
            if (!reg.isTransparent(nbt)) continue;

            int atten = (nbt == BlockType::Leaves) ? 2 : 1;
            if (src <= atten) continue; // no light to pass
            uint8_t newLight = static_cast<uint8_t>(src - atten);
            uint8_t curLight = getSkylight(target, localX, ny, localZ);
            if (curLight >= newLight) continue;

            setSkylight(target, localX, ny, localZ, newLight);
            q.push_back({ target, localX, ny, localZ });
        }
    }
}
