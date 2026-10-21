#version 330 core
in vec3 leftVector;
in vec3 rightVector;
in vec2 coordinates;
uniform sampler2D textureSampler;
out vec4 fragColor;

void main()
{
    vec3 normal = normalize(leftVector);
    float amount = dot(leftVector, rightVector);
    vec4 sampleColor = texture(textureSampler, coordinates);
    fragColor = sampleColor + vec4(normal, amount);
}
