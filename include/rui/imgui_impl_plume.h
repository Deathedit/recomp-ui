// A Dear ImGui renderer backend for plume (D3D12 or Vulkan): one pipeline,
// one vertex and index buffer per frame in flight, textures created and
// updated as ImGui asks (ImGuiBackendFlags_RendererHasTextures, 1.92+), and a
// scissor rect per draw command.
//
// It records into the caller's command list and framebuffer and knows
// nothing of windows, swap chains or fences: the host draws the overlay
// last, after its own frame, into the image it is about to present.
//
// A frame: UpdateTextures (outside a render pass: its copies would end one),
// then RenderDrawData (inside the target's). Frames in flight: each
// UpdateTextures moves on to the next of `framesInFlight` sets of buffers, so
// by then the host must have waited for the frame recorded `framesInFlight`
// frames before.
#pragma once

#include <imgui.h>
#include <plume_render_interface.h>

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

enum class ImGui_ImplPlume_ShaderStage
{
    Vertex,
    Pixel,
};

struct ImGui_ImplPlume_InitInfo
{
    plume::RenderDevice* device = nullptr;
    // DXIL (D3D12) or SPIR-V (Vulkan).
    plume::RenderShaderFormat shaderFormat = plume::RenderShaderFormat::DXIL;
    // The format of the framebuffers RenderDrawData draws into.
    plume::RenderFormat renderTargetFormat = plume::RenderFormat::B8G8R8A8_UNORM;
    uint32_t framesInFlight = 2;
    // Compiles the backend's HLSL (entry point main, shader model 6.0) to
    // `shaderFormat`; for SPIR-V, vertex shaders flip y (DXC -fvk-invert-y).
    std::function<bool(std::string_view hlsl, ImGui_ImplPlume_ShaderStage stage, std::vector<uint8_t>& code)> compileShader;
};

// Needs an ImGui context. False when the pipeline couldn't be made.
bool ImGui_ImplPlume_Init(const ImGui_ImplPlume_InitInfo& info);
// The GPU must be done with every frame RenderDrawData recorded.
void ImGui_ImplPlume_Shutdown();
void ImGui_ImplPlume_NewFrame();
// Uploads the textures ImGui changed: once a frame, before RenderDrawData
// and outside a render pass.
void ImGui_ImplPlume_UpdateTextures(ImDrawData* drawData, plume::RenderCommandList* list);
// Draws `drawData` into `framebuffer` (in COLOR_WRITE layout; the caller
// transitions it), with alpha blending over what is there. Leaves the list
// with this backend's pipeline layout, pipeline, viewport and scissor bound.
void ImGui_ImplPlume_RenderDrawData(ImDrawData* drawData, plume::RenderCommandList* list, const plume::RenderFramebuffer* framebuffer);
