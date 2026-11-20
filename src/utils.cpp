#include "utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <vector>

namespace mapgen {
namespace {

std::uint32_t Hash32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float SmoothStep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

std::uint32_t Adler32(const std::vector<std::uint8_t>& data) {
    constexpr std::uint32_t kMod = 65521;
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::uint8_t v : data) {
        a = (a + v) % kMod;
        b = (b + a) % kMod;
    }
    return (b << 16) | a;
}

std::uint32_t Crc32(const std::uint8_t* data, std::size_t len) {
    static std::array<std::uint32_t, 256> table = []() {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1U) ? (0xedb88320U ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();

    std::uint32_t c = 0xffffffffU;
    for (std::size_t i = 0; i < len; ++i) {
        c = table[(c ^ data[i]) & 0xffU] ^ (c >> 8);
    }
    return c ^ 0xffffffffU;
}

void AppendU32BE(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>(v & 0xff));
}

void AppendChunk(std::vector<std::uint8_t>& png, const char type[4], const std::vector<std::uint8_t>& data) {
    AppendU32BE(png, static_cast<std::uint32_t>(data.size()));

    const std::size_t typePos = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());

    const std::size_t crcStart = typePos;
    const std::size_t crcLen = 4 + data.size();
    const std::uint32_t crc = Crc32(png.data() + crcStart, crcLen);
    AppendU32BE(png, crc);
}

std::vector<std::uint8_t> DeflateStoredZlib(const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint8_t> z;
    z.reserve(raw.size() + raw.size() / 65535 + 16);

    z.push_back(0x78);
    z.push_back(0x01);

    std::size_t offset = 0;
    while (offset < raw.size()) {
        const std::size_t blockLen = std::min<std::size_t>(65535, raw.size() - offset);
        const bool isFinal = (offset + blockLen) == raw.size();

        z.push_back(isFinal ? 0x01 : 0x00);

        const std::uint16_t len = static_cast<std::uint16_t>(blockLen);
        const std::uint16_t nlen = static_cast<std::uint16_t>(~len);
        z.push_back(static_cast<std::uint8_t>(len & 0xff));
        z.push_back(static_cast<std::uint8_t>((len >> 8) & 0xff));
        z.push_back(static_cast<std::uint8_t>(nlen & 0xff));
        z.push_back(static_cast<std::uint8_t>((nlen >> 8) & 0xff));

        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + blockLen));
        offset += blockLen;
    }

    const std::uint32_t adler = Adler32(raw);
    AppendU32BE(z, adler);
    return z;
}

}  // namespace

float HashNoise2D(int x, int y, std::uint32_t seed) {
    const std::uint32_t ux = static_cast<std::uint32_t>(x);
    const std::uint32_t uy = static_cast<std::uint32_t>(y);
    const std::uint32_t h = Hash32(ux * 0x1f123bb5U ^ uy * 0x74a99c1dU ^ seed);
    return static_cast<float>(h & 0x00ffffffU) / 16777215.0f;
}

float ValueNoise2D(float x, float y, std::uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);

    const float v00 = HashNoise2D(ix, iy, seed);
    const float v10 = HashNoise2D(ix + 1, iy, seed);
    const float v01 = HashNoise2D(ix, iy + 1, seed);
    const float v11 = HashNoise2D(ix + 1, iy + 1, seed);

    const float sx = SmoothStep(fx);
    const float sy = SmoothStep(fy);
    const float a = Lerp(v00, v10, sx);
    const float b = Lerp(v01, v11, sx);
    return Lerp(a, b, sy);
}

float Fbm2D(float x, float y, int octaves, float lacunarity, float gain, std::uint32_t seed) {
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float sum = 0.0f;
    float norm = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * ValueNoise2D(x * frequency, y * frequency, seed + static_cast<std::uint32_t>(i * 977));
        norm += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }

    if (norm <= std::numeric_limits<float>::epsilon()) {
        return 0.0f;
    }
    return sum / norm;
}

float Worley2D(float x, float y, float cellSize, std::uint32_t seed) {
    if (cellSize <= 0.0001f) {
        return 0.0f;
    }

    const float gx = x / cellSize;
    const float gy = y / cellSize;
    const int cx = static_cast<int>(std::floor(gx));
    const int cy = static_cast<int>(std::floor(gy));

    float best = std::numeric_limits<float>::max();

    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            const int nx = cx + ox;
            const int ny = cy + oy;
            const float fx = static_cast<float>(nx) + HashNoise2D(nx, ny, seed);
            const float fy = static_cast<float>(ny) + HashNoise2D(nx, ny, seed ^ 0x9e3779b9U);
            const float dx = fx - gx;
            const float dy = fy - gy;
            const float d = std::sqrt(dx * dx + dy * dy);
            best = std::min(best, d);
        }
    }

    return std::clamp(best / 1.4142f, 0.0f, 1.0f);
}

bool WritePngRgb(const std::string& path, int width, int height, const std::vector<Rgb>& rgb, std::string& outError,
                 const std::vector<PngText>& text) {
    if (width <= 0 || height <= 0) {
        outError = "Invalid image size";
        return false;
    }

    const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (rgb.size() != expected) {
        outError = "RGB buffer size does not match dimensions";
        return false;
    }

    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1 + static_cast<std::size_t>(width) * 3));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        for (int x = 0; x < width; ++x) {
            const Rgb& c = rgb[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
            raw.push_back(c.r);
            raw.push_back(c.g);
            raw.push_back(c.b);
        }
    }

    const std::vector<std::uint8_t> idat = DeflateStoredZlib(raw);

    std::vector<std::uint8_t> png;
    png.reserve(idat.size() + 128);

    const std::array<std::uint8_t, 8> signature = {137, 80, 78, 71, 13, 10, 26, 10};
    png.insert(png.end(), signature.begin(), signature.end());

    std::vector<std::uint8_t> ihdr;
    ihdr.reserve(13);
    AppendU32BE(ihdr, static_cast<std::uint32_t>(width));
    AppendU32BE(ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(8);
    ihdr.push_back(2);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    AppendChunk(png, "IHDR", ihdr);

    for (const PngText& t : text) {
        if (t.key.empty() || t.key.size() > 79) {
            outError = "Invalid PNG text key: " + t.key;
            return false;
        }
        std::vector<std::uint8_t> chunk(t.key.begin(), t.key.end());
        chunk.push_back(0);
        chunk.insert(chunk.end(), t.value.begin(), t.value.end());
        AppendChunk(png, "tEXt", chunk);
    }

    AppendChunk(png, "IDAT", idat);
    AppendChunk(png, "IEND", {});

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        outError = "Failed to open output file: " + path;
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!ofs) {
        outError = "Failed to write PNG bytes";
        return false;
    }

    return true;
}

}  // namespace mapgen
