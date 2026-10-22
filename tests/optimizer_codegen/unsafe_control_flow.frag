#version 330 core
in float conditionValue;
out float result;

void main()
{
    float a = 5.0;
    if (conditionValue > 0.0)
        a = 10.0;
    float b = a + 2.0;
    result = b;
}
