#version 330 core
precision mediump float;

/* A lit fragment shader */
struct Light {
    vec3 direction;
    vec3 color;
    float intensity;
};

in vec3 vNormal;
in vec2 vUV;
layout(location = 0) out vec4 fragColor;

uniform sampler2D uTex;
uniform Light uLight;
uniform float uWeights[4];

const vec3 AMBIENT = vec3(0.1, 0.1, 0.1);

float lambert(vec3 n, vec3 l)
{
    return max(dot(n, -l), 0.0);
}

void main()
{
    vec3 n = normalize(vNormal);
    float diff = lambert(n, uLight.direction) * uLight.intensity;
    int mask = 0xFF;
    uint flags = 3u;

    for (int i = 0; i < 4; ++i) {
        diff *= uWeights[i];
    }

    if (diff <= .25 && mask != 0)
        diff += 0.1;
    else if (diff > 1.0)
        diff = 1.0;

    vec4 tex = texture(uTex, vUV);
    if (tex.a < 0.01)
        discard;

    vec3 color = AMBIENT + tex.rgb * uLight.color * diff;
    fragColor = vec4(color, flags > 0u ? 1.0 : 0.5);
}
