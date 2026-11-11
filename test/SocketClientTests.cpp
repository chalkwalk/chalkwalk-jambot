// SocketClient against a scripted server on loopback: the server half is the
// library's own builders, so what is checked is the client's handling of a
// real byte stream, not of a fake interface.

#include "SocketClient.h"

#include <chalkwalk/ninjam/NinjamProtocol.h>
#include <chalkwalk/ninjam/VorbisCodec.h>

#include <catch2/catch_test_macros.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0  // macOS: SO_NOSIGPIPE is set on the socket instead
#endif

namespace proto = chalkwalk::ninjam::protocol;
using chalkwalk::ninjam::ByteBuffer;

namespace {

struct Frame {
  std::uint8_t type = 0;
  ByteBuffer payload;
};

// The server's end: blocking, one client, one script.
class ScriptedServer {
public:
  ScriptedServer() {
    listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(listenFd, (sockaddr *)&a, sizeof a);
    ::listen(listenFd, 1);
    socklen_t len = sizeof a;
    ::getsockname(listenFd, (sockaddr *)&a, &len);
    port = ntohs(a.sin_port);
  }
  ~ScriptedServer() {
    if (fd >= 0)
      ::close(fd);
    ::close(listenFd);
  }
  void accept() {
    fd = ::accept(listenFd, nullptr, nullptr);
    timeval tv{5, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#ifdef SO_NOSIGPIPE
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
  }
  void send(std::uint8_t type, const ByteBuffer &payload) {
    std::uint8_t h[proto::kHeaderSize];
    proto::writeFrameHeader(h, type, (std::uint32_t)payload.size());
    ByteBuffer b(h, h + proto::kHeaderSize);
    b.insert(b.end(), payload.begin(), payload.end());
    ::send(fd, b.data(), b.size(), MSG_NOSIGNAL);
  }
  // The next frame that is not a keepalive.
  Frame next() {
    while (true) {
      std::uint8_t h[proto::kHeaderSize];
      if (!readAll(h, sizeof h))
        return {};
      proto::FrameHeader fh;
      proto::readFrameHeader(h, fh);
      Frame f;
      f.type = fh.type;
      f.payload.resize(fh.length);
      if (fh.length && !readAll(f.payload.data(), fh.length))
        return {};
      if (f.type != proto::KeepAlive)
        return f;
    }
  }
  void hangUp() {
    ::shutdown(fd, SHUT_RDWR);
  }
  int port = 0;

private:
  bool readAll(void *dst, size_t n) {
    auto *p = static_cast<std::uint8_t *>(dst);
    while (n) {
      const ssize_t r = ::recv(fd, p, n, 0);
      if (r <= 0)
        return false;
      p += r;
      n -= (size_t)r;
    }
    return true;
  }
  int listenFd = -1, fd = -1;
};

// What the bot was told, and on which thread.
struct Recorder : BotClient::Listener {
  std::mutex m;
  std::condition_variable cv;
  std::vector<std::string> events;
  std::thread::id thread;
  bool oneThread = true;
  std::vector<float> left, right;
  bool mono = false;

  void note(const std::string &e) {
    std::lock_guard<std::mutex> g(m);
    if (thread == std::thread::id())
      thread = std::this_thread::get_id();
    oneThread = oneThread && thread == std::this_thread::get_id();
    events.push_back(e);
    cv.notify_all();
  }
  bool waitFor(const std::string &e) {
    std::unique_lock<std::mutex> g(m);
    return cv.wait_for(g, std::chrono::seconds(5), [&] {
      return std::find(events.begin(), events.end(), e) != events.end();
    });
  }
  void onConnected() override { note("connected"); }
  void onDisconnected(const std::string &why) override { note("disconnected: " + why); }
  void onServerConfig(int bpm, int bpi) override {
    note("config " + std::to_string(bpm) + "/" + std::to_string(bpi));
  }
  void onUserInfoChange() override { note("userinfo"); }
  void onRoomMembershipChange(const std::string &u, bool joined) override {
    note((joined ? "join " : "part ") + u);
  }
  void onChatMessage(const std::string &type, const std::string &u,
                     const std::string &text) override {
    note(type + " " + u + ": " + text);
  }
  void onIntervalReceived(const std::string &u, int ch, const float *l, const float *r,
                          int n) override {
    {
      std::lock_guard<std::mutex> g(m);
      left.assign(l, l + n);
      mono = r == nullptr;
      if (r)
        right.assign(r, r + n);
    }
    note("interval " + u + " " + std::to_string(ch));
  }
};

ByteBuffer vorbisInterval(int rate, int frames, double hz) {
  chalkwalk::ninjam::VorbisEncoder enc(rate, 2, 128, 7);
  std::vector<float> inter((size_t)frames * 2);
  for (int i = 0; i < frames; ++i) {
    const float s = 0.5f * (float)std::sin(2.0 * 3.141592653589793 * hz * i / rate);
    inter[(size_t)i * 2] = s;
    inter[(size_t)i * 2 + 1] = -s;
  }
  enc.encode(inter.data(), frames);
  enc.encode(nullptr, 0);
  const auto *d = static_cast<const std::uint8_t *>(enc.data());
  return ByteBuffer(d, d + enc.available());
}

} // namespace

TEST_CASE("SocketClient logs in, hears the room, and talks to it", "[socket]") {
  ScriptedServer server;
  Recorder rec;
  BotClient::SocketClient client;
  client.addListener(&rec);
  client.setSampleRate(48000);
  client.setChannels({});
  client.setDefaultRecvEnabled(true);
  client.connect("127.0.0.1", server.port, "anonymous:balcony", "");
  server.accept();

  const std::uint8_t challenge[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  server.send(proto::ServerAuthChallenge, proto::buildAuthChallenge(challenge));
  Frame auth = server.next();
  REQUIRE(auth.type == proto::ClientAuthUser);
  proto::AuthUser au;
  REQUIRE(proto::parseAuthUser(auth.payload, au));
  CHECK(au.username == "anonymous:balcony");
  CHECK((au.caps & 1u) == 1u);
  std::uint8_t expect[20];
  proto::computeAuthHash("anonymous:balcony", "", challenge, expect);
  CHECK(std::equal(expect, expect + 20, au.hash));

  server.send(proto::ServerAuthReply, proto::buildAuthReply(true, "balcony@127.0.0.x"));
  Frame chans = server.next();
  CHECK(chans.type == proto::ClientSetChannelInfo);
  REQUIRE(rec.waitFor("connected"));
  CHECK(client.isConnected());
  CHECK(client.serverUsername() == "balcony@127.0.0.x");

  server.send(proto::ServerConfigChange, proto::buildServerConfig(120, 8));
  REQUIRE(rec.waitFor("config 120/8"));

  server.send(proto::ChatMessage, proto::buildChat("JOIN", "alice@1.2.3.x"));
  REQUIRE(rec.waitFor("join alice@1.2.3.x"));

  proto::UserInfoEntry e;
  e.active = true;
  e.channelIndex = 0;
  e.volume = -60; // -6 dB
  e.pan = -127;   // hard left
  e.username = "alice@1.2.3.x";
  e.channelName = "guitar";
  server.send(proto::ServerUserInfoChange, proto::buildUserInfo({e}));
  REQUIRE(rec.waitFor("userinfo"));

  // Deaf by default is off, so the new channel is subscribed at once.
  Frame mask = server.next();
  REQUIRE(mask.type == proto::ClientSetUsermask);
  std::vector<proto::UsermaskEntry> masks;
  REQUIRE(proto::parseUsermask(mask.payload, masks));
  REQUIRE(masks.size() == 1);
  CHECK(masks[0].username == "alice@1.2.3.x");
  CHECK(masks[0].mask == 1u);

  auto peers = client.peers();
  REQUIRE(peers.size() == 1);
  REQUIRE(peers[0].channels.size() == 1);
  CHECK(peers[0].channels[0].name == "guitar");
  CHECK(peers[0].channels[0].recvEnabled);
  CHECK(peers[0].channels[0].volumeDb == -6.0f);
  CHECK(peers[0].channels[0].pan == -1.0f);
  auto members = client.members();
  REQUIRE(members.size() == 1);
  CHECK(members[0].channelCount == 1);

  // An interval at 44.1 kHz, in two chunks, arrives resampled to 48 kHz.
  const ByteBuffer ogg = vorbisInterval(44100, 44100, 441.0);
  std::uint8_t guid[16];
  for (int i = 0; i < 16; ++i)
    guid[i] = (std::uint8_t)(i + 1);
  const char fourcc[4] = {'O', 'G', 'G', 'v'};
  server.send(proto::DownloadIntervalBegin,
              proto::buildIntervalBegin(guid, (std::uint32_t)ogg.size(), fourcc, 0,
                                        "alice@1.2.3.x"));
  const size_t half = ogg.size() / 2;
  server.send(proto::DownloadIntervalWrite,
              proto::buildIntervalWrite(guid, false, ogg.data(), (int)half));
  server.send(proto::DownloadIntervalWrite,
              proto::buildIntervalWrite(guid, true, ogg.data() + half,
                                        (int)(ogg.size() - half)));
  REQUIRE(rec.waitFor("interval alice@1.2.3.x 0"));
  {
    std::lock_guard<std::mutex> g(rec.m);
    CHECK_FALSE(rec.mono);
    // One second in; a resampler's worth of latency short of one second out.
    CHECK(rec.left.size() > 47900);
    CHECK(rec.left.size() <= 48000);
    float peak = 0;
    for (size_t i = 4000; i < rec.left.size(); ++i)
      peak = std::max(peak, std::fabs(rec.left[i]));
    CHECK(peak > 0.4f);
    CHECK(peak < 0.6f);
    CHECK(std::fabs(rec.left[10000] + rec.right[10000]) < 0.05f);
  }

  // An empty interval (the zero GUID) is the player's silence: not delivered.
  const std::uint8_t zero[16] = {};
  server.send(proto::DownloadIntervalBegin,
              proto::buildIntervalBegin(zero, 0, fourcc, 0, "alice@1.2.3.x"));

  server.send(proto::ChatMessage, proto::buildChat("MSG", "alice@1.2.3.x", "hello balcony"));
  REQUIRE(rec.waitFor("MSG alice@1.2.3.x: hello balcony"));

  client.sendChat("<balcony> <Pat> lovely");
  Frame chat = server.next();
  REQUIRE(chat.type == proto::ChatMessage);
  proto::Chat c;
  REQUIRE(proto::parseChat(chat.payload, c));
  CHECK(c.type == "MSG");
  CHECK(c.p1 == "<balcony> <Pat> lovely");

  client.setRecv("alice@1.2.3.x", 0, false);
  Frame unmask = server.next();
  REQUIRE(unmask.type == proto::ClientSetUsermask);
  std::vector<proto::UsermaskEntry> unmasks;
  REQUIRE(proto::parseUsermask(unmask.payload, unmasks));
  REQUIRE(unmasks.size() == 1);
  CHECK(unmasks[0].mask == 0u);

  server.send(proto::ChatMessage, proto::buildChat("PART", "alice@1.2.3.x"));
  REQUIRE(rec.waitFor("part alice@1.2.3.x"));
  CHECK(client.members().empty());
  CHECK(client.peers().empty());

  server.hangUp();
  REQUIRE(rec.waitFor("disconnected: the server closed the connection"));
  CHECK_FALSE(client.isConnected());
  std::lock_guard<std::mutex> g(rec.m);
  CHECK(rec.oneThread);
  CHECK(std::count(rec.events.begin(), rec.events.end(),
                   "interval alice@1.2.3.x 0") == 1);
}

TEST_CASE("SocketClient reports a refused login and stays down", "[socket]") {
  ScriptedServer server;
  Recorder rec;
  BotClient::SocketClient client;
  client.addListener(&rec);
  client.connect("127.0.0.1", server.port, "balcony", "wrong");
  server.accept();
  const std::uint8_t challenge[8] = {};
  server.send(proto::ServerAuthChallenge, proto::buildAuthChallenge(challenge));
  CHECK(server.next().type == proto::ClientAuthUser);
  server.send(proto::ServerAuthReply, proto::buildAuthReply(false, "invalid login/password"));
  REQUIRE(rec.waitFor("disconnected: the server refused us: invalid login/password"));
  CHECK_FALSE(client.isConnected());
}

TEST_CASE("SocketClient timers fire on the callback thread and cancel", "[socket]") {
  ScriptedServer server;
  Recorder rec;
  BotClient::SocketClient client;
  client.addListener(&rec);
  client.connect("127.0.0.1", server.port, "balcony", "");
  server.accept();
  const std::uint8_t challenge[8] = {};
  server.send(proto::ServerAuthChallenge, proto::buildAuthChallenge(challenge));
  server.next();
  server.send(proto::ServerAuthReply, proto::buildAuthReply(true, "balcony"));
  REQUIRE(rec.waitFor("connected"));

  auto fired = client.createTimer([&] { rec.note("fired"); });
  auto cancelled = client.createTimer([&] { rec.note("cancelled fired"); });
  cancelled->start(50);
  fired->start(100);
  CHECK(fired->isRunning());
  cancelled->stop();
  REQUIRE(rec.waitFor("fired"));
  CHECK_FALSE(fired->isRunning());
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Disconnecting from a callback is allowed and is terminal.
  auto bye = client.createTimer([&] { client.disconnect(); });
  bye->start(0);
  REQUIRE(rec.waitFor("disconnected: disconnected"));
  fired.reset();
  cancelled.reset();
  bye.reset();
  std::lock_guard<std::mutex> g(rec.m);
  CHECK(std::count(rec.events.begin(), rec.events.end(), "cancelled fired") == 0);
  CHECK(rec.oneThread);
}

TEST_CASE("SocketClient transmits in frames the stock server accepts", "[socket]") {
  // ninjamsrv closes the connection on a frame payload over 16 KiB; found
  // against the real server, when the first interval sent dropped the bot.
  ScriptedServer server;
  Recorder rec;
  BotClient::SocketClient client;
  client.addListener(&rec);
  client.setSampleRate(48000);
  client.setChannels({"voice"});
  client.connect("127.0.0.1", server.port, "balcony", "");
  server.accept();
  const std::uint8_t challenge[8] = {};
  server.send(proto::ServerAuthChallenge, proto::buildAuthChallenge(challenge));
  server.next();
  server.send(proto::ServerAuthReply, proto::buildAuthReply(true, "balcony"));
  REQUIRE(server.next().type == proto::ClientSetChannelInfo);
  REQUIRE(rec.waitFor("connected"));

  // Noise does not compress: four seconds of it is far more than one frame.
  std::vector<float> l(192000), r(192000);
  std::uint32_t seed = 1;
  for (size_t i = 0; i < l.size(); ++i) {
    seed = seed * 1664525u + 1013904223u;
    l[i] = (float)((int)(seed >> 8) % 2000 - 1000) / 1000.0f;
    r[i] = -l[i];
  }
  client.transmit(l.data(), r.data(), (int)l.size());

  Frame begin = server.next();
  REQUIRE(begin.type == proto::UploadIntervalBegin);
  REQUIRE(begin.payload.size() == 25);
  size_t total = 0;
  int frames = 0;
  ByteBuffer ogg;
  while (true) {
    Frame f = server.next();
    REQUIRE(f.type == proto::UploadIntervalWrite);
    CHECK(f.payload.size() <= 16384);
    proto::IntervalWrite w;
    REQUIRE(proto::parseIntervalWrite(f.payload, w));
    const auto *a = static_cast<const std::uint8_t *>(w.audioData);
    ogg.insert(ogg.end(), a, a + w.audioSize);
    total += (size_t)w.audioSize;
    ++frames;
    if (w.isFinal)
      break;
  }
  CHECK(frames > 2);
  chalkwalk::ninjam::VorbisDecoder dec;
  dec.decode(ogg.data(), (int)ogg.size());
  CHECK(dec.available() / 2 > 190000);
  CHECK(total == ogg.size());
}
