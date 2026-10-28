#version 440
// V1: Y'CbCr planes to R'G'B' with the CPU conversion's fixed-point formula
// (hikari/application/yuv_bgra.h), so each texel is the byte the CPU path
// makes: the samples are read back as integers, the products and sums are
// integers well inside a float's exact range, and the shift is a division by
// a power of two. Chroma is not interpolated: pixel (x, y) takes sample
// (x / 2, y / 2), or (x / 2, y) for 4:2:2. Each fragment takes its nearest
// texel, as the BGRA frame's texture node does.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 coefficients; // cy, rv, gu, gv
    vec4 offsets;      // bu, yOffset, round, 1 / 2^shift
    vec4 sizes;        // width, height, chroma width, chroma height
    vec4 shape;        // interleaved, full-height chroma
};

layout(binding = 1) uniform sampler2D luma;
layout(binding = 2) uniform sampler2D chromaFirst;  // U, or U and V pairs (interleaved)
layout(binding = 3) uniform sampler2D chromaSecond; // V (planar)

float sample8(float normalized)
{
    return floor(normalized * 255.0 + 0.5);
}

vec3 converted(vec2 at)
{
    float y = sample8(texture(luma, (at + 0.5) / sizes.xy).r);
    vec2 c = vec2(floor(at.x * 0.5), shape.y > 0.5 ? at.y : floor(at.y * 0.5));
    vec2 cc = (c + 0.5) / sizes.zw;
    float u, v;
    if (shape.x > 0.5) {
        vec2 pair = texture(chromaFirst, cc).rg;
        u = sample8(pair.r);
        v = sample8(pair.g);
    } else {
        u = sample8(texture(chromaFirst, cc).r);
        v = sample8(texture(chromaSecond, cc).r);
    }
    float l = coefficients.x * (y - offsets.y);
    float d = u - 128.0, e = v - 128.0;
    float r = floor((l + coefficients.y * e + offsets.z) * offsets.w);
    float g = floor((l + coefficients.z * d + coefficients.w * e + offsets.z) * offsets.w);
    float b = floor((l + offsets.x * d + offsets.z) * offsets.w);
    return clamp(vec3(r, g, b), 0.0, 255.0) / 255.0;
}

void main()
{
    // Nearest, as the BGRA frame's texture node samples (QSGSimpleTextureNode).
    vec2 at = clamp(floor(texCoord * sizes.xy), vec2(0.0), sizes.xy - 1.0);
    fragColor = vec4(converted(at), 1.0) * qt_Opacity;
}
