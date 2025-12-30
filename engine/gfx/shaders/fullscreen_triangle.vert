// engine/gfx/shaders/fullscreen_triangle.vert

#version 450

layout(set = 0, binding = 0) uniform SceneUBO
{
    mat4 view_proj;
    mat4 model;
    vec4 tint;
} ubo;

layout(location = 0) out vec4 v_color;

// 36-vertex cube (12 triangles). CCW winding in a typical RH system.
const vec3 kPositions[36] = vec3[36](
    // back face (-Z)
    vec3(-0.5, -0.5, -0.5),
    vec3( 0.5,  0.5, -0.5),
    vec3( 0.5, -0.5, -0.5),
    vec3( 0.5,  0.5, -0.5),
    vec3(-0.5, -0.5, -0.5),
    vec3(-0.5,  0.5, -0.5),

    // front face (+Z)
    vec3(-0.5, -0.5,  0.5),
    vec3( 0.5, -0.5,  0.5),
    vec3( 0.5,  0.5,  0.5),
    vec3( 0.5,  0.5,  0.5),
    vec3(-0.5,  0.5,  0.5),
    vec3(-0.5, -0.5,  0.5),

    // left face (-X)
    vec3(-0.5,  0.5,  0.5),
    vec3(-0.5,  0.5, -0.5),
    vec3(-0.5, -0.5, -0.5),
    vec3(-0.5, -0.5, -0.5),
    vec3(-0.5, -0.5,  0.5),
    vec3(-0.5,  0.5,  0.5),

    // right face (+X)
    vec3( 0.5,  0.5,  0.5),
    vec3( 0.5, -0.5, -0.5),
    vec3( 0.5,  0.5, -0.5),
    vec3( 0.5, -0.5, -0.5),
    vec3( 0.5,  0.5,  0.5),
    vec3( 0.5, -0.5,  0.5),

    // bottom face (-Y)
    vec3(-0.5, -0.5, -0.5),
    vec3( 0.5, -0.5, -0.5),
    vec3( 0.5, -0.5,  0.5),
    vec3( 0.5, -0.5,  0.5),
    vec3(-0.5, -0.5,  0.5),
    vec3(-0.5, -0.5, -0.5),

    // top face (+Y)
    vec3(-0.5,  0.5, -0.5),
    vec3( 0.5,  0.5,  0.5),
    vec3( 0.5,  0.5, -0.5),
    vec3( 0.5,  0.5,  0.5),
    vec3(-0.5,  0.5, -0.5),
    vec3(-0.5,  0.5,  0.5)
);

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
    uint idx = uint(gl_VertexIndex);
    vec3 pos = kPositions[idx];

    int face = int(idx / 6u);
    vec3 fc  = kFaceColors[face];

    v_color = vec4(fc, 1.0) * ubo.tint;

    gl_Position = ubo.view_proj * ubo.model * vec4(pos, 1.0);
}

