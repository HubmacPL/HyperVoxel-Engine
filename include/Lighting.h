#pragma once

#include "Chunk.h"

// Neighbor chunk pointers used by the lighting propagation algorithm.
// Any pointer may be nullptr — algorithm will stop at unloaded boundaries.
struct NeighborChunks {
    Chunk* px = nullptr; // +X
    Chunk* nx = nullptr; // -X
    Chunk* pz = nullptr; // +Z
    Chunk* nz = nullptr; // -Z
    Chunk* py = nullptr; // +Y (unused for single-chunk vertical layout)
    Chunk* ny = nullptr; // -Y
};

// Compute skylight propagation using BFS (flood-fill) starting from skylight seeds
// present in chunk->lightData_ (value 15). This will update skylight nibble in
// chunk and any loaded neighbour chunks passed in NeighborChunks.
void computeFloodFillSkylight(Chunk& chunk, const NeighborChunks& neighbors);
