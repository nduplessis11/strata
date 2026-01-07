// -----------------------------------------------------------------------------
// engine/gfx/shaders/vertex_color.frag
//
// Purpose:
//   Pass-through fragment shader that outputs interpolated vertex color.
// -----------------------------------------------------------------------------

#version 450

layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 out_color;

void main()
{
    out_color = v_color;
}
