#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <thread>

#include "World.h"
#include "Renderer.h"
#include "Camera.h"
#include "Player.h"
#include "InputHandler.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Application — owns the window, game loop, and top-level subsystems
// ─────────────────────────────────────────────────────────────────────────────
class Application {
public:
    Application() {
        initGLFW();
        initGLEW();

        renderer_.init();

        // Spawn player above terrain
        player_ = std::make_unique<Player>(glm::vec3(0, 90, 0));
        camera_.setPosition(player_->eyePos());
    }

    ~Application() {
        glfwDestroyWindow(window_);
        glfwTerminate();
    }

    void run() {

        using Clock = std::chrono::high_resolution_clock;
        auto prev = Clock::now();

        while (!glfwWindowShouldClose(window_)) {
            auto now = Clock::now();
            float dt = std::chrono::duration<float>(now - prev).count();
            dt = std::min(dt, 0.05f);  // cap at 20 FPS equivalent (prevent spiral)
            prev = now;

            processInput(dt);
            world_.update(player_->position());
            player_->update(world_, dt);
            camera_.setPosition(player_->eyePos());

            // ── Render ────────────────────────────────────────────────────────
            glClearColor(0.53f, 0.81f, 0.98f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            renderer_.renderWorld(world_, camera_);

            glfwSwapBuffers(window_);
            glfwPollEvents();

            // No FPS limiter: do not sleep to cap frame rate.
        }
    }

private:
    // ─────────────────────────────────────────────────────────────────────────
    void initGLFW() {
        if (!glfwInit())
            throw std::runtime_error("Failed to init GLFW");

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_SAMPLES, 0);  // no MSAA (we use custom AA)

        window_ = glfwCreateWindow(1280, 720, "VoxelEngine", nullptr, nullptr);
        if (!window_) throw std::runtime_error("Failed to create GLFW window");

        glfwMakeContextCurrent(window_);
        glfwSwapInterval(1);  // uncapped for FPS measurement; set 1 for vsync

        // Capture mouse
        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        // Store this pointer for callbacks
        glfwSetWindowUserPointer(window_, this);

        glfwSetCursorPosCallback(window_, [](GLFWwindow* w, double x, double y) {
            static double lastX = x, lastY = y;
            static bool first = true;
            if (first) { lastX = x; lastY = y; first = false; }
            auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
            app->camera_.processMouseDelta(
                static_cast<float>(x - lastX),
                static_cast<float>(y - lastY));
            lastX = x; lastY = y;
        });

        glfwSetKeyCallback(window_, [](GLFWwindow* w, int key, int, int action, int) {
            auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
            if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
                glfwSetWindowShouldClose(w, true);
            if (key == GLFW_KEY_F3 && action == GLFW_PRESS)
                app->renderer_.setWireframe(!app->renderer_.wireframe());
            if (key == GLFW_KEY_F4 && action == GLFW_PRESS)
                app->player_->setFly(!app->player_->onGround());
            if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
                app->player_->jump();
        });

        glfwSetMouseButtonCallback(window_, [](GLFWwindow* w, int btn, int action, int) {
            auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
            if (action == GLFW_PRESS) {
                if (btn == GLFW_MOUSE_BUTTON_LEFT)
                    app->player_->breakBlock(app->world_, app->camera_.front());
                if (btn == GLFW_MOUSE_BUTTON_RIGHT)
                    app->player_->placeBlock(app->world_, app->camera_.front(), BlockType::Stone);
            }
        });
    }

    void initGLEW() {
        glewExperimental = GL_TRUE;
        if (glewInit() != GLEW_OK)
            throw std::runtime_error("Failed to init GLEW");
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
    }

    void processInput(float dt) {
        glm::vec3 dir{0};
        const glm::vec3 fwd  = glm::normalize(glm::vec3(camera_.front().x, 0, camera_.front().z));
        const glm::vec3 rgt  = camera_.right();

        if (glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS) dir += fwd;
        if (glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS) dir -= fwd;
        if (glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS) dir -= rgt;
        if (glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS) dir += rgt;

        if (glm::length(dir) > 0.01f)
            dir = glm::normalize(dir);

        player_->moveInput(dir);
    }

    GLFWwindow*               window_ = nullptr;
    World                     world_{42};
    Camera                    camera_{{0, 90, 0}};
    std::unique_ptr<Player>   player_;
    Renderer                  renderer_;
};

// ─────────────────────────────────────────────────────────────────────────────
int main() {
    try {
        Application app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << "\n";
        return 1;
    }
    return 0;
}
