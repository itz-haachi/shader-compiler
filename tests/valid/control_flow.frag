#version 330 core
precision highp float;

/* Exercises loops, switch, short-circuit logic, out/inout parameters,
   overloading, prototypes, arrays, initializer lists and swizzle writes. */

struct Material {
    vec3 albedo;
    float roughness;
    int flags[2];
};

in vec2 vUV;
out vec4 fragColor;

uniform Material uMat;
uniform int uMode;
uniform mat3 uNormalMatrix;

const int STEPS = 4;
const float KERNEL[3] = { 0.25, 0.5, 0.25 };

float luminance(vec3 c);            /* prototype, defined below */

void splitColor(vec4 c, out vec3 rgb, out float alpha)
{
    rgb = c.rgb;
    alpha = c.a;
}

void accumulate(inout float total, float x)
{
    total += x;
}

/* overloads */
float scale(float x) { return x * 2.0; }
vec3  scale(vec3 v)  { return v * 2.0; }

void main()
{
    vec4 color = vec4(uMat.albedo, 1.0);
    vec3 rgb;
    float alpha;
    float total = 0.0;
    int i = 0;
    float weights[3] = { 0.1, 0.2, 0.7 };

    splitColor(color, rgb, alpha);

    while (i < STEPS) {
        if (i == 2)
            break;
        accumulate(total, float(i) * 0.5);
        i++;
    }

    do {
        total -= 0.1;
    } while (total > 1.0 || alpha < 0.0);

    for (int k = 0; k < 3; k++) {
        if (k == 1)
            continue;
        total += weights[k];
    }

    switch (uMode) {
    case 0:
        rgb = scale(rgb);
        break;
    case 1:
        rgb *= uNormalMatrix;
        break;
    default:
        rgb.xy = vec2(total, scale(alpha));
    }

    bool bright = luminance(rgb) > 0.5 && !(alpha < 0.1);
    color.rgb = bright ? rgb : vec3(0.0);
    color.a = clamp(total, 0.0, 1.0);

    /* shadowing: this 'i' is a different variable in the IR */
    {
        float i = 3.0;
        color.r += i * uMat.roughness;
    }

    fragColor = color;
}

float luminance(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}
