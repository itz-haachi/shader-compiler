#version 330 core
in int limit;
out int result;

void main()
{
    int i = 0;
    int sum = 0;
    while (i < limit) {
        sum += i;
        i++;
    }
    result = sum;
}
