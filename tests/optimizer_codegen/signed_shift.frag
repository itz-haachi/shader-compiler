#version 330 core

in int inputValue;
out int result;

void main()
{
    result = inputValue << 1u;
}
