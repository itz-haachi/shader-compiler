#version 330 core
uniform float weights[2];
in int indexValue;
out float result;

void main()
{
    result = weights[indexValue];
}
