#pragma once

#include "../src/BotClient.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// The BotClient::Client a suite drives instead of a socket.
//
// Shared, because three suites need one and a forked double stops telling you
// the same truth. The two it replaces had diverged in four behaviours, and one
// of them was wrong about the real client -- see `connect` below.

namespace jambot::test {

class FakeClient final : public BotClient::Client {
public:
  std::vector<std::string> said;
  std::vector<std::pair<std::string, std::string>> whispered;
  std::vector<BotClient::Member> room;
  int intervalsSent = 0;
  bool connected = false;

  void say(const std::string &who, const std::string &what) {
    for (auto *l : listeners)
      l->onChatMessage("MSG", who, what);
  }
  void joins(const std::string &who) {
    room.push_back({who, 1});
    for (auto *l : listeners)
      l->onRoomMembershipChange(who, true);
  }
  // One interval of somebody's audio, at a level that reads as playing.
  void plays(const std::string &who, int numSamples = 48000) {
    std::vector<float> block((std::size_t)numSamples, 0.1f);
    sends(who, block);
  }

  // One interval that arrives and is audible, but only just.
  void playsQuietly(const std::string &who, int numSamples = 48000) {
    std::vector<float> block((std::size_t)numSamples, 0.003f);
    sends(who, block);
  }

  // One interval of full-scale sound, and one of a single-sample spike.
  void playsClipped(const std::string &who, int numSamples = 48000) {
    std::vector<float> block((std::size_t)numSamples, 1.0f);
    sends(who, block);
  }

  void playsClicks(const std::string &who, int numSamples = 48000) {
    std::vector<float> block((std::size_t)numSamples, 0.0f);
    block[(std::size_t)(numSamples / 2)] = 1.0f;
    sends(who, block);
  }

  // One interval with nothing in it. Not the same as no interval at all: the
  // audio path is working and carrying silence, which is the case the tutor's
  // first diagnostic row is about.
  void playsNothing(const std::string &who, int numSamples = 48000) {
    std::vector<float> block((std::size_t)numSamples, 0.0f);
    sends(who, block);
  }

  void sends(const std::string &who, std::vector<float> &block) {
    for (auto *l : listeners)
      l->onIntervalReceived(who, 0, block.data(), nullptr, (int)block.size());
  }

  void leaves(const std::string &who) {
    room.erase(std::remove_if(room.begin(), room.end(),
                              [&](const auto &m) { return m.username == who; }),
               room.end());
    for (auto *l : listeners)
      l->onRoomMembershipChange(who, false);
  }

  void addListener(BotClient::Listener *l) override { listeners.push_back(l); }
  void removeListener(BotClient::Listener *l) override {
    listeners.erase(std::remove(listeners.begin(), listeners.end(), l),
                    listeners.end());
  }
  void setSampleRate(double) override {}
  // Every channel name the bot has published, in order. The bot renames its
  // own strip now, so what it sends and WHEN is behaviour rather than setup.
  std::vector<std::string> channelNames;

  // Whether setChannels was called at all, and with what. Separate from
  // `channelNames` because that one drops an empty list, and an empty list is
  // the whole assertion for a bot with no instrument.
  bool sawChannelCall = false;
  std::vector<std::string> lastChannels;

  void setChannels(const std::vector<std::string> &names) override {
    sawChannelCall = true;
    lastChannels = names;
    if (!names.empty())
      channelNames.push_back(names.front());
  }
  void setDefaultRecvEnabled(bool) override {}
  // A client does NOT appear in its own members(). The server skips the
  // connecting client in both JOIN loops (PracticeServer.cpp:390-401), and
  // NinjamClient::roomMembers is filled only from JOINs and other users'
  // USERINFO -- so self is never in there. A double that adds itself teaches a
  // false model, and every consumer in PracticeBot then has to filter it out.
  void connect(const std::string &, int, const std::string &,
               const std::string &) override {
    connected = true;
    for (auto *l : listeners)
      l->onConnected();
  }
  void disconnect() override { connected = false; }
  bool isConnected() const override { return connected; }
  std::vector<BotClient::Member> members() const override { return room; }
  std::vector<BotClient::Peer> peers() const override { return {}; }
  void setRecv(const std::string &, int, bool) override {}
  // Guarded because one tutor test drives the bot from several threads at
  // once, which is the only way to reach the check-and-act the thread is built
  // to survive. An unguarded vector would race in the FAKE and report the bug
  // it was meant to be testing for.
  void sendChat(const std::string &text) override {
    std::lock_guard<std::mutex> sl(saidMutex);
    said.push_back(text);
  }

  std::vector<std::string> saidCopy() const {
    std::lock_guard<std::mutex> sl(saidMutex);
    return said;
  }
  void sendPrivate(const std::string &to, const std::string &text) override {
    whispered.push_back({to, text});
  }
  void transmit(const float *, const float *, int) override { ++intervalsSent; }

  // Timers a test drives by hand. Nothing here waits: `fire()` runs whatever
  // is pending, which is what makes the delayed behaviour -- the roster, the
  // band's one voice, the departure countdown -- testable in microseconds
  // rather than in seconds of sleeping.
  std::unique_ptr<BotClient::Timer> createTimer(
      std::function<void()> onFire) override {
    auto t = std::make_unique<ManualTimer>(std::move(onFire), this);
    return t;
  }

  void fireDueTimers() {
    const auto pending = armed;
    for (auto *t : pending)
      t->fireNow();
  }

  struct ManualTimer final : public BotClient::Timer {
    ManualTimer(std::function<void()> fn, FakeClient *owner)
        : onFire(std::move(fn)), client(owner) {}
    ~ManualTimer() override { stop(); }

    void start(int) override {
      if (!running) {
        running = true;
        client->armed.push_back(this);
      }
    }
    void stop() override {
      running = false;
      client->armed.erase(
          std::remove(client->armed.begin(), client->armed.end(), this),
          client->armed.end());
    }
    bool isRunning() const override { return running; }

    void fireNow() {
      if (!running)
        return;
      stop();
      if (onFire)
        onFire();
    }

    std::function<void()> onFire;
    FakeClient *client;
    bool running = false;
  };

  std::vector<ManualTimer *> armed;

private:
  std::vector<BotClient::Listener *> listeners;
  mutable std::mutex saidMutex;
};

} // namespace jambot::test
