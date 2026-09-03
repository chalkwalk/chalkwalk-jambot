#pragma once

#include "BotClient.h"
#include "Conductor.h"
#include "InputCheck.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

// The fifth bot: no instrument, no channel, no audio. It joins and teaches six
// lines, and then it stops teaching.
//
// Designed in `docs/BOT-CHAT.md` section 7, and the three properties that
// section argues for are the whole reason it is a separate class rather than a
// mode on `PracticeBot`:
//
//   It can be absent. A room started by somebody who has done this before has
//   four bots and nothing to silence.
//
//   It finishes. When the thread is done the teaching is over and does not
//   resume. Section 7 had it PART here, and it cannot: a tutor is a conductor
//   now (16.9) and a room always has one. What that section was against is a
//   tutorial that lingers uselessly, and a conductor has a job for the rest of
//   the session -- so the property that survives is the one that mattered.
//
//   It is not a player, so it may speak more. The budget that keeps the
//   instrument bots quiet is about not drowning a jam; this one's whole purpose
//   is speech, and it is finite by construction -- six lines and done.
//
// GENERIC, deliberately. What it teaches is the interval form and the band's
// behaviour, both of which are this library's concepts rather than any one
// host's, so it works in any room its client can reach. It knows nothing about
// any particular client's window and must not: a bot that could drive a host's
// UI would stop being an ordinary client, which is the property that lets any
// client host one.
class TutorBot : public Conductor {
public:
  // The thread, in order. Each step waits for its own trigger; nothing fires
  // early because something later happened first.
  enum class Step {
    Greeting,     // said on arrival
    FirstPlayed,  // the first interval the owner sends
    SecondPlayed, // the second
    KeySet,       // the first key announcement
    Shaken,       // the first shake
    Done          // signed off and parted
  };

  TutorBot(std::string username, std::unique_ptr<BotClient::Client> client);
  ~TutorBot() override = default;

  TutorBot(const TutorBot &) = delete;
  TutorBot &operator=(const TutorBot &) = delete;

  // `setOwner`, `join`, `part`, `isActive`, `name` and `say` come from
  // Conductor. Who the tutor is teaching is the OWNER, and only that player's
  // audio advances the thread -- a room with somebody else already playing must
  // not carry the newcomer past the lines about their own first interval.

  // How far through. For tests: from outside, the difference between waiting
  // for your first interval and waiting for your second is several seconds of
  // audio that nothing can watch.
  Step step() const;

  // How many consecutive intervals must agree before a diagnostic is said.
  //
  // Section 7: a single quiet interval is a person thinking. Nothing about the
  // first four rows is urgent enough to be worth saying to somebody who paused
  // to turn a page.
  static constexpr int kIntervalsToAgree = 3;

private:
  void onConnected() override;
  void onDisconnected(const std::string &reason) override;
  void onChatMessage(const std::string &type, const std::string &username,
                     const std::string &text) override;
  void onIntervalReceived(const std::string &username, int channelIndex,
                          const float *left, const float *right,
                          int numSamples) override;

  // Says the line for `from` and moves to the next step, if the thread is
  // still at `from`. Takes the lock itself; callers must not hold it.
  //
  // The step is the argument and the line is derived from it, so a caller
  // cannot pair one step's line with another's -- see the note at the
  // definition for the race that makes this worth enforcing.
  void advance(Step from);

  // Counts the run of agreeing readings and says the row's line if this is the
  // interval that completes it. Returns nothing: whether the thread moves on is
  // a separate question, answered by the reading itself.
  void noteDiagnostic(InputCheck::Reading reading);

  mutable std::mutex stateMutex;
  Step current = Step::Greeting;

  // What the last interval read as, and how many in a row have agreed.
  InputCheck::Reading lastReading = InputCheck::Reading::Playing;
  int agreeingIntervals = 0;

  // Each of the first four rows fires at most once, ever. Indexed by the
  // enumerator, which is why `Playing` has a slot it never uses -- a lookup
  // that cannot be off by one is worth one unused bool.
  bool saidDiagnostic[5] = {false, false, false, false, false};

};
