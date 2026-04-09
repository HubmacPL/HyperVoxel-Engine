#pragma once
#include <future>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include "Chunk.h"
#include "World.h"
#include "ThreadPool.h"

// ─────────────────────────────────────────────────────────────────────────────
//  ChunkManager — drives the async chunk lifecycle:
//
//   Empty → [BG: terrain gen]  → TerrainGenerated
//         → [BG: decoration]    → Decorated / NeedsMeshing
//         → [BG: mesh build]   → Ready
//         → [GL: GPU upload]   → Uploaded
//         → [if dirty] → back to NeedsMeshing
//
//  Three-stage pipeline (gen, decorate and mesh can overlap on different chunks):
//    Stage 1: TerrainGenerator::generate()  — CPU heavy, pure output
//    Stage 2: TerrainGenerator::decorate() — feature placement, cross-chunk writes
//    Stage 3: ChunkMesher::buildMesh()      — CPU heavy, needs neighbours
// ─────────────────────────────────────────────────────────────────────────────
class TerrainGenerator;

struct PendingMesh {
    glm::ivec2            chunkPos;
    std::future<ChunkMesh> future;
};

class ChunkManager {
public:
    explicit ChunkManager(World& world, TerrainGenerator& gen, ChunkMap<ChunkPtr>& worldChunks);
    ~ChunkManager();

    // ── Called from main thread each frame ───────────────────────────────────
    // Submits new generation/mesh jobs, collects finished ones.
    void tick(const glm::ivec2& centerChunk, int renderDistance);

    // Upload ready meshes to GPU (must run on GL thread)
    // Returns number of chunks uploaded this frame (budget-limited)
    int uploadReadyMeshes(int maxPerFrame = 4);

    // Request immediate unload of a chunk
    void unloadChunk(glm::ivec2 cp);

private:
    // Submit terrain generation for chunk
    void scheduleGeneration(ChunkPtr chunk);
    // Submit mesh build for chunk (requires neighbours)
    void scheduleMeshing(ChunkPtr chunk);
    // Apply decoration once terrain is stable in a 3x3 neighbourhood
    void decorateChunk(ChunkPtr chunk);
    // Check if all 4 cardinal neighbours are at least TerrainGenerated state
    bool neighboursReady(glm::ivec2 cp) const;
    // Check if self and all 8 neighbours are at least TerrainGenerated state
    bool neighbours3x3Ready(glm::ivec2 cp) const;

    World&              world_;
    TerrainGenerator&    gen_;
    ChunkMap<ChunkPtr>&  chunks_;   // reference to World's map

    // Background workers: more gen threads, fewer mesh threads
    // (gen is embarrassingly parallel; meshing needs neighbour data)
    ThreadPool           genPool_;
    ThreadPool           meshPool_;

    // Futures tracking in-flight operations
    struct GenFuture  { glm::ivec2 cp; std::future<void> fut; };
    struct MeshFuture { glm::ivec2 cp; std::future<ChunkMesh> fut; };

    std::vector<GenFuture>  pendingGen_;
    std::vector<MeshFuture> pendingMesh_;
    std::mutex              pendingMutex_;

    // Chunks with completed meshes waiting for GL upload
    struct ReadyMesh { glm::ivec2 cp; ChunkMesh mesh; };
    std::vector<ReadyMesh>  readyQueue_;
    std::mutex              readyMutex_;
};
