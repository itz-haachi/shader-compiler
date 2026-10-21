#version 330 core
in vec2 inputValue;
in int indexValue;
out vec4 result;

void main()
{
    vec4 values[2];
    values[indexValue].xy = inputValue;
    result = values[indexValue];
}
