#include "Player.h"
#include "World.h"
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
//  Player
// ─────────────────────────────────────────────────────────────────────────────
Player::Player(const glm::vec3& pos) {
    aabb_.min = pos - glm::vec3(kWidth/2, 0, kWidth/2);
    aabb_.max = pos + glm::vec3(kWidth/2, kHeight, kWidth/2);
}

glm::vec3 Player::position() const noexcept {
    return { (aabb_.min.x + aabb_.max.x) * 0.5f,
              aabb_.min.y,
             (aabb_.min.z + aabb_.max.z) * 0.5f };
}

glm::vec3 Player::eyePos() const noexcept {
    auto p = position();
    return { p.x, p.y + kEyeOffset, p.z };
}

void Player::jump() {
    if (onGround_ || flying_) {
        velocity_.y = flying_ ? kFlySpeed : kJumpSpeed;
        onGround_ = false;
    }
}

void Player::update(World& world, float dt) {
    // ── Guard: respawn if fallen out of world ─────────────────────────────────
    if (aabb_.min.y < -64.f) {
        glm::vec3 spawn{0, 90, 0};
        aabb_.min = spawn - glm::vec3(kWidth/2, 0, kWidth/2);
        aabb_.max = spawn + glm::vec3(kWidth/2, kHeight, kWidth/2);
        velocity_ = {0, 0, 0};
        return;
    }

    // ── Apply gravity ─────────────────────────────────────────────────────────
    if (!flying_) {
        velocity_.y += kGravity * dt;
        velocity_.y  = std::max(velocity_.y, -60.f);  // terminal velocity
    }

    // ── Horizontal movement (XZ plane) ────────────────────────────────────────
    float speed = flying_ ? kFlySpeed : kMoveSpeed;
    velocity_.x = inputDir_.x * speed;
    velocity_.z = inputDir_.z * speed;
    if (flying_) velocity_.y = inputDir_.y * speed;

    // ── Resolve collisions ────────────────────────────────────────────────────
    velocity_ = Physics::resolveMovement(world, aabb_, velocity_, dt, onGround_);

    if (onGround_ && velocity_.y < 0) velocity_.y = 0;
}

// ── Ray cast (DDA) for block interaction ─────────────────────────────────────
static bool raycastBlock(const World& world,
                          const glm::vec3& origin, const glm::vec3& dir,
                          float maxDist,
                          glm::ivec3& hitBlock, glm::ivec3& prevBlock)
{
    glm::vec3 pos = origin;
    glm::ivec3 cell = { (int)std::floor(pos.x),
                        (int)std::floor(pos.y),
                        (int)std::floor(pos.z) };
    glm::ivec3 step = { dir.x > 0 ? 1 : -1,
                        dir.y > 0 ? 1 : -1,
                        dir.z > 0 ? 1 : -1 };

    glm::vec3 tDelta = {
        std::abs(1.f / (dir.x != 0 ? dir.x : 1e-6f)),
        std::abs(1.f / (dir.y != 0 ? dir.y : 1e-6f)),
        std::abs(1.f / (dir.z != 0 ? dir.z : 1e-6f)),
    };
    glm::vec3 tMax = {
        (step.x > 0 ? (std::floor(pos.x) + 1.0f - pos.x) : (pos.x - std::floor(pos.x))) * tDelta.x,
        (step.y > 0 ? (std::floor(pos.y) + 1.0f - pos.y) : (pos.y - std::floor(pos.y))) * tDelta.y,
        (step.z > 0 ? (std::floor(pos.z) + 1.0f - pos.z) : (pos.z - std::floor(pos.z))) * tDelta.z,
    };

    prevBlock = cell;
    float t = 0;
    while (t < maxDist) {
        BlockType bt = world.getBlock(cell.x, cell.y, cell.z);
        if (BlockRegistry::instance().isSolid(bt) && bt != BlockType::Water) {
            hitBlock = cell;
            return true;
        }
        prevBlock = cell;
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            t = tMax.x; tMax.x += tDelta.x; cell.x += step.x;
        } else if (tMax.y < tMax.z) {
            t = tMax.y; tMax.y += tDelta.y; cell.y += step.y;
        } else {
            t = tMax.z; tMax.z += tDelta.z; cell.z += step.z;
        }
    }
    return false;
}

void Player::breakBlock(World& world, const glm::vec3& camDir) {
    glm::ivec3 hit, prev;
    if (raycastBlock(world, eyePos(), glm::normalize(camDir), kReach, hit, prev))
        world.setBlock(hit.x, hit.y, hit.z, BlockType::Air);
}

void Player::placeBlock(World& world, const glm::vec3& camDir, BlockType type) {
    glm::ivec3 hit, prev;
    if (raycastBlock(world, eyePos(), glm::normalize(camDir), kReach, hit, prev)) {
        // Ensure not placing inside player
        AABB blockAABB{
            { static_cast<float>(prev.x),   static_cast<float>(prev.y),   static_cast<float>(prev.z)   },
            { static_cast<float>(prev.x)+1, static_cast<float>(prev.y)+1, static_cast<float>(prev.z)+1 }
        };
        if (!aabb_.intersects(blockAABB))
            world.setBlock(prev.x, prev.y, prev.z, type);
    }
}
