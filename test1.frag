#version 330 core

in float inputA;
in float inputB;
out float result;

void main()
{
    float sum = inputA + inputB;
    float product = sum * inputB;

    if (inputA > inputB)
        result = product;
    else
        result = sum;
}
