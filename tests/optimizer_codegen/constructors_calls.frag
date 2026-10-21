#version 330 core
out vec4 result;

vec4 make_color(vec3 value)
{
    return vec4(value, 1.0);
}

void main()
{
    result = make_color(vec3(1.0));
}
