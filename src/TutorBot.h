#pragma once

#include "BotClient.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

// The fifth bot: no instrument, no channel, no audio. It joins, teaches six
// lines, and leaves.
//
// Designed in `docs/BOT-CHAT.md` section 7, and the three properties that
// section argues for are the whole reason it is a separate class rather than a
// mode on `PracticeBot`:
//
//   It can be absent. A room started by somebody who has done this before has
//   four bots and nothing to silence.
//
//   It finishes. When the thread is done it parts of its own accord and leaves
//   a band behind. A tutorial that leaves when you have got it is a rare thing.
//
//   It is not a player, so it may speak more. The budget that keeps the
//   instrument bots quiet is about not drowning a jam; this one's whole purpose
//   is speech, and it is finite by construction -- six lines and gone.
//
// GENERIC, deliberately. What it teaches is the interval form and the band's
// behaviour, both of which are this library's concepts rather than any one
// host's, so it works in any room its client can reach. It knows nothing about
// any particular client's window and must not: a bot that could drive a host's
// UI would stop being an ordinary client, which is the property that lets any
// client host one.
class TutorBot : private BotClient::Listener {
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
  ~TutorBot() override;

  TutorBot(const TutorBot &) = delete;
  TutorBot &operator=(const TutorBot &) = delete;

  // Who the tutor is teaching. Only this player's audio advances the thread --
  // a room with somebody else already playing must not carry the newcomer past
  // the lines about their own first interval.
  void setOwner(std::string ownerUsername);

  bool join(const std::string &host, int port, double sampleRate);

  // Leaves. Idempotent, and called by the thread itself when it finishes.
  void part();

  bool isActive() const { return active.load(); }
  const std::string &name() const { return username; }

  // How far through. For tests: from outside, the difference between waiting
  // for your first interval and waiting for your second is several seconds of
  // audio that nothing can watch.
  Step step() const;

private:
  void onConnected() override;
  void onDisconnected(const std::string &reason) override;
  void onChatMessage(const std::string &type, const std::string &username,
                     const std::string &text) override;
  void onIntervalReceived(const std::string &username, int channelIndex,
                          const float *left, const float *right,
                          int numSamples) override;

  // Says the line for the current step and moves to the next. Takes the lock
  // itself; callers must not hold it.
  void advance(const std::string &line);

  std::string username;
  std::unique_ptr<BotClient::Client> client;

  mutable std::mutex stateMutex;
  std::string owner;
  Step current = Step::Greeting;

  std::atomic<bool> active{false};
};
