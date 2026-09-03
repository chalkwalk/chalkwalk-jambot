#pragma once

#include "BandControl.h"
#include "BotAddress.h"
#include "BotLanguage.h"
#include "BotClient.h"

#include <chalkwalk/ninjam/Voting.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

// The band's leader, and the only bot that speaks for it.
//
// No instrument, no channel, no audio: a name in the room that talks, which is
// the shape TutorBot already proved and which costs a server almost nothing.
// What it buys is an AUTHORITY. Four bots agreeing by evaluating the same
// deterministic function is exact for music and wrong for speech, because two
// bots reaching the same conclusion both say it -- so the band had rank-stagger
// arbitration, and arbitration is what you build when nobody is in charge.
//
// One per band, always. An optional conductor means keeping both mechanisms
// alive and testing the fallback nobody exercises, which is how the roster race
// held on Linux until macOS found it.
//
// It decides WHAT and FROM WHICH INTERVAL, never WHEN: an interval index means
// the same thing whenever it arrives, where a deadline would turn a property
// that is currently exact into a race. That only holds while every bot reads
// one IntervalPump in one process, which is why the band is one process.
//
// Designed in Antiphon's docs/superpowers/specs/2026-08-31-conductor-design.md.
class Conductor : protected BotClient::Listener {
public:
  Conductor(std::string username, std::unique_ptr<BotClient::Client> client);
  ~Conductor() override;

  Conductor(const Conductor &) = delete;
  Conductor &operator=(const Conductor &) = delete;

  // Who the band is playing with. The conductor is the bot that cares whether
  // the owner is present, so this lives here rather than on the tutor.
  void setOwner(std::string ownerUsername);
  std::string owner() const;

  bool join(const std::string &host, int port, double rate);

  // Speaks for the band. A no-op once parted, so a line queued behind a
  // departure cannot arrive after the conductor has gone.
  void say(const std::string &text);

  // What the band calls itself, if anything. Empty means the roster is just a
  // list of names.
  void setBandName(std::string name);

  // Whoever hosts the room. NOT owned, and it must outlive this conductor --
  // the host creates both and destroys the conductor first.
  void setControl(BandControl *c);

  // Long enough for the join notices to finish scrolling before the one line
  // anybody is meant to read.
  //
  // NOT staggered, which is the point of moving this here: there is one
  // conductor, so there is nobody to stagger against. `PracticeBot` used this
  // base plus a per-bot rank offset, and the offset is what raced.
  static constexpr int kArrivalDelayMs = 4000;

  // Leaves. Idempotent -- a conductor must be as easy to get rid of as any
  // other bot.
  void part();

  bool isActive() const { return active.load(); }
  const std::string &name() const { return botName; }

protected:
  BotClient::Client *client() { return netClient.get(); }
  double sampleRate() const;

  // Names the band, once. Virtual so a subclass can extend the arrival without
  // this file knowing anything about it.
  virtual void onArrivalDue();

  // A player as the room should hear them named: "vurn (horn)".
  static std::string describe(const std::string &username);

  // Acts on a band-wide play or stop, and says the one thing about it.
  void commandBand(BandControl &band, BotLanguage::Intent intent);

private:
  // A line from the server's voting system. Returns true if it was one, in
  // which case it is not chat and nothing else looks at it.
  bool considerVote(const std::string &text);

  void onRoomMembershipChange(const std::string &username,
                              bool joined) override;
  void onChatMessage(const std::string &type, const std::string &username,
                     const std::string &text) override;

protected:

private:
  std::string botName;
  std::unique_ptr<BotClient::Client> netClient;

  mutable std::mutex stateMutex;
  std::string ownerName;
  std::string bandName;
  double rate = 0.0;

  BandControl *control = nullptr;
  BotAddress::Attention attention;

  // What the band is already behind, so a vote is cast once. Every vote the
  // band casts produces another line from the server, and without this the
  // conductor would answer its own vote with another one.
  struct Backing {
    bool active = false;
    bool isBpm = true;
    int value = 0;
  };
  Backing backing;

  std::unique_ptr<BotClient::Timer> arrivalTimer;
  std::atomic<bool> arrivalDone{false};

  std::atomic<bool> active{false};
};
