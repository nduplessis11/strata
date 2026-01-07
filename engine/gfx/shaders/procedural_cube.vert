// path: engine/gfx/shaders/procedural_cube.vert
// -----------------------------------------------------------------------------
// engine/gfx/shaders/procedural_cube.vert
//
// Purpose:
//   Cube/mesh vertex shader using vertex input positions at location 0.
//   Uses SceneUBO view-proj + model.
//
// Notes:
//   - Consumes layout(location=0) input to match pipeline vertex input state.
//   - Supports the demo "face color" mode when tint.a >= 0.5 (uses gl_VertexIndex
//     for a 36-vertex non-indexed cube draw).
//   - For normal meshes (tint.a < 0.5), outputs a solid tint color.
// -----------------------------------------------------------------------------

#version 450

layout(set = 0, binding = 0) uniform SceneUBO
{
    mat4 view_proj;
    mat4 model;
    vec4 tint;
} ubo;

layout(location = 0) in vec3 in_pos;
layout(location = 0) out vec4 v_color;

const vec3 kFaceColors[6] = vec3[6](
    vec3(0.90, 0.20, 0.20), // back
    vec3(0.20, 0.90, 0.20), // front
    vec3(0.20, 0.20, 0.90), // left
    vec3(0.90, 0.90, 0.20), // right
    vec3(0.90, 0.20, 0.90), // bottom
    vec3(0.20, 0.90, 0.90)  // top
);

void main()
{
    // tint.a is treated as a simple "mode" flag:
    //   >= 0.5 : demo cube face colors
    //   <  0.5 : solid tint (for general meshes)
    bool use_face_colors = (ubo.tint.a >= 0.5);

    vec3 color = ubo.tint.rgb;

    if (use_face_colors)
    {
        // For the demo cube we draw 36 vertices non-indexed, so gl_VertexIndex is 0..35
        // and face = idx/6 is 0..5.
        uint idx = uint(gl_VertexIndex);
        int face = int((idx / 6u) % 6u);
        color = kFaceColors[face] * ubo.tint.rgb;
    }

    v_color = vec4(color, 1.0);
    gl_Position = ubo.view_proj * ubo.model * vec4(in_pos, 1.0);
}
