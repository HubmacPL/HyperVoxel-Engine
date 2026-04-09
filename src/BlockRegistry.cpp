#include "BlockRegistry.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Atlas tile index helper
//  Tile ID = row * 16 + col  (row 0 is top of atlas)
// ─────────────────────────────────────────────────────────────────────────────
static constexpr uint8_t tile(int col, int row) {
    return static_cast<uint8_t>(row * 16 + col);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Per-face tile helper
//  Faces order: +X, -X, +Y (top), -Y (bottom), +Z, -Z
// ─────────────────────────────────────────────────────────────────────────────
static constexpr std::array<uint8_t,6> allFaces(uint8_t t) {
    return {t,t,t,t,t,t};
}
static constexpr std::array<uint8_t,6> topSideBottom(uint8_t top, uint8_t side, uint8_t bot) {
    return {side,side,top,bot,side,side};
}

BlockRegistry::BlockRegistry() {
    // ── Air ──────────────────────────────────────────────────────────────────
    defs_[0] = { "Air",    false, true,  false, 0.f, allFaces(0) };

    // ── Grass ─────────────────────────────────────────────────────────────────
    // Top: grass (0,0), Sides: grass-side (3,0), Bottom: dirt (2,0)
    defs_[1] = { "Grass",  true, false, false, 0.6f,
                 topSideBottom(tile(0,0), tile(3,0), tile(2,0)) };

    // ── Dirt ─────────────────────────────────────────────────────────────────
    defs_[2] = { "Dirt",   true, false, false, 0.5f, allFaces(tile(2,0)) };

    // ── Stone ─────────────────────────────────────────────────────────────────
    defs_[3] = { "Stone",  true, false, false, 1.5f, allFaces(tile(1,0)) };

    // ── Sand ──────────────────────────────────────────────────────────────────
    defs_[4] = { "Sand",   true, false, false, 0.5f, allFaces(tile(2,1)) };

    // ── Water ─────────────────────────────────────────────────────────────────
    defs_[5] = { "Water",  false, true,  true,  0.f,  allFaces(tile(13,12)) };

    // ── Wood (log) ────────────────────────────────────────────────────────────
    // Top/Bottom: log-top (5,1), Sides: log-bark (4,1)
    defs_[6] = { "Wood",   true, false, false, 2.0f,
                 topSideBottom(tile(5,1), tile(4,1), tile(5,1)) };

    // ── Leaves ────────────────────────────────────────────────────────────────
    defs_[7] = { "Leaves", true, true,  false, 0.2f, allFaces(tile(6,1)) };

    // ── Snow ──────────────────────────────────────────────────────────────────
    // Top: snow (2,4), Sides: snow-side (4,4), Bottom: dirt
    defs_[8] = { "Snow",   true, false, false, 0.2f,
                 topSideBottom(tile(2,4), tile(4,4), tile(2,0)) };

    // ── Ice ───────────────────────────────────────────────────────────────────
    defs_[9] = { "Ice",    true, true,  false, 0.5f, allFaces(tile(3,4)) };

    // ── Gravel ────────────────────────────────────────────────────────────────
    defs_[10] = { "Gravel", true, false, false, 0.6f, allFaces(tile(3,1)) };

    // ── Bedrock ───────────────────────────────────────────────────────────────
    defs_[11] = { "Bedrock", true, false, false, -1.f, allFaces(tile(1,1)) };
}

const BlockRegistry& BlockRegistry::instance() {
    static BlockRegistry inst;
    return inst;
}
