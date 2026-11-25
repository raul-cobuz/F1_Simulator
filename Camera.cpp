#include "Camera.hpp"

#include <algorithm>
#include <cmath>

namespace gps {

    Camera::Camera(glm::vec3 cameraPosition, glm::vec3 cameraTarget, glm::vec3 cameraUp) {
        this->cameraPosition = cameraPosition;
        this->cameraUpDirection = glm::normalize(cameraUp);

        glm::vec3 direction = glm::normalize(cameraTarget - cameraPosition);
        yaw = glm::degrees(std::atan2(direction.z, direction.x));
        pitch = glm::degrees(std::asin(std::max(-1.0f, std::min(1.0f, direction.y))));
        updateVectors();
    }

    void Camera::updateVectors() {
        float pitchRadians = glm::radians(pitch);
        float yawRadians = glm::radians(yaw);
        cameraFrontDirection = glm::normalize(glm::vec3(
            std::cos(pitchRadians) * std::cos(yawRadians),
            std::sin(pitchRadians),
            std::cos(pitchRadians) * std::sin(yawRadians)));
        cameraRightDirection = glm::normalize(glm::cross(cameraFrontDirection, cameraUpDirection));
        cameraTarget = cameraPosition + cameraFrontDirection;
    }

    glm::mat4 Camera::getViewMatrix() {
        return glm::lookAt(cameraPosition, cameraTarget, cameraUpDirection);
    }

    void Camera::move(MOVE_DIRECTION direction, float speed) {
        glm::vec3 forward(cameraFrontDirection.x, 0.0f, cameraFrontDirection.z);
        if (glm::length(forward) > 1e-5f) forward = glm::normalize(forward);

        switch (direction) {
            case MOVE_FORWARD: cameraPosition += forward * speed; break;
            case MOVE_BACKWARD: cameraPosition -= forward * speed; break;
            case MOVE_RIGHT: cameraPosition += cameraRightDirection * speed; break;
            case MOVE_LEFT: cameraPosition -= cameraRightDirection * speed; break;
        }
        updateVectors();
    }

    void Camera::rotate(float pitchDelta, float yawDelta) {
        yaw += yawDelta;
        pitch = std::max(-89.0f, std::min(89.0f, pitch + pitchDelta));
        updateVectors();
    }

    void Camera::setView(glm::vec3 position, glm::vec3 target) {
        cameraPosition = position;
        glm::vec3 direction = glm::normalize(target - position);
        yaw = glm::degrees(std::atan2(direction.z, direction.x));
        pitch = glm::degrees(std::asin(std::max(-1.0f, std::min(1.0f, direction.y))));
        updateVectors();
    }

    void Camera::setPosition(glm::vec3 position) {
        cameraPosition = position;
        updateVectors();
    }

    glm::vec3 Camera::getPosition() const {
        return cameraPosition;
    }
}
