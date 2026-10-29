#version 440

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    // MUST KILL: coefficients packs cy, rv, gu, gv
    vec4 coefficients;
    // MUST KILL: offsets packs bu, yOffset, round, 1 / 2^shift
    vec4 offsets;
    // MUST KILL: sizes packs width, height, chroma width, chroma height
    vec4 sizes;
    // MUST KILL: shape packs the interleaved and full-height-chroma flags
    vec4 shape;
};

layout(binding = 1) uniform sampler2D luma;
// MUST KILL: chromaFirst holds U, or interleaved U and V pairs when shape.x is set
layout(binding = 2) uniform sampler2D chromaFirst;
layout(binding = 3) uniform sampler2D chromaSecond;

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
    vec2 at = clamp(floor(texCoord * sizes.xy), vec2(0.0), sizes.xy - 1.0);
    fragColor = vec4(converted(at), 1.0) * qt_Opacity;
}
