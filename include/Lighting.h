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

// Set of chunks whose skylight nibble was actually mutated by a BFS pass.
// Returned by computeFloodFillSkylight so the caller can mark neighbours
// for remeshing (their stale border vertices need rebuilding).
struct LightingUpdate {
    bool center = false;
    bool px = false;
    bool nx = false;
    bool pz = false;
    bool nz = false;
};

// Compute skylight propagation using BFS (flood-fill).
//
// Performs vertical seeding inside `center` (skylight=15 down each column until
// the first opaque block) and then runs a symmetric BFS across the 5-chunk
// region (center + 4 cardinal neighbours). Already-lit border strips of the
// neighbours are seeded into the queue so light can flow *into* center across
// shared boundaries (not just out of it). Stops at any nullptr neighbour.
//
// Returns which chunks had any cell modified — caller should re-mesh those.
LightingUpdate computeFloodFillSkylight(Chunk& center, const NeighborChunks& neighbors);
