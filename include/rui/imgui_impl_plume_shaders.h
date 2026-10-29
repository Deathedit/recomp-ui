// The overlay's two shaders (imgui_impl_plume.cpp), HLSL only. A host with
// a precompiled shader cache can compile the same strings ahead of time
// (Hlsl(VERTEX_SHADER), Hlsl(PIXEL_SHADER): what compileShader is given)
// and answer compileShader from it.
#pragma once

#include <string>
#include <string_view>

namespace ImGui_ImplPlume_Shaders
{
    // Texture descriptors in space0, the (immutable) sampler in space1 (plume puts set i
    // in register space i; SPIR-V gets set i, binding 0).
    constexpr std::string_view SHADER_COMMON = R"(
struct PushConstants
{
    float2 scale;
    float2 translate;
    uint textureIndex;
};
#ifdef __spirv__
[[vk::push_constant]] ConstantBuffer<PushConstants> g_Push;
#else
ConstantBuffer<PushConstants> g_Push : register(b0, space2);
#endif
Texture2D<float4> g_Textures[] : register(t0, space0);
SamplerState g_Sampler : register(s0, space1);

struct Interpolators
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 texCoord : TEXCOORD0;
};
)";

    constexpr std::string_view VERTEX_SHADER = R"(
struct Vertex
{
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float2 texCoord : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
};

Interpolators main(Vertex input)
{
    Interpolators output;
    output.position = float4(input.position * g_Push.scale + g_Push.translate, 0.0, 1.0);
    output.color = input.color;
    output.texCoord = input.texCoord;
    return output;
}
)";

    constexpr std::string_view PIXEL_SHADER = R"(
float4 main(Interpolators input) : SV_Target
{
    return input.color * g_Textures[g_Push.textureIndex].Sample(g_Sampler, input.texCoord);
}
)";

    // What the backend hands to its compileShader: the common part, then
    // the shader.
    inline std::string Hlsl(std::string_view source)
    {
        std::string hlsl(SHADER_COMMON);
        hlsl += source;
        return hlsl;
    }
}
