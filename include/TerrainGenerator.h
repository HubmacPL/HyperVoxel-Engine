#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <glm/glm.hpp>
#include "Chunk.h"

class World;

// ─────────────────────────────────────────────────────────────────────────────
//  Minimal Perlin / Value noise (no external dep, header-only)
//  For production: swap with FastNoiseLite (single-header, excellent quality)
// ─────────────────────────────────────────────────────────────────────────────
class PerlinNoise {
public:
    explicit PerlinNoise(uint32_t seed = 0);
    // Returns value in [-1, 1]
    [[nodiscard]] float noise2D(float x, float y) const noexcept;
    [[nodiscard]] float noise3D(float x, float y, float z) const noexcept;
    // Fractal Brownian Motion
    [[nodiscard]] float fbm2D(float x, float y, int octaves,
                               float lacunarity = 2.0f, float gain = 0.5f) const noexcept;
private:
    std::array<uint8_t, 512> perm_;
    float fade(float t) const noexcept;
    float lerp(float t, float a, float b) const noexcept;
    float grad2(int hash, float x, float y) const noexcept;
    float grad3(int hash, float x, float y, float z) const noexcept;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Biome — determined by temperature + humidity
// ─────────────────────────────────────────────────────────────────────────────
enum class Biome : uint8_t {
    Ocean, Beach, Plains, Desert, Forest, Taiga, Mountains, Tundra
};

struct BiomeParams {
    float baseHeight;     // average surface Y
    float heightVariance; // amplitude of local height noise
    int   waterLevel = 64;
};

// ─────────────────────────────────────────────────────────────────────────────
//  TerrainGenerator
// ─────────────────────────────────────────────────────────────────────────────
class TerrainGenerator {
public:
    explicit TerrainGenerator(int seed);

    // Fill a chunk with terrain. Called from background thread.
    void generate(Chunk& chunk) const;
    void decorate(World& world, const glm::ivec2& chunkPos) const;
    void plantTreeAtWorld(World& world, int wx, int surfaceY, int wz,
                          std::vector<glm::ivec2>& touchedChunks) const noexcept;

private:
    // ── Biome sampling ────────────────────────────────────────────────────────
    [[nodiscard]] Biome getBiome(int wx, int wz) const noexcept;
    [[nodiscard]] float getTemperature(int wx, int wz) const noexcept;
    [[nodiscard]] float getHumidity(int wx, int wz) const noexcept;
    [[nodiscard]] float getBaseHeight(Biome b, int wx, int wz) const noexcept;

    // ── Block selection for column ────────────────────────────────────────────
    void fillColumn(Chunk& chunk, int lx, int lz,
                    int surfaceY, Biome biome) const noexcept;

    // ── Tree / feature placement ───────────────────────────────────────────────
    void plantTree(Chunk& chunk, int lx, int surfaceY, int lz) const noexcept;

    static constexpr int kSeaLevel = 64;

    int            seed_;
    PerlinNoise    heightNoise_;
    PerlinNoise    detailNoise_;
    PerlinNoise    tempNoise_;
    PerlinNoise    humidNoise_;
    PerlinNoise    caveNoise_;
};
