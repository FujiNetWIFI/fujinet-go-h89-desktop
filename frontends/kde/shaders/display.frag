#version 440

// Alpha is forced to 1: the session's pixels are 0x00RRGGBB, and a window
// composited with alpha 0 shows what is behind it (KWin does).
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Params {
    vec4 rect;
    vec4 misc;    // x: 1 = invert (the visual bell)
};
layout(binding = 1) uniform sampler2D tex;

void main()
{
    vec3 c = texture(tex, vUv).rgb;
    fragColor = vec4(misc.x > 0.5 ? vec3(1.0) - c : c, 1.0);
}
