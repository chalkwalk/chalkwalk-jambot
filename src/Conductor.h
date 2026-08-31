#pragma once

#include "BotClient.h"

#include <atomic>
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

  // Leaves. Idempotent -- a conductor must be as easy to get rid of as any
  // other bot.
  void part();

  bool isActive() const { return active.load(); }
  const std::string &name() const { return botName; }

protected:
  BotClient::Client *client() { return netClient.get(); }
  double sampleRate() const;

private:
  std::string botName;
  std::unique_ptr<BotClient::Client> netClient;

  mutable std::mutex stateMutex;
  std::string ownerName;
  double rate = 0.0;

  std::atomic<bool> active{false};
};
