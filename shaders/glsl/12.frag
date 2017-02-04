// Vulkan samples
// The MIT License (MIT)
// Copyright (c) 2016 Alexey Gruzdev

#version 450

layout(location = 0) in  vec4 inPosition;
layout(location = 1) in  vec4 inNormal;
layout(location = 2) in  vec2 inTexCoord;

layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 1) uniform sampler2D texSampler;

layout(push_constant) uniform AnimationState {
    vec2 frameSize;
    vec2 frameOffset;
} animation;

void main() {
    vec2 frameCoord = vec2(inTexCoord.x * animation.frameSize.x, inTexCoord.y * animation.frameSize.y + 1.0 - animation.frameSize.y);
    frameCoord += vec2(1.0, -1.0) * animation.frameOffset * animation.frameSize;

    fragColor = vec4(texture(texSampler, frameCoord).rgb, 1.0);
}
