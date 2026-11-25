#version 410 core

layout(location = 0) in vec3 vPosition;

uniform mat4 projection;
uniform mat4 view;

out vec3 vDirection;

void main() {
    vDirection = vPosition;
    gl_Position = (projection * view * vec4(vPosition, 1.0)).xyww;
}
