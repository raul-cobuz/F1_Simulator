#version 410 core

in vec3 fPosition;
in vec3 fNormal;
in vec2 fTexCoords;
in vec4 fFragPosLightSpace;

out vec4 fColor;

uniform vec3 viewPos;
uniform vec3 ambientColor;
uniform vec3 dirLightDirection;
uniform vec3 dirLightColor;

const int MAX_POINT_LIGHTS = 6;
uniform int pointLightCount;
uniform vec3 pointLightPos[MAX_POINT_LIGHTS];
uniform vec3 pointLightColor[MAX_POINT_LIGHTS];

uniform vec3 spotLightPos;
uniform vec3 spotLightDirection;
uniform vec3 spotLightColor;
uniform float spotInnerCos;
uniform float spotOuterCos;

uniform sampler2D diffuseTexture;
uniform sampler2D specularTexture;
uniform vec3 accentColor;
uniform float accentMix;
uniform sampler2D shadowMap;
uniform bool flatShading;

uniform vec3 fogColor;
uniform float fogDensity;
uniform bool fogEnabled;

const float shininess = 80.0;

vec3 blinnPhong(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 albedo, vec3 specMask) {
    float diffuse = max(dot(N, L), 0.0);
    vec3 halfVector = normalize(V + L);
    float specular = pow(max(dot(N, halfVector), 0.0), shininess) * 0.35;
    return (albedo * diffuse + specMask * specular) * lightColor;
}

float shadowFactor(vec3 N, vec3 L) {
    vec3 proj = fFragPosLightSpace.xyz / fFragPosLightSpace.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;

    float bias = max(0.0015 * (1.0 - dot(N, L)), 0.0004);
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float closest = texture(shadowMap, proj.xy + vec2(x, y) * texel).r;
            shadow += proj.z - bias > closest ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

void main() {
    vec3 N = normalize(fNormal);
    vec3 V = normalize(viewPos - fPosition);

    if (flatShading) {
        vec3 faceNormal = cross(dFdx(fPosition), dFdy(fPosition));
        float faceLength = length(faceNormal);
        if (faceLength > 1e-8) {
            faceNormal /= faceLength;
            N = dot(faceNormal, V) < 0.0 ? -faceNormal : faceNormal;
        }
    }

    vec3 albedo = texture(diffuseTexture, fTexCoords).rgb;
    float luma = dot(albedo, vec3(0.299, 0.587, 0.114));
    albedo = mix(albedo, accentColor * (0.35 + 0.65 * luma), accentMix);
    vec3 specMask = texture(specularTexture, fTexCoords).rgb;
    vec3 color = ambientColor * albedo;

    vec3 dirToLight = normalize(-dirLightDirection);
    color += (1.0 - shadowFactor(N, dirToLight)) * blinnPhong(N, V, dirToLight, dirLightColor, albedo, specMask);

    for (int i = 0; i < pointLightCount; ++i) {
        vec3 toLight = pointLightPos[i] - fPosition;
        float distance = length(toLight);
        vec3 L = toLight / max(distance, 0.001);
        float attenuation = 1.0 / (1.0 + 0.02 * distance + 0.0016 * distance * distance);
        color += blinnPhong(N, V, L, pointLightColor[i] * attenuation, albedo, specMask);
    }

    vec3 toSpotLight = spotLightPos - fPosition;
    float spotDistance = length(toSpotLight);
    vec3 Lspot = toSpotLight / max(spotDistance, 0.001);
    float theta = dot(normalize(fPosition - spotLightPos), spotLightDirection);
    float spotMask = clamp((theta - spotOuterCos) / (spotInnerCos - spotOuterCos), 0.0, 1.0);
    spotMask = spotMask * spotMask * (3.0 - 2.0 * spotMask);
    float spotAttenuation = 1.0 / (1.0 + 0.02 * spotDistance + 0.002 * spotDistance * spotDistance);
    color += blinnPhong(N, V, Lspot, spotLightColor * spotMask * spotAttenuation, albedo, specMask);

    if (fogEnabled) {
        float fogAmount = 1.0 - exp(-pow(fogDensity * length(viewPos - fPosition), 2.0));
        color = mix(color, fogColor, fogAmount);
    }

    fColor = vec4(min(color, vec3(1.0)), 1.0);
}
