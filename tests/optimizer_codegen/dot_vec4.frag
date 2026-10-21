#version 330 core
in vec4 leftVector;
in vec4 rightVector;
out float result;

void main()
{
    result = dot(leftVector, rightVector);
}
