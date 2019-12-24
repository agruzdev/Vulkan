// Vulkan samples
// The MIT License (MIT)
// Copyright (c) 2016 Alexey Gruzdev

#version 450

layout(triangles) in;
layout(line_strip, max_vertices=256) out;
//layout(line_strip, max_vertices=32) out;

layout(set = 0, binding = 0) uniform uniformBuffer {
    mat4 modelView;
    mat4 projection;
} matrixes;

layout(set = 0, binding = 1) uniform sampler2D texSampler;

layout(location = 4) in VertexData {
    vec4 position;
    vec4 normal;
    vec2 texCoord;
} inVertex[3];

layout(location = 8) out VertexData {
    vec4 position;
    vec4 normal;
    vec2 texCoord;
} outVertex;

layout(push_constant) uniform PushConstants {
    vec2  seed;
    float steplen;
    float chldCount;
} pushconst;

const int PATH_LENGTH  = 3;
const int MAX_CHILDREN_NUM = 64;

float rand(vec2 co){
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

void main() {

    vec3 origin    = vec3(0.0);
    vec3 direction = vec3(0.0);
    vec2 texcoord  = vec2(0.0);

    for(int i = 0; i < gl_in.length(); i++) {
        origin += inVertex[i].position.xyz;
        direction += inVertex[i].normal.xyz;
        texcoord += inVertex[i].texCoord.xy;
    }

    origin    /= gl_in.length();
    direction /= gl_in.length();
    texcoord  /= gl_in.length();

    const vec4 randTex = texture(texSampler, texcoord).rgba;

    const float s0 = rand(randTex.ww - pushconst.seed) * 0.02;
    const float step = pushconst.steplen + s0;

    const vec3 parentTip = origin + (PATH_LENGTH * step) * direction;

    const vec3 e1 = cross(direction, normalize(inVertex[0].position.xyz - inVertex[1].position.xyz));
    const vec3 e2 = cross(e1, direction);

    for(int j = 0; j < min(pushconst.chldCount, MAX_CHILDREN_NUM); ++j) {
        float r0 = abs(rand(randTex.wy + pushconst.seed + vec2(j, 2.0 * j)));
        float r1 = abs(rand(randTex.xw + pushconst.seed + vec2(-j, j)));
        if(r0 + r1 > 1.0) {
            r0 = 1.0 - r0;
            r1 = 1.0 - r1;
        }
        float r2 = 1.0 - r0 - r1;

        float n1 = rand(randTex.yw + pushconst.seed + vec2(-2.0 * j, -j)) * 0.25;
        float n2 = rand(randTex.wx + pushconst.seed + vec2(4.3 * j, j)) * 0.25;

        vec3 childOrigin    = r0 * inVertex[0].position.xyz + r1 * inVertex[1].position.xyz + r2 * inVertex[2].position.xyz;
        vec3 childDirection = r0 * inVertex[0].normal.xyz   + r1 * inVertex[1].normal.xyz   + r2 * inVertex[2].normal.xyz;
        vec2 childTexCoord  = r0 * inVertex[0].texCoord     + r1 * inVertex[1].texCoord     + r2 * inVertex[2].texCoord;

        vec3 dirToParent = normalize(parentTip - childOrigin);
        childDirection = mix(dirToParent, childDirection, 0.9);

        childDirection = normalize(childDirection + n1 * e1 + n2 * e2);

        for(int i = 0; i <= PATH_LENGTH; ++i) {
            vec4 p = vec4(childOrigin + (i * step) * childDirection, 1.0);
            gl_Position = matrixes.projection * p;
            outVertex.position = p;
            outVertex.normal   = vec4(childDirection, float(PATH_LENGTH - i) / float(PATH_LENGTH));
            outVertex.texCoord = childTexCoord;
            EmitVertex();
        }
    }

    EndPrimitive();
}
