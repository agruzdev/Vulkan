// Copyright 2016 Intel Corporation All Rights Reserved
// 
// Intel makes no representations about the suitability of this software for any purpose.
// THIS SOFTWARE IS PROVIDED ""AS IS."" INTEL SPECIFICALLY DISCLAIMS ALL WARRANTIES,
// EXPRESS OR IMPLIED, AND ALL LIABILITY, INCLUDING CONSEQUENTIAL AND OTHER INDIRECT DAMAGES,
// FOR THE USE OF THIS SOFTWARE, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PROPRIETARY
// RIGHTS, AND INCLUDING THE WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
// Intel does not assume any responsibility for any errors which may appear in this software
// nor any responsibility to update it.

#version 450

layout(set = 0, binding = 0) uniform uniformBuffer {
    mat4 modelView;
    mat4 projection;
} matrixes;

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
    outPosition = matrixes.modelView * inPosition;
    gl_Position = matrixes.projection * outPosition;
}
