#include "Physics.h"
#include "World.h"
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
//  AABB
// ─────────────────────────────────────────────────────────────────────────────
bool AABB::intersects(const AABB& o) const noexcept {
    return max.x > o.min.x && min.x < o.max.x &&
           max.y > o.min.y && min.y < o.max.y &&
           max.z > o.min.z && min.z < o.max.z;
}

AABB AABB::expand(const glm::vec3& d) const noexcept {
    return {
        { std::min(min.x, min.x+d.x), std::min(min.y, min.y+d.y), std::min(min.z, min.z+d.z) },
        { std::max(max.x, max.x+d.x), std::max(max.y, max.y+d.y), std::max(max.z, max.z+d.z) }
    };
}

// ─────────────────────────────────────────────────────────────────────────────
//  Physics::resolveMovement
//
//  Classic Minecraft-style per-axis swept AABB:
//  1. Expand entity AABB along movement axis to cover full swept volume
//  2. Find all solid blocks overlapping that expanded box
//  3. Clamp movement to nearest collision minus EPSILON
//  Order: Y first (gravity most common), then X, then Z
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float EPSILON = 0.005f;

glm::vec3 Physics::resolveMovement(const World& world,
                                    AABB& entity,
                                    glm::vec3 vel,
                                    float dt,
                                    bool& onGround)
{
    glm::vec3 move = vel * dt;
    onGround = false;

    // Helper: collect all solid blocks overlapping AABB and clamp resolvedMove.
    // The search box may cover blocks behind the entity (expand covers both
    // current and destination).  We must never let a block behind the entity
    // reverse the direction of movement, so each offset is clamped to [0,+∞)
    // for positive moves and (-∞,0] for negative moves.
    auto testBlocks = [&](const AABB& box, float& resolvedMove, int axis) {
        // Skip near-zero movement — avoids NaN / pointless work
        if (std::abs(resolvedMove) < 1e-7f) return;

        int x0 = static_cast<int>(std::floor(box.min.x));
        int x1 = static_cast<int>(std::floor(box.max.x - 1e-4f));
        int y0 = static_cast<int>(std::floor(box.min.y));
        int y1 = static_cast<int>(std::floor(box.max.y - 1e-4f));
        int z0 = static_cast<int>(std::floor(box.min.z));
        int z1 = static_cast<int>(std::floor(box.max.z - 1e-4f));

        for (int bx = x0; bx <= x1; ++bx) {
            for (int by = y0; by <= y1; ++by) {
                for (int bz = z0; bz <= z1; ++bz) {
                    BlockType bt = world.getBlock(bx, by, bz);
                    if (!BlockRegistry::instance().isSolid(bt)) continue;

                    AABB block{ {(float)bx, (float)by, (float)bz},
                                {(float)bx+1, (float)by+1, (float)bz+1} };

                    if (!box.intersects(block)) continue;

                    if (axis == 0) { // X
                        if (resolvedMove > 0) {
                            float limit = (block.min.x - entity.max.x) - EPSILON;
                            resolvedMove = std::min(resolvedMove, std::max(0.0f, limit));
                        } else {
                            float limit = (block.max.x - entity.min.x) + EPSILON;
                            resolvedMove = std::max(resolvedMove, std::min(0.0f, limit));
                        }
                    } else if (axis == 1) { // Y
                        if (resolvedMove > 0) {
                            float limit = (block.min.y - entity.max.y) - EPSILON;
                            resolvedMove = std::min(resolvedMove, std::max(0.0f, limit));
                        } else {
                            float limit = (block.max.y - entity.min.y) + EPSILON;
                            resolvedMove = std::max(resolvedMove, std::min(0.0f, limit));
                        }
                    } else { // Z
                        if (resolvedMove > 0) {
                            float limit = (block.min.z - entity.max.z) - EPSILON;
                            resolvedMove = std::min(resolvedMove, std::max(0.0f, limit));
                        } else {
                            float limit = (block.max.z - entity.min.z) + EPSILON;
                            resolvedMove = std::max(resolvedMove, std::min(0.0f, limit));
                        }
                    }
                }
            }
        }
    };

    glm::vec3 resolved = move;

    // ── Y axis first (gravity) ────────────────────────────────────────────────
    {
        AABB searchBox = entity.expand(glm::vec3(0, resolved.y, 0));
        testBlocks(searchBox, resolved.y, 1);
        if (resolved.y != move.y) {
            if (move.y < 0) onGround = true;
            vel.y = 0;
        }
        entity.min.y += resolved.y;
        entity.max.y += resolved.y;
    }

    // ── X axis ────────────────────────────────────────────────────────────────
    {
        AABB searchBox = entity.expand(glm::vec3(resolved.x, 0, 0));
        testBlocks(searchBox, resolved.x, 0);
        if (resolved.x != move.x) vel.x = 0;
        entity.min.x += resolved.x;
        entity.max.x += resolved.x;
    }

    // ── Z axis ────────────────────────────────────────────────────────────────
    {
        AABB searchBox = entity.expand(glm::vec3(0, 0, resolved.z));
        testBlocks(searchBox, resolved.z, 2);
        if (resolved.z != move.z) vel.z = 0;
        entity.min.z += resolved.z;
        entity.max.z += resolved.z;
    }

    return vel;
}
