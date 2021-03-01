#version 450 core

out vec4 FragColor;

in vec2 TexCoords;

layout(location = 0) uniform sampler2D colorAccTexture;
layout(location = 1) uniform sampler2D depthAccTexture;
layout(location = 2) uniform sampler2D counterTexture;

uniform float far;
uniform float near;

float LinearizeDepth(vec2 uv) {
  float z = texture(depthAccTexture, uv).x;
  float counter = texture(counterTexture, TexCoords).r;
  z /= counter;
  z = z * 2.0 - 1.0; // back to NDC
  return (2.0 * near * far) / (far + near - z * (far - near));
}

void main() {
  float counter = texture(counterTexture, TexCoords).r;
  float d = LinearizeDepth(TexCoords);
  if (isinf(d) || isnan(d)) {
    d = 0;
  }

  // uncomment to per-pixel visualize overdraw
  FragColor = vec4(counter / 200);
  float nonLinearDepth = texture(depthAccTexture, TexCoords).r;
  nonLinearDepth /= counter;
  gl_FragDepth = nonLinearDepth;
  
  //FragColor = vec4(vec3(d)/5.0f, 1.0);
}