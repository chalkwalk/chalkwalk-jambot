// SPDX-License-Identifier: MIT
#pragma once

// juce::UnitTest's three verbs, on top of Catch2.
//
// These tests came from Antiphon, which uses juce::UnitTest, and a JUCE-free
// library cannot link it. The obvious port is to restructure each file into
// TEST_CASEs -- and restructuring is exactly where a port quietly drops an
// assertion, which for a wire protocol is the worst possible thing to drop
// silently.
//
// So the bodies are untouched and the verbs are shimmed instead. `beginTest`
// names the group in any failure message that follows it, which is what it did
// before; `expect` and `expectEquals` become Catch2 checks. The assertion count
// is therefore comparable across the move, and it was: 261 either side.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace shim {

// The name of the group currently running, so a failure says which one.
inline std::string currentGroup;

inline void setGroup(const std::string &name) { currentGroup = name; }

template <typename T>
std::string show(const T &v) {
  std::ostringstream os;
  os << v;
  return os.str();
}

// A base with the shape juce::UnitTest had, so a ported file's class
// declaration does not have to change either.
class UnitTest {
public:
  explicit UnitTest(std::string name, std::string = {}) : name_(std::move(name)) {}
  virtual ~UnitTest() = default;
  virtual void runTest() = 0;

protected:
  void beginTest(const std::string &n) { setGroup(name_ + " / " + n); }
  // ...and the juce::String form, for a group name built by concatenation.
  // Constrained to types that have `toStdString`, or it would out-compete the
  // std::string overload for a plain string literal.
  template <typename T,
            typename = decltype(std::declval<const T &>().toStdString())>
  void beginTest(const T &n) {
    setGroup(name_ + " / " + n.toStdString());
  }

  // juce::UnitTest's logMessage: output that is read, not asserted on. The
  // bot suites use it to print every reply in full, because a line that reads
  // badly is a defect no `expect` will catch.
  void logMessage(const std::string &m) { UNSCOPED_INFO(m); }
  // ...and a juce::String, which is what most call sites build. Templated
  // because that type is declared below this one, and constrained for the
  // reason beginTest is.
  template <typename T,
            typename = decltype(std::declval<const T &>().toStdString())>
  void logMessage(const T &m) {
    UNSCOPED_INFO(m.toStdString());
  }

private:
  std::string name_;
};

}  // namespace shim

// juce's debug assertion. These suites use it for invariants they consider
// impossible rather than for things under test, so it stays a hard stop in a
// debug build and vanishes in a release one, exactly as it did.
#ifndef jassert
#ifdef NDEBUG
#define jassert(x) ((void)0)
#else
#define jassert(x) assert(x)
#endif
#endif

// Argument-count dispatch: expect(cond) and expect(cond, msg) both exist in
// juce::UnitTest. This needs a CONFORMING preprocessor -- see the MSVC note in
// test/CMakeLists.txt, where it silently picked the wrong arm.
#define expect(...) SHIM_EXPECT_PICK(__VA_ARGS__, SHIM_EXPECT2, SHIM_EXPECT1)(__VA_ARGS__)
#define SHIM_EXPECT_PICK(_1, _2, NAME, ...) NAME
#define SHIM_EXPECT1(cond)                                                     \
  do {                                                                         \
    INFO(::shim::currentGroup);                                                \
    CHECK((cond));                                                             \
  } while (false)
#define SHIM_EXPECT2(cond, msg)                                                \
  do {                                                                         \
    INFO(::shim::currentGroup);                                                \
    INFO(msg);                                                                 \
    CHECK((cond));                                                             \
  } while (false)

#define expectEquals(...)                                                      \
  SHIM_EQ_PICK(__VA_ARGS__, SHIM_EQ3, SHIM_EQ2)(__VA_ARGS__)
#define SHIM_EQ_PICK(_1, _2, _3, NAME, ...) NAME
#define SHIM_EQ2(a, b)                                                         \
  do {                                                                         \
    INFO(::shim::currentGroup);                                                \
    CHECK((a) == (b));                                                         \
  } while (false)
#define SHIM_EQ3(a, b, msg)                                                    \
  do {                                                                         \
    INFO(::shim::currentGroup);                                                \
    INFO(msg);                                                                 \
    CHECK((a) == (b));                                                         \
  } while (false)

// juce::UnitTest's fourth argument is an optional failure message, and both
// arities appear in the ported suites.
#define expectWithinAbsoluteError(...)                                         \
  SHIM_NEAR_PICK(__VA_ARGS__, SHIM_NEAR4, SHIM_NEAR3)(__VA_ARGS__)
#define SHIM_NEAR_PICK(_1, _2, _3, _4, NAME, ...) NAME
#define SHIM_NEAR3(actual, expected, tolerance)                                \
  do {                                                                         \
    INFO(::shim::currentGroup);                                                \
    INFO("expected " << (expected) << " +/- " << (tolerance) << ", got "       \
                     << (actual));                                             \
    CHECK(std::abs((actual) - (expected)) <= (tolerance));                     \
  } while (false)
#define SHIM_NEAR4(actual, expected, tolerance, msg)                           \
  do {                                                                         \
    INFO(::shim::currentGroup);                                                \
    INFO(msg);                                                                 \
    INFO("expected " << (expected) << " +/- " << (tolerance) << ", got "       \
                     << (actual));                                             \
    CHECK(std::abs((actual) - (expected)) <= (tolerance));                     \
  } while (false)

namespace shim {

// juce::Random, as much of it as these tests use.
//
// Seeded and deterministic, which is the only property the tests depend on --
// a fuzz over random payloads has to replay identically when it finds
// something. std::mt19937 rather than JUCE's LCG, so the sequences differ from
// the originals; nothing here asserts a particular sequence.
class Random {
public:
  explicit Random(std::uint32_t seed) : engine_(seed) {}

  double nextDouble() {
    return std::uniform_real_distribution<double>(0.0, 1.0)(engine_);
  }
  int nextInt(int upperExclusive) {
    if (upperExclusive <= 0)
      return 0;
    return std::uniform_int_distribution<int>(0, upperExclusive - 1)(engine_);
  }

private:
  std::mt19937 engine_;
};

}  // namespace shim

// juce's debug assertion. These suites use it for invariants they consider
// impossible rather than for things under test, so it stays a hard stop in a
// debug build and vanishes in a release one, exactly as it did.
#ifndef jassert
#ifdef NDEBUG
#define jassert(x) ((void)0)
#else
#define jassert(x) assert(x)
#endif
#endif

namespace juce {

// juce::String and juce::StringArray, as much of them as these suites use.
//
// Shimmed rather than rewritten. `juce::String` appears 540 times across the
// ten bot suites and `StringArray` nine more; converting each one by hand is
// exactly the kind of pass that changes behaviour by accident -- an earlier
// mechanical strip of `.toLowerCase()` in this codebase would have turned
// "dave said so" into "Dave said so" and the suite caught it. Keeping the type
// keeps the assertions byte-identical, so the diff of the move is the harness
// and nothing else.
//
// Only the methods the suites call are here. A missing one is a compile error,
// which is the right failure: it says a test used something nobody has thought
// about, rather than quietly doing something slightly different.
class String {
public:
  String() = default;
  String(const char *s) : t_(s == nullptr ? "" : s) {}
  String(const std::string &s) : t_(s) {}
  String(char c) : t_(1, c) {}

  template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
  explicit String(T v) {
    std::ostringstream os;
    os << v;
    t_ = os.str();
  }

  // juce's "this many decimal places".
  String(double v, int decimalPlaces) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(decimalPlaces) << v;
    t_ = os.str();
  }

  const std::string &toStdString() const { return t_; }
  int length() const { return static_cast<int>(t_.size()); }
  bool isEmpty() const { return t_.empty(); }
  bool isNotEmpty() const { return !t_.empty(); }

  String toLowerCase() const { return String(lower(t_)); }
  String toUpperCase() const {
    std::string o = t_;
    for (auto &c : o)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return String(o);
  }

  bool contains(const String &o) const { return t_.find(o.t_) != npos(); }
  bool containsIgnoreCase(const String &o) const {
    return lower(t_).find(lower(o.t_)) != npos();
  }
  bool containsChar(char c) const { return t_.find(c) != npos(); }
  bool equalsIgnoreCase(const String &o) const { return lower(t_) == lower(o.t_); }

  // A whole word, not a substring: "i" must not match inside "list".
  //
  // An apostrophe is a BOUNDARY, not a word character, which is what juce's
  // `isLetterOrDigit` decides -- so "we" is a whole word inside "we're". That
  // is not a detail: the bots' rule is that one speaking for the band says
  // "we", and "we're wrapping it up" is the commonest way it does so. Counting
  // the apostrophe as a letter made that line fail, and the suite caught it
  // on the first run in this repository.
  bool containsWholeWord(const String &word) const {
    if (word.t_.empty())
      return false;
    for (std::size_t at = t_.find(word.t_); at != npos();
         at = t_.find(word.t_, at + 1)) {
      const bool leftOk = at == 0 || !isWordChar(t_[at - 1]);
      const std::size_t after = at + word.t_.size();
      const bool rightOk = after >= t_.size() || !isWordChar(t_[after]);
      if (leftOk && rightOk)
        return true;
    }
    return false;
  }

  bool startsWith(const String &o) const { return t_.rfind(o.t_, 0) == 0; }
  bool startsWithChar(char c) const { return !t_.empty() && t_.front() == c; }
  bool endsWithChar(char c) const { return !t_.empty() && t_.back() == c; }

  char operator[](int i) const {
    return i >= 0 && i < length() ? t_[static_cast<std::size_t>(i)] : '\0';
  }

  int indexOfChar(char c) const {
    const auto at = t_.find(c);
    return at == npos() ? -1 : static_cast<int>(at);
  }
  int indexOfAnyOf(const String &chars) const {
    const auto at = t_.find_first_of(chars.t_);
    return at == npos() ? -1 : static_cast<int>(at);
  }

  String substring(int from) const {
    if (from < 0)
      from = 0;
    if (static_cast<std::size_t>(from) >= t_.size())
      return {};
    return String(t_.substr(static_cast<std::size_t>(from)));
  }
  String substring(int from, int to) const {
    if (from < 0)
      from = 0;
    if (to < from || static_cast<std::size_t>(from) >= t_.size())
      return {};
    if (static_cast<std::size_t>(to) > t_.size())
      to = static_cast<int>(t_.size());
    return String(t_.substr(static_cast<std::size_t>(from),
                            static_cast<std::size_t>(to - from)));
  }

  // Whitespace both ends, which is all these suites ask of it.
  String trim() const {
    const auto first = t_.find_first_not_of(" \t\r\n");
    if (first == npos())
      return {};
    const auto last = t_.find_last_not_of(" \t\r\n");
    return String(t_.substr(first, last - first + 1));
  }

  String upToFirstOccurrenceOf(const String &mark, bool include,
                               bool ignoreCase) const {
    const auto at = find(mark, ignoreCase);
    if (at == npos())
      return *this;
    return String(t_.substr(0, at + (include ? mark.t_.size() : 0)));
  }
  String fromFirstOccurrenceOf(const String &mark, bool include,
                               bool ignoreCase) const {
    const auto at = find(mark, ignoreCase);
    if (at == npos())
      return *this;
    return String(t_.substr(at + (include ? 0 : mark.t_.size())));
  }

  // juce reads the leading integer and answers 0 rather than throwing.
  int getIntValue() const { return std::atoi(t_.c_str()); }

  String &operator+=(const String &o) {
    t_ += o.t_;
    return *this;
  }
  friend String operator+(String a, const String &b) { return a += b; }
  friend bool operator==(const String &a, const String &b) { return a.t_ == b.t_; }
  friend bool operator!=(const String &a, const String &b) { return a.t_ != b.t_; }
  friend bool operator<(const String &a, const String &b) { return a.t_ < b.t_; }

private:
  static std::size_t npos() { return std::string::npos; }
  static bool isWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
  }
  static std::string lower(std::string v) {
    for (auto &c : v)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return v;
  }
  std::size_t find(const String &mark, bool ignoreCase) const {
    return ignoreCase ? lower(t_).find(lower(mark.t_)) : t_.find(mark.t_);
  }

  std::string t_;
};

inline std::ostream &operator<<(std::ostream &os, const String &s) {
  return os << s.toStdString();
}

class StringArray {
public:
  StringArray() = default;
  StringArray(std::initializer_list<String> items) : v_(items) {}

  void add(const String &s) { v_.push_back(s); }
  int size() const { return static_cast<int>(v_.size()); }
  bool isEmpty() const { return v_.empty(); }
  const String &operator[](int i) const {
    static const String empty;
    return i >= 0 && i < size() ? v_[static_cast<std::size_t>(i)] : empty;
  }

  // juce's `sort(ignoreCase)`. Only the ignoreCase form is used here, and it
  // is what makes the corpus comparison order-independent.
  void sort(bool ignoreCase) {
    std::sort(v_.begin(), v_.end(), [ignoreCase](const String &a, const String &b) {
      return ignoreCase ? a.toLowerCase() < b.toLowerCase() : a < b;
    });
  }

  String joinIntoString(const String &sep) const {
    String out;
    for (std::size_t i = 0; i < v_.size(); ++i) {
      if (i != 0)
        out += sep;
      out += v_[i];
    }
    return out;
  }

  static StringArray fromLines(const String &text) {
    StringArray out;
    std::istringstream in(text.toStdString());
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      out.add(String(line));
    }
    return out;
  }

  // juce splits on any character in `breakChars`; `quoteChars` is unused by
  // these suites and is accepted only so the call sites do not change.
  static StringArray fromTokens(const String &text, const String &breakChars,
                                const String &) {
    StringArray out;
    const std::string &s = text.toStdString();
    const std::string &breaks = breakChars.toStdString();
    std::string current;
    for (const char c : s) {
      if (breaks.find(c) != std::string::npos) {
        out.add(String(current));
        current.clear();
      } else {
        current += c;
      }
    }
    out.add(String(current));
    return out;
  }

  auto begin() const { return v_.begin(); }
  auto end() const { return v_.end(); }

private:
  std::vector<String> v_;
};

// juce::File, only as much as reading a fixture and naming an output needs.
class File {
public:
  File() = default;
  File(const String &path) : path_(path.toStdString()) {}

  bool existsAsFile() const {
    std::ifstream in(path_, std::ios::binary);
    return in.good();
  }
  String loadFileAsString() const {
    std::ifstream in(path_, std::ios::binary);
    if (!in)
      return {};
    std::ostringstream os;
    os << in.rdbuf();
    return String(os.str());
  }
  void deleteFile() const { std::remove(path_.c_str()); }
  const std::string &getFullPathName() const { return path_; }

private:
  std::string path_;
};

// juce::AudioBuffer, for the one opt-in audition render. Deliberately not a
// general buffer: it grows, it holds samples, and it is handed to writeWav.
template <typename T> class AudioBuffer {
public:
  AudioBuffer(int channels, int samples) { setSize(channels, samples); }

  int getNumChannels() const { return static_cast<int>(data_.size()); }
  int getNumSamples() const { return data_.empty() ? 0 : static_cast<int>(data_[0].size()); }

  // juce's extra flags say whether to keep what is there and clear what is
  // new. Both are what the caller asks for, so both are simply done.
  void setSize(int channels, int samples, bool keepExisting = false,
               bool clearExtra = true, bool = false) {
    if (!keepExisting)
      data_.clear();
    data_.resize(static_cast<std::size_t>(channels < 0 ? 0 : channels));
    for (auto &ch : data_)
      ch.resize(static_cast<std::size_t>(samples < 0 ? 0 : samples),
                clearExtra ? T{} : T{});
  }
  void setSample(int channel, int index, T value) {
    data_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(index)] = value;
  }
  T getSample(int channel, int index) const {
    return data_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(index)];
  }

private:
  std::vector<std::vector<T>> data_;
};

// juce::jmin / juce::jmax, and the one constant these suites reach for.
template <typename T> T jmin(T a, T b) { return a < b ? a : b; }
template <typename T> T jmax(T a, T b) { return a > b ? a : b; }
template <typename T> T jmin(T a, T b, T c) { return jmin(jmin(a, b), c); }
template <typename T> T jmax(T a, T b, T c) { return jmax(jmax(a, b), c); }

template <typename T> struct MathConstants {
  static constexpr T pi = static_cast<T>(3.14159265358979323846);
  static constexpr T twoPi = static_cast<T>(2.0 * 3.14159265358979323846);
};

// juce::SystemStats, for the one thing the band suite asks it: an environment
// variable with a default. The debug renders are opt-in, and stay that way.
struct SystemStats {
  static String getEnvironmentVariable(const String &name,
                                       const String &fallback) {
    const char *v = std::getenv(name.toStdString().c_str());
    return v != nullptr ? String(v) : fallback;
  }
};

}  // namespace juce

namespace shim {

// 24-bit PCM WAV, little-endian, interleaved.
//
// This exists for one env-gated audition render. Shimming juce's
// WavAudioFormat/AudioFormatWriter/FileOutputStream trio to reach the same
// forty bytes of header would be more code than the header is, so the four
// lines at the call site name this instead. Nothing asserts on the output --
// it is written to be listened to.
inline bool writeWav(const std::string &path, const juce::AudioBuffer<float> &buf,
                     double sampleRate) {
  const int channels = buf.getNumChannels();
  const int frames = buf.getNumSamples();
  if (channels <= 0 || frames <= 0)
    return false;

  std::ofstream out(path, std::ios::binary);
  if (!out)
    return false;

  const std::uint32_t byteRate =
      static_cast<std::uint32_t>(sampleRate) * static_cast<std::uint32_t>(channels) * 3u;
  const std::uint32_t dataBytes =
      static_cast<std::uint32_t>(frames) * static_cast<std::uint32_t>(channels) * 3u;

  auto u32 = [&out](std::uint32_t v) {
    const char b[4] = {char(v & 0xff), char((v >> 8) & 0xff), char((v >> 16) & 0xff),
                       char((v >> 24) & 0xff)};
    out.write(b, 4);
  };
  auto u16 = [&out](std::uint16_t v) {
    const char b[2] = {char(v & 0xff), char((v >> 8) & 0xff)};
    out.write(b, 2);
  };

  out.write("RIFF", 4);
  u32(36u + dataBytes);
  out.write("WAVEfmt ", 8);
  u32(16u);
  u16(1u);                                          // PCM
  u16(static_cast<std::uint16_t>(channels));
  u32(static_cast<std::uint32_t>(sampleRate));
  u32(byteRate);
  u16(static_cast<std::uint16_t>(channels * 3));    // block align
  u16(24u);
  out.write("data", 4);
  u32(dataBytes);

  for (int i = 0; i < frames; ++i) {
    for (int ch = 0; ch < channels; ++ch) {
      float v = buf.getSample(ch, i);
      v = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
      const std::int32_t q = static_cast<std::int32_t>(v * 8388607.0f);
      const char b[3] = {char(q & 0xff), char((q >> 8) & 0xff), char((q >> 16) & 0xff)};
      out.write(b, 3);
    }
  }
  return out.good();
}

// Where the corpora live. CMake passes the source directory, which is more
// honest than walking eight parents up from the executable looking for a
// directory that might be there -- that search answered "no corpus" for a
// build tree in the wrong place, and a skipped corpus test looks like a
// passing one.
inline juce::File fixture(const std::string &name) {
#ifndef JAMBOT_FIXTURE_DIR
#error "JAMBOT_FIXTURE_DIR must be defined by the build"
#endif
  return juce::File(juce::String(std::string(JAMBOT_FIXTURE_DIR) + "/" + name));
}

}  // namespace shim

// juce's debug assertion. These suites use it for invariants they consider
// impossible rather than for things under test, so it stays a hard stop in a
// debug build and vanishes in a release one, exactly as it did.
#ifndef jassert
#ifdef NDEBUG
#define jassert(x) ((void)0)
#else
#define jassert(x) assert(x)
#endif
#endif
