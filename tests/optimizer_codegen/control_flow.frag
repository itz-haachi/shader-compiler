#version 330 core
in float conditionValue;
out float result;

void main()
{
    float value = 0.0;
    while (value < conditionValue) {
        value += 1.0;
    }
    result = value;
}
