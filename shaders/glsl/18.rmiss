#version 460
#extension GL_NV_ray_tracing : require

// https://github.com/iOrange/rtxON/blob/happy_triangle_fixed/src/shaders/ray_miss.glsl
//

layout(location = 0) rayPayloadInNV vec3 ResultColor;

void main()
{
    ResultColor = vec3(0.0f, 0.0f, 0.0f);
}
