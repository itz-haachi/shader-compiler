#version 330 core
out uint result;

void main()
{
    uint value = 1u + 2u;
    bool flag = true == false;
    if (flag)
        result = value;
    else
        result = 3u;
}
