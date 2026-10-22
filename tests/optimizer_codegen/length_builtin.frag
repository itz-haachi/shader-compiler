#version 330 core
in vec3 inputVector;
out float result;

void main()
{
    result = length(inputVector);
}
