#include "kke/TextureCompress.h"

#include "kke/TextureMips.h"

#include <bc7decomp.h>
#include <bc7enc.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

namespace kke {

namespace {

void initEncoder() {
    static std::once_flag once;
    std::call_once(once, [] { bc7enc_compress_block_init(); });
}

// The 4x4 texels of block (bx, by), clamped at the edges.
void gatherBlock(const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t bx, uint32_t by, uint8_t out[64]) {
    for (uint32_t y = 0; y < 4; ++y)
        for (uint32_t x = 0; x < 4; ++x) {
            const uint32_t sx = std::min(bx * 4 + x, width - 1), sy = std::min(by * 4 + y, height - 1);
            std::memcpy(out + (y * 4 + x) * 4, rgba + (static_cast<size_t>(sy) * width + sx) * 4, 4);
        }
}

} // namespace

std::vector<uint8_t> encodeBc7(const uint8_t* rgba, uint32_t width, uint32_t height, bool perceptual, unsigned threads) {
    initEncoder();
    const uint32_t bw = (width + 3) / 4, bh = (height + 3) / 4;
    std::vector<uint8_t> out(size_t(bw) * bh * 16);
    if (width == 0 || height == 0) return out;
    bc7enc_compress_block_params params;
    bc7enc_compress_block_params_init(&params);
    if (!perceptual) bc7enc_compress_block_params_init_linear_weights(&params);
    if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());
    threads = std::min<unsigned>(threads, bh);
    // Rows of blocks handed out one at a time: every core stays busy
    // whatever the image looks like.
    std::atomic<uint32_t> nextRow{0};
    auto work = [&] {
        uint8_t block[64];
        for (uint32_t by; (by = nextRow++) < bh;)
            for (uint32_t bx = 0; bx < bw; ++bx) {
                gatherBlock(rgba, width, height, bx, by, block);
                bc7enc_compress_block(out.data() + (size_t(by) * bw + bx) * 16, block, &params);
            }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < threads; ++t) pool.emplace_back(work);
    work();
    for (std::thread& t : pool) t.join();
    return out;
}

void decodeBc7(const uint8_t* blocks, uint32_t width, uint32_t height, uint8_t* rgba) {
    const uint32_t bw = (width + 3) / 4, bh = (height + 3) / 4;
    bc7decomp::color_rgba texels[16];
    for (uint32_t by = 0; by < bh; ++by)
        for (uint32_t bx = 0; bx < bw; ++bx) {
            bc7decomp::unpack_bc7(blocks + (size_t(by) * bw + bx) * 16, texels);
            for (uint32_t y = 0; y < 4 && by * 4 + y < height; ++y)
                for (uint32_t x = 0; x < 4 && bx * 4 + x < width; ++x)
                    std::memcpy(rgba + ((size_t(by) * 4 + y) * width + bx * 4 + x) * 4, &texels[y * 4 + x], 4);
        }
}

ktx2::Image cookBc7(const uint8_t* rgba, uint32_t width, uint32_t height, bool srgb, unsigned threads) {
    std::vector<uint8_t> chain;
    const uint32_t levels = buildMipChainRgba8(rgba, width, height, srgb, chain);
    ktx2::Image image;
    image.vkFormat = srgb ? ktx2::kBc7Srgb : ktx2::kBc7Unorm;
    image.width = width;
    image.height = height;
    size_t offset = 0;
    for (uint32_t l = 0; l < levels; ++l) {
        const uint32_t w = std::max(1u, width >> l), h = std::max(1u, height >> l);
        image.levels.push_back(encodeBc7(chain.data() + offset, w, h, srgb, threads));
        offset += size_t(w) * h * 4;
    }
    return image;
}

std::vector<uint8_t> toRgba8Chain(const ktx2::Image& image) {
    std::vector<uint8_t> chain;
    const bool bc7 = image.vkFormat == ktx2::kBc7Srgb || image.vkFormat == ktx2::kBc7Unorm;
    for (size_t l = 0; l < image.levels.size(); ++l) {
        const uint32_t w = std::max(1u, image.width >> l), h = std::max(1u, image.height >> l);
        const size_t at = chain.size();
        chain.resize(at + size_t(w) * h * 4);
        if (bc7) decodeBc7(image.levels[l].data(), w, h, chain.data() + at);
        else std::memcpy(chain.data() + at, image.levels[l].data(), size_t(w) * h * 4);
    }
    return chain;
}

} // namespace kke
