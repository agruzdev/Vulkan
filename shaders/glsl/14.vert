// Vulkan samples
// The MIT License (MIT)
// Copyright (c) 2016 Alexey Gruzdev

#version 450

layout(set = 0, binding = 0) uniform uniformBuffer {
    mat4 modelView;
    mat4 projection;
    mat4 texTransform;
} matrixes;

layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 4) out VertexData {
    vec4 position;
    vec4 normal;
    vec2 texCoord;
} outVertex;

void main() {
    mat4 normalMatrix  = transpose(inverse(matrixes.modelView));
    outVertex.normal   = normalize(normalMatrix * inNormal);
    outVertex.position = matrixes.modelView * inPosition;
    outVertex.texCoord = (vec4(inTexCoord, 0.0, 1.0) * matrixes.texTransform).st;
    gl_Position = matrixes.projection * outVertex.position;
}
