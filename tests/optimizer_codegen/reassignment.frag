#version 330 core
in float inputValue;
out float result;

void main()
{
    float a = 5.0;
    a = inputValue;
    float b = a + 2.0;
    result = b;
}
