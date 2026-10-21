#version 330 core
in vec3 leftVector;
in vec3 rightVector;
out vec3 result;

void main()
{
    vec3 sum = leftVector + rightVector;
    result = sum;
}
