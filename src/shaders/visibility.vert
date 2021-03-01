#version 450 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 offset;
layout(location = 2) in float radius;
layout(location = 3) in vec3 color;
layout(location = 4) in vec3 normal;

uniform mat4 modelview;
uniform mat4 projection;

uniform float epsilon;

out vec3 vColor;

mat4 rotationMatrix(vec3 axis, float angle) {
  axis = normalize(axis);
  float s = sin(angle);
  float c = cos(angle);
  float oc = 1.0 - c;

  return mat4(oc * axis.x * axis.x + c, oc * axis.x * axis.y - axis.z * s, oc * axis.z * axis.x + axis.y * s, 0.0,
              oc * axis.x * axis.y + axis.z * s, oc * axis.y * axis.y + c, oc * axis.y * axis.z - axis.x * s, 0.0,
              oc * axis.z * axis.x - axis.y * s, oc * axis.y * axis.z + axis.x * s, oc * axis.z * axis.z + c, 0.0, 0.0,
              0.0, 0.0, 1.0);
}

void main() {
  vec3 currOrientation = vec3(0.0, 0.0, -1.0); // straight to the front
  vec3 axis = cross(normal, currOrientation);
  float theta = acos(dot(currOrientation, normalize(normal)));

  // rotate each point such that the currOrientation and the target orientation (normal) align
  mat3 rot = mat3(rotationMatrix(axis, theta));
  vec4 viewPos = modelview * vec4(rot * (radius * aPos) + offset, 1.0);

  // move slightly in viewing direction
  vec3 direction = normalize(viewPos.xyz);
  if(viewPos.z < 0)
    viewPos += vec4(epsilon * direction, 0);

  gl_Position = projection * viewPos;

  vColor = color;
}