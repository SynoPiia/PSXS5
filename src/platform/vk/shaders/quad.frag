// PSXS5 v2 - samples the rectangle's picture.
// SPDX-License-Identifier: GPL-3.0-or-later
// Compiled into shaders_spv.h by tools/make-shaders.sh.
#version 450

layout(set = 0, binding = 0) uniform sampler2D picture;
layout(push_constant) uniform Quad
{
    vec4 dst, uv, info, colour;
} quad;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

// the Display settings' brightness, saturation and warmth (1, 1, 0: unchanged)
vec3 grade(vec3 c)
{
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, quad.colour.y) * quad.colour.x;
    return clamp(c * vec3(1.0 + quad.colour.z, 1.0, 1.0 - quad.colour.z), 0.0, 1.0);
}

void main()
{
    vec4 c = texture(picture, in_uv);
    out_color = vec4(grade(c.rgb), c.a); // the interface passes 1, 1, 0
}
