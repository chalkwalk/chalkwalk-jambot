#include "../src/TutorBot.h"
#include "JuceUnitShim.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// The tutor's thread, driven directly.
//
// Through a room this is six lines separated by several seconds of audio and a
// key change; here it is six calls. That is the whole reason the tutor takes a
// `BotClient::Client` rather than owning a socket.

namespace {

// Only what a tutor touches. It never transmits, never uses a timer and never
// whispers, so this is much smaller than the fake the players need.
class FakeClient final : public BotClient::Client {
public:
  std::vector<std::string> said;
  std::vector<std::string> channels;
  bool connected = false;
  bool sawChannelCall = false;

  void say(const std::string &who, const std::string &what) {
    for (auto *l : listeners)
      l->onChatMessage("MSG", who, what);
  }

  // One interval of somebody's audio. The content does not matter yet -- the
  // tutor ignores the samples -- but the shape of the call does.
  void plays(const std::string &who, int numSamples = 4800) {
    std::vector<float> block((std::size_t)numSamples, 0.1f);
    for (auto *l : listeners)
      l->onIntervalReceived(who, 0, block.data(), nullptr, numSamples);
  }

  void addListener(BotClient::Listener *l) override { listeners.push_back(l); }
  void removeListener(BotClient::Listener *l) override {
    listeners.erase(std::remove(listeners.begin(), listeners.end(), l),
                    listeners.end());
  }
  void setSampleRate(double) override {}
  void setChannels(const std::vector<std::string> &names) override {
    sawChannelCall = true;
    channels = names;
  }
  void setDefaultRecvEnabled(bool) override {}
  void connect(const std::string &, int, const std::string &,
               const std::string &) override {
    connected = true;
    for (auto *l : listeners)
      l->onConnected();
  }
  void disconnect() override { connected = false; }
  bool isConnected() const override { return connected; }
  std::vector<BotClient::Member> members() const override { return {}; }
  std::vector<BotClient::Peer> peers() const override { return {}; }
  void setRecv(const std::string &, int, bool) override {}
  void sendChat(const std::string &text) override { said.push_back(text); }
  void sendPrivate(const std::string &, const std::string &) override {}
  std::unique_ptr<BotClient::Timer>
  createTimer(std::function<void()>) override {
    return nullptr;
  }
  void transmit(const float *, const float *, int) override {}

private:
  std::vector<BotClient::Listener *> listeners;
};

struct Rig {
  FakeClient *client = nullptr;
  std::unique_ptr<TutorBot> tutor;

  Rig() {
    auto owned = std::make_unique<FakeClient>();
    client = owned.get();
    tutor = std::make_unique<TutorBot>("Tutor", std::move(owned));
    tutor->setOwner("you");
    tutor->join("127.0.0.1", 1234, 48000.0);
  }
};

class TutorBotTests : public shim::UnitTest {
public:
  TutorBotTests() : shim::UnitTest("TutorBot", "bots") {}

  void runTest() override {
    beginTest("it greets on arrival and takes no channel");
    {
      Rig rig;
      expectEquals((int)rig.client->said.size(), 1);
      expect(rig.client->sawChannelCall,
             "the tutor never declared its channels");
      expect(rig.client->channels.empty(),
             "the tutor claimed a channel; it has no audio to put in one");
      expect(rig.tutor->step() == TutorBot::Step::FirstPlayed);
    }

    beginTest("the thread runs in order and the tutor leaves at the end");
    {
      Rig rig;
      rig.client->plays("you");
      expect(rig.tutor->step() == TutorBot::Step::SecondPlayed);

      rig.client->plays("you");
      expect(rig.tutor->step() == TutorBot::Step::KeySet);

      rig.client->say("you", "[key: D minor]");
      expect(rig.tutor->step() == TutorBot::Step::Shaken);

      rig.client->say("you", "shake");
      expect(rig.tutor->step() == TutorBot::Step::Done);

      // Six lines: the five steps plus the sign-off.
      expectEquals((int)rig.client->said.size(), 6);
      expect(!rig.tutor->isActive(), "the tutor stayed after signing off");
      expect(!rig.client->connected, "the tutor did not actually leave");
    }

    beginTest("somebody else playing does not advance the newcomer's thread");
    {
      // A room where another player is already going must not carry the person
      // being taught past the lines about their own first interval.
      Rig rig;
      rig.client->plays("dave");
      rig.client->plays("dave");
      expect(rig.tutor->step() == TutorBot::Step::FirstPlayed,
             "another player's audio advanced the thread");

      rig.client->plays("you");
      expect(rig.tutor->step() == TutorBot::Step::SecondPlayed);
    }

    beginTest("steps are gated in order, not fired by whatever happens first");
    {
      // A key set before a note is played waits. The thread is a sequence, and
      // line 4 only makes sense after line 3 has explained the delay.
      Rig rig;
      rig.client->say("you", "[key: D minor]");
      rig.client->say("you", "shake");
      expect(rig.tutor->step() == TutorBot::Step::FirstPlayed,
             "the thread skipped ahead");
      expectEquals((int)rig.client->said.size(), 1);
    }

    beginTest("it does not teach bots, or answer itself");
    {
      Rig rig;
      rig.client->plays("you");
      rig.client->plays("you");
      const int before = (int)rig.client->said.size();

      rig.client->say("Delvo[bass-bot]", "[key: D minor]");
      expectEquals((int)rig.client->said.size(), before,
                   "a bot's key announcement advanced the thread");

      rig.client->say("Tutor", "[key: D minor]");
      expectEquals((int)rig.client->said.size(), before,
                   "the tutor answered its own line");
    }

    beginTest("an empty interval is not a played one");
    {
      Rig rig;
      rig.client->plays("you", 0);
      expect(rig.tutor->step() == TutorBot::Step::FirstPlayed);
    }
  }
};

TEST_CASE("tutor bot") {
  TutorBotTests t;
  t.runTest();
}

} // namespace
