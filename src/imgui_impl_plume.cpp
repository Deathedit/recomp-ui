#include <rui/imgui_impl_plume.h>
#include <rui/imgui_impl_plume_shaders.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>

namespace
{
    // Texture descriptors (one boundless array); an ImTextureID is an index
    // into it. Index 0 stays empty: it is ImTextureID_Invalid.
    constexpr uint32_t TEXTURE_SLOTS = 256;

    // D3D12 wants buffer-to-texture copies at 512-byte offsets with rows of a
    // multiple of 256 bytes.
    constexpr uint64_t STAGING_ALIGNMENT = 512;
    constexpr uint32_t ROW_ALIGNMENT = 256;

    // Push constants: b0 space2 on D3D12 (after the two descriptor sets).
    struct PushConstants
    {
        float scale[2];
        float translate[2];
        uint32_t textureIndex;
    };

    using namespace ImGui_ImplPlume_Shaders;

    struct Texture
    {
        std::unique_ptr<plume::RenderTexture> texture;
        std::unique_ptr<plume::RenderTextureView> view;
        uint32_t slot = 0;
    };

    // What one frame in flight writes: its geometry and its texture uploads,
    // reused once the host has waited for that frame.
    struct Frame
    {
        std::unique_ptr<plume::RenderBuffer> vertices, indices, staging;
        void *vertexMemory = nullptr, *indexMemory = nullptr;
        uint8_t* stagingMemory = nullptr;
        uint64_t vertexCapacity = 0, indexCapacity = 0, stagingCapacity = 0, stagingUsed = 0;
        // Staging buffers outgrown during the frame, freed with it.
        std::vector<std::unique_ptr<plume::RenderBuffer>> retired;
    };

    struct Backend
    {
        ImGui_ImplPlume_InitInfo info;
        std::unique_ptr<plume::RenderPipelineLayout> layout;
        std::unique_ptr<plume::RenderDescriptorSet> textureSet, samplerSet;
        std::unique_ptr<plume::RenderSampler> sampler;
        std::unique_ptr<plume::RenderShader> vertexShader, pixelShader;
        std::unique_ptr<plume::RenderPipeline> pipeline;
        std::vector<Frame> frames;
        uint32_t frame = 0;
        std::vector<uint32_t> freeSlots;
    };

    Backend* GetBackend()
    {
        return ImGui::GetCurrentContext() != nullptr ? static_cast<Backend*>(ImGui::GetIO().BackendRendererUserData) : nullptr;
    }

    std::unique_ptr<plume::RenderShader> CreateShader(Backend& backend, std::string_view source, ImGui_ImplPlume_ShaderStage stage)
    {
        const std::string hlsl = Hlsl(source);
        std::vector<uint8_t> code;
        if (!backend.info.compileShader || !backend.info.compileShader(hlsl, stage, code) || code.empty())
            return nullptr;
        return backend.info.device->createShader(code.data(), code.size(), "main", backend.info.shaderFormat);
    }

    // `size` bytes of this frame's staging memory, at an aligned offset.
    plume::RenderBufferReference Stage(Backend& backend, uint64_t size, uint8_t*& cpu)
    {
        Frame& frame = backend.frames[backend.frame];
        uint64_t offset = (frame.stagingUsed + STAGING_ALIGNMENT - 1) / STAGING_ALIGNMENT * STAGING_ALIGNMENT;
        if (frame.staging == nullptr || offset + size > frame.stagingCapacity)
        {
            if (frame.staging != nullptr)
                frame.retired.push_back(std::move(frame.staging));
            frame.stagingCapacity = std::max<uint64_t>({ size, frame.stagingCapacity * 2, 1ull << 20 });
            frame.staging = backend.info.device->createBuffer(plume::RenderBufferDesc::UploadBuffer(frame.stagingCapacity));
            frame.stagingMemory = static_cast<uint8_t*>(frame.staging->map());
            offset = 0;
        }
        frame.stagingUsed = offset + size;
        cpu = frame.stagingMemory + offset;
        return plume::RenderBufferReference(frame.staging.get(), offset);
    }

    // Copies a rectangle of the texture's pixels from ImGui's copy to the GPU.
    void Upload(Backend& backend, plume::RenderCommandList* list, ImTextureData* tex, const Texture& texture, int x, int y, int width, int height)
    {
        const uint32_t pitch = (uint32_t(width) * 4 + ROW_ALIGNMENT - 1) / ROW_ALIGNMENT * ROW_ALIGNMENT;
        uint8_t* cpu = nullptr;
        const plume::RenderBufferReference staging = Stage(backend, uint64_t(pitch) * height, cpu);
        for (int row = 0; row < height; row++)
            memcpy(cpu + size_t(row) * pitch, tex->GetPixelsAt(x, y + row), size_t(width) * 4);

        list->barriers(plume::RenderBarrierStage::COPY, plume::RenderTextureBarrier(texture.texture.get(), plume::RenderTextureLayout::COPY_DEST));
        list->copyTextureRegion(plume::RenderTextureCopyLocation::Subresource(texture.texture.get()),
            plume::RenderTextureCopyLocation::PlacedFootprint(staging.ref, plume::RenderFormat::R8G8B8A8_UNORM, uint32_t(width), uint32_t(height), 1,
                pitch / 4, staging.offset),
            uint32_t(x), uint32_t(y), 0);
        list->barriers(plume::RenderBarrierStage::GRAPHICS, plume::RenderTextureBarrier(texture.texture.get(), plume::RenderTextureLayout::SHADER_READ));
    }

    void DestroyTexture(Backend& backend, ImTextureData* tex)
    {
        if (auto* texture = static_cast<Texture*>(tex->BackendUserData))
        {
            backend.freeSlots.push_back(texture->slot);
            IM_DELETE(texture);
        }
        tex->BackendUserData = nullptr;
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }

    void UpdateTexture(Backend& backend, plume::RenderCommandList* list, ImTextureData* tex)
    {
        if (tex->Status == ImTextureStatus_WantCreate)
        {
            IM_ASSERT(tex->TexID == ImTextureID_Invalid && tex->BackendUserData == nullptr);
            IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);
            if (backend.freeSlots.empty())
                return;  // tried again next frame
            auto* texture = IM_NEW(Texture)();
            texture->slot = backend.freeSlots.back();
            backend.freeSlots.pop_back();
            texture->texture = backend.info.device->createTexture(
                plume::RenderTextureDesc::Texture2D(uint32_t(tex->Width), uint32_t(tex->Height), 1, plume::RenderFormat::R8G8B8A8_UNORM));
            texture->view = texture->texture->createTextureView(plume::RenderTextureViewDesc::Texture2D(plume::RenderFormat::R8G8B8A8_UNORM));
            backend.textureSet->setTexture(texture->slot, texture->texture.get(), plume::RenderTextureLayout::SHADER_READ, texture->view.get());
            Upload(backend, list, tex, *texture, 0, 0, tex->Width, tex->Height);
            tex->BackendUserData = texture;
            tex->SetTexID(ImTextureID(texture->slot));
            tex->SetStatus(ImTextureStatus_OK);
        }
        else if (tex->Status == ImTextureStatus_WantUpdates)
        {
            // ImGui only writes to parts no frame has drawn with yet, so the
            // frame in flight can keep reading the rest.
            const Texture& texture = *static_cast<Texture*>(tex->BackendUserData);
            const ImTextureRect& rect = tex->UpdateRect;
            Upload(backend, list, tex, texture, rect.x, rect.y, rect.w, rect.h);
            tex->SetStatus(ImTextureStatus_OK);
        }
        else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames >= int(backend.info.framesInFlight))
        {
            // No frame still on the GPU draws with it.
            DestroyTexture(backend, tex);
        }
    }

    // Grows a frame's vertex or index buffer to hold `size` bytes.
    void Reserve(Backend& backend, std::unique_ptr<plume::RenderBuffer>& buffer, void*& memory, uint64_t& capacity, uint64_t size,
        plume::RenderBufferFlags flags)
    {
        if (buffer != nullptr && capacity >= size)
            return;
        capacity = std::max<uint64_t>(size + size / 2, 64 << 10);
        buffer = backend.info.device->createBuffer(plume::RenderBufferDesc::UploadBuffer(capacity, flags));
        memory = buffer->map();
    }

    void SetupRenderState(Backend& backend, ImDrawData* drawData, plume::RenderCommandList* list, const Frame& frame, float width, float height)
    {
        list->setGraphicsPipelineLayout(backend.layout.get());
        list->setPipeline(backend.pipeline.get());
        list->setGraphicsDescriptorSet(backend.textureSet.get(), 0);
        // The immutable sampler's set: on D3D12 the sampler is in the root
        // signature and the set has no heap entries (binding it crashes
        // plume); Vulkan wants the set the shader uses bound.
        if (backend.info.shaderFormat == plume::RenderShaderFormat::SPIRV)
            list->setGraphicsDescriptorSet(backend.samplerSet.get(), 1);

        const plume::RenderInputSlot slot(0, sizeof(ImDrawVert));
        const plume::RenderVertexBufferView vertices(plume::RenderBufferReference(frame.vertices.get()),
            uint32_t(drawData->TotalVtxCount * sizeof(ImDrawVert)));
        list->setVertexBuffers(0, &vertices, 1, &slot);
        const plume::RenderIndexBufferView indices(plume::RenderBufferReference(frame.indices.get()), uint32_t(drawData->TotalIdxCount * sizeof(ImDrawIdx)),
            sizeof(ImDrawIdx) == 2 ? plume::RenderFormat::R16_UINT : plume::RenderFormat::R32_UINT);
        list->setIndexBuffer(&indices);
        list->setViewports(plume::RenderViewport(0.0f, 0.0f, width, height));
    }
}

bool ImGui_ImplPlume_Init(const ImGui_ImplPlume_InitInfo& info)
{
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");
    IM_ASSERT(info.device != nullptr && info.framesInFlight >= 1);

    auto* backend = IM_NEW(Backend)();
    backend->info = info;
    backend->frames.resize(info.framesInFlight);
    for (uint32_t slot = TEXTURE_SLOTS - 1; slot >= 1; slot--)
        backend->freeSlots.push_back(slot);

    plume::RenderDevice* device = info.device;
    // The sampler is immutable (built into the pipeline layout): on D3D12 it
    // takes nothing from the shared sampler heap, which a host's own bindless
    // samplers may fill to the last entry.
    plume::RenderSamplerDesc linear;
    linear.mipmapMode = plume::RenderMipmapMode::NEAREST;
    linear.addressU = linear.addressV = linear.addressW = plume::RenderTextureAddressMode::CLAMP;
    backend->sampler = device->createSampler(linear);
    if (backend->sampler == nullptr)
    {
        IM_DELETE(backend);
        return false;
    }
    plume::RenderDescriptorSetBuilder textures, samplers;
    textures.begin();
    textures.addTexture(0, TEXTURE_SLOTS);
    textures.end(true, TEXTURE_SLOTS);
    samplers.begin();
    samplers.addImmutableSampler(0, backend->sampler.get());
    samplers.end();

    plume::RenderPipelineLayoutBuilder layout;
    layout.begin(false, true);
    layout.addPushConstant(0, 2, sizeof(PushConstants), plume::RenderShaderStageFlag::VERTEX | plume::RenderShaderStageFlag::PIXEL);
    layout.addDescriptorSet(textures);
    layout.addDescriptorSet(samplers);
    layout.end();
    backend->layout = layout.create(device);
    backend->textureSet = textures.create(device);
    backend->samplerSet = samplers.create(device);

    backend->vertexShader = CreateShader(*backend, VERTEX_SHADER, ImGui_ImplPlume_ShaderStage::Vertex);
    backend->pixelShader = CreateShader(*backend, PIXEL_SHADER, ImGui_ImplPlume_ShaderStage::Pixel);
    if (backend->layout != nullptr && backend->vertexShader != nullptr && backend->pixelShader != nullptr)
    {
        const plume::RenderInputSlot slot(0, sizeof(ImDrawVert));
        const plume::RenderInputElement elements[] = {
            { "POSITION", 0, 0, plume::RenderFormat::R32G32_FLOAT, 0, uint32_t(offsetof(ImDrawVert, pos)) },
            { "TEXCOORD", 0, 1, plume::RenderFormat::R32G32_FLOAT, 0, uint32_t(offsetof(ImDrawVert, uv)) },
            { "COLOR", 0, 2, plume::RenderFormat::R8G8B8A8_UNORM, 0, uint32_t(offsetof(ImDrawVert, col)) },
        };
        plume::RenderGraphicsPipelineDesc desc;
        desc.pipelineLayout = backend->layout.get();
        desc.vertexShader = backend->vertexShader.get();
        desc.pixelShader = backend->pixelShader.get();
        desc.renderTargetFormat[0] = info.renderTargetFormat;
        desc.renderTargetBlend[0] = plume::RenderBlendDesc::AlphaBlend();
        desc.renderTargetCount = 1;
        desc.inputSlots = &slot;
        desc.inputSlotsCount = 1;
        desc.inputElements = elements;
        desc.inputElementsCount = uint32_t(std::size(elements));
        backend->pipeline = device->createGraphicsPipeline(desc);
    }
    if (backend->pipeline == nullptr || backend->textureSet == nullptr || backend->samplerSet == nullptr)
    {
        IM_DELETE(backend);
        return false;
    }

    io.BackendRendererUserData = backend;
    io.BackendRendererName = "imgui_impl_plume";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    platform.Renderer_TextureMaxWidth = platform.Renderer_TextureMaxHeight = 16384;
    return true;
}

void ImGui_ImplPlume_Shutdown()
{
    Backend* backend = GetBackend();
    IM_ASSERT(backend != nullptr && "No renderer backend to shutdown, or already shutdown?");
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
    {
        if (tex->RefCount == 1)
            DestroyTexture(*backend, tex);
    }
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    IM_DELETE(backend);
}

void ImGui_ImplPlume_NewFrame()
{
    IM_ASSERT(GetBackend() != nullptr && "Did you call ImGui_ImplPlume_Init()?");
}

void ImGui_ImplPlume_UpdateTextures(ImDrawData* drawData, plume::RenderCommandList* list)
{
    Backend& backend = *GetBackend();
    // This frame's buffers: the host waited for the frame that used them.
    backend.frame = (backend.frame + 1) % backend.info.framesInFlight;
    Frame& frame = backend.frames[backend.frame];
    frame.retired.clear();
    frame.stagingUsed = 0;

    if (drawData->Textures != nullptr)
    {
        for (ImTextureData* tex : *drawData->Textures)
        {
            if (tex->Status != ImTextureStatus_OK)
                UpdateTexture(backend, list, tex);
        }
    }
}

void ImGui_ImplPlume_RenderDrawData(ImDrawData* drawData, plume::RenderCommandList* list, const plume::RenderFramebuffer* framebuffer)
{
    Backend& backend = *GetBackend();
    const float width = drawData->DisplaySize.x * drawData->FramebufferScale.x;
    const float height = drawData->DisplaySize.y * drawData->FramebufferScale.y;
    if (width <= 0.0f || height <= 0.0f || drawData->TotalVtxCount == 0)
        return;
    Frame& frame = backend.frames[backend.frame];

    Reserve(backend, frame.vertices, frame.vertexMemory, frame.vertexCapacity, drawData->TotalVtxCount * sizeof(ImDrawVert),
        plume::RenderBufferFlag::VERTEX);
    Reserve(backend, frame.indices, frame.indexMemory, frame.indexCapacity, drawData->TotalIdxCount * sizeof(ImDrawIdx), plume::RenderBufferFlag::INDEX);
    auto* vertices = static_cast<ImDrawVert*>(frame.vertexMemory);
    auto* indices = static_cast<ImDrawIdx*>(frame.indexMemory);
    for (const ImDrawList* drawList : drawData->CmdLists)
    {
        memcpy(vertices, drawList->VtxBuffer.Data, drawList->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(indices, drawList->IdxBuffer.Data, drawList->IdxBuffer.Size * sizeof(ImDrawIdx));
        vertices += drawList->VtxBuffer.Size;
        indices += drawList->IdxBuffer.Size;
    }

    list->setFramebuffer(framebuffer);
    SetupRenderState(backend, drawData, list, frame, width, height);

    // ImGui's coordinates (DisplayPos at the top left) to clip space.
    PushConstants constants;
    constants.scale[0] = 2.0f / drawData->DisplaySize.x;
    constants.scale[1] = -2.0f / drawData->DisplaySize.y;
    constants.translate[0] = -1.0f - drawData->DisplayPos.x * constants.scale[0];
    constants.translate[1] = 1.0f - drawData->DisplayPos.y * constants.scale[1];
    constants.textureIndex = 0;
    ImTextureID boundTexture = ImTextureID_Invalid;

    const ImVec2 clipOffset = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;
    uint32_t vertexOffset = 0, indexOffset = 0;
    for (const ImDrawList* drawList : drawData->CmdLists)
    {
        for (const ImDrawCmd& cmd : drawList->CmdBuffer)
        {
            if (cmd.UserCallback != nullptr)
            {
                if (cmd.UserCallback == ImDrawCallback_ResetRenderState)
                {
                    SetupRenderState(backend, drawData, list, frame, width, height);
                    boundTexture = ImTextureID_Invalid;
                }
                else
                    cmd.UserCallback(drawList, &cmd);
                continue;
            }

            const float left = std::max((cmd.ClipRect.x - clipOffset.x) * clipScale.x, 0.0f);
            const float top = std::max((cmd.ClipRect.y - clipOffset.y) * clipScale.y, 0.0f);
            const float right = std::min((cmd.ClipRect.z - clipOffset.x) * clipScale.x, width);
            const float bottom = std::min((cmd.ClipRect.w - clipOffset.y) * clipScale.y, height);
            if (right <= left || bottom <= top)
                continue;
            list->setScissors(plume::RenderRect(int32_t(left), int32_t(top), int32_t(right), int32_t(bottom)));

            const ImTextureID texture = cmd.GetTexID();
            if (texture != boundTexture)
            {
                constants.textureIndex = uint32_t(texture);
                list->setGraphicsPushConstants(0, &constants, 0, sizeof(constants));
                boundTexture = texture;
            }
            list->drawIndexedInstanced(cmd.ElemCount, 1, indexOffset + cmd.IdxOffset, int32_t(vertexOffset + cmd.VtxOffset), 0);
        }
        vertexOffset += uint32_t(drawList->VtxBuffer.Size);
        indexOffset += uint32_t(drawList->IdxBuffer.Size);
    }
}
