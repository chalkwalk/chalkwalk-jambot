#pragma once

#include "BandPlayState.h"
#include "BotAddress.h"
#include "BotBand.h"
#include "BotChat.h"
#include "BotClient.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// A bot is a Ninjam client.
//
// Not a server-side fake and not a special case inside NinjamClient: it opens a
// socket and joins a room like any other player. Two things follow, and both
// are the point rather than a side effect.
//
// It can join any server, so the same bots that fill a practice room could sit
// in a real one. `join` takes a host and a port and has no idea which it is.
//
// And it exercises the code you do. A practice room where the other players are
// real clients tests the real path -- the encoder, the relay, the decoder, the
// interval delay, the mixer -- rather than a parallel one built to look like it.
//
// TRANSMIT: the conductor calls renderInterval once per interval, off the audio
// thread, and it goes out through NinjamClient::processCapturedAudio -- the
// same call the plugin makes. That is safe from a non-audio thread by
// construction: it allocates and does file I/O, so it already runs on the
// message thread via callAsync, and writeFull documents being called from
// several threads under a lock (NinjamClient.cpp:199).
//
// LEAVING: a bot must be trivially easy to get rid of. See the rules on
// `part()` below; they live here rather than in PracticeRoom so they hold
// wherever the bot is pointed.
class PracticeBot : private BotClient::Listener {
public:
  // Fills one interval. Called on the conductor thread, never the audio thread,
  // so it may allocate -- though there is no reason for it to.
  using Render = std::function<void(float *left, float *right, int numSamples,
                                    int intervalIndex, BotBand::Phase phase)>;

  // The client is supplied rather than owned outright, which is the whole of
  // the inversion: a bot no longer knows what NinjamClient is. Antiphon passes
  // a `NinjamBotClient`; a standalone jambot would pass something smaller.
  PracticeBot(std::string botName, std::vector<std::string> channelNames,
              BotClient::ClientPtr client);
  ~PracticeBot() override;

  // Silence unless a render is set, which is deliberate: a bot that can join a
  // room and do nothing is the first thing worth proving.
  void setRender(Render r);

  // Play an instrument, and follow the room while doing it.
  //
  // Everything a band member needs to know arrives over the wire -- tempo and
  // BPI from SERVER_CONFIG_CHANGE, the key from a `[key: ...]` chat line, the
  // chords from a Jamtaba-style `| Am | F | C | G |` -- so a bot that follows
  // does so wherever it is pointed, with no orchestrator to tell it. That is
  // why this lives here and not in PracticeRoom.
  void playAs(BotBand::Voice voice, const MusicalKey::Key &key, int bpm,
              int bpi, double sampleRate, std::uint32_t seed);

  // A fresh seed, so the figures change. What "shake" does.
  void shake();

  BotBand::Settings currentSettings() const;

  // Whether this bot is transmitting at all, and how it stops. `BandPlayState`
  // carries the rules; this class only samples it once per interval.
  BandPlayState::State playPhase() const;
  bool isPlaying() const { return playPhase() != BandPlayState::State::Silent; }

  // Asked to play, or to bring it to an end. Both go through `BandPlayState`,
  // so "start during the wrap-up cancels the ending" is decided in one place
  // rather than at each caller.
  void startPlaying();
  void stopPlaying();

  // The commands a bot answers to, beyond parting.
  static bool isShakeCommand(const std::string &text);

  // When this player leaves the room, so does the bot -- but not at once. Empty
  // means nothing but the connection itself ends it. PracticeRoom always sets
  // it.
  void setOwner(std::string ownerUsername);

  // How long to wait for the owner: after they leave, and before they have
  // ever arrived. See PracticeRoom::Config for why the second is longer.
  void setGrace(int afterDepartureMs, int beforeFirstArrivalMs);

  // Who else this bot arrived with, and what the group is called.
  //
  // Needed only for the arrival roster, and only to decide whether to use the
  // band's NAME: two strangers' bots in one room are a list, not a band, and
  // calling them one would be a small lie in the first line anybody reads.
  // A bot told nothing simply lists whoever it can see.

  // Whose audio this bot wants. Empty subscribes to nobody, which is the
  // default and what a generative bot wants: it follows the grid, not the room,
  // and an unsubscribed client never causes an interval to be allocated.
  void setListensTo(std::string username);

  bool join(const std::string &host, int port, double sampleRate);

  // Idempotent, and safe from any thread. Once parted a bot stays parted --
  // there is no rejoin.
  void part();

  bool isActive() const { return active.load(); }
  const std::string &name() const { return botName; }
  BotClient::Client &client() { return *netClient; }

  // IntervalPump thread. A no-op once parted.
  // Latch the phase this bot will render for the interval about to start, and
  // advance the state machine once for it.
  //
  // Split out of `renderInterval` because a band's renders are SPREAD across
  // the interval -- one bot per conductor slice, so the compute does not all
  // land on the boundary -- and a decision taken inside each render is taken
  // at four different times. Asked to stop mid-interval, the bots that had
  // already rendered kept playing an interval longer than the ones that had
  // not, and the last interval of the ending had only half a band in it.
  //
  // So: the WORK is spread and the DECISION is not. The room calls this for
  // every bot at the head of the interval, and each bot then renders whatever
  // was latched for it whenever its turn comes round.
  void beginInterval();

  // Re-read the play state into the latch WITHOUT advancing, for a start that
  // arrives mid-interval and can still be honoured inside it. Advancing twice
  // in one interval would eat an interval of an ending.
  void refreshLatch();

  // What `beginInterval` latched. `Silent` renders and transmits nothing.
  BandPlayState::State latchedPhase() const;

  // True when a start has landed since the latch was taken, so this bot would
  // play if the room re-latched it. The room asks every bot, because the band
  // starts together or not at all.
  bool startPending() const;

  void renderInterval(int numSamples, int intervalIndex);

  // The commands a bot answers to by private message, from anyone in the room.
  static bool isPartCommand(const std::string &text);
  static std::string helpLine(const std::string &botName);

private:
  void onConnected() override;
  void onDisconnected(const std::string &reason) override;
  void onServerConfig(int bpm, int bpi) override;
  void onUserInfoChange() override;
  void onRoomMembershipChange(const std::string &username,
                              bool joined) override;

  static bool isOwnerName(const std::string &username,
                          const std::string &ownerName);

  void onChatMessage(const std::string &type, const std::string &username,
                     const std::string &text) override;


  // The subset that needs no address, because its SYNTAX is unmistakable: a
  // `[key: Dm]` tag and a `| Am | F |` chart. Nobody writes either by accident,
  // and both are things the whole band must agree about, so they are acted on
  // wherever they appear and answered by nobody.
  //
  // Deliberately excludes `shake`, which is an ordinary English word and needs
  // to be aimed at somebody.
  bool handleStructured(const std::string &text,
                        const std::string &username);

  // The room as the addressing engine understands it: who is here, which of
  // them are bots, what each is called and what their channel is named. Built
  // fresh per message, because it is small and staleness here means answering
  // somebody who has left.
  BotAddress::Room currentRoom() const;



  // False once the bot has parted because its owner left.
  bool checkOwnerStillHere();

  // A reply the whole band owes the room, waiting to see whether one of the
  // others says it first.
  //
  // Delay-and-watch rather than a fixed order, for the reason section 5 of
  // docs/BOT-CHAT.md gives: a fixed order can elect a bot that has been told to
  // be quiet, and then the room gets silence where it asked a question. Nobody
  // coordinates and nothing is shared -- each bot waits its own interval and
  // drops the line if it hears one.

  // Counting down to leaving, because the owner is not here.
  //
  // A departure is not a decision: people's connections drop, and a band that
  // vanished on a thirty-second blip could not be got back at all, since there
  // is deliberately no reconnect.
  void onGraceExpired();

  // All three fire on the thread the client delivers callbacks on, which is
  // what lets them read and write the state above without a lock.
  std::unique_ptr<BotClient::Timer> graceTimer;

  int graceMs = 3 * 60 * 1000;
  int initialGraceMs = 6 * 60 * 1000;

  // Everybody in the room who is not a bot and not us.
  int humansPresent() const;

  // The owner has gone, or has not turned up yet. Starts the countdown, and
  // stops the music if there is nobody left to play it to.
  void ownerAbsent(bool everArrived);
  void ownerBack();

  // Every bot in the room right now, sorted so that every bot computes the
  // same list and therefore agrees about who speaks.

  std::string botName;
  // What was passed in at construction. A bot in a band overrides this from
  // its role and instrument the moment `playAs` gives it one; a bot with no
  // voice -- the echo bot, a test's bare bot -- keeps it.
  std::vector<std::string> channels;

  // The channel name last sent to the server, so a re-send happens when it
  // changes and not on every interval. Empty until the first `publishChannel`.
  std::string publishedChannel;

  // Composes `role: instrument` from the current voice and settings, and sends
  // it if it differs from what the room was last told. Cheap enough to call
  // whenever the settings might have moved.
  void publishChannel();
  std::string owner;
  std::string listensTo;
  Render render;

  BotBand::Voice bandVoice = BotBand::Voice::Drums;
  BotBand::Settings settings;
  // Whether this bot has been given a voice at all -- a bot that never had
  // `playAs` called on it is not a band member and follows nothing. Distinct
  // from being SILENT, which is a band member between tunes.
  std::atomic<bool> inBand{false};

  // Guarded by stateMutex, and sampled exactly once per interval at the top of
  // the render: reading it again part-way would tear an interval across two
  // states, and interval delivery is all-or-nothing.
  BandPlayState playState;

  // The phase this interval is being rendered at, taken once at its head. See
  // `beginInterval`.
  BandPlayState::State latched = BandPlayState::State::Silent;
  bool latchTaken = false;

  BotClient::ClientPtr netClient;
  // Two channels, kept between intervals. A bot renders into these and hands
  // the pointers to the client, which is the only place audio crosses out.
  std::vector<float> renderLeft, renderRight;

  // The arrival choreography (docs/BOT-CHAT.md section 6).
  //
  // A bot announces the band unless somebody has already announced IT.
  //
  // Self-referential on purpose. A bot cannot know whether it is the first to
  // arrive -- the membership list has not come through when `onConnected`
  // fires, so every bot sees an empty room -- but it can always know whether it
  // has been introduced, because that is observed rather than inferred. One
  // question covers the ordinary startup, a bot arriving an hour late, and a
  // band whose other members never connected.

  // One conversation, with one person. Belongs to whoever opened it, not to
  // the room -- two other people talking are not talking to the bot.
  BotAddress::Attention attention;

  // Where the key and the chart CAME FROM, which a bot must say when it
  // reports either. Both always have a value -- a room starts in C major and a
  // key implies a chart -- so reporting one without its provenance tells the
  // room it agreed on something nobody chose. Tracked here because only this
  // class sees the message that changed them.
  BotAnswer::Source keySource = BotAnswer::Source::Defaulted;
  std::string keySetBy;
  BotAnswer::Source chartSource = BotAnswer::Source::Defaulted;

  // Told to stop talking. Per bot rather than per band, so one voice can be
  // hushed without silencing the room -- and atomic because it is read on
  // every message and written from the same thread that reads it.
  std::atomic<bool> chatMuted{false};

  // The room and this bot, in the shape the pure answering code takes.
  BotChat::Context currentContext() const;

  std::atomic<bool> active{false};
  std::atomic<bool> sawOwner{false};
  double rate = 48000.0;

  mutable std::mutex stateMutex;

  PracticeBot(const PracticeBot &) = delete;
  PracticeBot &operator=(const PracticeBot &) = delete;
};
