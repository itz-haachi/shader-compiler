#version 330 core
out float result;

float twice(float value)
{
    return value * 2.0;
}

void main()
{
    result = twice(2.0);
}
