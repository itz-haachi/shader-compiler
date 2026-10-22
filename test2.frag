#version 330 core

in float leftValue;
in float rightValue;
in uint inputIndex;
out float result;
out uint indexResult;

void main()
{
    /* Constant folding followed by constant propagation. */
    float base = 12.0 / 3.0;
    float offset = base + 2.0;

    /* The second expression reuses the first one through CSE. */
    float firstSum = leftValue + rightValue;
    float secondSum = leftValue + rightValue;

    /* These unsigned operations are safe strength-reduction candidates. */
    uint doubled = inputIndex * 2u;
    uint shifted = inputIndex << 1u;

    /* Pure expression statements become dead temporary calculations. */
    (leftValue + 1.0) * rightValue;
    (leftValue + 2.0) * rightValue;
    (leftValue + 3.0) * rightValue;
    (leftValue + 4.0) * rightValue;
    (leftValue + 5.0) * rightValue;
    (leftValue + 6.0) * rightValue;

    if (leftValue > rightValue)
        result = offset + firstSum;
    else
        result = offset + secondSum;

    indexResult = doubled + shifted;
}
