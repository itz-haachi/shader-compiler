// A shader without main() is rejected.
out vec4 fragColor;

vec4 shade(vec3 n)
{
    return vec4(n * 0.5 + 0.5, 1.0);
}
