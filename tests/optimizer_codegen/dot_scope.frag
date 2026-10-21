#version 330 core

out float result;

float helper(vec3 v, vec3 w)
{
    return dot(v, w);
}

void main()
{
    vec4 v = vec4(1.0);
    vec4 w = vec4(2.0);
    result = dot(v, w);
}
