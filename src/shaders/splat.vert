#version 450 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 offset;
layout(location=2) in float radius;
layout(location=3) in vec3 color;

uniform mat4 projection;
uniform mat4 view;

out vec3 vColor;

void main()
{
    gl_Position = projection * view * vec4(radius*aPos + offset, 1.0);
    vColor = color;
}