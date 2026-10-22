#version 330 core
in float conditionValue;
out float result;

void main()
{
    if (conditionValue > 0.0)
        result = 1.0;
    else
        result = 2.0;
}
