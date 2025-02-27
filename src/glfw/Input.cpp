#include "Input.h"

#include <GLFW/glfw3.h>
#include <algorithm>

#include "../Logger.h"
#include "Window.h"

namespace glfw {
    Input::Input(const glfw::Window &window) : window_(window) {
        if (instance != nullptr) {
            Logger::panic("Only one instance of Input can be created");
        }
        instance = this;

        for (int key = GLFW_KEY_SPACE; key < keysWrite_.size(); key++) {
            int sc = glfwGetKeyScancode(key);
            if (sc == -1)
                continue;
            const char *name = glfwGetKeyName(key, sc);
            if (name != nullptr) {
                keyMap_[name] = key;
            }
        }

        auto win_ptr = static_cast<GLFWwindow *>(window_);
        storedKeyCallback_ = reinterpret_cast<void *(*) ()>( //
                glfwSetKeyCallback(
                        win_ptr,
                        [](GLFWwindow *window, int key, int scancode, int action, int mods) { //
                            instance->onKey(window, key, scancode, action, mods);
                        }
                )
        );
        storedCurorPosCallback_ = reinterpret_cast<void *(*) ()>( //
                glfwSetCursorPosCallback(
                        win_ptr, [](GLFWwindow *window, double x, double y) { instance->onCursorPos(window, x, y); }
                )
        );
        storedMouseButtonCallback_ = reinterpret_cast<void *(*) ()>( //
                glfwSetMouseButtonCallback(
                        win_ptr,
                        [](GLFWwindow *window, int button, int action, int mods) { //
                            instance->onMouseButton(window, button, action, mods);
                        }
                )
        );
        storedScrollCallback_ = reinterpret_cast<void *(*) ()>( //
                glfwSetScrollCallback(
                        win_ptr,
                        [](GLFWwindow *window, double dx, double dy) { //
                            instance->onScroll(window, dx, dy);
                        }
                )
        );
        storedCharCallback_ = reinterpret_cast<void *(*) ()>( //
                glfwSetCharCallback(
                        win_ptr,
                        [](GLFWwindow *window, unsigned int codepoint) { //
                            instance->onChar(window, codepoint);
                        }
                )
        );
        storedWindowFocusCallback_ = reinterpret_cast<void *(*) ()>( //
                glfwSetWindowFocusCallback(
                        win_ptr,
                        [](GLFWwindow * /*window*/, int /*focused*/) { //
                            instance->invalidate();
                        }
                )
        );
    }

    Input::~Input() {
        auto win_ptr = static_cast<GLFWwindow *>(window_);
        glfwSetKeyCallback(win_ptr, reinterpret_cast<GLFWkeyfun>(storedKeyCallback_));
        glfwSetCursorPosCallback(win_ptr, reinterpret_cast<GLFWcursorposfun>(storedCurorPosCallback_));
        glfwSetMouseButtonCallback(win_ptr, reinterpret_cast<GLFWmousebuttonfun>(storedMouseButtonCallback_));
        glfwSetScrollCallback(win_ptr, reinterpret_cast<GLFWscrollfun>(storedScrollCallback_));
        glfwSetCharCallback(win_ptr, reinterpret_cast<GLFWcharfun>(storedCharCallback_));
        glfwSetWindowFocusCallback(win_ptr, reinterpret_cast<GLFWwindowfocusfun>(storedWindowFocusCallback_));

        instance = nullptr;
    }

    void Input::pollCurrentState_() {
        stateInvalid_ = false;

        for (int key = GLFW_KEY_SPACE; key < keysWrite_.size(); key++) {
            int state = glfwGetKey(static_cast<GLFWwindow *>(window_), key);
            keysWrite_[key] = state == GLFW_PRESS ? State::PersistentPressedMask : State::Zero;
        }

        for (int button = GLFW_MOUSE_BUTTON_1; button < mouseButtonsWrite_.size(); button++) {
            int state = glfwGetMouseButton(static_cast<GLFWwindow *>(window_), button);
            mouseButtonsWrite_[button] = state == GLFW_PRESS ? State::PersistentPressedMask : State::Zero;
        }

        double mouse_x = 0, mouse_y = 0;
        glfwGetCursorPos(static_cast<GLFWwindow *>(window_), &mouse_x, &mouse_y);
        mousePosWrite_ = {mouse_x, mouse_y};
        // No mouse delta
        mousePosRead_ = mousePosWrite_;

        mouseCaptured_ = glfwGetInputMode(static_cast<GLFWwindow *>(window_), GLFW_CURSOR) == GLFW_CURSOR_DISABLED;

        // No time delta
        timeRead_ = glfwGetTime();
    }

    void Input::update() {
        glfwPollEvents();

        if (stateInvalid_) {
            pollCurrentState_();
        }

        double time = glfwGetTime();
        timeDelta_ = static_cast<float>(time - timeRead_);
        timeRead_ = time;

        mouseDelta_ = mousePosWrite_ - mousePosRead_;
        mousePosRead_ = mousePosWrite_;

        scrollDeltaRead_ = scrollDeltaWrite_;
        scrollDeltaWrite_ = glm::vec2(0.0f);

        // During a frame key events are captured and flags set in the keysWrite_ buffer.
        // After a frame the keysWrite_ buffer is copied to the keysRead_ buffer and then cleared.
        std::copy(std::begin(keysWrite_), std::end(keysWrite_), std::begin(keysRead_));
        // The state changes (pressed, released) are cleared but the current state is kept
        for (auto &state: keysWrite_) {
            state &= State::ClearMask;
        }

        // Same for mouse buttons
        std::copy(std::begin(mouseButtonsWrite_), std::end(mouseButtonsWrite_), std::begin(mouseButtonsRead_));
        for (auto &state: mouseButtonsWrite_) {
            state &= State::ClearMask;
        }
    }

    void Input::captureMouse() {
        glfwSetInputMode(static_cast<GLFWwindow *>(window_), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        mouseCaptured_ = true;
    }

    void Input::releaseMouse() {
        glfwSetInputMode(static_cast<GLFWwindow *>(window_), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        mouseCaptured_ = false;
    }

    void Input::centerMouse() const {
        int w, h;
        glfwGetWindowSize(static_cast<GLFWwindow *>(window_), &w, &h);
        glfwSetCursorPos(static_cast<GLFWwindow *>(window_), w / 2, h / 2);
    }

    bool Input::isWindowFocused() const {
        return glfwGetWindowAttrib(static_cast<GLFWwindow *>(window_), GLFW_FOCUSED) == GLFW_TRUE;
    }

    void Input::onKey(GLFWwindow * /*window*/, int key, int scancode, int action, int mods) {
        if (key < 0 || key >= keysWrite_.size())
            return; // special keys, e.g.: mute sound
        if (action == GLFW_PRESS) {
            // set the pressed and down bit
            keysWrite_[key] |= State::PressedBit;
            keysWrite_[key] |= State::PersistentPressedBit;
        }

        if (action == GLFW_RELEASE) {
            // set the released and clear the down bit
            keysWrite_[key] |= State::ReleasedBit;
            keysWrite_[key] &= static_cast<State>(~static_cast<uint8_t>(State::PersistentPressedBit));
        }

        for (auto &&reg: keyCallbacks_)
            reg.callback(key, scancode, action, mods);
    }

    void Input::onCursorPos(GLFWwindow * /*window*/, double x, double y) {
        mousePosWrite_.x = static_cast<float>(x);
        mousePosWrite_.y = static_cast<float>(y);

        for (auto &&reg: mousePosCallbacks_)
            reg.callback(static_cast<float>(x), static_cast<float>(y));
    }

    void Input::onMouseButton(GLFWwindow * /*window*/, int button, int action, int mods) {
        if (action == GLFW_PRESS) {
            // set the pressed and down bit
            mouseButtonsWrite_[button] |= State::PressedBit;
            mouseButtonsWrite_[button] |= State::PersistentPressedBit;
        }

        if (action == GLFW_RELEASE) {
            // set the released and clear the down bit
            mouseButtonsWrite_[button] |= State::ReleasedBit;
            mouseButtonsWrite_[button] &= static_cast<State>(~static_cast<uint8_t>(State::PersistentPressedBit));
        }

        for (auto &&reg: mouseButtonCallbacks_)
            reg.callback(button, action, mods);
    }

    void Input::onScroll(GLFWwindow * /*window*/, double dx, double dy) {
        scrollDeltaWrite_.x += static_cast<float>(dx);
        scrollDeltaWrite_.y += static_cast<float>(dy);

        for (auto &&reg: scrollCallbacks_)
            reg.callback(static_cast<float>(dx), static_cast<float>(dy));
    }

    void Input::onChar(GLFWwindow * /*window*/, unsigned int codepoint) {
        for (auto &&reg: charCallbacks_)
            reg.callback(codepoint);
    }

    // a little helper function
    template<typename T>
    void removeCallbackFromVec(std::vector<T> &vec, Input::CallbackRegistrationID id) {
        vec.erase(std::remove_if(vec.begin(), vec.end(), [&id](T &elem) { return elem.id == id; }), vec.end());
    }

    void Input::removeCallback(CallbackRegistrationID &registration) {
        if (registration == 0) {
            Logger::warning("removeCallback called with invalid registration id (0)");
        }
        removeCallbackFromVec(mousePosCallbacks_, registration);
        removeCallbackFromVec(mouseButtonCallbacks_, registration);
        removeCallbackFromVec(scrollCallbacks_, registration);
        removeCallbackFromVec(keyCallbacks_, registration);
        removeCallbackFromVec(charCallbacks_, registration);
        registration = 0;
    }
} // namespace glfw
