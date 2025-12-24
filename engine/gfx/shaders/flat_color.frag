// engine/gfx/shaders/flat_color.frag

#version 450

layout(set = 0, binding = 0) uniform Ubo
{
    vec4 color;
} ubo;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = ubo.color;
}
