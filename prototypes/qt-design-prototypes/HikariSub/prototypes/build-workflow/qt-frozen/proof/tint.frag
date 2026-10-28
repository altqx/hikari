#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};
layout(binding = 1) uniform sampler2D source;

// White input becomes magenta: proves the compiled shader actually ran.
void main()
{
    vec4 c = texture(source, qt_TexCoord0);
    fragColor = vec4(c.r, 0.0, c.b, 1.0) * qt_Opacity;
}
