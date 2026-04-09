#include "TerrainGenerator.h"
#include "World.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>

// ─────────────────────────────────────────────────────────────────────────────
//  PerlinNoise implementation
// ─────────────────────────────────────────────────────────────────────────────
PerlinNoise::PerlinNoise(uint32_t seed) {
    std::iota(perm_.begin(), perm_.begin() + 256, 0);
    std::default_random_engine rng(seed);
    std::shuffle(perm_.begin(), perm_.begin() + 256, rng);
    for (int i = 0; i < 256; ++i) perm_[256 + i] = perm_[i];
}

float PerlinNoise::fade(float t) const noexcept {
    return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
}
float PerlinNoise::lerp(float t, float a, float b) const noexcept {
    return a + t * (b - a);
}
float PerlinNoise::grad2(int hash, float x, float y) const noexcept {
    int h = hash & 7;
    float u = h < 4 ? x : y;
    float v = h < 4 ? y : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}
float PerlinNoise::grad3(int hash, float x, float y, float z) const noexcept {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

float PerlinNoise::noise2D(float x, float y) const noexcept {
    int X = static_cast<int>(std::floor(x)) & 255;
    int Y = static_cast<int>(std::floor(y)) & 255;
    x -= std::floor(x); y -= std::floor(y);
    float u = fade(x), v = fade(y);
    int A = perm_[X]+Y, AA = perm_[A], AB = perm_[A+1];
    int B = perm_[X+1]+Y, BA = perm_[B], BB = perm_[B+1];
    return lerp(v, lerp(u, grad2(perm_[AA], x,   y  ),
                           grad2(perm_[BA], x-1, y  )),
                   lerp(u, grad2(perm_[AB], x,   y-1),
                           grad2(perm_[BB], x-1, y-1)));
}

float PerlinNoise::noise3D(float x, float y, float z) const noexcept {
    int X = static_cast<int>(std::floor(x)) & 255;
    int Y = static_cast<int>(std::floor(y)) & 255;
    int Z = static_cast<int>(std::floor(z)) & 255;
    x -= std::floor(x); y -= std::floor(y); z -= std::floor(z);
    float u = fade(x), v = fade(y), w = fade(z);
    int A  = perm_[X]   + Y, AA = perm_[A] + Z, AB = perm_[A+1] + Z;
    int B  = perm_[X+1] + Y, BA = perm_[B] + Z, BB = perm_[B+1] + Z;
    return lerp(w, lerp(v, lerp(u, grad3(perm_[AA  ], x,   y,   z  ),
                                   grad3(perm_[BA  ], x-1, y,   z  )),
                           lerp(u, grad3(perm_[AB  ], x,   y-1, z  ),
                                   grad3(perm_[BB  ], x-1, y-1, z  ))),
                   lerp(v, lerp(u, grad3(perm_[AA+1], x,   y,   z-1),
                                   grad3(perm_[BA+1], x-1, y,   z-1)),
                           lerp(u, grad3(perm_[AB+1], x,   y-1, z-1),
                                   grad3(perm_[BB+1], x-1, y-1, z-1))));
}

float PerlinNoise::fbm2D(float x, float y, int octaves,
                          float lacunarity, float gain) const noexcept {
    float val = 0.f, amp = 0.5f, freq = 1.f;
    for (int i = 0; i < octaves; ++i) {
        val += amp * noise2D(x * freq, y * freq);
        amp *= gain;
        freq *= lacunarity;
    }
    return val;
}

// ─────────────────────────────────────────────────────────────────────────────
//  TerrainGenerator
// ─────────────────────────────────────────────────────────────────────────────
TerrainGenerator::TerrainGenerator(int seed)
    : seed_(seed)
    , heightNoise_(seed ^ 0xDEAD)
    , detailNoise_(seed ^ 0xBEEF)
    , tempNoise_  (seed ^ 0x1234)
    , humidNoise_ (seed ^ 0x5678)
    , caveNoise_  (seed ^ 0x9ABC)
{}

float TerrainGenerator::getTemperature(int wx, int wz) const noexcept {
    return tempNoise_.fbm2D(wx * 0.001f, wz * 0.001f, 3) * 0.5f + 0.5f;
}

float TerrainGenerator::getHumidity(int wx, int wz) const noexcept {
    return humidNoise_.fbm2D(wx * 0.001f, wz * 0.001f, 3) * 0.5f + 0.5f;
}

Biome TerrainGenerator::getBiome(int wx, int wz) const noexcept {
    float t = getTemperature(wx, wz);
    float h = getHumidity(wx, wz);

    if (t < 0.2f)                return Biome::Tundra;
    if (t < 0.35f)               return Biome::Taiga;
    if (t > 0.75f && h < 0.3f)  return Biome::Desert;
    if (t > 0.6f  && h > 0.6f)  return Biome::Forest;
    if (h > 0.7f)                return Biome::Forest;
    return Biome::Plains;
}

float TerrainGenerator::getBaseHeight(Biome biome, int wx, int wz) const noexcept {
    float base = heightNoise_.fbm2D(wx * 0.004f, wz * 0.004f, 6);
    float detail = detailNoise_.fbm2D(wx * 0.02f, wz * 0.02f, 4) * 0.15f;
    float h = (base + detail) * 0.5f + 0.5f;  // [0,1]

    switch (biome) {
        case Biome::Ocean:      return 48.f  + h * 8.f;
        case Biome::Beach:      return 62.f  + h * 4.f;
        case Biome::Plains:     return 65.f  + h * 12.f;
        case Biome::Desert:     return 65.f  + h * 15.f;
        case Biome::Forest:     return 68.f  + h * 18.f;
        case Biome::Taiga:      return 70.f  + h * 20.f;
        case Biome::Mountains:  return 80.f  + h * 50.f;
        case Biome::Tundra:     return 64.f  + h * 10.f;
    }
    return 64.f;
}

void TerrainGenerator::fillColumn(Chunk& chunk, int lx, int lz,
                                    int surfaceY, Biome biome) const noexcept {
    // Bedrock bottom
    chunk.setBlock(lx, 0, lz, BlockType::Bedrock);

    // Stone base
    int stoneTop = surfaceY - 4;
    for (int y = 1; y < stoneTop; ++y)
        chunk.setBlock(lx, y, lz, BlockType::Stone);

    // Biome-specific surface layers
    bool underwater = surfaceY < kSeaLevel;

    switch (biome) {
        case Biome::Desert:
            for (int y = stoneTop; y < surfaceY; ++y)
                chunk.setBlock(lx, y, lz, BlockType::Sand);
            chunk.setBlock(lx, surfaceY, lz, BlockType::Sand);
            break;
        case Biome::Tundra:
            for (int y = stoneTop; y < surfaceY; ++y)
                chunk.setBlock(lx, y, lz, BlockType::Dirt);
            chunk.setBlock(lx, surfaceY, lz, BlockType::Snow);
            break;
        case Biome::Ocean:
        case Biome::Beach:
            for (int y = stoneTop; y <= surfaceY; ++y)
                chunk.setBlock(lx, y, lz, BlockType::Sand);
            break;
        default:
            for (int y = stoneTop; y < surfaceY; ++y)
                chunk.setBlock(lx, y, lz, BlockType::Dirt);
            chunk.setBlock(lx, surfaceY, lz,
                underwater ? BlockType::Dirt : BlockType::Grass);
            break;
    }

    // Fill water up to sea level
    for (int y = surfaceY + 1; y <= kSeaLevel; ++y)
        chunk.setBlock(lx, y, lz, BlockType::Water);
}

void TerrainGenerator::plantTree(Chunk& chunk, int lx, int surfaceY, int lz) const noexcept {
    if (lx < 2 || lx > CHUNK_W-3 || lz < 2 || lz > CHUNK_D-3) return;

    int trunkH = 4 + (surfaceY & 2); // Wysokość 4 lub 6

    // 1. Postaw pień
    for (int y = surfaceY + 1; y <= surfaceY + trunkH; ++y) {
        chunk.setBlock(lx, y, lz, BlockType::Wood);
    }

    // 2. Postaw koronę liści
    for (int dy = -2; dy <= 1; ++dy) {
        int radius = (dy < 0) ? 2 : 1;
        int leafY  = surfaceY + trunkH + dy;

        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                if (std::abs(dx) == radius && std::abs(dz) == radius) continue;
                if (chunk.getBlock(lx + dx, leafY, lz + dz) == BlockType::Air) {
                    chunk.setBlock(lx + dx, leafY, lz + dz, BlockType::Leaves);
                }
            }
        }
    }

    if (chunk.getBlock(lx, surfaceY + trunkH + 2, lz) == BlockType::Air) {
        chunk.setBlock(lx, surfaceY + trunkH + 2, lz, BlockType::Leaves);
    }
}

void TerrainGenerator::plantTreeAtWorld(World& world, int wx, int surfaceY, int wz,
                                       std::vector<glm::ivec2>& touchedChunks) const noexcept {
    // Basic placement constraints: surface and overhead must be free
    BlockType ground = world.getBlock(wx, surfaceY, wz);
    if (ground != BlockType::Grass && ground != BlockType::Dirt) return;
    if (world.getBlock(wx, surfaceY + 1, wz) != BlockType::Air) return;

    int trunkH = 4 + ((wx + wz + surfaceY + seed_) & 1);
    for (int y = surfaceY + 1; y <= surfaceY + trunkH; ++y) {
        if (world.getBlock(wx, y, wz) != BlockType::Air) return;
    }

    auto touch = [&](int x, int z) {
        touchedChunks.push_back(World::worldToChunkPos(x, z));
    };

    for (int y = surfaceY + 1; y <= surfaceY + trunkH; ++y) {
        world.setBlock(wx, y, wz, BlockType::Wood);
        touch(wx, wz);
    }

    for (int dy = -2; dy <= 1; ++dy) {
        int radius = (dy < 0) ? 2 : 1;
        int leafY = surfaceY + trunkH + dy;
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                if (std::abs(dx) == radius && std::abs(dz) == radius) continue;
                int lx = wx + dx;
                int lz = wz + dz;
                if (world.getBlock(lx, leafY, lz) == BlockType::Air) {
                    world.setBlock(lx, leafY, lz, BlockType::Leaves);
                    touch(lx, lz);
                }
            }
        }
    }

    if (world.getBlock(wx, surfaceY + trunkH + 2, wz) == BlockType::Air) {
        world.setBlock(wx, surfaceY + trunkH + 2, wz, BlockType::Leaves);
        touch(wx, wz);
    }
}

void TerrainGenerator::decorate(World& world, const glm::ivec2& chunkPos) const {
    auto chunk = world.getChunk(chunkPos);
    if (!chunk) return;

    const int baseX = chunkPos.x * CHUNK_W;
    const int baseZ = chunkPos.y * CHUNK_D;
    uint32_t chunkSeed = static_cast<uint32_t>(
        baseX * 1619 + baseZ * 31337 + seed_ * 1000003);
    std::default_random_engine rng(chunkSeed);
    std::uniform_real_distribution<float> dist01(0.f, 1.f);

    std::vector<glm::ivec2> touchedChunks;
    touchedChunks.reserve(32);

    for (int lz = 0; lz < CHUNK_D; ++lz) {
        for (int lx = 0; lx < CHUNK_W; ++lx) {
            int wx = baseX + lx;
            int wz = baseZ + lz;
            int surfaceY = chunk->heightmap[lz * CHUNK_W + lx];
            Biome biome = getBiome(wx, wz);

            if (surfaceY <= kSeaLevel) continue;

            if (biome == Biome::Forest && dist01(rng) < 0.04f) {
                plantTreeAtWorld(world, wx, surfaceY, wz, touchedChunks);
            } else if (biome == Biome::Plains && dist01(rng) < 0.01f) {
                plantTreeAtWorld(world, wx, surfaceY, wz, touchedChunks);
            }
        }
    }

    std::sort(touchedChunks.begin(), touchedChunks.end(),
        [](const glm::ivec2& a, const glm::ivec2& b) {
            return a.x < b.x || (a.x == b.x && a.y < b.y);
        });
    touchedChunks.erase(std::unique(touchedChunks.begin(), touchedChunks.end(),
        [](const glm::ivec2& a, const glm::ivec2& b) {
            return a.x == b.x && a.y == b.y;
        }), touchedChunks.end());

    for (auto& cp : touchedChunks) {
        auto touchedChunk = world.getChunk(cp);
        if (touchedChunk && touchedChunk->state() >= ChunkState::TerrainGenerated) {
            touchedChunk->setState(ChunkState::NeedsMeshing);
        }
    }

    if (chunk->state() >= ChunkState::TerrainGenerated) {
        chunk->setState(ChunkState::NeedsMeshing);
    }
}

void TerrainGenerator::generate(Chunk& chunk) const {
    const int baseX = chunk.chunkPos().x * CHUNK_W;
    const int baseZ = chunk.chunkPos().y * CHUNK_D;

    // Seed per-chunk RNG for deterministic decoration later
    uint32_t chunkSeed = static_cast<uint32_t>(
        baseX * 1619 + baseZ * 31337 + seed_ * 1000003);
    std::default_random_engine rng(chunkSeed);

    for (int lz = 0; lz < CHUNK_D; ++lz) {
        for (int lx = 0; lx < CHUNK_W; ++lx) {
            int wx = baseX + lx;
            int wz = baseZ + lz;

            Biome  biome    = getBiome(wx, wz);
            int    surfaceY = std::clamp(
                static_cast<int>(getBaseHeight(biome, wx, wz)),
                1, CHUNK_H - 2);

            // Store in heightmap for AO / feature lookups
            chunk.heightmap[lz * CHUNK_W + lx] = static_cast<uint8_t>(
                std::min(surfaceY, 255));

            fillColumn(chunk, lx, lz, surfaceY, biome);

            // ── Cave carving (3D Perlin threshold) ────────────────────────────
            for (int y = 1; y < surfaceY - 2; ++y) {
                float cave = caveNoise_.noise3D(
                    wx * 0.05f, y * 0.05f, wz * 0.05f);
                if (cave > 0.6f)
                    chunk.setBlock(lx, y, lz, BlockType::Air);
            }

            // Tree placement deferred to decorate(), so chunk edges can be written safely.
        }
    }
}
