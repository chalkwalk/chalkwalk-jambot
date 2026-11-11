// jambot-band: a practice band in any NINJAM room, from the command line.
//
// The practice room in Antiphon brings its own server; this brings only the
// band, over `SocketClient`, to a room that is already there -- a room of our
// own for a rehearsal, or a test server. The band is the same one: the same
// names, the same voices in the same arranging order, one pump for all of
// them, every bot latching at the head of the interval and rendering after.
//
// Point it only at a room whose owner wants a band in it. The bots announce
// themselves, answer when spoken to, and leave when told ("/msg <bot> leave")
// or when the owner (--owner) has been gone a while.

#include "BotBand.h"
#include "BotNames.h"
#include "IntervalPump.h"
#include "Music.h"
#include "PracticeBot.h"
#include "SocketClient.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop = true; }

// A stock server takes a login without a password only as `anonymous:<name>`;
// Antiphon's practice server takes any name, which is why the bot does not
// say it itself. The room then calls the bot `<name>@1.2.3.x`, which
// `BotNames::handleOf` reads back to the name.
class AnonymousClient : public BotClient::SocketClient {
public:
  void connect(const std::string &host, int port, const std::string &username,
               const std::string &password) override {
    SocketClient::connect(host, port,
                          password.empty() ? "anonymous:" + username : username,
                          password);
  }
};

void usage(const char *argv0) {
  std::fprintf(stderr,
               "usage: %s HOST PORT [options]\n"
               "  --players N   how many bots, 1-4 (default 4): drums, bass, keys, lead\n"
               "  --key NAME    starting key, e.g. \"D minor\" (default C major)\n"
               "  --seed N      the same seed is the same band (default 20260811)\n"
               "  --owner NAME  the player the band follows; it leaves a while after\n"
               "                they do (default: nobody, it stays until stopped)\n"
               "  --seconds N   stop after N seconds (default: until Ctrl-C)\n",
               argv0);
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    usage(argv[0]);
    return 2;
  }
  const std::string host = argv[1];
  const int port = std::atoi(argv[2]);
  int players = 4;
  std::string keyName = "C major";
  std::uint32_t seed = 20260811u;
  std::string owner;
  int seconds = 0;
  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    const bool more = i + 1 < argc;
    if (a == "--players" && more)
      players = std::atoi(argv[++i]);
    else if (a == "--key" && more)
      keyName = argv[++i];
    else if (a == "--seed" && more)
      seed = (std::uint32_t)std::strtoul(argv[++i], nullptr, 10);
    else if (a == "--owner" && more)
      owner = argv[++i];
    else if (a == "--seconds" && more)
      seconds = std::atoi(argv[++i]);
    else {
      usage(argv[0]);
      return 2;
    }
  }
  if (port <= 0 || players < 1 || players > 4) {
    usage(argv[0]);
    return 2;
  }
  const MusicalKey::Key key = chalkwalk::music::Notation::parseName(keyName);
  if (!key.valid) {
    std::fprintf(stderr, "not a key: %s\n", keyName.c_str());
    return 2;
  }

  constexpr double kRate = 48000.0;
  // A first guess at the grid; each bot reads the room's own from
  // SERVER_CONFIG_CHANGE, and the pump follows it below.
  int bpm = 100, bpi = 16;

  const BotBand::Voice voices[] = {BotBand::Voice::Drums, BotBand::Voice::Bass,
                                   BotBand::Voice::Keys, BotBand::Voice::Lead};
  std::vector<std::string> taken;
  if (!owner.empty())
    taken.push_back(owner);
  const auto names = BotNames::bandFor(players, seed, taken);

  std::mutex botsMutex;
  std::vector<std::unique_ptr<PracticeBot>> bots;
  std::uint32_t s = seed;
  for (int i = 0; i < players; ++i) {
    std::string instrument = BotBand::voiceName(voices[i]);
    for (auto &c : instrument)
      c = (char)std::tolower((unsigned char)c);
    auto bot = std::make_unique<PracticeBot>(
        BotNames::usernameFor(names[(size_t)i], instrument),
        std::vector<std::string>{instrument},
        std::make_unique<AnonymousClient>());
    if (!owner.empty())
      bot->setOwner(owner);
    bot->playAs(voices[i], key, bpm, bpi, kRate, s);
    s = s * 1664525u + 1013904223u;
    bots.push_back(std::move(bot));
  }
  for (auto &b : bots)
    if (!b->join(host, port, kRate)) {
      std::fprintf(stderr, "%s could not join %s:%d\n", b->name().c_str(),
                   host.c_str(), port);
      return 1;
    }
  for (auto &b : bots) {
    std::printf("joined: %s\n", b->name().c_str());
    b->startPlaying();
  }
  std::fflush(stdout);

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  // One pump for the band: latch every bot at the head of the interval, then
  // render each, so the band starts and ends together.
  jambot::IntervalPump pump;
  const auto startPump = [&](int pbpm, int pbpi) {
    const double intervalSeconds = 60.0 * pbpi / pbpm;
    const int samples = (int)(intervalSeconds * kRate + 0.5);
    pump.start(intervalSeconds, [&, samples](int intervalIndex) {
      std::lock_guard<std::mutex> lock(botsMutex);
      for (auto &b : bots)
        b->beginInterval();
      for (auto &b : bots)
        if (b->isActive())
          b->renderInterval(samples, intervalIndex);
    });
  };
  startPump(bpm, bpi);

  const auto started = std::chrono::steady_clock::now();
  while (!g_stop) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    bool anyActive = false;
    BotBand::Settings now{};
    {
      std::lock_guard<std::mutex> lock(botsMutex);
      for (auto &b : bots)
        if (b->isActive()) {
          if (!anyActive)
            now = b->currentSettings();
          anyActive = true;
        }
    }
    if (!anyActive) {
      std::printf("the whole band has left\n");
      break;
    }
    // The room changed its grid (a vote, or a server not at 100/16).
    if (now.bpm > 0 && now.bpi > 0 && (now.bpm != bpm || now.bpi != bpi)) {
      bpm = now.bpm;
      bpi = now.bpi;
      std::printf("grid: %d bpm, %d bpi\n", bpm, bpi);
      std::fflush(stdout);
      startPump(bpm, bpi);
    }
    if (seconds > 0 && std::chrono::steady_clock::now() - started >
                           std::chrono::seconds(seconds))
      break;
  }

  pump.stop();
  for (auto &b : bots)
    b->part();
  // Let the goodbyes go out before the clients are torn down.
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  bots.clear();
  std::printf("stopped\n");
  return 0;
}
