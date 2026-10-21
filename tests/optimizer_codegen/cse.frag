#version 330 core
in float leftValue;
in float rightValue;
out float result;

void main()
{
    float first = leftValue + rightValue;
    float second = leftValue + rightValue;
    result = second;
}
