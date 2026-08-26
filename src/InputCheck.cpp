#include "InputCheck.h"

#include <chalkwalk/dsp/Measure.h>

#include <algorithm>
#include <cmath>

namespace {

double linearForDb(double db) { return std::pow(10.0, db / 20.0); }

// Loudest sample across the channels present, so a hard-panned input is
// measured rather than halved.
inline float across(const float *left, const float *right, int i) {
  const float l = std::abs(left[i]);
  return right != nullptr ? std::max(l, std::abs(right[i])) : l;
}

} // namespace

namespace InputCheck {

Signals measure(const float *left, const float *right, int numSamples,
                double sampleRate) {
  Signals s;
  if (left == nullptr || numSamples <= 0 || sampleRate <= 0.0)
    return s;

  namespace m = chalkwalk::dsp::measure;

  // Per channel, then combined -- the instruments take one pointer each, and
  // interleaving to hand them a single buffer would allocate for no gain.
  const double peakL = m::peak(left, numSamples);
  const double rmsL = m::rms(left, numSamples);
  if (right != nullptr) {
    const double peakR = m::peak(right, numSamples);
    const double rmsR = m::rms(right, numSamples);
    s.peak = std::max(peakL, peakR);
    s.rms = std::sqrt(0.5 * (rmsL * rmsL + rmsR * rmsR));
  } else {
    s.peak = peakL;
    s.rms = rmsL;
  }

  s.crest = s.rms > 0.0 ? s.peak / s.rms : 0.0;

  // Duty, framewise. Two passes rather than one because the floor is relative
  // to the LOUDEST frame: how much of the interval had sound in it is a
  // question about this signal, not about full scale, so a quiet part played
  // throughout reads as sustained rather than as silence with a spike.
  const int frameSamples =
      std::max(1, (int)std::lround(kFrameSeconds * sampleRate));
  const int frames = numSamples / frameSamples;
  if (frames < 1)
    return s;

  double loudestFrame = 0.0;
  for (int f = 0; f < frames; ++f) {
    double sum = 0.0;
    const int start = f * frameSamples;
    for (int i = start; i < start + frameSamples; ++i) {
      const double v = across(left, right, i);
      sum += v * v;
    }
    loudestFrame = std::max(loudestFrame, std::sqrt(sum / frameSamples));
  }

  if (loudestFrame <= 0.0)
    return s;

  const double floor = loudestFrame * linearForDb(kDutyFloorDb);
  int sounding = 0;
  for (int f = 0; f < frames; ++f) {
    double sum = 0.0;
    const int start = f * frameSamples;
    for (int i = start; i < start + frameSamples; ++i) {
      const double v = across(left, right, i);
      sum += v * v;
    }
    if (std::sqrt(sum / frameSamples) >= floor)
      ++sounding;
  }

  s.duty = (double)sounding / (double)frames;
  s.soundingSeconds = (double)sounding * frameSamples / sampleRate;
  return s;
}

Reading classify(const Signals &s) {
  namespace m = chalkwalk::dsp::measure;

  // Nothing arrived. Checked first because every other reading is a statement
  // about a signal, and there is not one.
  if (m::toDb(s.peak) < kSilentPeakDb)
    return Reading::Silent;

  // Clipping and clicks both reach full scale; the duty cycle is what tells
  // them apart, and both guards carry it, so neither depends on being tested
  // before the other.
  if (s.peak >= kClipPeak && s.duty > kClipDuty)
    return Reading::Clipping;

  // BEFORE faint, deliberately. A quiet click train is both, and "that is
  // usually a buffer size" is the one a player can act on; "it is quiet" would
  // send them to the wrong knob.
  if (s.crest > kClickCrest && s.soundingSeconds < kClickSeconds)
    return Reading::Clicks;

  if (m::toDb(s.rms) < kFaintRmsDb)
    return Reading::Faint;

  return Reading::Playing;
}

} // namespace InputCheck
