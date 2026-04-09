#version 450 core

in vec2 v_UV;
in vec4 v_Color;

out vec4 FragColor;

uniform sampler2D u_Texture;

void main() {
    vec4 tex = texture(u_Texture, v_UV);
    FragColor = tex * v_Color;
    if (FragColor.a < 0.003) discard; // tiny alpha cutoff
}
