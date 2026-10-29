// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#version 440

layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vColor;
}
