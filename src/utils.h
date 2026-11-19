#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mapgen {

struct Rgb {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

float HashNoise2D(int x, int y, std::uint32_t seed);
float ValueNoise2D(float x, float y, std::uint32_t seed);
float Fbm2D(float x, float y, int octaves, float lacunarity, float gain, std::uint32_t seed);
float Worley2D(float x, float y, float cellSize, std::uint32_t seed);

// A PNG tEXt entry. The key must be 1-79 printable ASCII characters; the value must be ASCII without NUL.
struct PngText {
    std::string key;
    std::string value;
};

bool WritePngRgb(const std::string& path, int width, int height, const std::vector<Rgb>& rgb, std::string& outError,
                 const std::vector<PngText>& text = {});

}  // namespace mapgen
