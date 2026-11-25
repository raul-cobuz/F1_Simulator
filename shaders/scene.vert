#version 410 core

layout(location = 0) in vec3 vPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vTexCoords;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat3 normalMatrix;
uniform mat4 lightSpaceMatrix;

out vec3 fPosition;
out vec3 fNormal;
out vec2 fTexCoords;
out vec4 fFragPosLightSpace;

void main() {
    vec4 worldPosition = model * vec4(vPosition, 1.0);
    fPosition = worldPosition.xyz;
    fNormal = normalMatrix * vNormal;
    fTexCoords = vTexCoords;
    fFragPosLightSpace = lightSpaceMatrix * worldPosition;
    gl_Position = projection * view * worldPosition;
}
