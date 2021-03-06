// Vulkan samples
// The MIT License (MIT)
// Copyright (c) 2016 Alexey Gruzdev

#version 450

layout(location = 0) in  vec4 inPosition;
layout(location = 1) in  vec4 inNormal;
layout(location = 0) out vec4 fragColor;

const vec3 Light0 = vec3(20.0, -100.0, 50.0); // modelview

void main() {
    const vec3 Ambient = vec3(0.2, 0.0, 0.0);
    const vec3 Color   = vec3(1.0, 0.0, 0.0);

    const vec3 lightDir = normalize(Light0 - vec3(inPosition));
    const float diffuse = clamp(dot(lightDir, vec3(inNormal)), 0.0, 1.0);

    const vec3 rgb = Ambient + diffuse * Color;
    fragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
