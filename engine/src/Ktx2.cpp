#include "kke/Ktx2.h"

#include <algorithm>
#include <cstring>

namespace kke::ktx2 {

namespace {

constexpr uint8_t kIdentifier[12] = { 0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n' };
constexpr size_t kHeaderSize = 80;     // identifier, 9 header words, the index
constexpr size_t kLevelEntrySize = 24; // byteOffset, byteLength, uncompressedByteLength (u64 each)
constexpr size_t kAlign = 16;          // a multiple of every format's block size and of 4

uint32_t u32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v; // KTX2 is little-endian, as is every platform the engine runs on
}
uint64_t u64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}
void put32(std::vector<uint8_t>& out, size_t at, uint32_t v) { std::memcpy(out.data() + at, &v, 4); }
void put64(std::vector<uint8_t>& out, size_t at, uint64_t v) { std::memcpy(out.data() + at, &v, 8); }

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

// The basic data format descriptor (Khronos Data Format 1.3) for one of
// our formats: what a KTX2 reader other than ours needs to make sense of
// the bytes.
std::vector<uint32_t> descriptor(uint32_t format) {
    const bool bc7 = format == kBc7Unorm || format == kBc7Srgb;
    const uint32_t samples = bc7 ? 1 : 4;
    const uint32_t blockSize = 24 + 16 * samples;
    std::vector<uint32_t> d;
    d.push_back(4 + blockSize); // dfdTotalSize
    d.push_back(0);             // vendorId 0 (Khronos), descriptorType 0 (basic)
    d.push_back(2u | (blockSize << 16)); // version 2 (1.3)
    const uint32_t model = bc7 ? 134u : 1u;          // KHR_DF_MODEL_BC7 / RGBSDA
    const uint32_t transfer = isSrgb(format) ? 2u : 1u; // sRGB / linear
    d.push_back(model | (1u << 8) | (transfer << 16)); // BT.709 primaries, straight alpha
    d.push_back(bc7 ? 0x00000303u : 0u);               // block 4x4 (dimension - 1) / 1x1
    d.push_back(bc7 ? 16u : 4u);                       // bytesPlane0
    d.push_back(0);
    if (bc7) {
        d.insert(d.end(), { 127u << 16, 0u, 0u, 0xFFFFFFFFu }); // 128 bits of KHR_DF_CHANNEL_BC7_COLOR
    } else {
        const uint32_t channels[4] = { 0, 1, 2, 15 }; // R, G, B, A
        for (uint32_t c = 0; c < 4; ++c) d.insert(d.end(), { (c * 8) | (7u << 16) | (channels[c] << 24), 0u, 0u, 255u });
    }
    return d;
}

} // namespace

bool isSrgb(uint32_t vkFormat) { return vkFormat == kRgba8Srgb || vkFormat == kBc7Srgb; }

size_t levelSize(uint32_t vkFormat, uint32_t width, uint32_t height) {
    switch (vkFormat) {
    case kBc7Unorm:
    case kBc7Srgb: return size_t((width + 3) / 4) * ((height + 3) / 4) * 16;
    case kRgba8Unorm:
    case kRgba8Srgb: return size_t(width) * height * 4;
    default: return 0;
    }
}

bool isKtx2(const uint8_t* bytes, size_t size) { return size >= sizeof(kIdentifier) && std::memcmp(bytes, kIdentifier, sizeof(kIdentifier)) == 0; }

bool read(const uint8_t* b, size_t size, Image& out, std::string* error) {
    if (!isKtx2(b, size)) return fail(error, "not a KTX2 file");
    if (size < kHeaderSize) return fail(error, "KTX2 header cut short");
    const uint32_t format = u32(b + 12), width = u32(b + 20), height = u32(b + 24), depth = u32(b + 28);
    const uint32_t layers = u32(b + 32), faces = u32(b + 36), levelCount = u32(b + 40), scheme = u32(b + 44);
    if (levelSize(format, 1, 1) == 0) return fail(error, "KTX2 format " + std::to_string(format) + " isn't one the engine reads (BC7 or RGBA8)");
    if (depth > 1 || layers > 1 || faces != 1) return fail(error, "KTX2 with depth, layers or cube faces isn't supported");
    if (scheme != 0) return fail(error, "supercompressed KTX2 (scheme " + std::to_string(scheme) + ") isn't supported");
    if (width == 0 || height == 0 || width > 32768 || height > 32768) return fail(error, "KTX2 size out of range");
    uint32_t fullChain = 1;
    for (uint32_t m = std::max(width, height); m > 1; m >>= 1) ++fullChain;
    const uint32_t levels = std::max(1u, levelCount);
    if (levels > fullChain) return fail(error, "KTX2 has more levels than its size allows");
    if (size < kHeaderSize + levels * kLevelEntrySize) return fail(error, "KTX2 level index cut short");
    out.vkFormat = format;
    out.width = width;
    out.height = height;
    out.levels.assign(levels, {});
    for (uint32_t l = 0; l < levels; ++l) {
        const uint8_t* e = b + kHeaderSize + l * kLevelEntrySize;
        const uint64_t offset = u64(e), length = u64(e + 8);
        const size_t want = levelSize(format, std::max(1u, width >> l), std::max(1u, height >> l));
        if (length != want) return fail(error, "KTX2 level " + std::to_string(l) + " is " + std::to_string(length) + " bytes, not " + std::to_string(want));
        if (offset > size || length > size - offset) return fail(error, "KTX2 level " + std::to_string(l) + " runs past the end of the file");
        out.levels[l].assign(b + offset, b + offset + length);
    }
    return true;
}

std::vector<uint8_t> write(const Image& image) {
    const uint32_t levels = static_cast<uint32_t>(image.levels.size());
    const std::vector<uint32_t> dfd = descriptor(image.vkFormat);
    const size_t dfdOffset = kHeaderSize + levels * kLevelEntrySize;
    const size_t dfdBytes = dfd.size() * 4;
    auto aligned = [](size_t n) { return (n + kAlign - 1) / kAlign * kAlign; };
    // Data: the smallest level first.
    std::vector<size_t> offsets(levels);
    size_t end = aligned(dfdOffset + dfdBytes);
    for (uint32_t l = levels; l-- > 0;) {
        offsets[l] = end;
        end = aligned(end + image.levels[l].size());
    }
    std::vector<uint8_t> out(end, 0);
    std::memcpy(out.data(), kIdentifier, sizeof(kIdentifier));
    put32(out, 12, image.vkFormat);
    put32(out, 16, 1); // typeSize: bytes per component (1 for block formats)
    put32(out, 20, image.width);
    put32(out, 24, image.height);
    put32(out, 28, 0); // depth, layers: a plain 2D image
    put32(out, 32, 0);
    put32(out, 36, 1); // faces
    put32(out, 40, levels);
    put32(out, 44, 0); // no supercompression
    put32(out, 48, static_cast<uint32_t>(dfdOffset));
    put32(out, 52, static_cast<uint32_t>(dfdBytes));
    put32(out, 56, 0); // no key/value data
    put32(out, 60, 0);
    put64(out, 64, 0); // no supercompression global data
    put64(out, 72, 0);
    for (uint32_t l = 0; l < levels; ++l) {
        const size_t at = kHeaderSize + l * kLevelEntrySize;
        put64(out, at, offsets[l]);
        put64(out, at + 8, image.levels[l].size());
        put64(out, at + 16, image.levels[l].size());
        std::memcpy(out.data() + offsets[l], image.levels[l].data(), image.levels[l].size());
    }
    std::memcpy(out.data() + dfdOffset, dfd.data(), dfdBytes);
    return out;
}

} // namespace kke::ktx2
