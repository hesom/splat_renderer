#version 450 core

in vec3 vColor;
in vec4 vPos;

out vec4 FragColor;

void main()
{
    FragColor = vec4(vec3(-vPos.z/5.0f), 1.0f);
}