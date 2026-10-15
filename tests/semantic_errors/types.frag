#version 330 core
// Every marked line contains a type error.

in vec3 vNormal;
out vec4 fragColor;
uniform sampler2D uTex;

void main()
{
    vec3 a = vec3(1.0);
    vec4 b = vec4(0.5);
    vec3 c = a + b;                 // ERROR: vec3 + vec4
    float f = a;                    // ERROR: cannot initialize float with vec3
    vec2 d = a.xyzw;                // ERROR: swizzle beyond vec3
    vec2 e = a.xg;                  // ERROR: mixes xyzw and rgba sets
    int  i = 1.5;                   // ERROR: float -> int is not implicit
    vec4 g = vec4(a, 1.0, 2.0);     // ERROR: too many constructor arguments
    mat3 m = mat3(1.0);
    vec4 h = m * b;                 // ERROR: mat3 * vec4
    bool ok = a < a;                // ERROR: relational op on vectors
    if (f) { }                      // ERROR: condition is not bool
    float t = texture(uTex, 1.0).r; // ERROR: sampler2D needs vec2 coordinates
    int k = 5 % 2.0;                // ERROR: % on float
    fragColor = vec4(c, 1.0);
}
