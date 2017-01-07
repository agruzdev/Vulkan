// Vulkan samples
// The MIT License (MIT)
// Copyright (c) 2016 Alexey Gruzdev

#version 450

layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inNormal;

out gl_PerVertex
{
  vec4 gl_Position;
};

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormal;

void main() {
    outNormal = inNormal;
    outPosition = inPosition;
    gl_Position = inPosition;
}
