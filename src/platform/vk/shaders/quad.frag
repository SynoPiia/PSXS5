// PSXS5 v2 - samples the rectangle's picture.
// SPDX-License-Identifier: GPL-3.0-or-later
// Compiled into shaders_spv.h by tools/make-shaders.sh.
#version 450

layout(set = 0, binding = 0) uniform sampler2D picture;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    out_color = texture(picture, in_uv);
}
