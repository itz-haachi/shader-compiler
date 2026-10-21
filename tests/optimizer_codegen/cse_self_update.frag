#version 330 core

in float leftValue;
in float rightValue;
out float result;

void main() {
    float a = leftValue;
    a = a + rightValue;
    result = a + rightValue;
}
