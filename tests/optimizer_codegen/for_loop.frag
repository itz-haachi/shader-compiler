#version 330 core
in int limit;
out int result;

void main()
{
    int sum = 0;
    for (int i = 0; i < limit; i++)
        sum += i;
    result = sum;
}
