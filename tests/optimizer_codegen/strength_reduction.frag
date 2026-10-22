#version 330 core
in uint inputValue;
out uint result;

void main()
{
    uint doubled = inputValue * 2u;
    uint shifted = inputValue << 1u;
    result = doubled + shifted;
}
