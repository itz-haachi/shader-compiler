#version 330 core
in vec3 inputColor;
out vec4 fragColor;

void main()
{
    vec4 color = vec4(0.0);
    color.xyz = inputColor;
    fragColor = color;
}
