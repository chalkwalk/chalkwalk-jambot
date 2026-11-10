#pragma once

#include "BotClient.h"

#include <memory>
#include <string>

// The room over a plain TCP socket: `BotClient::Client` with nothing under it
// but POSIX and chalkwalk-ninjam.
//
// ONE THREAD OWNS THE SOCKET. `connect` starts it; every listener callback and
// every timer runs on it, which is the promise `BotClient.h` asks of a host
// (callbacks and timers on one thread, so a bot never races itself). The calls
// out -- chat, recv, transmit -- may come from any thread: they queue, and the
// network thread sends.
//
// Login, the grid, who is here, chat, and intervals both ways. Incoming
// intervals are keyed by GUID (two of one channel overlap in flight), decoded,
// and resampled to `setSampleRate` with one stateful resampler per channel, so
// consecutive intervals of a channel join without a click. A channel that
// arrives empty (the zero GUID: the player sent silence) is not delivered.
//
// NO RECONNECT, as the interface requires. A lost connection is reported once
// through `onDisconnected` and the client is finished.
//
// Not here, deliberately: an audio device, a mixer, playback or an interval
// delay. A host that wants to hear the room, as a listener does, builds those
// from `onIntervalReceived`; a bot does not want them.
//
// POSIX sockets only for now (Linux, macOS). Windows needs Winsock's spelling
// of the same few calls and a socket pair in place of the wake pipe.

namespace BotClient {

class SocketClient : public Client {
public:
  SocketClient();
  // Disconnects and joins the network thread. Timers made by this client must
  // be destroyed first.
  ~SocketClient() override;

  SocketClient(const SocketClient &) = delete;
  SocketClient &operator=(const SocketClient &) = delete;

  void addListener(Listener *listener) override;
  void removeListener(Listener *listener) override;

  void setSampleRate(double sampleRate) override;
  void setChannels(const std::vector<std::string> &names) override;
  void setDefaultRecvEnabled(bool enabled) override;

  // Returns at once; the outcome arrives as `onConnected` or
  // `onDisconnected(reason)`. Connecting twice is ignored.
  void connect(const std::string &host, int port, const std::string &username,
               const std::string &password) override;
  // Safe from any thread, including a callback.
  void disconnect() override;
  bool isConnected() const override;

  std::vector<Member> members() const override;
  std::vector<Peer> peers() const override;
  void setRecv(const std::string &username, int channelIndex,
               bool enabled) override;

  void sendChat(const std::string &text) override;
  void sendPrivate(const std::string &to, const std::string &text) override;
  void transmit(const float *left, const float *right,
                int numSamples) override;

  std::unique_ptr<Timer> createTimer(std::function<void()> onFire) override;

  // The name the server gave us, which is not always the one we asked for:
  // an anonymous login comes back as `name@1.2.3.x`. Empty until connected.
  std::string serverUsername() const;

  struct Impl;

private:
  std::shared_ptr<Impl> p;
};

} // namespace BotClient
