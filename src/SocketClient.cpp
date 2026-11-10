#include "SocketClient.h"

#include <chalkwalk/dsp/Resampler.h>
#include <chalkwalk/ninjam/NinjamProtocol.h>
#include <chalkwalk/ninjam/VorbisCodec.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <thread>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 // macOS: SO_NOSIGPIPE is set on the socket instead
#endif

namespace BotClient {

namespace proto = chalkwalk::ninjam::protocol;
using chalkwalk::ninjam::ByteBuffer;
using Clock = std::chrono::steady_clock;

namespace {

// The server drops a client it has not heard from in a while; three seconds of
// silence is when we speak up, well inside any server's patience.
constexpr auto kKeepAlive = std::chrono::seconds(3);

// Bounds on what a server can make us hold. An interval at 48 kHz stereo is a
// few hundred kilobytes of Vorbis at most; these are far above that and far
// below harm.
constexpr size_t kMaxDownloads = 512;
constexpr size_t kMaxDownloadBytes = 8u << 20;
constexpr size_t kMaxOutQueue = 32u << 20;

struct TimerState {
  std::function<void()> fn;
  Clock::time_point due;
  bool running = false;
  bool alive = true;
};

bool zeroGuid(const std::uint8_t g[16]) {
  for (int i = 0; i < 16; ++i)
    if (g[i])
      return false;
  return true;
}

} // namespace

struct SocketClient::Impl : std::enable_shared_from_this<Impl> {
  // ---- Shared between threads, under `m` ----------------------------------
  mutable std::mutex m;
  std::vector<Listener *> listeners;
  double rate = 48000.0;
  std::vector<std::string> channelNames;
  bool defaultRecv = false;
  ByteBuffer out; // whole frames waiting for the socket
  std::string serverName;
  std::map<std::string, Peer> peers; // only players with channels
  std::set<std::string> members;
  std::vector<std::shared_ptr<TimerState>> timers;

  std::atomic<bool> started{false}, stopping{false}, connected{false};
  std::thread thread;
  int wake[2] = {-1, -1};

  // ---- The network thread's own -------------------------------------------
  std::string host, user, pass;
  int port = 0;
  int fd = -1;
  ByteBuffer in;
  Clock::time_point lastSend = Clock::now();
  std::string reason;

  struct Download {
    std::string user;
    int channel = 0;
    chalkwalk::ninjam::VorbisDecoder dec;
    std::vector<float> interleaved;
    size_t bytes = 0;
  };
  std::map<std::string, std::unique_ptr<Download>> downloads;

  struct Resample {
    double inRate = 0, outRate = 0;
    std::unique_ptr<chalkwalk::dsp::Resampler> l, r;
  };
  std::map<std::pair<std::string, int>, Resample> resamplers;

  ~Impl() {
    for (int f : wake)
      if (f >= 0)
        ::close(f);
  }

  // ---- Outgoing -------------------------------------------------------------
  void queue(std::uint8_t type, const ByteBuffer &payload) {
    std::uint8_t h[proto::kHeaderSize];
    proto::writeFrameHeader(h, type, (std::uint32_t)payload.size());
    {
      std::lock_guard<std::mutex> g(m);
      if (out.size() + payload.size() > kMaxOutQueue)
        return; // a server that will not read is not one to buffer for
      out.insert(out.end(), h, h + proto::kHeaderSize);
      out.insert(out.end(), payload.begin(), payload.end());
    }
    poke();
  }

  void poke() {
    if (wake[1] >= 0) {
      const char c = 1;
      (void)!::write(wake[1], &c, 1);
    }
  }

  void sendUsermask(const std::string &username) {
    std::uint32_t mask = 0;
    {
      std::lock_guard<std::mutex> g(m);
      auto it = peers.find(username);
      if (it != peers.end())
        for (const auto &c : it->second.channels)
          if (c.recvEnabled && c.index >= 0 && c.index < 32)
            mask |= 1u << c.index;
    }
    queue(proto::ClientSetUsermask, proto::buildUsermask({{username, mask}}));
  }

  std::vector<Listener *> snapshot() const {
    std::lock_guard<std::mutex> g(m);
    return listeners;
  }

  // ---- The thread -----------------------------------------------------------
  void run() {
    if (open())
      loop();
    if (fd >= 0)
      ::close(fd);
    fd = -1;
    connected = false;
    if (reason.empty())
      reason = "disconnected";
    for (auto *l : snapshot())
      l->onDisconnected(reason);
  }

  bool open() {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *res = nullptr;
    const std::string service = std::to_string(port);
    if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &res) != 0 || !res) {
      reason = "cannot find " + host;
      return false;
    }
    for (addrinfo *a = res; a; a = a->ai_next) {
      fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
      if (fd < 0)
        continue;
      if (::connect(fd, a->ai_addr, a->ai_addrlen) == 0)
        break;
      ::close(fd);
      fd = -1;
    }
    ::freeaddrinfo(res);
    if (fd < 0) {
      reason = "cannot connect to " + host + ":" + service;
      return false;
    }
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    return true;
  }

  void loop() {
    while (!stopping) {
      int timeoutMs = 1000;
      const auto now = Clock::now();
      auto until = [&](Clock::time_point t) {
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(t - now).count();
        timeoutMs = (int)std::max<long long>(0, std::min<long long>(timeoutMs, ms + 1));
      };
      bool wantWrite;
      {
        std::lock_guard<std::mutex> g(m);
        wantWrite = !out.empty();
        for (const auto &t : timers)
          if (t->running && t->alive)
            until(t->due);
      }
      if (connected)
        until(lastSend + kKeepAlive);

      pollfd fds[2] = {{fd, (short)(POLLIN | (wantWrite ? POLLOUT : 0)), 0},
                       {wake[0], POLLIN, 0}};
      if (::poll(fds, 2, timeoutMs) < 0 && errno != EINTR) {
        reason = "poll failed";
        return;
      }
      if (fds[1].revents & POLLIN) {
        char buf[64];
        while (::read(wake[0], buf, sizeof buf) > 0) {
        }
      }
      if (stopping)
        return;
      if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
        if (!readSome())
          return;
      }
      if (!writeSome())
        return;
      fireTimers();
      if (connected && Clock::now() - lastSend >= kKeepAlive)
        queue(proto::KeepAlive, {});
    }
  }

  bool readSome() {
    std::uint8_t buf[65536];
    while (true) {
      const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
      if (n > 0) {
        in.insert(in.end(), buf, buf + n);
        continue;
      }
      if (n == 0) {
        reason = "the server closed the connection";
        return false;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      if (errno == EINTR)
        continue;
      reason = "connection lost";
      return false;
    }
    size_t at = 0;
    while (in.size() - at >= (size_t)proto::kHeaderSize && !stopping) {
      proto::FrameHeader h;
      if (!proto::readFrameHeader(in.data() + at, h) || h.length > proto::kMaxPayload) {
        reason = "malformed frame from the server";
        return false;
      }
      if (in.size() - at < proto::kHeaderSize + (size_t)h.length)
        break;
      const auto *b = in.data() + at + proto::kHeaderSize;
      ByteBuffer payload(b, b + h.length);
      at += proto::kHeaderSize + h.length;
      if (!handle(h.type, payload))
        return false;
    }
    in.erase(in.begin(), in.begin() + (std::ptrdiff_t)at);
    return true;
  }

  bool writeSome() {
    std::lock_guard<std::mutex> g(m);
    while (!out.empty()) {
      const ssize_t n = ::send(fd, out.data(), out.size(), MSG_NOSIGNAL);
      if (n > 0) {
        out.erase(out.begin(), out.begin() + n);
        lastSend = Clock::now();
        continue;
      }
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        break;
      if (n < 0 && errno == EINTR)
        continue;
      reason = "connection lost";
      return false;
    }
    return true;
  }

  void fireTimers() {
    std::vector<std::shared_ptr<TimerState>> due;
    {
      std::lock_guard<std::mutex> g(m);
      const auto now = Clock::now();
      for (auto &t : timers)
        if (t->alive && t->running && t->due <= now) {
          t->running = false;
          due.push_back(t);
        }
      timers.erase(std::remove_if(timers.begin(), timers.end(),
                                  [](const auto &t) { return !t->alive; }),
                   timers.end());
    }
    for (auto &t : due) {
      bool alive;
      {
        std::lock_guard<std::mutex> g(m);
        alive = t->alive;
      }
      if (alive && !stopping)
        t->fn();
    }
  }

  // ---- What the server says -------------------------------------------------
  bool handle(std::uint8_t type, const ByteBuffer &payload) {
    switch (type) {
    case proto::ServerAuthChallenge: {
      proto::AuthChallenge c;
      if (!proto::parseAuthChallenge(payload, c))
        return fail("malformed login challenge");
      std::uint8_t hash[20];
      proto::computeAuthHash(user, pass, c.challenge, hash);
      // caps 1: we accept the server's licence, if it has one. A bot cannot
      // read it to anybody, and a server that requires it would otherwise
      // wait for an answer that never comes.
      queue(proto::ClientAuthUser, proto::buildAuthUser(hash, user, 1));
      return true;
    }
    case proto::ServerAuthReply: {
      proto::AuthReply r;
      if (!proto::parseAuthReply(payload, r))
        return fail("malformed login reply");
      if (!r.granted)
        return fail("the server refused us: " +
                    (r.errorMessage.empty() ? std::string("no reason given")
                                            : r.errorMessage));
      std::vector<std::string> names;
      {
        std::lock_guard<std::mutex> g(m);
        // On success the server's message field carries the name it gave us.
        serverName = r.errorMessage.empty() ? user : r.errorMessage;
        names = channelNames;
      }
      queue(proto::ClientSetChannelInfo, proto::buildChannelInfo(names));
      connected = true;
      for (auto *l : snapshot())
        l->onConnected();
      return true;
    }
    case proto::ServerConfigChange: {
      proto::ServerConfig c;
      if (!proto::parseServerConfig(payload, c))
        return fail("malformed server config");
      for (auto *l : snapshot())
        l->onServerConfig(c.bpm, c.bpi);
      return true;
    }
    case proto::ServerUserInfoChange:
      return userInfo(payload);
    case proto::DownloadIntervalBegin:
      return intervalBegin(payload);
    case proto::DownloadIntervalWrite:
      return intervalWrite(payload);
    case proto::ChatMessage:
      return chat(payload);
    default:
      return true; // keepalives, and anything newer than we are
    }
  }

  bool fail(const std::string &why) {
    reason = why;
    return false;
  }

  bool userInfo(const ByteBuffer &payload) {
    std::vector<proto::UserInfoEntry> entries;
    if (!proto::parseUserInfo(payload, entries))
      return fail("malformed user info");
    std::set<std::string> resubscribe;
    {
      std::lock_guard<std::mutex> g(m);
      for (const auto &e : entries) {
        auto &chans = peers[e.username].channels;
        auto it = std::find_if(chans.begin(), chans.end(),
                               [&](const Channel &c) { return c.index == e.channelIndex; });
        if (!e.active) {
          if (it != chans.end())
            chans.erase(it);
          if (chans.empty())
            peers.erase(e.username);
          continue;
        }
        members.insert(e.username);
        if (it == chans.end()) {
          Channel c;
          c.index = e.channelIndex;
          c.recvEnabled = defaultRecv;
          if (defaultRecv)
            resubscribe.insert(e.username);
          chans.push_back(c);
          it = chans.end() - 1;
        }
        it->name = e.channelName;
        it->volumeDb = (float)e.volume / 10.0f;
        it->pan = std::clamp((float)e.pan / 127.0f, -1.0f, 1.0f);
        std::sort(chans.begin(), chans.end(),
                  [](const Channel &a, const Channel &b) { return a.index < b.index; });
      }
    }
    for (const auto &u : resubscribe)
      sendUsermask(u);
    for (auto *l : snapshot())
      l->onUserInfoChange();
    return true;
  }

  bool chat(const ByteBuffer &payload) {
    proto::Chat c;
    if (!proto::parseChat(payload, c))
      return fail("malformed chat");
    const bool join = c.type == "JOIN", part = c.type == "PART";
    if ((join || part) && !c.p1.empty()) {
      {
        std::lock_guard<std::mutex> g(m);
        if (join) {
          members.insert(c.p1);
        } else {
          members.erase(c.p1);
          peers.erase(c.p1);
        }
      }
      if (part) {
        for (auto it = resamplers.begin(); it != resamplers.end();)
          it = it->first.first == c.p1 ? resamplers.erase(it) : std::next(it);
      }
      for (auto *l : snapshot())
        l->onRoomMembershipChange(c.p1, join);
    }
    for (auto *l : snapshot())
      l->onChatMessage(c.type, c.p1, c.p2);
    return true;
  }

  bool intervalBegin(const ByteBuffer &payload) {
    proto::IntervalBegin b;
    if (!proto::parseIntervalBegin(payload, b))
      return fail("malformed interval");
    if (zeroGuid(b.guid) || !b.isOggAudio() || downloads.size() >= kMaxDownloads)
      return true;
    auto d = std::make_unique<Download>();
    d->user = b.username;
    d->channel = b.channelIndex;
    downloads[b.guidHex] = std::move(d);
    return true;
  }

  bool intervalWrite(const ByteBuffer &payload) {
    proto::IntervalWrite w;
    if (!proto::parseIntervalWrite(payload, w))
      return fail("malformed interval data");
    auto it = downloads.find(w.guidHex);
    if (it == downloads.end())
      return true;
    Download &d = *it->second;
    d.bytes += (size_t)w.audioSize;
    if (d.bytes > kMaxDownloadBytes) {
      downloads.erase(it);
      return true;
    }
    if (w.audioSize > 0)
      d.dec.decode(w.audioData, w.audioSize);
    while (d.dec.available() > 0) {
      const int n = d.dec.available();
      d.interleaved.insert(d.interleaved.end(), d.dec.pcm(), d.dec.pcm() + n);
      d.dec.skip(n);
    }
    if (w.isFinal) {
      deliver(d);
      downloads.erase(it);
    }
    return true;
  }

  void deliver(Download &d) {
    const int chans = d.dec.numChannels();
    const int srcRate = d.dec.sampleRate();
    if (chans <= 0 || srcRate <= 0 || d.interleaved.empty())
      return;
    const size_t frames = d.interleaved.size() / (size_t)chans;
    std::vector<float> l(frames), r;
    if (chans > 1)
      r.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
      l[i] = d.interleaved[i * (size_t)chans];
      if (chans > 1)
        r[i] = d.interleaved[i * (size_t)chans + 1];
    }
    double outRate;
    {
      std::lock_guard<std::mutex> g(m);
      outRate = rate;
    }
    if (std::fabs(outRate - srcRate) > 0.5) {
      // One resampler per channel across intervals, so they join seamlessly.
      auto &rs = resamplers[{d.user, d.channel}];
      if (rs.inRate != srcRate || rs.outRate != outRate) {
        rs.inRate = srcRate;
        rs.outRate = outRate;
        rs.l = std::make_unique<chalkwalk::dsp::Resampler>(srcRate, outRate);
        rs.r = std::make_unique<chalkwalk::dsp::Resampler>(srcRate, outRate);
      }
      std::vector<float> lo, ro;
      rs.l->process(l.data(), (int)l.size(), lo);
      l.swap(lo);
      if (chans > 1) {
        rs.r->process(r.data(), (int)r.size(), ro);
        r.swap(ro);
      }
    }
    const int n = (int)(r.empty() ? l.size() : std::min(l.size(), r.size()));
    for (auto *lis : snapshot())
      lis->onIntervalReceived(d.user, d.channel, l.data(), r.empty() ? nullptr : r.data(), n);
  }
};

// ---- The timer ---------------------------------------------------------------

namespace {
class SocketTimer : public Timer {
public:
  SocketTimer(std::weak_ptr<SocketClient::Impl> impl, std::shared_ptr<TimerState> st)
      : impl_(std::move(impl)), st_(std::move(st)) {}
  ~SocketTimer() override {
    if (auto p = impl_.lock()) {
      std::lock_guard<std::mutex> g(p->m);
      st_->alive = false;
      st_->running = false;
    }
  }
  void start(int delayMs) override {
    if (auto p = impl_.lock()) {
      {
        std::lock_guard<std::mutex> g(p->m);
        st_->due = Clock::now() + std::chrono::milliseconds(std::max(0, delayMs));
        st_->running = true;
      }
      p->poke();
    }
  }
  void stop() override {
    if (auto p = impl_.lock()) {
      std::lock_guard<std::mutex> g(p->m);
      st_->running = false;
    }
  }
  bool isRunning() const override {
    if (auto p = impl_.lock()) {
      std::lock_guard<std::mutex> g(p->m);
      return st_->running;
    }
    return false;
  }

private:
  std::weak_ptr<SocketClient::Impl> impl_;
  std::shared_ptr<TimerState> st_;
};
} // namespace

// ---- The client ----------------------------------------------------------------

SocketClient::SocketClient() : p(std::make_shared<Impl>()) {
  if (::pipe(p->wake) == 0) {
    for (int f : p->wake)
      ::fcntl(f, F_SETFL, ::fcntl(f, F_GETFL) | O_NONBLOCK);
  }
}

SocketClient::~SocketClient() {
  disconnect();
  if (p->thread.joinable()) {
    if (p->thread.get_id() == std::this_thread::get_id())
      p->thread.detach(); // destroyed from its own callback: it holds `p`
    else
      p->thread.join();
  }
}

void SocketClient::addListener(Listener *l) {
  std::lock_guard<std::mutex> g(p->m);
  if (std::find(p->listeners.begin(), p->listeners.end(), l) == p->listeners.end())
    p->listeners.push_back(l);
}

void SocketClient::removeListener(Listener *l) {
  std::lock_guard<std::mutex> g(p->m);
  p->listeners.erase(std::remove(p->listeners.begin(), p->listeners.end(), l),
                     p->listeners.end());
}

void SocketClient::setSampleRate(double sampleRate) {
  std::lock_guard<std::mutex> g(p->m);
  if (sampleRate > 0)
    p->rate = sampleRate;
}

void SocketClient::setChannels(const std::vector<std::string> &names) {
  {
    std::lock_guard<std::mutex> g(p->m);
    p->channelNames = names;
  }
  if (p->connected)
    p->queue(proto::ClientSetChannelInfo, proto::buildChannelInfo(names));
}

void SocketClient::setDefaultRecvEnabled(bool enabled) {
  std::lock_guard<std::mutex> g(p->m);
  p->defaultRecv = enabled;
}

void SocketClient::connect(const std::string &host, int port,
                           const std::string &username,
                           const std::string &password) {
  if (p->started.exchange(true))
    return;
  p->host = host;
  p->port = port;
  p->user = username;
  p->pass = password;
  auto self = p; // the thread keeps the state alive however it ends
  p->thread = std::thread([self] { self->run(); });
}

void SocketClient::disconnect() {
  p->stopping = true;
  p->poke();
  if (p->thread.joinable() && p->thread.get_id() != std::this_thread::get_id())
    p->thread.join();
}

bool SocketClient::isConnected() const { return p->connected; }

std::vector<Member> SocketClient::members() const {
  std::lock_guard<std::mutex> g(p->m);
  std::vector<Member> out;
  for (const auto &name : p->members) {
    Member mem;
    mem.username = name;
    auto it = p->peers.find(name);
    mem.channelCount = it == p->peers.end() ? 0 : (int)it->second.channels.size();
    out.push_back(mem);
  }
  return out;
}

std::vector<Peer> SocketClient::peers() const {
  std::lock_guard<std::mutex> g(p->m);
  std::vector<Peer> out;
  for (const auto &kv : p->peers) {
    Peer peer = kv.second;
    peer.username = kv.first;
    out.push_back(std::move(peer));
  }
  return out;
}

void SocketClient::setRecv(const std::string &username, int channelIndex, bool enabled) {
  {
    std::lock_guard<std::mutex> g(p->m);
    auto it = p->peers.find(username);
    if (it == p->peers.end())
      return;
    for (auto &c : it->second.channels)
      if (c.index == channelIndex)
        c.recvEnabled = enabled;
  }
  if (p->connected)
    p->sendUsermask(username);
}

void SocketClient::sendChat(const std::string &text) {
  if (p->connected)
    p->queue(proto::ChatMessage, proto::buildChat("MSG", text));
}

void SocketClient::sendPrivate(const std::string &to, const std::string &text) {
  if (p->connected)
    p->queue(proto::ChatMessage, proto::buildChat("PRIVMSG", to, text));
}

void SocketClient::transmit(const float *left, const float *right, int numSamples) {
  if (!p->connected || !left || numSamples <= 0)
    return;
  double rate;
  {
    std::lock_guard<std::mutex> g(p->m);
    if (p->channelNames.empty())
      return; // nothing published to carry it
    rate = p->rate;
  }
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uint8_t guid[16];
  for (auto &b : guid)
    b = (std::uint8_t)rng();
  const int chans = right ? 2 : 1;
  chalkwalk::ninjam::VorbisEncoder enc((int)std::lround(rate), chans, chans == 2 ? 128 : 64,
                                       (int)(rng() & 0x7fffffff));
  std::vector<float> inter((size_t)numSamples * (size_t)chans);
  for (int i = 0; i < numSamples; ++i) {
    inter[(size_t)i * (size_t)chans] = left[i];
    if (right)
      inter[(size_t)i * 2 + 1] = right[i];
  }
  enc.encode(inter.data(), numSamples);
  enc.encode(nullptr, 0);
  const auto *data = static_cast<const std::uint8_t *>(enc.data());
  const int size = enc.available();
  const char fourcc[4] = {'O', 'G', 'G', 'v'};
  p->queue(proto::UploadIntervalBegin, proto::buildIntervalBegin(guid, (std::uint32_t)size, fourcc, 0));
  constexpr int kChunk = 16384;
  for (int at = 0; at < size || at == 0; at += kChunk) {
    const int n = std::min(kChunk, size - at);
    const bool last = at + kChunk >= size;
    p->queue(proto::UploadIntervalWrite, proto::buildIntervalWrite(guid, last, data + at, n));
    if (last)
      break;
  }
}

std::unique_ptr<Timer> SocketClient::createTimer(std::function<void()> onFire) {
  auto st = std::make_shared<TimerState>();
  st->fn = std::move(onFire);
  {
    std::lock_guard<std::mutex> g(p->m);
    p->timers.push_back(st);
  }
  return std::make_unique<SocketTimer>(p, st);
}

std::string SocketClient::serverUsername() const {
  std::lock_guard<std::mutex> g(p->m);
  return p->serverName;
}

} // namespace BotClient
