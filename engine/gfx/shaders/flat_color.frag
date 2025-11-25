#version 450

layout(location = 0) out vec4 out_color;

void main() {
    // For now, constant color; later we can base this on UV, time, etc.
    out_color = vec4(0.5, 0.1, 0.1, 1.0); // Dark red (deep, muted)
}
