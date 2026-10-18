#pragma once

#include "kke/rhi/CommandList.h"

#include <cstdint>
#include <memory>

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

// Makes the GPU resources a backend draws with.
class Device {
public:
    virtual ~Device() = default;
    // `initial` (desc.size bytes) fills the buffer when it is made; Device
    // memory without it starts undefined (for buffers the GPU writes).
    virtual std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc, const void* initial = nullptr) = 0;
};

} // namespace kke::rhi
