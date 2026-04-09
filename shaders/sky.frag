#version 450 core

in vec2 v_UV;
out vec4 FragColor;

uniform vec3 u_Color;

void main() {
    // Draw a circular disc using the quad's UVs (0..1). This creates
    // a round sun/moon by alpha-masking fragments outside a radius.
    vec2 uv = v_UV;
    float d = length(uv - vec2(0.5));
    float inner = 0.45; // fully opaque radius
    float outer = 0.5;  // outer radius (alpha fades to 0)
    float alpha = 1.0 - smoothstep(inner, outer, d);
    if (alpha <= 0.001) discard;
    FragColor = vec4(u_Color, alpha);
}
