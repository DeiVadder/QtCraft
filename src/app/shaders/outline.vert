// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec3 position;

layout(std140, binding = 0) uniform Frame {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 fogColor;
    vec4 params;
} frame;

void main()
{
    gl_Position = frame.viewProjection * vec4(position, 1.0);
}
