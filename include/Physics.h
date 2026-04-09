#pragma once
#include <glm/glm.hpp>
#include "BlockRegistry.h"

class World;

// ─────────────────────────────────────────────────────────────────────────────
//  AABB — axis-aligned bounding box
// ─────────────────────────────────────────────────────────────────────────────
struct AABB {
    glm::vec3 min, max;
    bool intersects(const AABB& o) const noexcept;
    AABB expand(const glm::vec3& delta) const noexcept; // Minkowski sum
};

// ─────────────────────────────────────────────────────────────────────────────
//  Physics — swept AABB collision resolution against block world
// ─────────────────────────────────────────────────────────────────────────────
class Physics {
public:
    // Returns the velocity that was actually applied (after collisions)
    // Also sets onGround if entity is resting on a solid block
    static glm::vec3 resolveMovement(const World& world,
                                     AABB& entity,
                                     glm::vec3 velocity,
                                     float dt,
                                     bool& onGround);
private:
    static float sweepAxis(float vel, float boxMin, float boxMax,
                           float blockMin, float blockMax, float& tMin);
};

// ─────────────────────────────────────────────────────────────────────────────
//  Player
// ─────────────────────────────────────────────────────────────────────────────
class Player {
public:
    explicit Player(const glm::vec3& spawnPos);

    void update(World& world, float dt);

    void moveInput(const glm::vec3& dir) noexcept { inputDir_ = dir; }
    void jump();
    void setFly(bool on) noexcept { flying_ = on; }

    [[nodiscard]] glm::vec3 position()  const noexcept;
    [[nodiscard]] glm::vec3 eyePos()    const noexcept;   // pos + eyeOffset
    [[nodiscard]] const AABB& aabb()    const noexcept { return aabb_; }
    [[nodiscard]] bool onGround()       const noexcept { return onGround_; }

    // Block interaction
    void breakBlock(World& world, const glm::vec3& camDir);
    void placeBlock(World& world, const glm::vec3& camDir,
                    BlockType type = BlockType::Stone);

    static constexpr float kEyeOffset  = 1.62f;
    static constexpr float kHeight     = 1.8f;
    static constexpr float kWidth      = 0.6f;
    static constexpr float kReach      = 5.0f;
    static constexpr float kGravity    = -28.0f;  // m/s²
    static constexpr float kJumpSpeed  = 9.0f;
    static constexpr float kMoveSpeed  = 4.5f;
    static constexpr float kFlySpeed   = 12.0f;

private:
    AABB      aabb_;
    glm::vec3 velocity_  {0,0,0};
    glm::vec3 inputDir_  {0,0,0};
    bool      onGround_  = false;
    bool      flying_    = false;
};
