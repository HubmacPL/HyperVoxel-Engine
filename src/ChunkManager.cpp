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

            // Compute skylight propagation now (stops at unloaded neighbours).
            // The BFS is symmetric: it can mutate neighbour chunks too. Any
            // neighbour whose border light shifted needs its mesh rebuilt so
            // smooth-lighting samples don't read stale values across the seam.
            LightingUpdate touched = computeFloodFillSkylight(*center, nb);

            auto markIfUploaded = [](Chunk* c) {
                if (!c) return;
                ChunkState s = c->state();
                if (s >= ChunkState::Uploaded || s == ChunkState::NeedsMeshing)
                    c->markDirty();
            };
            if (touched.px) markIfUploaded(nb.px);
            if (touched.nx) markIfUploaded(nb.nx);
            if (touched.pz) markIfUploaded(nb.pz);
            if (touched.nz) markIfUploaded(nb.nz);

            // Mark chunk as TerrainGenerated so subsequent decoration/meshing may run
            center->setState(ChunkState::TerrainGenerated);
        }
    }

    // ── 1b. Process light-dirty chunks (block-edit relight) ──────────────────
    // Block placement/break sets lightDirty_. Run BFS on the main thread now,
    // before mesh scheduling, so the meshing snapshot we take below is stable
    // and any neighbours whose border was modified can be marked for remesh.
    for (auto& [cp, chunk] : chunks_) {
        if (!chunk->isLightDirty()) continue;
        // Need at least the four cardinal neighbours up so the BFS region is
        // well-defined; otherwise defer (lightDirty stays set).
        if (!neighboursReady(cp)) continue;

        auto getPtr = [&](glm::ivec2 ncp) -> Chunk* {
            auto nit = chunks_.find(ncp);
            if (nit != chunks_.end() && nit->second->state() >= ChunkState::TerrainGenerated)
                return nit->second.get();
            return nullptr;
        };
        NeighborChunks nb{};
        nb.px = getPtr({cp.x+1, cp.y});
        nb.nx = getPtr({cp.x-1, cp.y});
        nb.pz = getPtr({cp.x, cp.y+1});
        nb.nz = getPtr({cp.x, cp.y-1});

        chunk->resetSkylight();
        LightingUpdate touched = computeFloodFillSkylight(*chunk, nb);
        chunk->clearLightDirty();
        chunk->markDirty(); // center always needs remesh after relight

        auto markIfUploaded = [](Chunk* c) {
            if (!c) return;
            ChunkState s = c->state();
            if (s >= ChunkState::Uploaded || s == ChunkState::NeedsMeshing)
                c->markDirty();
        };
        (void)touched.center; // already handled
        if (touched.px) markIfUploaded(nb.px);
        if (touched.nx) markIfUploaded(nb.nx);
        if (touched.pz) markIfUploaded(nb.pz);
        if (touched.nz) markIfUploaded(nb.nz);
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

    // Keep neighbour chunks alive for the duration of meshing so their raw
    // block pointers (px/nx/pz/nz) cannot dangle if the chunk is unloaded.
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

    // Light recomputation now runs in tick() on the main thread *before* this
    // scheduling pass, so light arrays are stable here. We just snapshot them.
    auto centerLightData =
        std::make_shared<std::array<uint8_t, CHUNK_VOL>>(chunk->rawLightDataCopy());
    auto pxLightSnap = getLightSnap({cp.x+1, cp.y});
    auto nxLightSnap = getLightSnap({cp.x-1, cp.y});
    auto pzLightSnap = getLightSnap({cp.x, cp.y+1});
    auto nzLightSnap = getLightSnap({cp.x, cp.y-1});

    auto fut = meshPool_.submit([cp,
                                 px, nx, pz, nz,
                                 centerData,
                                 pxChunk, nxChunk, pzChunk, nzChunk,
                                 centerLightData,
                                 pxLightSnap, nxLightSnap,
                                 pzLightSnap, nzLightSnap]() mutable -> ChunkMesh {
        // pxChunk/nxChunk/pzChunk/nzChunk captured solely to keep neighbour
        // chunks alive while their raw block pointers (px/nx/pz/nz) are in use.
        (void)pxChunk; (void)nxChunk; (void)pzChunk; (void)nzChunk;

        const uint8_t* pxL = pxLightSnap ? pxLightSnap->data() : nullptr;
        const uint8_t* nxL = nxLightSnap ? nxLightSnap->data() : nullptr;
        const uint8_t* pzL = pzLightSnap ? pzLightSnap->data() : nullptr;
        const uint8_t* nzL = nzLightSnap ? nzLightSnap->data() : nullptr;

        MeshContext ctx{ centerData->data(), px, nx, nullptr, nullptr, pz, nz,
                         centerLightData->data(), pxL, nxL, nullptr, nullptr, pzL, nzL };

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
