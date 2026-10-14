#include <cstdio>
#include <cstdlib>
#include <vector>
#include "visual.h"

int main(int argc, char **argv) {
  const int scene = argc > 1 ? std::atoi(argv[1]) : 0;
  const unsigned elapsed = argc > 2 ? std::atoi(argv[2]) : 1400;
  const bool echo = argc > 3 && std::atoi(argv[3]);
  std::vector<unsigned char> pixels(466 * 466 * 3);
  auto rect = [&](int x, int y, int w, int h, uint32_t color) {
    for (int row = y; row < y + h; ++row) for (int col = x; col < x + w; ++col) {
      if (row < 0 || col < 0 || row >= 466 || col >= 466) continue;
      const int i = (row * 466 + col) * 3;
      pixels[i] = color >> 16; pixels[i + 1] = color >> 8; pixels[i + 2] = color;
    }
  };
  if (scene && elapsed < (echo ? 1400u : 10000u)) {
    Bloom::flower(rect, scene == 1 ? Bloom::Kind::bloom : Bloom::Kind::reply, elapsed, echo);
    if (echo) Bloom::clock(rect, "22:14", 177, 18);
    else if (elapsed > 8500) Bloom::clock(rect, "22:14", 177, 18, (elapsed - 8500) / 1500.0f);
    else Bloom::clock(rect, "22:14", 437, 4, 0.8f);
  } else Bloom::clock(rect, "22:14", 177, 18);
  std::printf("P6\n466 466\n255\n");
  std::fwrite(pixels.data(), 1, pixels.size(), stdout);
}
