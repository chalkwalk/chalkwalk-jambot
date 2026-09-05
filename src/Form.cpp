#include "Form.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Form {
namespace {

// The same xorshift the band draws its figures with, so a form and a figure
// taken from one seed behave alike.
struct Rng {
  std::uint32_t state;
  explicit Rng(std::uint32_t s) : state(s | 1u) {}
  std::uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
};

} // namespace

std::string table(std::uint32_t seed) {
  static const char *kTables[] = {"AABA", "ABAC", "AAAB"};
  Rng r(seed ^ 0xF0F17Au);
  return kTables[r.next() % 3];
}

int intervalsPerSection(std::uint32_t seed, int bpm, int bpi) {
  if (bpm <= 0 || bpi <= 0)
    return 2;
  Rng r(seed ^ 0x5EC7104u);
  const double target = 20.0 + (double)(r.next() % 21u);
  const double intervalSeconds = (double)bpi * 60.0 / (double)bpm;
  const int want = (int)std::lround(target / intervalSeconds);
  return std::max(2, std::min(8, want));
}

int letterAt(std::uint32_t seed, int bpm, int bpi, int origin,
             int intervalIndex) {
  const auto t = table(seed);
  if (t.empty())
    return 0;
  const int since = intervalIndex - origin;
  if (since <= 0)
    return 0;
  const int n = intervalsPerSection(seed, bpm, bpi);
  const int section = since / n;
  return t[(std::size_t)(section % (int)t.size())] - 'A';
}

int pulsesFor(int basePulses, int lo, int hi, int letter, std::uint32_t seed) {
  if (letter <= 0 || hi <= lo)
    return basePulses;

  // The salt only has to differ from the others in this file; the letter term
  // is what makes ABAC's B and C two densities rather than one drawn twice.
  Rng r(seed ^ (0xD315A7u + 6151u * (std::uint32_t)letter));
  const int magnitude = 1 + (int)(r.next() % 2u);
  const int direction = (r.next() % 2u) ? 1 : -1;

  int p = basePulses + direction * magnitude;
  if (p < lo || p > hi)
    p = basePulses - direction * magnitude; // reflect off the edge
  return std::max(lo, std::min(hi, p));
}

} // namespace Form
