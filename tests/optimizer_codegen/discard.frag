#version 330 core
in float alpha;
out vec4 fragColor;

void main()
{
    if (alpha < 0.5)
        discard;
    fragColor = vec4(1.0);
}
