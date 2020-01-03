#version 460
#extension GL_NV_ray_tracing : require

// https://github.com/iOrange/rtxON/blob/happy_triangle_fixed/src/shaders/ray_chit.glsl
//

layout(location = 0) rayPayloadInNV vec3 ResultColor;
hitAttributeNV vec2 HitAttribs;

layout(set = 0, binding = 2) buffer VertexBuffer {
    restrict readonly vec4 vertexes[];
};

layout(set = 0, binding = 3) buffer IndexBuffer {
    restrict readonly uint indexes[];
};

layout(set = 0, binding = 4) uniform CameraProperties
{
    mat4 viewInverse;
    mat4 projInverse;
} camera;

const vec3 Light0 = vec3(20.0, -100.0, 50.0); // modelview
const vec3 Ambient = vec3(0.3);
const vec3 Color = vec3(1.0);

void main()
{
    ivec3 ind = ivec3(indexes[3 * gl_PrimitiveID], indexes[3 * gl_PrimitiveID + 1], indexes[3 * gl_PrimitiveID + 2]);

    vec3 pos0 = vertexes[2 * ind.x].xyz;
    vec3 pos1 = vertexes[2 * ind.y].xyz;
    vec3 pos2 = vertexes[2 * ind.z].xyz;

    vec3 norm0 = vertexes[2 * ind.x + 1].xyz;
    vec3 norm1 = vertexes[2 * ind.y + 1].xyz;
    vec3 norm2 = vertexes[2 * ind.z + 1].xyz;

    const vec3 barycentrics = vec3(1.0f - HitAttribs.x - HitAttribs.y, HitAttribs.x, HitAttribs.y);

    vec3 position = (pos0 * barycentrics.x + pos1 * barycentrics.y + pos2 * barycentrics.z);
    vec3 normal = normalize(norm0 * barycentrics.x + norm1 * barycentrics.y + norm2 * barycentrics.z);

    const vec3 lightPos = (camera.viewInverse * vec4(Light0, 1.0f)).xyz;
    const vec3 lightDir = normalize(lightPos - vec3(position));
    const float diffuse = clamp(dot(lightDir, vec3(normal)), 0.0, 1.0);

    ResultColor = vec3(clamp(Ambient + diffuse * Color, 0.0, 1.0));
}
