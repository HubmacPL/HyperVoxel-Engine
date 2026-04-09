#include "ChunkManager.h"
#include "ChunkMesher.h"
#include "TerrainGenerator.h"
#include "Lighting.h"
#include <algorithm>
#include <memory>

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
    // Move ready generation futures out of the pending queue while holding the lock,
    // then process them (call computeFloodFillSkylight) outside the lock.
    std::vector<GenFuture> readyGens;
    {
        std::unique_lock lock(pendingMutex_);
        auto it = pendingGen_.begin();
        while (it != pendingGen_.end()) {
            if (it->fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                readyGens.push_back(std::move(*it));
                it = pendingGen_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Process completed generation jobs (no pendingMutex_ held)
    for (auto & gf : readyGens) {
        gf.fut.get(); // propagate exceptions
        auto it = chunks_.find(gf.cp);
        if (it != chunks_.end()) {
            // Build neighbor pointers (only immediate cardinal neighbours provided)
            auto getPtr = [&](glm::ivec2 ncp) -> Chunk* {
                auto nit = chunks_.find(ncp);
                if (nit != chunks_.end() && nit->second->state() >= ChunkState::TerrainGenerated)
                    return nit->second.get();
                return nullptr;
            };

            Chunk* center = it->second.get();
            NeighborChunks nb{};
            nb.px = getPtr({gf.cp.x+1, gf.cp.y});
            nb.nx = getPtr({gf.cp.x-1, gf.cp.y});
            nb.pz = getPtr({gf.cp.x, gf.cp.y+1});
            nb.nz = getPtr({gf.cp.x, gf.cp.y-1});

            // Compute skylight propagation now (stops at unloaded neighbours)
            computeFloodFillSkylight(*center, nb);

            // Mark chunk as TerrainGenerated so subsequent decoration/meshing may run
            center->setState(ChunkState::TerrainGenerated);
        }
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
    // Decoration runs synchronously — cap at 1 per tick to bound frame time
    constexpr int kMaxDecorationsPerTick = 1;
    int decorationsThisTick = 0;

    for (auto& [cp, chunk] : chunks_) {
        // Prioritise chunks close to player (spiral order handled by World)
        if (chunk->state() == ChunkState::Empty) {
            std::unique_lock lock(pendingMutex_);
            if (pendingGen_.size() < kMaxPendingGen) {
                scheduleGeneration(chunk);
            }
        } else if (chunk->state() == ChunkState::TerrainGenerated) {
            if (neighbours3x3Ready(cp) && decorationsThisTick < kMaxDecorationsPerTick) {
                decorateChunk(chunk);
                ++decorationsThisTick;
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

    // Snapshot center block data under blockMutex — prevents data race with setBlock().
    auto centerData = std::make_shared<std::array<uint16_t, CHUNK_VOL>>(
        chunk->rawDataCopy());

    const uint16_t* px = nullptr, *nx = nullptr;
    const uint16_t* pz = nullptr, *nz = nullptr;

    auto get = [&](glm::ivec2 ncp) -> const uint16_t* {
        auto it = chunks_.find(ncp);
        if (it != chunks_.end() &&
            it->second->state() >= ChunkState::TerrainGenerated)
            return it->second->rawBlocks();
        return nullptr;
    };

    // Helper to create a snapshot (shared_ptr) of neighbour light data if available
    auto getLightSnap = [&](glm::ivec2 ncp) -> std::shared_ptr<std::array<uint8_t, CHUNK_VOL>> {
        auto it = chunks_.find(ncp);
        if (it != chunks_.end() && it->second->state() >= ChunkState::TerrainGenerated) {
            return std::make_shared<std::array<uint8_t, CHUNK_VOL>>(it->second->rawLightDataCopy());
        }
        return nullptr;
    };

    px = get({cp.x+1, cp.y});
    nx = get({cp.x-1, cp.y});
    pz = get({cp.x, cp.y+1});
    nz = get({cp.x, cp.y-1});

    // Capture neighbour ChunkPtrs (shared ownership) so the BFS inside the lambda
    // can read their block+light data safely — they won't be destroyed under us.
    auto getNeighbourChunk = [&](glm::ivec2 ncp) -> ChunkPtr {
        auto it = chunks_.find(ncp);
        if (it != chunks_.end() && it->second->state() >= ChunkState::TerrainGenerated)
            return it->second;
        return nullptr;
    };
    ChunkPtr pxChunk = getNeighbourChunk({cp.x+1, cp.y});
    ChunkPtr nxChunk = getNeighbourChunk({cp.x-1, cp.y});
    ChunkPtr pzChunk = getNeighbourChunk({cp.x, cp.y+1});
    ChunkPtr nzChunk = getNeighbourChunk({cp.x, cp.y-1});

    bool needsLightRecompute = chunk->isLightDirty();

    // If light is NOT dirty, snapshot it now on the main thread (stable).
    // If light IS dirty, we'll recompute + snapshot inside the background thread.
    auto centerLightData = needsLightRecompute
        ? nullptr
        : std::make_shared<std::array<uint8_t, CHUNK_VOL>>(chunk->rawLightDataCopy());

    auto pxLightSnap = needsLightRecompute ? nullptr : getLightSnap({cp.x+1, cp.y});
    auto nxLightSnap = needsLightRecompute ? nullptr : getLightSnap({cp.x-1, cp.y});
    auto pzLightSnap = needsLightRecompute ? nullptr : getLightSnap({cp.x, cp.y+1});
    auto nzLightSnap = needsLightRecompute ? nullptr : getLightSnap({cp.x, cp.y-1});

    auto fut = meshPool_.submit([=, centerData = centerData, chunk = chunk,
                                 pxChunk = pxChunk, nxChunk = nxChunk,
                                 pzChunk = pzChunk, nzChunk = nzChunk,
                                 centerLightData = centerLightData,
                                 pxLightSnap = pxLightSnap, nxLightSnap = nxLightSnap,
                                 pzLightSnap = pzLightSnap, nzLightSnap = nzLightSnap]() mutable -> ChunkMesh {

        // ── Light recomputation (background thread) ───────────────────────────
        // Runs only when a block was broken/placed (lightDirty flag set).
        // Neighbour chunks are kept alive via shared_ptr captures above.
        std::shared_ptr<std::array<uint8_t, CHUNK_VOL>> cld = centerLightData;
        std::shared_ptr<std::array<uint8_t, CHUNK_VOL>> pxls = pxLightSnap;
        std::shared_ptr<std::array<uint8_t, CHUNK_VOL>> nxls = nxLightSnap;
        std::shared_ptr<std::array<uint8_t, CHUNK_VOL>> pzls = pzLightSnap;
        std::shared_ptr<std::array<uint8_t, CHUNK_VOL>> nzls = nzLightSnap;

        if (needsLightRecompute) {
            chunk->resetSkylight();
            NeighborChunks nb{};
            nb.px = pxChunk ? pxChunk.get() : nullptr;
            nb.nx = nxChunk ? nxChunk.get() : nullptr;
            nb.pz = pzChunk ? pzChunk.get() : nullptr;
            nb.nz = nzChunk ? nzChunk.get() : nullptr;
            computeFloodFillSkylight(*chunk, nb);
            chunk->clearLightDirty();

            // Take snapshots after recomputation
            cld  = std::make_shared<std::array<uint8_t, CHUNK_VOL>>(chunk->rawLightDataCopy());
            pxls = pxChunk ? std::make_shared<std::array<uint8_t, CHUNK_VOL>>(pxChunk->rawLightDataCopy()) : nullptr;
            nxls = nxChunk ? std::make_shared<std::array<uint8_t, CHUNK_VOL>>(nxChunk->rawLightDataCopy()) : nullptr;
            pzls = pzChunk ? std::make_shared<std::array<uint8_t, CHUNK_VOL>>(pzChunk->rawLightDataCopy()) : nullptr;
            nzls = nzChunk ? std::make_shared<std::array<uint8_t, CHUNK_VOL>>(nzChunk->rawLightDataCopy()) : nullptr;
        }

        const uint8_t* pxL = pxls ? pxls->data() : nullptr;
        const uint8_t* nxL = nxls ? nxls->data() : nullptr;
        const uint8_t* pzL = pzls ? pzls->data() : nullptr;
        const uint8_t* nzL = nzls ? nzls->data() : nullptr;

        MeshContext ctx{ centerData->data(), px, nx, nullptr, nullptr, pz, nz,
                         cld->data(), pxL, nxL, nullptr, nullptr, pzL, nzL };

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
