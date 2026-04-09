#include "World.h"
#include "ChunkManager.h"
#include "TerrainGenerator.h"
#include <cmath>

World::World(int seed) : seed_(seed) {
    terrainGen_ = std::make_unique<TerrainGenerator>(seed_);
    chunkManager_ = std::make_unique<ChunkManager>(*this, *terrainGen_, chunks_);
}

World::~World() = default;

void World::update(const glm::vec3& playerPos) {
    glm::ivec2 playerChunk = worldToChunkPos(
        static_cast<int>(playerPos.x),
        static_cast<int>(playerPos.z));

    // Only trigger a new load sweep when player crosses a chunk boundary
    if (playerChunk != lastPlayerChunk_) {
        lastPlayerChunk_ = playerChunk;
        loadChunksAround(playerPos);
        unloadFarChunks(playerPos);
    }

    // Each frame: tick the manager (collects finished jobs, submits new ones)
    chunkManager_->tick(playerChunk, renderDistance_);

    // Upload up to 4 completed meshes to GPU per frame (budget to avoid stutters)
    chunkManager_->uploadReadyMeshes(4);
}

void World::loadChunksAround(const glm::vec3& pos) {
    glm::ivec2 center = worldToChunkPos(
        static_cast<int>(pos.x), static_cast<int>(pos.z));

    for (int dz = -renderDistance_; dz <= renderDistance_; ++dz) {
        for (int dx = -renderDistance_; dx <= renderDistance_; ++dx) {
            if (dx*dx + dz*dz > renderDistance_*renderDistance_) continue;

            glm::ivec2 cp{center.x + dx, center.y + dz};
            if (chunks_.find(cp) == chunks_.end()) {
                chunks_[cp] = std::make_shared<Chunk>(cp);
                // ChunkManager::tick will pick it up and schedule generation
            }
        }
    }
}

void World::unloadFarChunks(const glm::vec3& pos) {
    glm::ivec2 center = worldToChunkPos(
        static_cast<int>(pos.x), static_cast<int>(pos.z));

    const int unloadDist = renderDistance_ + 2;

    std::vector<glm::ivec2> toUnload;
    for (auto& [cp, chunk] : chunks_) {
        int dx = cp.x - center.x;
        int dz = cp.y - center.y;
        if (dx*dx + dz*dz > unloadDist * unloadDist)
            toUnload.push_back(cp);
    }
    for (auto& cp : toUnload) {
        chunkManager_->unloadChunk(cp);
        chunks_.erase(cp);
    }
}

BlockType World::getBlock(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= CHUNK_H) return BlockType::Air;
    auto chunk = getChunkAt(wx, wz);
    if (!chunk) return BlockType::Air;
    auto local = worldToLocalPos(wx, wy, wz);
    return chunk->getBlock(local.x, local.y, local.z);
}

void World::setBlock(int wx, int wy, int wz, BlockType type) {
    if (wy < 0 || wy >= CHUNK_H) return;
    auto chunk = getChunkAt(wx, wz);
    if (!chunk) return;
    auto local = worldToLocalPos(wx, wy, wz);
    chunk->setBlock(local.x, local.y, local.z, type);

    // Flag light as stale — the recomputation runs in the background mesh thread.
    chunk->markLightDirty();

    // Mark center + boundary neighbours dirty for remeshing
    auto cp = worldToChunkPos(wx, wz);
    chunk->markDirty();
    if (local.x == 0)          { auto n = getChunk({cp.x-1, cp.y}); if(n) n->markDirty(); }
    if (local.x == CHUNK_W-1)  { auto n = getChunk({cp.x+1, cp.y}); if(n) n->markDirty(); }
    if (local.z == 0)          { auto n = getChunk({cp.x, cp.y-1}); if(n) n->markDirty(); }
    if (local.z == CHUNK_D-1)  { auto n = getChunk({cp.x, cp.y+1}); if(n) n->markDirty(); }
}

ChunkPtr World::getChunk(glm::ivec2 cp) const {
    auto it = chunks_.find(cp);
    return (it != chunks_.end()) ? it->second : nullptr;
}

ChunkPtr World::getChunkAt(int wx, int wz) const {
    return getChunk(worldToChunkPos(wx, wz));
}

bool World::isChunkLoaded(glm::ivec2 cp) const {
    auto it = chunks_.find(cp);
    return it != chunks_.end() &&
           it->second->state() == ChunkState::Uploaded;
}
