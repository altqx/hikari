#version 440
// V1: a player frame kept as Y'CbCr planes (ycbcr_node.h). The texture
// coordinates are normalized over the frame; ycbcr.frag converts.

layout(location = 0) in vec4 qt_VertexPosition;
layout(location = 1) in vec2 qt_VertexTexCoord;

layout(location = 0) out vec2 texCoord;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 coefficients; // cy, rv, gu, gv
    vec4 offsets;      // bu, yOffset, round, 1 / 2^shift
    vec4 sizes;        // width, height, chroma width, chroma height
    vec4 shape;        // interleaved, full-height chroma
};

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    texCoord = qt_VertexTexCoord;
    gl_Position = qt_Matrix * qt_VertexPosition;
}
