#version 330 core
out int result;

void main()
{
    int value = 5 / 0;
    result = value;
}
