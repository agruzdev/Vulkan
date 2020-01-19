#version 460
#extension GL_NV_ray_tracing : require

layout(set = 0, binding = 0) uniform accelerationStructureNV Scene;

layout(set = 0, binding = 2) buffer VertexBuffer {
    restrict readonly vec4 vertexes[];
};

layout(set = 0, binding = 4) uniform CameraProperties
{
    mat4 viewInverse;
    mat4 projInverse;
} camera;

layout(location = 0) rayPayloadInNV vec3 ResultColor;

layout(location = 1) rayPayloadNV bool isShadowed;

hitAttributeNV vec3 hitColor;


const vec3 Light0  = vec3(500.0, -1000.0, 500.0); // modelview
const vec3 Ambient = vec3(0.3);
const vec3 Color   = vec3(0.7);

float CastShadowRay(vec3 pos, vec3 L)
{
    uint flags = gl_RayFlagsTerminateOnFirstHitNV | gl_RayFlagsOpaqueNV | gl_RayFlagsSkipClosestHitShaderNV;
    const uint cullMask = 0xFF;
    const uint sbtRecordOffset = 0;
    const uint sbtRecordStride = 0;
    const uint missIndex = 1;
    float tMin = 0.001f;
    float tMax = length(L - pos) + 0.002f;
    vec3 dir = normalize(L - pos);
    isShadowed = true;
    traceNV(Scene, flags, cullMask, sbtRecordOffset, sbtRecordStride, missIndex, pos, tMin, dir, tMax, 1);

    float s = 1.0f;

    if (isShadowed) {
        s = 0.0f;
    }

    return s;
}

void main()
{
    vec3 P = gl_WorldRayOriginNV + gl_WorldRayDirectionNV * gl_HitTNV;

    vec3 L0 = (camera.viewInverse * vec4(Light0, 1.0f)).xyz;

    float shadow = CastShadowRay(P, L0);

    ResultColor = (Ambient + shadow * Color) * hitColor;
}
