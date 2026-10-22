#version 330 core
uniform float values[];
out int result;

void main()
{
    result = values.length();
}
