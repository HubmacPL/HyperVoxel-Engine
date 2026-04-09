#include "ChunkManager.h"
#include "ChunkMesher.h"
#include "TerrainGenerator.h"
#include <algorithm>

ChunkManager::ChunkManager(World& world, TerrainGenerator& gen, ChunkMap<ChunkPtr>& worldChunks)
    : world_(world)
    , gen_(gen)
    , chunks_(worldChunks)
    // Leave 1 core for the main/render thread
    , genPool_(std::max(1u, std::thread::hardware_concurrency() - 1))
    , meshPool_(std::max(1u, std::thread::hardware_concurrency() / 2))
{}

ChunkManager::~ChunkManager() {
    // Thread pools RAII destructors join workers
}

void ChunkManager::tick(const glm::ivec2& centerChunk, int renderDist) {
    (void)centerChunk;
    (void)renderDist;
    // ── 1. Collect finished generation jobs ──────────────────────────────────
    {
        std::unique_lock lock(pendingMutex_);
        pendingGen_.erase(std::remove_if(pendingGen_.begin(), pendingGen_.end(),
            [this](GenFuture& gf) {
                if (gf.fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    gf.fut.get();  // propagate exceptions
                    auto it = chunks_.find(gf.cp);
                    if (it != chunks_.end()) {
                        it->second->setState(ChunkState::TerrainGenerated);
                    }
                    return true;
                }
                return false;
            }), pendingGen_.end());
    }

    // ── 2. Collect finished mesh jobs ────────────────────────────────────────
    {
        std::unique_lock lock(pendingMutex_);
        pendingMesh_.erase(std::remove_if(pendingMesh_.begin(), pendingMesh_.end(),
            [this](MeshFuture& mf) {
                if (mf.fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    ChunkMesh mesh = mf.fut.get();
                    std::unique_lock rLock(readyMutex_);
                    readyQueue_.push_back({ mf.cp, std::move(mesh) });
                    auto it = chunks_.find(mf.cp);
                    if (it != chunks_.end())
                        it->second->setState(ChunkState::Ready);
                    return true;
                }
                return false;
            }), pendingMesh_.end());
    }

    // ── 3. Schedule new work ──────────────────────────────────────────────────
    // Budget: don't flood the thread pool (causes memory pressure)
    constexpr size_t kMaxPendingGen  = 16;
    constexpr size_t kMaxPendingMesh = 8;

    for (auto& [cp, chunk] : chunks_) {
        // Prioritise chunks close to player (spiral order handled by World)
        if (chunk->state() == ChunkState::Empty) {
            std::unique_lock lock(pendingMutex_);
            if (pendingGen_.size() < kMaxPendingGen) {
                scheduleGeneration(chunk);
            }
        } else if (chunk->state() == ChunkState::TerrainGenerated) {
            if (neighbours3x3Ready(cp)) {
                decorateChunk(chunk);
            }
        } else if (chunk->state() == ChunkState::NeedsMeshing ||
                   (chunk->state() == ChunkState::Uploaded && chunk->isDirty())) {
            std::unique_lock lock(pendingMutex_);
            if (pendingMesh_.size() < kMaxPendingMesh && neighboursReady(cp)) {
                chunk->clearDirty();
                scheduleMeshing(chunk);
            }
        }
    }
}

int ChunkManager::uploadReadyMeshes(int maxPerFrame) {
    std::unique_lock lock(readyMutex_);
    int count = 0;
    while (!readyQueue_.empty() && count < maxPerFrame) {
        auto& rm = readyQueue_.back();
        auto it = chunks_.find(rm.cp);
        if (it != chunks_.end()) {
            it->second->mesh() = std::move(rm.mesh);
            it->second->mesh().uploadToGPU();
            it->second->setState(ChunkState::Uploaded);
            ++count;
        }
        readyQueue_.pop_back();
    }
    return count;
}

void ChunkManager::unloadChunk(glm::ivec2 cp) {
    (void)cp;
    // Futures for this chunk will become orphaned — they check chunks_ on completion
    // The chunk's destructor frees the GPU mesh via RAII
}

void ChunkManager::scheduleGeneration(ChunkPtr chunk) {
    chunk->setState(ChunkState::Generating);
    glm::ivec2 cp = chunk->chunkPos();

    auto fut = genPool_.submit([this, chunk]() {
        gen_.generate(*chunk);
    });

    pendingGen_.push_back({ cp, std::move(fut) });
}

void ChunkManager::scheduleMeshing(ChunkPtr chunk) {
    chunk->setState(ChunkState::Meshing);
    glm::ivec2 cp = chunk->chunkPos();

    // Capture raw block pointers — safe because Chunk lifetime > future
    // Chunks are only removed after unloadChunk which waits for futures
    const uint16_t* center = chunk->rawBlocks();
    const uint16_t* px = nullptr, *nx = nullptr;
    const uint16_t* pz = nullptr, *nz = nullptr;

    auto get = [&](glm::ivec2 ncp) -> const uint16_t* {
        auto it = chunks_.find(ncp);
        if (it != chunks_.end() &&
            it->second->state() >= ChunkState::TerrainGenerated)
            return it->second->rawBlocks();
        return nullptr;
    };

    px = get({cp.x+1, cp.y});
    nx = get({cp.x-1, cp.y});
    pz = get({cp.x, cp.y+1});
    nz = get({cp.x, cp.y-1});

    MeshContext ctx{ center, px, nx, nullptr, nullptr, pz, nz };

    auto fut = meshPool_.submit([ctx, cp]() -> ChunkMesh {
        return ChunkMesher::buildMesh(ctx, cp);
    });

    pendingMesh_.push_back({ cp, std::move(fut) });
}

bool ChunkManager::neighboursReady(glm::ivec2 cp) const {
    static constexpr glm::ivec2 dirs[4] = {{1,0},{-1,0},{0,1},{0,-1}};
    for (auto d : dirs) {
        auto it = chunks_.find(cp + d);
        if (it == chunks_.end() || it->second->state() < ChunkState::TerrainGenerated)
            return false;
    }
    return true;
}

bool ChunkManager::neighbours3x3Ready(glm::ivec2 cp) const {
    static constexpr glm::ivec2 dirs[8] = {
        {1,0},{-1,0},{0,1},{0,-1},
        {1,1},{1,-1},{-1,1},{-1,-1}
    };
    for (auto d : dirs) {
        auto it = chunks_.find(cp + d);
        if (it == chunks_.end() || it->second->state() < ChunkState::TerrainGenerated)
            return false;
    }
    return true;
}

void ChunkManager::decorateChunk(ChunkPtr chunk) {
    chunk->setState(ChunkState::Decorated);
    gen_.decorate(world_, chunk->chunkPos());
}
