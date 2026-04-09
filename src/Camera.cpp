#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

Camera::Camera(glm::vec3 pos, float yaw, float pitch)
    : pos_(pos), yaw_(yaw), pitch_(pitch) {
    updateVectors();
}

void Camera::processMouseDelta(float dx, float dy, float sensitivity) {
    yaw_   += dx * sensitivity;
    pitch_ -= dy * sensitivity;
    pitch_  = std::clamp(pitch_, -89.f, 89.f);
    updateVectors();
}

glm::mat4 Camera::viewMatrix() const noexcept {
    return glm::lookAt(pos_, pos_ + front_, up_);
}

glm::mat4 Camera::projMatrix(float aspect, float fov, float near, float far) const noexcept {
    return glm::perspective(glm::radians(fov), aspect, near, far);
}

void Camera::updateVectors() noexcept {
    float cy = std::cos(glm::radians(yaw_));
    float sy = std::sin(glm::radians(yaw_));
    float cp = std::cos(glm::radians(pitch_));
    float sp = std::sin(glm::radians(pitch_));

    front_ = glm::normalize(glm::vec3(cy*cp, sp, sy*cp));
    right_ = glm::normalize(glm::cross(front_, glm::vec3(0,1,0)));
    up_    = glm::normalize(glm::cross(right_, front_));
}
