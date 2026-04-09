#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    Camera(glm::vec3 pos = {0,80,0},
           float yaw   = -90.f,
           float pitch = 0.f);

    void processMouseDelta(float dx, float dy, float sensitivity = 0.1f);
    void setPosition(const glm::vec3& pos) noexcept { pos_ = pos; }

    [[nodiscard]] glm::mat4 viewMatrix()       const noexcept;
    [[nodiscard]] glm::mat4 projMatrix(float aspect,
                                        float fov  = 70.f,
                                        float near = 0.1f,
                                        float far  = 800.f) const noexcept;
    [[nodiscard]] glm::vec3 position()  const noexcept { return pos_; }
    [[nodiscard]] glm::vec3 front()     const noexcept { return front_; }
    [[nodiscard]] glm::vec3 right()     const noexcept { return right_; }
    [[nodiscard]] glm::vec3 up()        const noexcept { return up_; }
    [[nodiscard]] float     yaw()       const noexcept { return yaw_; }
    [[nodiscard]] float     pitch()     const noexcept { return pitch_; }

private:
    void updateVectors() noexcept;
    glm::vec3 pos_, front_, right_, up_;
    float yaw_, pitch_;
};
