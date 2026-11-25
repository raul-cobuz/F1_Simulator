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

SceneCoords coords;
std::vector<std::unique_ptr<SceneAsset>> sceneAssets;
SceneAsset* carBody = nullptr;
std::array<SceneAsset*, 4> carWheels{};
std::vector<Aabb> colliders;
std::vector<float> segmentLengths;
float lapLength = 0.0f;

struct CarState {
    glm::mat4 pose{1.0f};
    float distance = 0.0f;
    float wheelSpin = 0.0f;
    float scale = 1.0f;
    bool moving = true;
} car;

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
float tourTime = 0.0f;
bool floodlightsEnabled = true;
bool spotlightsEnabled = true;
bool flatShading = false;
GLenum polygonMode = GL_FILL;

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
            car.moving = !car.moving;
            std::cout << "Car: " << (car.moving ? "moving" : "paused") << std::endl;
            break;
        case GLFW_KEY_P:
            cameraTour = !cameraTour;
            tourTime = 0.0f;
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
            car.scale = std::max(0.5f, car.scale - 0.1f);
            std::cout << "Car scale: " << car.scale << std::endl;
            break;
        case GLFW_KEY_X:
            car.scale = std::min(2.0f, car.scale + 0.1f);
            std::cout << "Car scale: " << car.scale << std::endl;
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

void buildPath() {
    if (coords.waypoints.size() < 2) return;
    for (size_t i = 0; i < coords.waypoints.size(); ++i) {
        glm::vec3 delta = coords.waypoints[(i + 1) % coords.waypoints.size()] - coords.waypoints[i];
        float length = glm::length(delta);
        segmentLengths.push_back(length);
        lapLength += length;
    }
}

void updateCar(float dt) {
    if (coords.waypoints.size() < 2) return;

    if (car.moving) {
        car.distance = std::fmod(car.distance + CAR_SPEED * dt, lapLength);
        car.wheelSpin = std::fmod(car.wheelSpin - CAR_SPEED * dt / WHEEL_RADIUS, 2.0f * 3.14159265f);
    }

    float travelled = car.distance;
    size_t segment = 0;
    while (segment < segmentLengths.size() && travelled > segmentLengths[segment]) {
        travelled -= segmentLengths[segment];
        ++segment;
    }
    if (segment >= segmentLengths.size()) segment = 0;

    glm::vec3 start = coords.waypoints[segment];
    glm::vec3 end = coords.waypoints[(segment + 1) % coords.waypoints.size()];
    glm::vec3 forward = glm::normalize(end - start);
    glm::vec3 position = start + (end - start) * (segmentLengths[segment] > 0.0f ? travelled / segmentLengths[segment] : 0.0f);
    float heading = std::atan2(-forward.z, forward.x);

    car.pose = glm::translate(glm::mat4(1.0f), position)
             * glm::rotate(glm::mat4(1.0f), heading, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 carScale = glm::scale(glm::mat4(1.0f), glm::vec3(car.scale));
    carBody->transform = car.pose * carScale;

    for (int i = 0; i < 4; ++i) {
        carWheels[i]->transform = car.pose * carScale
            * glm::translate(glm::mat4(1.0f), coords.wheelCenters[i])
            * glm::rotate(glm::mat4(1.0f), car.wheelSpin, coords.wheelAxle);
    }
}

void addAsset(const std::string& name, glm::mat4 transform = glm::mat4(1.0f)) {
    auto asset = std::make_unique<SceneAsset>();
    asset->name = name;
    asset->transform = transform;
    asset->model.LoadModel("models/f1_circuit/" + name + ".obj");
    sceneAssets.push_back(std::move(asset));
}

void initModels() {
    loadSceneCoords();
    buildPath();

    float carYaw = std::atan2(-coords.carForward.z, coords.carForward.x);
    glm::mat4 carTransform = glm::translate(glm::mat4(1.0f), coords.carStart)
                           * glm::rotate(glm::mat4(1.0f), carYaw, glm::vec3(0.0f, 1.0f, 0.0f));

    addAsset("ground");
    addAsset("track");
    addAsset("curbs");
    addAsset("barriers");
    addAsset("pit_building");
    addAsset("grandstand");
    addAsset("start_bridge");
    addAsset("light_poles");
    addAsset("car_body", carTransform);

    const char* wheelNames[4] = {"wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr"};
    for (int i = 0; i < 4; ++i) {
        addAsset(wheelNames[i], carTransform * glm::translate(glm::mat4(1.0f), coords.wheelCenters[i]));
    }

    for (const auto& asset : sceneAssets) {
        if (asset->name == "car_body") carBody = asset.get();
        for (int i = 0; i < 4; ++i) {
            if (asset->name == wheelNames[i]) carWheels[i] = asset.get();
        }
    }

    updateCar(0.0f);
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
    float speed = cameraSpeed * dt;
    if (pressedKeys[GLFW_KEY_LEFT_SHIFT] || pressedKeys[GLFW_KEY_RIGHT_SHIFT]) speed *= 3.0f;

    bool moved = false;
    if (pressedKeys[GLFW_KEY_W]) { moveCameraStep(gps::MOVE_FORWARD, speed); moved = true; }
    if (pressedKeys[GLFW_KEY_S]) { moveCameraStep(gps::MOVE_BACKWARD, speed); moved = true; }
    if (pressedKeys[GLFW_KEY_A]) { moveCameraStep(gps::MOVE_LEFT, speed); moved = true; }
    if (pressedKeys[GLFW_KEY_D]) { moveCameraStep(gps::MOVE_RIGHT, speed); moved = true; }

    if (moved && cameraTour) cameraTour = false;
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

    glm::vec3 forward = glm::normalize(glm::vec3(car.pose[0][0], 0.0f, car.pose[0][2]));
    glm::vec3 carPosition(car.pose[3]);
    glm::vec3 spotPosition = carPosition + forward * (3.2f * car.scale) + glm::vec3(0.0f, 0.8f * car.scale, 0.0f);
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
    for (const auto& asset : sceneAssets) {
        glUniformMatrix4fv(depthModelLoc, 1, GL_FALSE, glm::value_ptr(asset->transform));
        asset->model.Draw(depthShader);
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

    for (const auto& asset : sceneAssets) {
        glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(asset->transform)));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(asset->transform));
        glUniformMatrix3fv(normalMatrixLoc, 1, GL_FALSE, glm::value_ptr(normalMatrix));
        asset->model.Draw(sceneShader);
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

    std::cout << "WASD: move | mouse: look | Shift: boost | Tab: capture/release mouse | Space: pause car\n"
              << "P: camera tour | 1/2/3: solid/wire/points | 4: flat/smooth | Z/X: car scale\n"
              << "K: floodlights | L: car spotlight | F: fog | -/=: fog density | Esc: quit" << std::endl;

    glCheckError();

    while (!glfwWindowShouldClose(myWindow.getWindow())) {
        float now = (float)glfwGetTime();
        deltaTime = std::min(now - lastFrameTime, 0.1f);
        lastFrameTime = now;

        updateCar(deltaTime);
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
