#pragma once

#include "kke/rhi/CommandList.h"
#include "kke/rhi/GraphicsPipeline.h"

#include <cstdint>
#include <memory>
#include <span>

namespace kke::rhi {

// What a buffer is used for, any combination (docs/plans/rhi.md, R1c).
enum BufferUsage : uint32_t { kVertexBuffer = 1, kIndexBuffer = 2, kUniformBuffer = 4, kStorageBuffer = 8 };

// Where a buffer lives: Upload is CPU-written memory the GPU reads (data
// that changes every frame); Device is GPU memory, filled once when made.
enum class Memory { Upload, Device };

struct BufferDesc {
    uint64_t size = 0;
    uint32_t usage = 0; // BufferUsage bits
    Memory memory = Memory::Upload;
};

// A GPU buffer made by the Device; a draw names it by ref().
class Buffer {
public:
    virtual ~Buffer() = default;
    virtual BufferRef ref() const = 0;
    virtual uint64_t size() const = 0;
    // Writes `bytes` bytes at `offset` (Upload memory only).
    virtual void upload(const void* data, uint64_t bytes, uint64_t offset = 0) = 0;
};

// What one binding of a bind group holds (R1d).
enum class BindingType : uint8_t { UniformBuffer, StorageBuffer, Texture };

struct BindingLayout {
    uint32_t binding = 0;
    BindingType type = BindingType::Texture;
    uint32_t stages = 0; // Stage bits
};

// The shape of a bind group: its bindings, as a pipeline expects them.
class BindGroupLayout {
public:
    virtual ~BindGroupLayout() = default;
};

// A sampled texture as a binding sees it: its view and its sampler (an
// rhi::Texture once the device makes textures).
struct TextureRef {
    uint64_t view = 0;
    uint64_t sampler = 0;
    bool computeWritten = false; // a compute pass writes it too (Vulkan reads it in the GENERAL layout)
};

// What to put at one binding: a buffer range or a texture.
struct BindingWrite {
    uint32_t binding = 0;
    BufferRef buffer{};
    uint64_t offset = 0;
    uint64_t range = 0; // 0: to the buffer's end
    TextureRef texture{};
};

// A sampled 2D texture's pixels (R1e): 8-bit colour (sRGB or linear data) or half-float light.
// BC7: 4x4 blocks of 16 bytes (kke/TextureCompress.h), only where the
// GPU samples them (VulkanDevice::bc7Supported).
enum class TextureFormat : uint8_t { RGBA8Srgb, RGBA8, RGBA16F, BC7Srgb, BC7 };
// How it repeats: Repeat both ways (a material), or around the horizon only (an equirectangular sky).
enum class Wrap : uint8_t { Repeat, RepeatAroundOnly };

struct TextureDesc {
    uint32_t width = 0, height = 0;
    uint32_t levels = 1; // mips, stored one after another, each half as wide and tall
    TextureFormat format = TextureFormat::RGBA8Srgb;
    Wrap wrap = Wrap::Repeat;
    bool blendLevels = true; // trilinear between mips (else the nearest one)
    bool anisotropic = false; // sharp at glancing angles, where the GPU can
};

// A texture made by the Device, with its sampler; a bind group names it by ref().
class Texture {
public:
    virtual ~Texture() = default;
    virtual TextureRef ref() const = 0;
};

// Makes the GPU resources a backend draws with.
class Device {
public:
    virtual ~Device() = default;
    // `initial` (desc.size bytes) fills the buffer when it is made; Device
    // memory without it starts undefined (for buffers the GPU writes).
    virtual std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc, const void* initial = nullptr) = 0;
    virtual std::unique_ptr<BindGroupLayout> createBindGroupLayout(std::span<const BindingLayout> bindings) = 0;
    // A bind group of `layout`, kept until the device goes; {0} when none can be made.
    virtual BindGroup createBindGroup(const BindGroupLayout& layout) = 0;
    // Points the group's bindings at buffers and textures (each binding's type from the layout).
    virtual void writeBindGroup(BindGroup group, const BindGroupLayout& layout, std::span<const BindingWrite> writes) = 0;
    // Gives a group back, once the GPU is done with it (its owner going away).
    virtual void releaseBindGroup(BindGroup group) = 0;
    // `pixels`: every level of desc, level 0 first.
    virtual std::unique_ptr<Texture> createTexture(const TextureDesc& desc, const void* pixels) = 0;
    // `spirv`: the shader's 32-bit words (R1h).
    virtual std::unique_ptr<ShaderModule> createShaderModule(std::span<const uint32_t> spirv) = 0;
    // A pipeline of the two shaders, the state and the bind group layouts in `desc`; null when none can be made.
    virtual std::unique_ptr<GraphicsPipeline> createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
};

} // namespace kke::rhi
