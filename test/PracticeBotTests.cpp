#include "../src/PracticeBot.h"
#include "JuceUnitShim.h"

#include <algorithm>

// PracticeBot, with no socket and no room.
//
// It could not be tested this way before: it owned a `NinjamClient`, so every
// question about it -- does it answer, does it leave, does it stop rendering --
// needed a server, a thread and several seconds of waiting. `PracticeRoomTests`
// does that and takes three minutes, which is why the roadmap has carried "no
// test file of its own" since the class was written.
//
// The interface is what changes that. A fake client is thirty lines, the bot
// cannot tell the difference, and the answers arrive synchronously.

namespace {

// Records what the bot said and lets a test say what the room did.
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
  void setChannels(const std::vector<std::string> &) override {}
  void setDefaultRecvEnabled(bool) override {}
  void connect(const std::string &, int, const std::string &name,
               const std::string &) override {
    connected = true;
    room.push_back({name, 1});
  }
  void disconnect() override { connected = false; }
  bool isConnected() const override { return connected; }
  std::vector<BotClient::Member> members() const override { return room; }
  std::vector<BotClient::Peer> peers() const override { return {}; }
  void setRecv(const std::string &, int, bool) override {}
  void sendChat(const std::string &text) override { said.push_back(text); }
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
};

struct Rig {
  FakeClient *fake;
  std::unique_ptr<PracticeBot> bot;

  explicit Rig(const std::string &name = "Ravo[keys-bot]") {
    auto client = std::make_unique<FakeClient>();
    fake = client.get();
    bot = std::make_unique<PracticeBot>(name, std::vector<std::string>{"keys"},
                                        std::move(client));
    bot->setOwner("you");
    bot->join("127.0.0.1", 1234, 48000.0);
    bot->playAs(BotBand::Voice::Keys, MusicalKey::parseName("D minor"), 120, 8,
                48000.0, 20260811);
    fake->joins("you");
  }
};

class PracticeBotTests : public shim::UnitTest {
public:
  PracticeBotTests() : shim::UnitTest("PracticeBot", "bots") {}

  void runTest() override {
    beginTest("a bot answers what it is asked, with no room around it");
    {
      Rig rig;
      rig.fake->say("you", "Ravo: what key are we in");
      expect(!rig.fake->said.empty(), "the bot said nothing");
      expect(rig.fake->said.back().find("D minor") != std::string::npos,
             "did not name the key: " + rig.fake->said.back());
    }

    beginTest("an unaddressed line is not answered");
    {
      Rig rig;
      const auto before = rig.fake->said.size();
      rig.fake->say("you", "what key are we in");
      rig.fake->say("you", "the bass is a bit loud");
      expectEquals((int)rig.fake->said.size(), (int)before,
                   "answered a question nobody asked it");
    }

    beginTest("a silent bot puts nothing on the wire");
    {
      // The property that makes an empty room free, asserted directly rather
      // than inferred from a room's phase list.
      Rig rig;
      rig.bot->renderInterval(4800, 0);
      expectEquals(rig.fake->intervalsSent, 0, "a silent bot transmitted");

      rig.bot->startPlaying();
      rig.bot->renderInterval(4800, 1);
      expectEquals(rig.fake->intervalsSent, 1, "a playing bot did not transmit");
    }

    beginTest("an ending is two intervals, and then nothing");
    {
      Rig rig;
      rig.bot->startPlaying();
      rig.bot->stopPlaying();
      for (int i = 0; i < 5; ++i)
        rig.bot->renderInterval(4800, i);
      // Wrap-up and resolve go out; the three after them do not.
      expectEquals(rig.fake->intervalsSent, 2,
                   "the ending was not exactly two intervals");
    }

    beginTest("told to leave, it goes and stays gone");
    {
      Rig rig;
      rig.fake->say("you", "Ravo: leave");
      expect(!rig.bot->isActive(), "the bot did not leave");
      expect(!rig.fake->connected, "the bot left without disconnecting");

      const auto after = rig.fake->said.size();
      rig.fake->say("you", "Ravo: what key are we in");
      expectEquals((int)rig.fake->said.size(), (int)after,
                   "a parted bot went on answering");
    }

    beginTest("the key follows the room, and a chart travels with it");
    {
      Rig rig;
      // From C major so the move is a pure transposition and the expected
      // answer is obvious: vi IV I V, a whole tone up.
      rig.fake->say("you", "[key: C major]");
      rig.fake->say("you", "| Am | F | C | G |");
      rig.fake->say("you", "[key: D major]");

      const auto s = rig.bot->currentSettings();
      expectEquals(s.key.tonic, 2, "the key did not follow");
      const auto chords = Harmony::flatten(s.chart);
      expectEquals((int)chords.size(), 4, "the chart was replaced");
      if (chords.size() == 4)
        expectEquals(chords[0].root, 11, "the chart did not transpose");
    }

    beginTest("no two bots in a band wake close enough to race");
    {
      // The arbitration is delay-and-watch: each bot waits its own delay, then
      // checks whether the answer has already been given. That only works if
      // the winner's line has crossed the server and come back BEFORE the next
      // bot wakes -- so the stagger needs a guaranteed minimum, not an average
      // one.
      //
      // A hash modulo has no minimum. It put two of these four 32ms apart, and
      // on a loaded macOS runner both timers fired in one scheduling wake and
      // both bots posted the roster. It passed on Linux for years by luck.
      const std::vector<std::string> band{"Quado[kit-bot]", "Vessa[bass-bot]",
                                          "Ravo[keys-bot]", "Pemo[lead-bot]"};

      std::vector<int> delays;
      for (const auto &name : band)
        delays.push_back(PracticeBot::speakDelayMs(name, band));
      std::sort(delays.begin(), delays.end());

      for (std::size_t i = 1; i < delays.size(); ++i)
        expect(delays[i] - delays[i - 1] >= PracticeBot::kSpeakStaggerMs,
               "two bots wake " + std::to_string(delays[i] - delays[i - 1]) +
                   "ms apart, which is not enough for the first to be heard");

      // Every bot has to compute the SAME answer, or they do not agree about
      // who speaks. The roster it is ranked in is sorted, so the order does
      // not depend on who asks.
      std::vector<std::string> shuffled{band.rbegin(), band.rend()};
      for (const auto &name : band)
        expectEquals(PracticeBot::speakDelayMs(name, shuffled),
                     PracticeBot::speakDelayMs(name, band),
                     "a bot's delay depends on the order it sees the room in");

      // A bot that is not in the roster still has to answer something, rather
      // than land on top of whoever holds rank 0.
      expect(PracticeBot::speakDelayMs("Zeno[lead-bot]", band) > delays.back(),
             "an unlisted bot collides with the band");
    }

  }
};

TEST_CASE("practice bot") {
  PracticeBotTests t;
  t.runTest();
}

} // namespace
