#if defined (__APPLE__)
    #define GLFW_INCLUDE_GLCOREARB
    #define GL_SILENCE_DEPRECATION
#else
    #define GLEW_STATIC
    #include <GL/glew.h>
#endif

#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Window.h"
#include "Shader.hpp"
#include "Camera.hpp"
#include "Model3D.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

const int MAX_POINT_LIGHTS = 6;
const int SHADOW_SIZE = 2048;
const int RAIN_COUNT = 2000;
const float CAR_SPEED = 26.0f;
const float WHEEL_RADIUS = 0.36f;
const float CAMERA_RADIUS = 0.6f;
const float CAR_COLLISION_RADIUS = 1.1f;
const float DRIVE_MAX_SPEED = 55.0f;
const float DRIVE_REVERSE_SPEED = 8.0f;
const float DRIVE_ACCEL = 14.0f;
const float DRIVE_BRAKE = 26.0f;
const float DRIVE_DRAG = 4.0f;

const glm::vec3 SCENE_MIN(-230.0f, 0.0f, -150.0f);
const glm::vec3 SCENE_MAX(240.0f, 120.0f, 90.0f);

struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

struct SceneCoords {
    glm::vec3 carStart{0.0f, 0.0f, 40.0f};
    glm::vec3 carForward{1.0f, 0.0f, 0.0f};
    glm::vec3 wheelAxle{0.0f, 0.0f, 1.0f};
    std::array<glm::vec3, MAX_POINT_LIGHTS> lightHeads{{
        {110.0049f, 18.3f, -84.5847f},
        {39.6842f, 18.3f, -69.2822f},
        {-42.9136f, 18.3f, -20.8174f},
        {-130.7782f, 18.3f, -61.3689f},
        {-167.6293f, 18.3f, -34.8953f},
        {-153.2566f, 18.3f, 21.6330f}
    }};
    std::array<glm::vec3, 4> wheelCenters{{
        {1.5f, 0.36f, -0.95f},
        {1.5f, 0.36f, 0.95f},
        {-1.5f, 0.36f, -0.95f},
        {-1.5f, 0.36f, 0.95f}
    }};
    std::vector<glm::vec3> waypoints;
};

struct SceneAsset {
    std::string name;
    gps::Model3D model;
    glm::mat4 transform{1.0f};
};

gps::Window myWindow;
gps::Shader sceneShader;
gps::Shader depthShader;
gps::Shader rainShader;
gps::Shader skyboxShader;
gps::Camera myCamera(glm::vec3(26.0f, 4.0f, 46.0f), glm::vec3(0.0f, 1.0f, 40.0f), glm::vec3(0.0f, 1.0f, 0.0f));

glm::mat4 view;
glm::mat4 projection;

GLint modelLoc;
GLint viewLoc;
GLint projectionLoc;
GLint normalMatrixLoc;
GLint viewPosLoc;
GLint flatShadingLoc;
GLint accentColorLoc;
GLint accentMixLoc;
GLint ambientColorLoc;
GLint dirLightDirectionLoc;
GLint dirLightColorLoc;
GLint pointLightCountLoc;
GLint pointLightPosLoc;
GLint pointLightColorLoc;
GLint spotLightPosLoc;
GLint spotLightDirectionLoc;
GLint spotLightColorLoc;
GLint spotInnerCosLoc;
GLint spotOuterCosLoc;
GLint lightSpaceMatrixLoc;
GLint fogColorLoc;
GLint fogDensityLoc;
GLint fogEnabledLoc;
GLint depthModelLoc;
GLint depthLightSpaceLoc;
GLint rainViewProjectionLoc;
GLint skyboxProjectionLoc;
GLint skyboxViewLoc;
GLint skyboxSamplerLoc;

const int CAR_COUNT = 4;

struct CarState {
    glm::mat4 pose{1.0f};
    glm::mat4 bodyTransform{1.0f};
    std::array<glm::mat4, 4> wheelTransforms{};
    glm::vec3 accentColor{1.0f};
    float accentMix = 0.0f;
    float laneOffset = 0.0f;
    float speed = CAR_SPEED;
    float cruiseSpeed = CAR_SPEED;
    float distance = 0.0f;
    float yaw = 0.0f;
    float wheelSpin = 0.0f;
    float steerAngle = 0.0f;
    float roll = 0.0f;
    float scale = 1.0f;
    bool manual = false;
};

struct CarSetup {
    float speed;
    float laneOffset;
    float gridOffset;
    glm::vec3 accentColor;
};

const std::array<CarSetup, CAR_COUNT> CAR_SETUPS{{
    {26.0f, -1.4f,   0.0f, {1.0f, 1.0f, 1.0f}},
    {25.4f,  1.4f, -14.0f, {0.15f, 0.45f, 1.0f}},
    {26.7f, -3.8f, -28.0f, {1.0f, 0.8f, 0.2f}},
    {26.3f,  3.8f, -42.0f, {0.2f, 0.9f, 0.45f}}
}};

SceneCoords coords;
std::vector<std::unique_ptr<SceneAsset>> sceneAssets;
std::unique_ptr<SceneAsset> carBody;
std::array<std::unique_ptr<SceneAsset>, 4> carWheels;
std::array<CarState, CAR_COUNT> cars;
int focusedCar = 0;
bool carsMoving = true;
std::vector<Aabb> colliders;
std::vector<glm::vec3> splinePoints;
std::vector<glm::vec3> splineTangents;
std::vector<float> splineArc;
float lapLength = 0.0f;

const glm::vec3 moonDirection = glm::normalize(glm::vec3(0.25f, -0.9f, 0.2f));
const glm::vec3 moonColor(0.55f, 0.62f, 0.85f);
const glm::vec3 fogColor(0.004f, 0.008f, 0.02f);
float fogDensity = 0.0022f;
bool fogEnabled = true;
glm::mat4 lightSpaceMatrix(1.0f);
GLuint shadowFBO = 0;
GLuint shadowMap = 0;

GLuint rainVAO = 0;
GLuint rainVBO = 0;
GLuint skyboxVAO = 0;
GLuint skyboxVBO = 0;
GLuint skyboxTexture = 0;
std::vector<glm::vec3> rainDrops;
std::vector<float> rainSpeeds;
std::vector<float> rainVertices;
std::mt19937 rng(2024);
std::uniform_real_distribution<float> random01(0.0f, 1.0f);

GLboolean pressedKeys[1024];
float lastFrameTime = 0.0f;
float deltaTime = 0.0f;
float cameraSpeed = 28.0f;
float mouseSensitivity = 0.12f;
bool firstMouse = true;
double lastMouseX = 0.0;
double lastMouseY = 0.0;
double ignoreMouseUntil = 0.0;
bool cursorDisabled = true;
bool cameraTour = false;
bool chaseCamera = false;
bool driveMode = false;
glm::vec3 chasePosition(0.0f);
glm::vec3 chaseTarget(0.0f);
float tourTime = 0.0f;
bool floodlightsEnabled = true;
bool spotlightsEnabled = true;
bool flatShading = false;
GLenum polygonMode = GL_FILL;

void enterDriveMode();
void exitDriveMode();
void resetFocusedCar();
void snapChaseCamera();

GLenum glCheckError_(const char *file, int line)
{
    GLenum errorCode;
    while ((errorCode = glGetError()) != GL_NO_ERROR) {
        std::string error;
        switch (errorCode) {
            case GL_INVALID_ENUM: error = "INVALID_ENUM"; break;
            case GL_INVALID_VALUE: error = "INVALID_VALUE"; break;
            case GL_INVALID_OPERATION: error = "INVALID_OPERATION"; break;
            case GL_OUT_OF_MEMORY: error = "OUT_OF_MEMORY"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: error = "INVALID_FRAMEBUFFER_OPERATION"; break;
        }
        std::cout << error << " | " << file << " (" << line << ")" << std::endl;
    }
    return errorCode;
}
#define glCheckError() glCheckError_(__FILE__, __LINE__)

void updateProjection(int width, int height) {
    projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 2000.0f);
    sceneShader.useShaderProgram();
    glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
}

void windowResizeCallback(GLFWwindow* window, int width, int height) {
    if (width == 0 || height == 0) return;
    myWindow.setWindowDimensions({width, height});
    glViewport(0, 0, width, height);
    updateProjection(width, height);
}

void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    if (!cursorDisabled || !glfwGetWindowAttrib(window, GLFW_FOCUSED)) {
        firstMouse = true;
        return;
    }

    if (firstMouse) {
        lastMouseX = xpos;
        lastMouseY = ypos;
        firstMouse = false;
        return;
    }

    double dx = xpos - lastMouseX;
    double dy = ypos - lastMouseY;
    lastMouseX = xpos;
    lastMouseY = ypos;

    if (glfwGetTime() < ignoreMouseUntil) return;
    if (std::abs(dx) > 120.0 || std::abs(dy) > 120.0) return;

    if (cameraTour) {
        cameraTour = false;
        return;
    }

    myCamera.rotate((float)(-dy * mouseSensitivity), (float)(dx * mouseSensitivity));
}

void keyboardCallback(GLFWwindow* window, int key, int scancode, int action, int mode) {
    if (key >= 0 && key < 1024) {
        pressedKeys[key] = action != GLFW_RELEASE;
    }

    if (action != GLFW_PRESS) return;

    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(window, GL_TRUE);
        return;
    }

    switch (key) {
        case GLFW_KEY_TAB:
            cursorDisabled = !cursorDisabled;
            glfwSetInputMode(window, GLFW_CURSOR, cursorDisabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            firstMouse = true;
            ignoreMouseUntil = glfwGetTime() + 0.25;
            break;
        case GLFW_KEY_SPACE:
            carsMoving = !carsMoving;
            std::cout << "Cars: " << (carsMoving ? "moving" : "paused") << std::endl;
            break;
        case GLFW_KEY_C:
            if (driveMode) {
                exitDriveMode();
                std::cout << "Drive mode: off" << std::endl;
            } else {
                enterDriveMode();
                std::cout << "Drive mode: on (car " << focusedCar + 1 << ")" << std::endl;
            }
            break;
        case GLFW_KEY_V:
            if (driveMode) {
                exitDriveMode();
                std::cout << "Chase camera: off | Drive mode: off" << std::endl;
            } else {
                chaseCamera = !chaseCamera;
                if (chaseCamera) {
                    cameraTour = false;
                    snapChaseCamera();
                }
                std::cout << "Chase camera: " << (chaseCamera ? "on" : "off") << std::endl;
            }
            break;
        case GLFW_KEY_B:
            if (driveMode) {
                std::cout << "Exit drive mode to switch car" << std::endl;
            } else {
                focusedCar = (focusedCar + 1) % CAR_COUNT;
                if (chaseCamera) snapChaseCamera();
                std::cout << "Spotlight car: " << focusedCar + 1 << "/" << CAR_COUNT << std::endl;
            }
            break;
        case GLFW_KEY_R:
            resetFocusedCar();
            std::cout << "Car reset" << std::endl;
            break;
        case GLFW_KEY_P:
            cameraTour = !cameraTour;
            tourTime = 0.0f;
            if (cameraTour) {
                chaseCamera = false;
                if (driveMode) exitDriveMode();
            }
            std::cout << "Camera tour: " << (cameraTour ? "on" : "off") << std::endl;
            break;
        case GLFW_KEY_1:
            polygonMode = GL_FILL;
            glPolygonMode(GL_FRONT_AND_BACK, polygonMode);
            break;
        case GLFW_KEY_2:
            polygonMode = GL_LINE;
            glPolygonMode(GL_FRONT_AND_BACK, polygonMode);
            break;
        case GLFW_KEY_3:
            polygonMode = GL_POINT;
            glPolygonMode(GL_FRONT_AND_BACK, polygonMode);
            glPointSize(3.0f);
            break;
        case GLFW_KEY_4:
            flatShading = !flatShading;
            std::cout << "Shading: " << (flatShading ? "flat" : "smooth") << std::endl;
            break;
        case GLFW_KEY_K:
            floodlightsEnabled = !floodlightsEnabled;
            std::cout << "Floodlights: " << (floodlightsEnabled ? "on" : "off") << std::endl;
            break;
        case GLFW_KEY_L:
            spotlightsEnabled = !spotlightsEnabled;
            std::cout << "Car spotlight: " << (spotlightsEnabled ? "on" : "off") << std::endl;
            break;
        case GLFW_KEY_Z:
            cars[focusedCar].scale = std::max(0.5f, cars[focusedCar].scale - 0.1f);
            std::cout << "Car scale: " << cars[focusedCar].scale << std::endl;
            break;
        case GLFW_KEY_X:
            cars[focusedCar].scale = std::min(2.0f, cars[focusedCar].scale + 0.1f);
            std::cout << "Car scale: " << cars[focusedCar].scale << std::endl;
            break;
        case GLFW_KEY_F:
            fogEnabled = !fogEnabled;
            std::cout << "Fog: " << (fogEnabled ? "on" : "off") << std::endl;
            break;
        case GLFW_KEY_EQUAL:
            fogDensity = std::min(fogDensity + 0.0005f, 0.01f);
            std::cout << "Fog density: " << fogDensity << std::endl;
            break;
        case GLFW_KEY_MINUS:
            fogDensity = std::max(fogDensity - 0.0005f, 0.0f);
            std::cout << "Fog density: " << fogDensity << std::endl;
            break;
        default:
            break;
    }
}

bool parseVec3(const std::string& text, const std::string& key, glm::vec3& out) {
    std::regex pattern("\"" + key + "\"\\s*:\\s*\\[\\s*([-+0-9.eE]+)\\s*,\\s*([-+0-9.eE]+)\\s*,\\s*([-+0-9.eE]+)\\s*\\]");
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) return false;
    out = glm::vec3(std::stof(match[1]), std::stof(match[2]), std::stof(match[3]));
    return true;
}

// shortcut: regex parsing for the fixed scene_coords.json layout, swap for a JSON library if the schema grows
void loadSceneCoords() {
    std::ifstream file("models/f1_circuit/scene_coords.json");
    if (!file) {
        std::cout << "scene_coords.json not found, using built-in coordinates" << std::endl;
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();

    parseVec3(text, "car_start_gl", coords.carStart);
    parseVec3(text, "car_forward_gl", coords.carForward);
    parseVec3(text, "wheel_axle_gl", coords.wheelAxle);

    const char* wheelNames[4] = {"wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr"};
    for (int i = 0; i < 4; ++i) {
        parseVec3(text, wheelNames[i], coords.wheelCenters[i]);
    }

    size_t begin = text.find("\"light_heads_gl\"");
    size_t end = text.find("\"wheel_centers_gl\"", begin);
    if (begin != std::string::npos) {
        std::string section = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        std::regex triplet("\\[\\s*([-+0-9.eE]+)\\s*,\\s*([-+0-9.eE]+)\\s*,\\s*([-+0-9.eE]+)\\s*\\]");
        int index = 0;
        for (std::sregex_iterator it(section.begin(), section.end(), triplet), last; it != last && index < MAX_POINT_LIGHTS; ++it, ++index) {
            coords.lightHeads[index] = glm::vec3(std::stof((*it)[1]), std::stof((*it)[2]), std::stof((*it)[3]));
        }
    }

    begin = text.find("\"waypoints_gl\"");
    end = text.find("\"aabb\"", begin);
    if (begin != std::string::npos) {
        std::string section = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        std::regex triplet("\\[\\s*([-+0-9.eE]+)\\s*,\\s*([-+0-9.eE]+)\\s*,\\s*([-+0-9.eE]+)\\s*\\]");
        for (std::sregex_iterator it(section.begin(), section.end(), triplet), last; it != last; ++it) {
            coords.waypoints.emplace_back(std::stof((*it)[1]), std::stof((*it)[2]), std::stof((*it)[3]));
        }
    }

    std::regex triplet("\\[\\s*([-+0-9.eE]+)\\s*,\\s*([-+0-9.eE]+)\\s*,\\s*([-+0-9.eE]+)\\s*\\]");
    size_t barrierBegin = text.find("\"barrier_segments\"");
    size_t barrierEnd = text.find("\"pit_building\"", barrierBegin);
    if (barrierBegin != std::string::npos && barrierEnd != std::string::npos) {
        std::string section = text.substr(barrierBegin, barrierEnd - barrierBegin);
        std::sregex_iterator it(section.begin(), section.end(), triplet), last;
        while (it != last) {
            Aabb box;
            box.min = glm::vec3(std::stof((*it)[1]), std::stof((*it)[2]), std::stof((*it)[3]));
            ++it;
            if (it == last) break;
            box.max = glm::vec3(std::stof((*it)[1]), std::stof((*it)[2]), std::stof((*it)[3]));
            ++it;
            colliders.push_back(box);
        }
    }

    const char* colliderNames[2] = {"pit_building", "grandstand"};
    for (const char* name : colliderNames) {
        size_t colliderBegin = text.find("\"" + std::string(name) + "\"");
        if (colliderBegin == std::string::npos) continue;
        size_t colliderEnd = text.find('}', text.find("\"max\"", colliderBegin));
        if (colliderEnd == std::string::npos) continue;
        std::string section = text.substr(colliderBegin, colliderEnd - colliderBegin + 1);
        std::sregex_iterator it(section.begin(), section.end(), triplet), last;
        if (it == last) continue;
        Aabb box;
        box.min = glm::vec3(std::stof((*it)[1]), std::stof((*it)[2]), std::stof((*it)[3]));
        ++it;
        if (it == last) continue;
        box.max = glm::vec3(std::stof((*it)[1]), std::stof((*it)[2]), std::stof((*it)[3]));
        colliders.push_back(box);
    }
}

glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t
                 + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

glm::vec3 catmullRomTangent(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
    float t2 = t * t;
    return 0.5f * ((-p0 + p2)
                 + 2.0f * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t
                 + 3.0f * (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t2);
}

void buildPath() {
    splinePoints.clear();
    splineTangents.clear();
    splineArc.clear();
    lapLength = 0.0f;

    size_t count = coords.waypoints.size();
    if (count < 2) return;

    const int samplesPerSegment = 24;
    for (size_t i = 0; i < count; ++i) {
        glm::vec3 p0 = coords.waypoints[(i + count - 1) % count];
        glm::vec3 p1 = coords.waypoints[i];
        glm::vec3 p2 = coords.waypoints[(i + 1) % count];
        glm::vec3 p3 = coords.waypoints[(i + 2) % count];

        for (int step = 0; step < samplesPerSegment; ++step) {
            float t = (float)step / samplesPerSegment;
            splinePoints.push_back(catmullRom(p0, p1, p2, p3, t));
            splineTangents.push_back(glm::normalize(catmullRomTangent(p0, p1, p2, p3, t)));
        }
    }

    splineArc.push_back(0.0f);
    for (size_t i = 1; i <= splinePoints.size(); ++i) {
        glm::vec3 delta = splinePoints[i % splinePoints.size()] - splinePoints[i - 1];
        splineArc.push_back(splineArc.back() + glm::length(delta));
    }
    lapLength = splineArc.back();
}

glm::vec3 samplePath(float distance, glm::vec3& tangent) {
    float d = std::fmod(distance, lapLength);
    if (d < 0.0f) d += lapLength;

    auto it = std::upper_bound(splineArc.begin(), splineArc.end(), d);
    size_t index = (size_t)std::max<long>(1, it - splineArc.begin()) - 1;
    index = std::min(index, splinePoints.size() - 1);

    float span = splineArc[index + 1] - splineArc[index];
    float t = span > 0.0f ? (d - splineArc[index]) / span : 0.0f;

    glm::vec3 a = splinePoints[index];
    glm::vec3 b = splinePoints[(index + 1) % splinePoints.size()];
    tangent = glm::normalize(glm::mix(splineTangents[index], splineTangents[(index + 1) % splinePoints.size()], t));
    return glm::mix(a, b, t);
}

float wrapAngle(float angle) {
    while (angle > 3.14159265f) angle -= 2.0f * 3.14159265f;
    while (angle < -3.14159265f) angle += 2.0f * 3.14159265f;
    return angle;
}

glm::vec3 lanePoint(float distance, float laneOffset) {
    glm::vec3 tangent;
    glm::vec3 position = samplePath(distance, tangent);
    float yaw = std::atan2(-tangent.z, tangent.x);
    return position + glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw)) * laneOffset;
}

glm::vec3 laneForward(float distance, float laneOffset) {
    glm::vec3 a = lanePoint(distance - 0.5f, laneOffset);
    glm::vec3 b = lanePoint(distance + 0.5f, laneOffset);
    return glm::normalize(glm::vec3(b.x - a.x, 0.0f, b.z - a.z));
}

void updateCarTransforms(CarState& state) {
    glm::mat4 carScale = glm::scale(glm::mat4(1.0f), glm::vec3(state.scale));
    state.bodyTransform = state.pose * carScale;

    for (int i = 0; i < 4; ++i) {
        glm::mat4 steer = i < 2
            ? glm::rotate(glm::mat4(1.0f), state.steerAngle, glm::vec3(0.0f, 1.0f, 0.0f))
            : glm::mat4(1.0f);
        state.wheelTransforms[i] = state.pose * carScale
            * glm::translate(glm::mat4(1.0f), coords.wheelCenters[i])
            * steer
            * glm::rotate(glm::mat4(1.0f), state.wheelSpin, coords.wheelAxle);
    }
}

void updateCars(float dt) {
    if (splinePoints.size() < 2) return;

    float wheelbase = std::max(0.1f, std::abs(coords.wheelCenters[0].x - coords.wheelCenters[2].x));

    for (CarState& state : cars) {
        if (state.manual) continue;

        if (carsMoving) {
            float travelled = state.speed * dt;
            state.distance = std::fmod(state.distance + travelled, lapLength);
            state.wheelSpin = std::fmod(state.wheelSpin - travelled / WHEEL_RADIUS, 2.0f * 3.14159265f);
        }

        glm::vec3 position = lanePoint(state.distance, state.laneOffset);
        glm::vec3 forward = laneForward(state.distance, state.laneOffset);
        float heading = std::atan2(-forward.z, forward.x);

        const float lookahead = 3.0f * state.scale;
        glm::vec3 aheadForward = laneForward(state.distance + lookahead, state.laneOffset);
        float yawDelta = wrapAngle(std::atan2(-aheadForward.z, aheadForward.x) - heading);
        float curvature = yawDelta / lookahead;

        float steerTarget = glm::clamp(std::atan(wheelbase * state.scale * curvature), -0.45f, 0.45f);
        float rollTarget = glm::clamp(curvature * state.speed * state.speed * 0.00025f, -0.05f, 0.05f);

        float responseDt = carsMoving ? dt : 0.0f;
        state.steerAngle += (steerTarget - state.steerAngle) * (1.0f - std::exp(-10.0f * responseDt));
        state.roll += (rollTarget - state.roll) * (1.0f - std::exp(-6.0f * responseDt));

        state.pose = glm::translate(glm::mat4(1.0f), position)
                   * glm::rotate(glm::mat4(1.0f), heading, glm::vec3(0.0f, 1.0f, 0.0f))
                   * glm::rotate(glm::mat4(1.0f), state.roll, glm::vec3(1.0f, 0.0f, 0.0f));
        updateCarTransforms(state);
    }
}

float nearestSplineDistance(const glm::vec3& position) {
    float bestDistance = 0.0f;
    float bestSquared = std::numeric_limits<float>::max();
    for (size_t i = 0; i < splinePoints.size(); ++i) {
        float dx = splinePoints[i].x - position.x;
        float dz = splinePoints[i].z - position.z;
        float squared = dx * dx + dz * dz;
        if (squared < bestSquared) {
            bestSquared = squared;
            bestDistance = splineArc[i];
        }
    }
    return bestDistance;
}

bool pointBlocked(const glm::vec3& position, float radius) {
    if (position.x < SCENE_MIN.x + radius || position.x > SCENE_MAX.x - radius ||
        position.z < SCENE_MIN.z + radius || position.z > SCENE_MAX.z - radius) {
        return true;
    }

    for (const Aabb& box : colliders) {
        if (position.x > box.min.x - radius && position.x < box.max.x + radius &&
            position.z > box.min.z - radius && position.z < box.max.z + radius) {
            return true;
        }
    }
    return false;
}

bool carBlocked(const glm::vec3& position, const CarState* self) {
    glm::vec3 forward(1.0f, 0.0f, 0.0f);
    if (self) forward = glm::vec3(std::cos(self->yaw), 0.0f, -std::sin(self->yaw));

    for (float offset : {-1.3f, 0.0f, 1.3f}) {
        if (pointBlocked(position + forward * offset, CAR_COLLISION_RADIUS)) {
            return true;
        }
    }

    if (self) {
        glm::vec3 right(std::sin(self->yaw), 0.0f, std::cos(self->yaw));
        for (const CarState& other : cars) {
            if (&other == self) continue;
            glm::vec3 delta = glm::vec3(other.pose[3]) - position;
            if (std::abs(glm::dot(delta, forward)) < 4.2f && std::abs(glm::dot(delta, right)) < 1.5f) {
                return true;
            }
        }
    }
    return false;
}

void updateManualCar(CarState& state, float dt) {
    if (!carsMoving) return;

    float throttle = (pressedKeys[GLFW_KEY_W] ? 1.0f : 0.0f) - (pressedKeys[GLFW_KEY_S] ? 1.0f : 0.0f);
    float steerInput = (pressedKeys[GLFW_KEY_A] ? 1.0f : 0.0f) - (pressedKeys[GLFW_KEY_D] ? 1.0f : 0.0f);

    if (throttle > 0.0f) {
        state.speed += DRIVE_ACCEL * dt;
    } else if (throttle < 0.0f) {
        state.speed -= DRIVE_BRAKE * dt;
    } else {
        float drag = DRIVE_DRAG * dt;
        state.speed = std::abs(state.speed) <= drag ? 0.0f : state.speed - std::copysign(drag, state.speed);
    }
    state.speed = std::clamp(state.speed, -DRIVE_REVERSE_SPEED, DRIVE_MAX_SPEED);

    float maxSteer = 0.5f / (1.0f + 0.05f * std::abs(state.speed));
    state.steerAngle += (steerInput * maxSteer - state.steerAngle) * (1.0f - std::exp(-8.0f * dt));

    float wheelbase = std::max(0.1f, std::abs(coords.wheelCenters[0].x - coords.wheelCenters[2].x));
    float yawRate = state.speed / wheelbase * std::tan(state.steerAngle);
    state.yaw += yawRate * dt;

    float rollTarget = glm::clamp(yawRate * state.speed * 0.0006f, -0.05f, 0.05f);
    state.roll += (rollTarget - state.roll) * (1.0f - std::exp(-6.0f * dt));

    glm::vec3 forward(std::cos(state.yaw), 0.0f, -std::sin(state.yaw));
    glm::vec3 position(state.pose[3]);
    glm::vec3 target = position + forward * (state.speed * dt);

    if (carBlocked(target, &state)) {
        glm::vec3 slideX(target.x, position.y, position.z);
        glm::vec3 slideZ(position.x, position.y, target.z);
        if (!carBlocked(slideX, &state)) {
            target = slideX;
        } else if (!carBlocked(slideZ, &state)) {
            target = slideZ;
        } else {
            target = position;
            state.speed = 0.0f;
        }
    }

    float travelled = glm::dot(target - position, forward);
    state.wheelSpin = std::fmod(state.wheelSpin - travelled / WHEEL_RADIUS, 2.0f * 3.14159265f);

    state.pose = glm::translate(glm::mat4(1.0f), target)
               * glm::rotate(glm::mat4(1.0f), state.yaw, glm::vec3(0.0f, 1.0f, 0.0f))
               * glm::rotate(glm::mat4(1.0f), state.roll, glm::vec3(1.0f, 0.0f, 0.0f));
    updateCarTransforms(state);
}

void enterDriveMode() {
    driveMode = true;
    chaseCamera = true;
    cameraTour = false;

    CarState& state = cars[focusedCar];
    state.manual = true;
    state.speed = state.cruiseSpeed;
    state.yaw = std::atan2(-state.pose[0][2], state.pose[0][0]);
    state.steerAngle = 0.0f;
    state.roll = 0.0f;
    snapChaseCamera();
}

void exitDriveMode() {
    CarState& state = cars[focusedCar];
    state.manual = false;
    state.speed = state.cruiseSpeed;
    state.distance = nearestSplineDistance(state.pose[3]);
    state.steerAngle = 0.0f;
    state.roll = 0.0f;
    driveMode = false;
    chaseCamera = false;
}

void resetFocusedCar() {
    CarState& state = cars[focusedCar];
    float distance = nearestSplineDistance(state.pose[3]);
    float lane = state.manual ? 0.0f : state.laneOffset;

    glm::vec3 position = lanePoint(distance, lane);
    glm::vec3 forward = laneForward(distance, lane);
    state.distance = distance;
    state.yaw = std::atan2(-forward.z, forward.x);
    state.speed = state.manual ? 0.0f : state.cruiseSpeed;
    state.steerAngle = 0.0f;
    state.roll = 0.0f;
    state.pose = glm::translate(glm::mat4(1.0f), position)
               * glm::rotate(glm::mat4(1.0f), state.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    updateCarTransforms(state);
    if (chaseCamera) snapChaseCamera();
}

std::unique_ptr<SceneAsset> loadModel(const std::string& name) {
    auto asset = std::make_unique<SceneAsset>();
    asset->name = name;
    asset->model.LoadModel("models/f1_circuit/" + name + ".obj");
    return asset;
}

void addAsset(const std::string& name) {
    sceneAssets.push_back(loadModel(name));
}

void initCars() {
    for (int i = 0; i < CAR_COUNT; ++i) {
        CarState& state = cars[i];
        state.speed = CAR_SETUPS[i].speed;
        state.cruiseSpeed = CAR_SETUPS[i].speed;
        state.laneOffset = CAR_SETUPS[i].laneOffset;
        state.distance = CAR_SETUPS[i].gridOffset;
        state.accentColor = CAR_SETUPS[i].accentColor;
        state.accentMix = i == 0 ? 0.0f : 0.65f;
    }
}

void initModels() {
    loadSceneCoords();
    buildPath();

    addAsset("ground");
    addAsset("track");
    addAsset("curbs");
    addAsset("barriers");
    addAsset("pit_building");
    addAsset("grandstand");
    addAsset("start_bridge");
    addAsset("light_poles");

    carBody = loadModel("car_body");
    const char* wheelNames[4] = {"wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr"};
    for (int i = 0; i < 4; ++i) {
        carWheels[i] = loadModel(wheelNames[i]);
    }

    initCars();
    updateCars(0.0f);
}

void initShadowResources() {
    glGenFramebuffers(1, &shadowFBO);
    glGenTextures(1, &shadowMap);

    glBindTexture(GL_TEXTURE_2D, shadowMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_SIZE, SHADOW_SIZE, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float borderColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cout << "Shadow framebuffer incomplete" << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const glm::vec3 circuitMin(-230.0f, 0.0f, -150.0f);
    const glm::vec3 circuitMax(240.0f, 40.0f, 90.0f);
    const glm::vec3 center = (circuitMin + circuitMax) * 0.5f;
    glm::mat4 lightView = glm::lookAt(center - moonDirection * 300.0f, center, glm::vec3(0.0f, 1.0f, 0.0f));

    glm::vec3 lightMin(std::numeric_limits<float>::max());
    glm::vec3 lightMax(-std::numeric_limits<float>::max());
    for (int i = 0; i < 8; ++i) {
        glm::vec3 corner((i & 1) ? circuitMax.x : circuitMin.x,
                         (i & 2) ? circuitMax.y : circuitMin.y,
                         (i & 4) ? circuitMax.z : circuitMin.z);
        glm::vec3 lightCorner = glm::vec3(lightView * glm::vec4(corner, 1.0f));
        lightMin = glm::min(lightMin, lightCorner);
        lightMax = glm::max(lightMax, lightCorner);
    }

    lightSpaceMatrix = glm::ortho(lightMin.x - 2.0f, lightMax.x + 2.0f,
                                  lightMin.y - 2.0f, lightMax.y + 2.0f,
                                  -lightMax.z - 2.0f, -lightMin.z + 2.0f) * lightView;

    depthShader.useShaderProgram();
    depthLightSpaceLoc = glGetUniformLocation(depthShader.shaderProgram, "lightSpaceMatrix");
    depthModelLoc = glGetUniformLocation(depthShader.shaderProgram, "model");
}

void initRain() {
    glm::vec3 cam = myCamera.getPosition();
    rainDrops.resize(RAIN_COUNT);
    rainSpeeds.resize(RAIN_COUNT);
    rainVertices.resize(RAIN_COUNT * 6);

    for (int i = 0; i < RAIN_COUNT; ++i) {
        rainDrops[i] = glm::vec3(cam.x + (random01(rng) * 2.0f - 1.0f) * 55.0f,
                                 5.0f + random01(rng) * 55.0f,
                                 cam.z + (random01(rng) * 2.0f - 1.0f) * 55.0f);
        rainSpeeds[i] = 45.0f + random01(rng) * 20.0f;
    }

    glGenVertexArrays(1, &rainVAO);
    glGenBuffers(1, &rainVBO);
    glBindVertexArray(rainVAO);
    glBindBuffer(GL_ARRAY_BUFFER, rainVBO);
    glBufferData(GL_ARRAY_BUFFER, rainVertices.size() * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

float linearToSrgb(float value) {
    return value <= 0.0031308f ? 12.92f * value : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

unsigned char srgbByte(float linear) {
    return (unsigned char)std::clamp(linearToSrgb(linear) * 255.0f + 0.5f, 0.0f, 255.0f);
}

glm::vec3 cubeFaceDirection(int face, float u, float v) {
    float s = 2.0f * u - 1.0f;
    float t = 2.0f * v - 1.0f;
    switch (face) {
        case 0: return {1.0f, -t, -s};
        case 1: return {-1.0f, -t, s};
        case 2: return {s, 1.0f, t};
        case 3: return {s, -1.0f, -t};
        case 4: return {s, -t, 1.0f};
        default: return {-s, -t, -1.0f};
    }
}

void initSkybox() {
    const int size = 512;
    const glm::vec3 zenith(0.0008f, 0.0018f, 0.0075f);
    const glm::vec3 moonDir = -moonDirection;
    std::vector<unsigned char> pixels(size * size * 3);

    glGenTextures(1, &skyboxTexture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxTexture);

    for (int face = 0; face < 6; ++face) {
        std::mt19937 starRng(1000 + face);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);

        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                glm::vec3 dir = glm::normalize(cubeFaceDirection(face, (x + 0.5f) / size, (y + 0.5f) / size));
                glm::vec3 color = dir.y > 0.0f
                    ? glm::mix(fogColor, zenith, std::pow(dir.y, 0.45f))
                    : fogColor;
                float moon = glm::dot(dir, moonDir);
                color += glm::vec3(0.10f, 0.11f, 0.14f) * std::pow(std::max(moon, 0.0f), 180.0f);
                if (moon > 0.9996f) color = glm::vec3(1.0f, 1.02f, 1.08f);

                size_t index = (y * size + x) * 3;
                pixels[index + 0] = srgbByte(color.r);
                pixels[index + 1] = srgbByte(color.g);
                pixels[index + 2] = srgbByte(color.b);
            }
        }

        for (int star = 0; star < 260; ++star) {
            float su = unit(starRng);
            float sv = unit(starRng);
            glm::vec3 dir = glm::normalize(cubeFaceDirection(face, su, sv));
            if (dir.y < 0.04f || glm::dot(dir, moonDir) > 0.9995f) continue;

            int sx = std::min(size - 1, (int)(su * size));
            int sy = std::min(size - 1, (int)(sv * size));
            float brightness = 0.35f + 0.65f * unit(starRng);
            auto plot = [&](int px, int py, float scale) {
                if (px < 0 || px >= size || py < 0 || py >= size) return;
                size_t index = (py * size + px) * 3;
                unsigned char value = (unsigned char)(std::clamp(brightness * scale, 0.0f, 1.0f) * 255.0f);
                pixels[index + 0] = std::max(pixels[index + 0], (unsigned char)(value * 0.9f));
                pixels[index + 1] = std::max(pixels[index + 1], (unsigned char)(value * 0.95f));
                pixels[index + 2] = std::max(pixels[index + 2], value);
            };
            plot(sx, sy, 1.0f);
            plot(sx - 1, sy, 0.3f);
            plot(sx + 1, sy, 0.3f);
            plot(sx, sy - 1, 0.3f);
            plot(sx, sy + 1, 0.3f);
        }

        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_SRGB8, size, size, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    const float vertices[] = {
        -1, -1, -1,  1, -1, -1,  1,  1, -1,  1,  1, -1, -1,  1, -1, -1, -1, -1,
        -1, -1,  1,  1, -1,  1,  1,  1,  1,  1,  1,  1, -1,  1,  1, -1, -1,  1,
        -1,  1,  1,  1,  1,  1,  1,  1, -1,  1,  1, -1, -1,  1, -1, -1,  1,  1,
        -1, -1, -1,  1, -1, -1,  1, -1,  1,  1, -1,  1, -1, -1,  1, -1, -1, -1,
        -1, -1, -1, -1,  1, -1, -1,  1,  1, -1,  1,  1, -1, -1,  1, -1, -1, -1,
         1, -1, -1,  1,  1, -1,  1,  1,  1,  1,  1,  1,  1, -1,  1,  1, -1, -1
    };

    glGenVertexArrays(1, &skyboxVAO);
    glGenBuffers(1, &skyboxVBO);
    glBindVertexArray(skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    skyboxShader.useShaderProgram();
    skyboxProjectionLoc = glGetUniformLocation(skyboxShader.shaderProgram, "projection");
    skyboxViewLoc = glGetUniformLocation(skyboxShader.shaderProgram, "view");
    skyboxSamplerLoc = glGetUniformLocation(skyboxShader.shaderProgram, "skybox");
    glUniform1i(skyboxSamplerLoc, 9);
}

void renderSkybox() {
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    skyboxShader.useShaderProgram();
    glm::mat4 skyboxView = glm::mat4(glm::mat3(view));
    glUniformMatrix4fv(skyboxProjectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(skyboxViewLoc, 1, GL_FALSE, glm::value_ptr(skyboxView));

    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxTexture);
    glBindVertexArray(skyboxVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);

    glPolygonMode(GL_FRONT_AND_BACK, polygonMode);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
}

void updateRain(float dt) {
    glm::vec3 cam = myCamera.getPosition();
    float time = (float)glfwGetTime();
    glm::vec3 wind(std::sin(time * 0.7f) * 4.0f, 0.0f, std::cos(time * 0.5f) * 2.5f);

    for (int i = 0; i < RAIN_COUNT; ++i) {
        glm::vec3& drop = rainDrops[i];
        drop.y -= rainSpeeds[i] * dt;
        drop.x += wind.x * dt * 0.5f;
        drop.z += wind.z * dt * 0.5f;
        bool outOfRange = std::abs(drop.x - cam.x) > 55.0f || std::abs(drop.z - cam.z) > 55.0f;
        if (drop.y <= 0.0f || outOfRange) {
            drop = glm::vec3(cam.x + (random01(rng) * 2.0f - 1.0f) * 55.0f,
                             20.0f + random01(rng) * 45.0f,
                             cam.z + (random01(rng) * 2.0f - 1.0f) * 55.0f);
        }

        float* v = &rainVertices[i * 6];
        v[0] = drop.x;        v[1] = drop.y;        v[2] = drop.z;
        v[3] = drop.x - 0.3f + wind.x * 0.2f;
        v[4] = drop.y - 2.4f;
        v[5] = drop.z - 0.1f + wind.z * 0.2f;
    }

    glBindBuffer(GL_ARRAY_BUFFER, rainVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, rainVertices.size() * sizeof(float), rainVertices.data());
}

void initOpenGLWindow() {
    myWindow.Create(1280, 800, "F1 Night Circuit");
}

void setWindowCallbacks() {
    glfwSetWindowSizeCallback(myWindow.getWindow(), windowResizeCallback);
    glfwSetKeyCallback(myWindow.getWindow(), keyboardCallback);
    glfwSetCursorPosCallback(myWindow.getWindow(), mouseCallback);
}

void initOpenGLState() {
    glClearColor(fogColor.r, fogColor.g, fogColor.b, 1.0f);
    glViewport(0, 0, myWindow.getWindowDimensions().width, myWindow.getWindowDimensions().height);
    glEnable(GL_FRAMEBUFFER_SRGB);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_MULTISAMPLE);
}

void initShaders() {
    sceneShader.loadShader("shaders/scene.vert", "shaders/scene.frag");
    depthShader.loadShader("shaders/depth.vert", "shaders/depth.frag");
    rainShader.loadShader("shaders/rain.vert", "shaders/rain.frag");
    skyboxShader.loadShader("shaders/skybox.vert", "shaders/skybox.frag");
}

void initUniforms() {
    sceneShader.useShaderProgram();
    GLuint program = sceneShader.shaderProgram;

    modelLoc = glGetUniformLocation(program, "model");
    viewLoc = glGetUniformLocation(program, "view");
    projectionLoc = glGetUniformLocation(program, "projection");
    normalMatrixLoc = glGetUniformLocation(program, "normalMatrix");
    viewPosLoc = glGetUniformLocation(program, "viewPos");
    flatShadingLoc = glGetUniformLocation(program, "flatShading");
    accentColorLoc = glGetUniformLocation(program, "accentColor");
    accentMixLoc = glGetUniformLocation(program, "accentMix");
    ambientColorLoc = glGetUniformLocation(program, "ambientColor");
    dirLightDirectionLoc = glGetUniformLocation(program, "dirLightDirection");
    dirLightColorLoc = glGetUniformLocation(program, "dirLightColor");
    pointLightCountLoc = glGetUniformLocation(program, "pointLightCount");
    pointLightPosLoc = glGetUniformLocation(program, "pointLightPos[0]");
    pointLightColorLoc = glGetUniformLocation(program, "pointLightColor[0]");
    spotLightPosLoc = glGetUniformLocation(program, "spotLightPos");
    spotLightDirectionLoc = glGetUniformLocation(program, "spotLightDirection");
    spotLightColorLoc = glGetUniformLocation(program, "spotLightColor");
    spotInnerCosLoc = glGetUniformLocation(program, "spotInnerCos");
    spotOuterCosLoc = glGetUniformLocation(program, "spotOuterCos");
    lightSpaceMatrixLoc = glGetUniformLocation(program, "lightSpaceMatrix");
    fogColorLoc = glGetUniformLocation(program, "fogColor");
    fogDensityLoc = glGetUniformLocation(program, "fogDensity");
    fogEnabledLoc = glGetUniformLocation(program, "fogEnabled");

    glm::vec3 ambientColor(0.05f, 0.06f, 0.09f);

    glUniform3fv(ambientColorLoc, 1, glm::value_ptr(ambientColor));
    glUniform3fv(dirLightDirectionLoc, 1, glm::value_ptr(moonDirection));
    glUniform3fv(dirLightColorLoc, 1, glm::value_ptr(moonColor));
    glUniform1i(pointLightCountLoc, MAX_POINT_LIGHTS);
    glUniform1f(spotInnerCosLoc, std::cos(glm::radians(16.0f)));
    glUniform1f(spotOuterCosLoc, std::cos(glm::radians(28.0f)));
    glUniform1i(glGetUniformLocation(program, "shadowMap"), 8);
    glUniform3fv(fogColorLoc, 1, glm::value_ptr(fogColor));

    rainShader.useShaderProgram();
    rainViewProjectionLoc = glGetUniformLocation(rainShader.shaderProgram, "viewProjection");

    updateProjection(myWindow.getWindowDimensions().width, myWindow.getWindowDimensions().height);
}

bool cameraBlocked(const glm::vec3& position) {
    for (const Aabb& box : colliders) {
        if (position.x > box.min.x - CAMERA_RADIUS && position.x < box.max.x + CAMERA_RADIUS &&
            position.z > box.min.z - CAMERA_RADIUS && position.z < box.max.z + CAMERA_RADIUS) {
            return true;
        }
    }
    return false;
}

void moveCameraStep(gps::MOVE_DIRECTION direction, float distance) {
    glm::vec3 before = myCamera.getPosition();
    myCamera.move(direction, distance);

    glm::vec3 target = myCamera.getPosition();
    target.x = std::clamp(target.x, SCENE_MIN.x, SCENE_MAX.x);
    target.z = std::clamp(target.z, SCENE_MIN.z, SCENE_MAX.z);

    glm::vec3 resolved = before;
    if (!cameraBlocked(glm::vec3(target.x, before.y, before.z))) resolved.x = target.x;
    if (!cameraBlocked(glm::vec3(resolved.x, before.y, target.z))) resolved.z = target.z;
    myCamera.setPosition(resolved);
}

void processMovement(float dt) {
    if (driveMode) return;

    float speed = cameraSpeed * dt;
    if (pressedKeys[GLFW_KEY_LEFT_SHIFT] || pressedKeys[GLFW_KEY_RIGHT_SHIFT]) speed *= 3.0f;

    bool moved = false;
    if (pressedKeys[GLFW_KEY_W]) { moveCameraStep(gps::MOVE_FORWARD, speed); moved = true; }
    if (pressedKeys[GLFW_KEY_S]) { moveCameraStep(gps::MOVE_BACKWARD, speed); moved = true; }
    if (pressedKeys[GLFW_KEY_A]) { moveCameraStep(gps::MOVE_LEFT, speed); moved = true; }
    if (pressedKeys[GLFW_KEY_D]) { moveCameraStep(gps::MOVE_RIGHT, speed); moved = true; }

    if (moved && cameraTour) cameraTour = false;
}

void chaseDesired(const CarState& state, glm::vec3& position, glm::vec3& target) {
    glm::vec3 forward = glm::normalize(glm::vec3(state.pose[0][0], 0.0f, state.pose[0][2]));
    glm::vec3 carPosition(state.pose[3]);
    position = carPosition - forward * (8.0f * state.scale) + glm::vec3(0.0f, 5.5f * state.scale, 0.0f);
    target = carPosition + forward * (5.0f * state.scale) + glm::vec3(0.0f, 1.0f * state.scale, 0.0f);
}

void snapChaseCamera() {
    chaseDesired(cars[focusedCar], chasePosition, chaseTarget);
}

void updateChaseCamera(float dt) {
    glm::vec3 position;
    glm::vec3 target;
    chaseDesired(cars[focusedCar], position, target);

    float blend = 1.0f - std::exp(-6.0f * dt);
    chasePosition = glm::mix(chasePosition, position, blend);
    chaseTarget = glm::mix(chaseTarget, target, blend);
    chasePosition.y = std::max(chasePosition.y, 1.0f);
    myCamera.setView(chasePosition, chaseTarget);
}

void updateCamera(float dt) {
    if (cameraTour) {
        tourTime += dt;
        const glm::vec3 center(0.0f, 0.0f, -25.0f);
        float angle = tourTime * 0.10f;
        glm::vec3 position(
            center.x + std::cos(angle) * 330.0f,
            150.0f + std::sin(tourTime * 0.23f) * 25.0f,
            center.z + std::sin(angle) * 330.0f);
        myCamera.setView(position, center);
    } else if (chaseCamera) {
        updateChaseCamera(dt);
    }

    view = myCamera.getViewMatrix();
}

void setLightUniforms() {
    glm::vec3 pointPositions[MAX_POINT_LIGHTS];
    glm::vec3 pointColors[MAX_POINT_LIGHTS];
    glm::vec3 floodColor = floodlightsEnabled ? glm::vec3(1.0f, 0.82f, 0.58f) * 1.35f : glm::vec3(0.0f);

    for (int i = 0; i < MAX_POINT_LIGHTS; ++i) {
        pointPositions[i] = coords.lightHeads[i];
        pointColors[i] = floodColor;
    }

    glUniform3fv(pointLightPosLoc, MAX_POINT_LIGHTS, glm::value_ptr(pointPositions[0]));
    glUniform3fv(pointLightColorLoc, MAX_POINT_LIGHTS, glm::value_ptr(pointColors[0]));

    const CarState& focus = cars[focusedCar];
    glm::vec3 forward = glm::normalize(glm::vec3(focus.pose[0][0], 0.0f, focus.pose[0][2]));
    glm::vec3 carPosition(focus.pose[3]);
    glm::vec3 spotPosition = carPosition + forward * (3.2f * focus.scale) + glm::vec3(0.0f, 0.8f * focus.scale, 0.0f);
    glm::vec3 spotColor = spotlightsEnabled ? glm::vec3(1.9f, 1.7f, 1.4f) : glm::vec3(0.0f);

    glUniform3fv(spotLightPosLoc, 1, glm::value_ptr(spotPosition));
    glUniform3fv(spotLightDirectionLoc, 1, glm::value_ptr(forward));
    glUniform3fv(spotLightColorLoc, 1, glm::value_ptr(spotColor));
}

void renderShadowPass() {
    glViewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
    glClear(GL_DEPTH_BUFFER_BIT);

    depthShader.useShaderProgram();
    glUniformMatrix4fv(depthLightSpaceLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    glCullFace(GL_FRONT);
    auto drawDepth = [](gps::Model3D& model, const glm::mat4& transform) {
        glUniformMatrix4fv(depthModelLoc, 1, GL_FALSE, glm::value_ptr(transform));
        model.Draw(depthShader);
    };
    for (const auto& asset : sceneAssets) {
        drawDepth(asset->model, asset->transform);
    }
    for (const CarState& state : cars) {
        drawDepth(carBody->model, state.bodyTransform);
        for (int i = 0; i < 4; ++i) {
            drawDepth(carWheels[i]->model, state.wheelTransforms[i]);
        }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glCullFace(GL_BACK);
    glPolygonMode(GL_FRONT_AND_BACK, polygonMode);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, myWindow.getWindowDimensions().width, myWindow.getWindowDimensions().height);
}

void renderRain() {
    rainShader.useShaderProgram();
    glm::mat4 viewProjection = projection * view;
    glUniformMatrix4fv(rainViewProjectionLoc, 1, GL_FALSE, glm::value_ptr(viewProjection));

    glBindVertexArray(rainVAO);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDrawArrays(GL_LINES, 0, RAIN_COUNT * 2);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

void renderScene(float dt) {
    renderShadowPass();

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    sceneShader.useShaderProgram();
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniform3fv(viewPosLoc, 1, glm::value_ptr(myCamera.getPosition()));
    glUniform1i(flatShadingLoc, flatShading ? 1 : 0);
    glUniformMatrix4fv(lightSpaceMatrixLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));
    glUniform1f(fogDensityLoc, fogDensity);
    glUniform1i(fogEnabledLoc, fogEnabled ? 1 : 0);
    setLightUniforms();

    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, shadowMap);
    glActiveTexture(GL_TEXTURE0);

    auto drawLit = [](gps::Model3D& model, const glm::mat4& transform, const glm::vec3& accent, float accentMix) {
        glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(transform)));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(transform));
        glUniformMatrix3fv(normalMatrixLoc, 1, GL_FALSE, glm::value_ptr(normalMatrix));
        glUniform3fv(accentColorLoc, 1, glm::value_ptr(accent));
        glUniform1f(accentMixLoc, accentMix);
        model.Draw(sceneShader);
    };
    for (const auto& asset : sceneAssets) {
        drawLit(asset->model, asset->transform, glm::vec3(1.0f), 0.0f);
    }
    for (const CarState& state : cars) {
        drawLit(carBody->model, state.bodyTransform, state.accentColor, state.accentMix);
        for (int i = 0; i < 4; ++i) {
            drawLit(carWheels[i]->model, state.wheelTransforms[i], glm::vec3(1.0f), 0.0f);
        }
    }

    renderSkybox();
    updateRain(dt);
    renderRain();
}

void cleanup() {
    myWindow.Delete();
}

int main(int argc, const char * argv[]) {
    try {
        initOpenGLWindow();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    initOpenGLState();
    initShaders();
    initModels();
    initShadowResources();
    initUniforms();
    initSkybox();
    initRain();
    setWindowCallbacks();

    glfwSetInputMode(myWindow.getWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    ignoreMouseUntil = glfwGetTime() + 0.5;

    std::cout << "WASD: move | mouse: look | Shift: boost | Tab: capture/release mouse | Space: pause cars\n"
              << "C: drive mode (WASD drive) | V: chase camera | R: reset car | B: switch car\n"
              << "P: camera tour | 1/2/3: solid/wire/points | 4: flat/smooth | Z/X: scale car\n"
              << "K: floodlights | L: car spotlight | F: fog | -/=: fog density | Esc: quit" << std::endl;

    glCheckError();

    while (!glfwWindowShouldClose(myWindow.getWindow())) {
        float now = (float)glfwGetTime();
        deltaTime = std::min(now - lastFrameTime, 0.1f);
        lastFrameTime = now;

        if (driveMode) updateManualCar(cars[focusedCar], deltaTime);
        updateCars(deltaTime);
        processMovement(deltaTime);
        updateCamera(deltaTime);
        renderScene(deltaTime);

        glfwPollEvents();
        glfwSwapBuffers(myWindow.getWindow());

        glCheckError();
    }

    cleanup();

    return EXIT_SUCCESS;
}
