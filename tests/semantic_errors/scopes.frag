#version 330 core
// Declarations, scopes and qualifiers.

in vec3 vNormal;
uniform float uTime;
out vec4 fragColor;
const float PI = 3.14159;
const float E;                      // ERROR: const without initializer

struct Light { vec3 dir; };
struct Light { vec3 color; };       // ERROR: struct redefinition

float helper(float x);              // prototype that is never defined

int square(int x) { return x * x; }
int square(int y) { return y; }     // ERROR: redefinition of square(int)

void noValue() { return 1.0; }      // ERROR: void function returns a value
float noReturn() { }                // WARNING: may not return a value

void main()
{
    float x = 1.0;
    float x = 2.0;                  // ERROR: redeclaration in the same scope
    y = 3.0;                        // ERROR: undeclared identifier
    uTime = 1.0;                    // ERROR: assignment to a uniform
    PI = 3.0;                       // ERROR: assignment to const
    vNormal = vec3(0.0);            // ERROR: assignment to an 'in' variable
    gl_FragCoord = vec4(0.0);       // ERROR: read-only built-in
    in float local;                 // ERROR: 'in' on a local variable
    Light l;
    l.position = vec3(0.0);         // ERROR: no member 'position'
    fragColor.xx = vec2(1.0);       // ERROR: repeated swizzle component on the left
    break;                          // ERROR: break outside loop/switch
    float r = helper(x);            // ERROR: helper is never defined
    float s = square(x, 2);         // ERROR: no matching function
    float u = undefinedFn(x);       // ERROR: undeclared function
    fragColor = vec4(x);
}
