#version 330 core
out float result;

vec3 make_vector()
{
    return vec3(1.0);
}

void main()
{
    result = dot(make_vector(), make_vector());
}
