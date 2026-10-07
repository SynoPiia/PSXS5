// PSXS5 - sharp bilinear: each source pixel stays a crisp block, and only the
// seams between blocks are blended, so uneven scaling doesn't shimmer.
// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

layout(set = 0, binding = 0) uniform sampler2D picture;
layout(push_constant) uniform Quad
{
    vec4 dst, uv, info; // info: texture width, height, then screen pixels per texel (x, y)
} quad;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    vec2 size = quad.info.xy;
    vec2 scale = max(quad.info.zw, vec2(1.0)); // screen pixels per texel
    vec2 texel = in_uv * size;
    vec2 base = floor(texel);
    vec2 offset = fract(texel) - 0.5;
    vec2 region = 0.5 - 0.5 / scale;
    vec2 f = (offset - clamp(offset, -region, region)) * scale + 0.5;
    out_color = vec4(texture(picture, (base + f) / size).rgb, 1.0);
}
