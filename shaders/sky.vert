#version 450 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 u_VP;

out vec2 v_UV;

void main() {
    gl_Position = u_VP * vec4(aPos, 1.0);
    v_UV = aUV;
}
