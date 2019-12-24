// Vulkan samples
// The MIT License (MIT)
// Copyright (c) 2016 Alexey Gruzdev

#version 450

layout(triangles) in;
layout(line_strip, max_vertices=3) out;

layout(set = 0, binding = 0) uniform uniformBuffer {
    mat4 modelView;
    mat4 projection;
} matrixes;


layout(location = 2) in VertexData {
    vec4 position;
    vec4 normal;
} inVertex[3];

layout(location = 4) out VertexData {
    vec4 position;
    vec4 normal;
} outVertex;

 void main() {
    for(int i = 0; i < gl_in.length(); i++) {
        // copy attributes
        gl_Position = matrixes.projection * (inVertex[i].position + 0.1 * inVertex[i].normal);
        outVertex.position = inVertex[i].position;
        outVertex.normal   = inVertex[i].normal;

        // done with the vertex
        EmitVertex();
    }
    EndPrimitive();
}
