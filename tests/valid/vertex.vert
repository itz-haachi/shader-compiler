#version 330 core

/* A vertex shader: matrices, uniform block, built-in gl_Position. */

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform Transforms {
    mat4 model;
    mat4 view;
    mat4 projection;
} uT;

out vec3 vNormal;
out vec2 vUV;

void main()
{
    mat4 mvp = uT.projection * uT.view * uT.model;
    vec4 world = uT.model * vec4(aPosition, 1.0);
    mat3 normalMat = mat3(uT.model);

    vNormal = normalize(normalMat * aNormal);
    vUV = aUV;
    gl_Position = mvp * vec4(aPosition, 1.0);
    gl_PointSize = world.w > 0.0 ? 1.0 : 2.0;
}
