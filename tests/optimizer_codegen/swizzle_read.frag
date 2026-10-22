#version 330 core
in vec4 inputColor;
out vec3 result;

void main()
{
    result = inputColor.rgb;
}
