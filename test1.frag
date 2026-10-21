#version 330 core

in float inputA;
in float inputB;
in uint inputValue;
out float result;
out uint uintResult;

void main()
{
    float folded = 2.0 + 3.0;
    float propagated = folded + 4.0;
    float first = inputA + inputB;
    float second = inputA + inputB;
    uint doubled = inputValue * 2u;
    uint shifted = inputValue << 1u;

    /* These expression statements create unused pure temporaries. */
    (inputA + 1.0) * inputB;
    (inputA + 2.0) * inputB;
    (inputA + 3.0) * inputB;
    (inputA + 4.0) * inputB;
    (inputA + 5.0) * inputB;
    (inputA + 6.0) * inputB;
    (inputA + 7.0) * inputB;
    (inputA + 8.0) * inputB;
    (inputA + 9.0) * inputB;
    (inputA + 10.0) * inputB;
    (inputA + 11.0) * inputB;
    (inputA + 12.0) * inputB;
    (inputA + 13.0) * inputB;
    (inputA + 14.0) * inputB;
    (inputA + 15.0) * inputB;
    (inputA + 16.0) * inputB;
    (inputA + 17.0) * inputB;
    (inputA + 18.0) * inputB;

    if (inputA > inputB)
        result = propagated + first;
    else
        result = propagated + second;

    uintResult = doubled + shifted;
}
