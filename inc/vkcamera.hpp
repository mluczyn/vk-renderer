#pragma once
#include <optional>
#include <set>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

class CameraInputHandler {
public:
    void onCursorPosChange(double x, double y) {
        glm::vec2 newPos{x, y};
        if (mPrevCursorPos) {
            auto delta = newPos - mPrevCursorPos.value();
            auto r = glm::cross(mCameraForward, mCameraUp);
            auto u = glm::cross(r, mCameraForward);
            auto axis = r * delta.x + u * delta.y;
            axis = glm::cross(mCameraForward, axis);
            auto angle = glm::radians(60.0f) * glm::length(delta);
            auto rotMat = glm::rotate(glm::mat4(1.0f), angle, axis);
            mCameraForward = rotMat * glm::vec4{mCameraForward, 1.0f};
        }
        mPrevCursorPos = newPos;
    }
    void onKeyEvent(int key, int action, [[maybe_unused]] int mods) {
        if (action == GLFW_RELEASE)
            return;
        
        if (mCameraKeySet.count(key)) {
            float mult = mCameraMoveMult;
            if(key == GLFW_KEY_SPACE || key == GLFW_KEY_A || key == GLFW_KEY_S)
                mult *= -1.0f;
            
            auto dir = glm::cross(mCameraForward, mCameraUp);
            if(key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_SPACE)
                dir = mCameraUp;
            else if(key == GLFW_KEY_W || key == GLFW_KEY_S)
                dir = mCameraForward;

            mCameraPos += dir * mult;
        }
    }
    inline glm::mat4 getView() const {
        return glm::lookAt(mCameraPos, mCameraPos + mCameraForward, mCameraUp);
    }
    inline glm::vec3 getPos() const {
        return mCameraPos;
    }
private:
    const glm::vec3 mCameraUp{0.0f, 1.0f, 0.0f};
    glm::vec3 mCameraPos{0.0f, 0.0f, 3.0f}, mCameraForward{0.0f, 0.0f, -1.0f};
    std::optional<glm::vec2> mPrevCursorPos;
    const float mCameraMoveMult = 0.05f;
    const std::set<int> mCameraKeySet = {GLFW_KEY_SPACE, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_W, GLFW_KEY_A, GLFW_KEY_S, GLFW_KEY_D};
};