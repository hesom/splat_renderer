#version 450 core

in VertexData {
  vec3 vColor;
  vec4 vPos;
  flat vec3 viewCenter;
  flat float R;
}
inData;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 counter;
layout(location = 2) out vec4 accDepth;

float gauss(float x /* in [0,1] */)
{
    const float pi = 3.14159265358979323846;
    const float sigma =  inData.R/4.0; // chosen so that there is almost no visible cutoff
    return exp(- x * x / (2.0 * sigma * sigma)) / (sigma * sqrt(2.0 * pi));
}

void main() {

  float dist = length(inData.viewCenter - inData.vPos.xyz);
  float weight = gauss(dist);

  FragColor = weight*vec4(inData.vColor, 1.0f);
  counter = weight*vec4(1.0f, 1.0f, 1.0f, 1.0f);
  float d = gl_FragCoord.z;
  accDepth = weight*vec4(d, d, d, 1.0);
}