#include "../src/PracticeBot.h"
#include "FakeBotClient.h"
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
using jambot::test::FakeClient;

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
    // --- the form origin, and the four things that move it ---
    //
    // A form read from the absolute interval index could not restart, and it
    // has to. None of these four triggers has an interval index of its own --
    // they all arrive on the chat thread -- so the bot remembers the pump's
    // and resets to the NEXT one: the interval being rendered already has its
    // letter, and restarting at it would change the form under an interval
    // that is half written.

    beginTest("a key change restarts the form");
    {
      Rig rig;
      rig.bot->renderInterval(4800, 12);
      rig.fake->say("you", "/key F minor");
      expectEquals(rig.bot->formOrigin(), 13,
                   "the form did not restart on a key change");
    }

    beginTest("a chart change restarts the form");
    {
      Rig rig;
      rig.bot->renderInterval(4800, 7);
      rig.fake->say("you", "| i | VII | i | VII |");
      expectEquals(rig.bot->formOrigin(), 8,
                   "the form did not restart on a chart change");
    }

    beginTest("a tempo change restarts the form, and a repeat of it does not");
    {
      // The section LENGTH comes from the tempo and the metre, so a carried
      // vote would otherwise strand the band half way through a section of a
      // length that no longer exists.
      Rig rig;
      rig.bot->renderInterval(4800, 4);
      rig.fake->configures(140, 8);
      expectEquals(rig.bot->formOrigin(), 5, "a tempo change did not restart");

      // The same values again change nothing. This fires with the values
      // already in force whenever somebody joins, and a reset there would put
      // the band back to A every time the room grew.
      rig.bot->renderInterval(4800, 9);
      rig.fake->configures(140, 8);
      expectEquals(rig.bot->formOrigin(), 5,
                   "a config message that changed nothing restarted the form");
    }

    beginTest("a metre change restarts the form too");
    {
      Rig rig;
      rig.bot->renderInterval(4800, 2);
      rig.fake->configures(120, 16);
      expectEquals(rig.bot->formOrigin(), 3);
    }

    beginTest("coming in from silence starts the form, and staying in does not");
    {
      Rig rig;
      rig.bot->renderInterval(4800, 6);
      rig.bot->startPlaying();
      expectEquals(rig.bot->formOrigin(), 7,
                   "the band came in mid-structure");

      // Already playing: a second start is not a new tune.
      rig.bot->renderInterval(4800, 20);
      rig.bot->startPlaying();
      expectEquals(rig.bot->formOrigin(), 7,
                   "a second start restarted a form already running");
    }

    beginTest("a bot names its own channel `role: instrument`");
    {
      Rig rig;
      expect(!rig.fake->channelNames.empty(), "no channel was ever published");

      const auto &latest = rig.fake->channelNames.back();
      expect(latest.rfind("chords: ", 0) == 0,
             "the keyboard's channel does not lead with its ROLE -- got '" +
                 latest + "'");
      expect(latest.size() > std::string("chords: ").size(),
             "the role was published with no instrument after it");

      // The role is not the voice's own name. `voiceName` says how the part is
      // MADE and the role says what it does; they are separable, and section
      // 16 separates them.
      expect(latest.find("Keys") == std::string::npos,
             "the channel name leaked the synthesis name");
    }

    beginTest("a shake renames the strip, and only when it changed");
    {
      // The seed chose the instrument, so rerolling the seed can change it --
      // and every client's mixer has to be told, because the strip is how
      // anybody knows what they are listening to.
      Rig rig;
      const auto before = rig.fake->channelNames.size();

      // Enough shakes that the instrument is overwhelmingly likely to move at
      // least once; asserting a specific reroll would be asserting the hash.
      // Addressed by name, which is the form proven in BotChatTests. A BARE
      // "shake" did not reshuffle this bot, with or without bandmates set --
      // see the note in ROADMAP.md; it is a question about addressing rather
      // than about naming, and it is not this test's to answer.
      for (int i = 0; i < 40; ++i)
        rig.fake->say("you", "Ravo: shake");

      expect(rig.fake->channelNames.size() > before,
             "forty shakes never renamed the channel");

      // No entry repeats the one before it: a re-send that says nothing is a
      // broadcast to the whole room for no reason.
      for (std::size_t i = 1; i < rig.fake->channelNames.size(); ++i)
        expect(rig.fake->channelNames[i] != rig.fake->channelNames[i - 1],
               "the same channel name was published twice in a row");
    }

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

  }
};

TEST_CASE("practice bot") {
  PracticeBotTests t;
  t.runTest();
}

} // namespace
