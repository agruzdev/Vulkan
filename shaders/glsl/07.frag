// Vulkan samples
// The MIT License (MIT)
// Copyright (c) 2016 Alexey Gruzdev

#version 450

layout(location = 0) in  vec4 inPosition;
layout(location = 1) in  vec4 inNormal;
layout(location = 0) out vec4 fragColor;

void main() {
    const vec3 Ambient = vec3(0.1);
    const vec3 Light   = vec3(20.0, -100.0, 50.0); // Screen space for now
    const vec3 Color   = vec3(1.0);

    const vec3 lightDir = normalize(Light - vec3(inPosition));
    const float diffuse = clamp(dot(lightDir, vec3(inNormal)), 0.0, 1.0);

    const vec3 rgb = Ambient + 0.8 * diffuse * Color;
    fragColor = vec4(rgb, 1.0);
}
