#version 410 core

in vec3 vDirection;

out vec4 fColor;

uniform samplerCube skybox;

void main() {
    fColor = texture(skybox, normalize(vDirection));
}
