#include "TutorBot.h"

#include "BotNames.h"

#include <cstddef>

#include <chalkwalk/music/Text.h>
#include <chalkwalk/ninjam/RoomConventions.h>

namespace {

// The six lines.
//
// Plain, lowercase, and each one says a thing that just happened rather than a
// thing that might. `docs/BOT-CHAT.md` section 8 is the voice; the constraint
// that matters here is that none of them is an instruction to go and do
// something -- every one is triggered BY the thing it describes, so it reads as
// somebody noticing rather than a wizard with a next button.
constexpr const char *kGreeting =
    "welcome -- this is a practice room, and the band plays while you do. "
    "say `part` to any of us to send them home.";

constexpr const char *kFirstPlayed =
    "that interval just went out, and nobody has heard it yet -- everything "
    "you play reaches everyone else one interval later.";

constexpr const char *kSecondPlayed =
    "and that is why the band sounds a bar behind you. they are answering the "
    "interval you played last, not this one. that is the form rather than a "
    "fault.";

constexpr const char *kKeySet =
    "the band took that key. chords work the same way -- write a chart like "
    "| Am | F | and they will follow it.";

constexpr const char *kShaken =
    "the parts changed and the tune did not. shake rerolls what everyone "
    "plays, never what you agreed to play.";

// The four diagnostic lines, one per row of section 7's table.
//
// Every one of them is actionable and none is an opinion about music. That is
// the rule the table exists to enforce: the reading gates which ENCOURAGING
// line is said, and never produces a criticism. "i am getting clicks" names a
// buffer size; it does not say the playing was bad, and there is no reading
// that could make it say so.
constexpr const char *kNothingArrived =
    "i am not seeing anything from you yet -- is the right input armed?";

constexpr const char *kQuiet =
    "that went out, though it is quiet -- others may struggle to hear it.";

constexpr const char *kClicks =
    "i am getting clicks rather than playing -- that is usually a buffer size.";

constexpr const char *kClipping =
    "that is clipping, and it will distort for everyone else.";

const char *lineFor(InputCheck::Reading r) {
  switch (r) {
  case InputCheck::Reading::Silent:
    return kNothingArrived;
  case InputCheck::Reading::Faint:
    return kQuiet;
  case InputCheck::Reading::Clicks:
    return kClicks;
  case InputCheck::Reading::Clipping:
    return kClipping;
  case InputCheck::Reading::Playing:
    break;
  }
  return nullptr;
}

// Whether what arrived was something the room could hear.
//
// This is what decides the THREAD, and it is a different question from whether
// there was anything to remark on. Silence and clicks did not reach anybody, so
// "that interval just went out" would be false and the step has not happened
// yet. Quiet and clipping both went out -- the lesson about the interval delay
// is true of them -- so they carry the thread even though each also earns a
// line of its own.
bool wentOut(InputCheck::Reading r) {
  return r != InputCheck::Reading::Silent && r != InputCheck::Reading::Clicks;
}

constexpr const char *kSignOff =
    "that is the whole of it -- i'll get out of the way. the band will keep "
    "playing.";

bool isShake(const std::string &text) {
  const auto t =
      chalkwalk::music::text::lower(chalkwalk::music::text::trim(text));
  return t == "shake" || t == "new" || t == "again";
}

} // namespace

TutorBot::TutorBot(std::string name,
                   std::unique_ptr<BotClient::Client> botClient)
    : username(std::move(name)), client(std::move(botClient)) {
  if (client)
    client->addListener(this);
}

TutorBot::~TutorBot() {
  if (client) {
    client->removeListener(this);
    client->disconnect();
  }
}

void TutorBot::setOwner(std::string ownerUsername) {
  std::lock_guard<std::mutex> sl(stateMutex);
  owner = std::move(ownerUsername);
}

bool TutorBot::join(const std::string &host, int port, double rate) {
  if (!client)
    return false;

  client->setSampleRate(rate);

  // No channel at all. The tutor sends nothing and is not a strip in anybody's
  // mixer; it is a name in the room that talks.
  client->setChannels({});

  // Subscribed by default, because the thread is gated on hearing the owner
  // play. It measures nothing and keeps nothing -- see `onIntervalReceived`.
  client->setDefaultRecvEnabled(true);

  client->connect(host, port, username, "");
  {
    std::lock_guard<std::mutex> sl(stateMutex);
    sampleRate = rate;
  }
  active = true;
  return true;
}

void TutorBot::part() {
  if (!active.exchange(false))
    return;
  if (client)
    client->disconnect();
}

TutorBot::Step TutorBot::step() const {
  std::lock_guard<std::mutex> sl(stateMutex);
  return current;
}

void TutorBot::advance(Step from) {
  const char *line = nullptr;
  Step next = Step::Done;
  {
    std::lock_guard<std::mutex> sl(stateMutex);

    // The step is re-checked under the lock, and the line is derived from it
    // rather than passed in. Every caller tests `step()` and then acts, which
    // releases the lock in between -- so two triggers arriving together could
    // both see the same step, and a caller-supplied line would then be said
    // twice while the thread moved on two places. Deriving it here makes the
    // line and the step incapable of disagreeing, and makes a second call for
    // a step already taken do nothing.
    if (current != from)
      return;

    switch (current) {
    case Step::Greeting:
      line = kGreeting;
      next = Step::FirstPlayed;
      break;
    case Step::FirstPlayed:
      line = kFirstPlayed;
      next = Step::SecondPlayed;
      break;
    case Step::SecondPlayed:
      line = kSecondPlayed;
      next = Step::KeySet;
      break;
    case Step::KeySet:
      line = kKeySet;
      next = Step::Shaken;
      break;
    case Step::Shaken:
      line = kShaken;
      next = Step::Done;
      break;
    case Step::Done:
      return;
    }
    current = next;
  }

  if (client && line != nullptr)
    client->sendChat(line);

  // The sign-off ends the TEACHING, not the bot. A tutor used to leave here,
  // and section 7 argued hard for it -- "a tutorial that leaves when you have
  // got it is a rare and good thing". It cannot now: the tutor is a conductor,
  // and the band always has one.
  //
  // What section 7 was actually against is a tutorial that lingers uselessly,
  // and a conductor is not lingering; it has a job for the rest of the session.
  // So the property that survives is the one that mattered: the teaching is
  // finite by construction and stops saying things, which is what the budget
  // test holds it to.
  if (next == Step::Done) {
    if (client)
      client->sendChat(kSignOff);
  }
}

void TutorBot::onConnected() {
  if (step() == Step::Greeting)
    advance(Step::Greeting);
}

void TutorBot::onDisconnected(const std::string &) { active = false; }

void TutorBot::onChatMessage(const std::string &type, const std::string &sender,
                             const std::string &text) {
  if (!active.load())
    return;

  // Bots do not teach bots, and the tutor does not react to its own line.
  if (BotNames::looksLikeBot(sender) || sender == username)
    return;
  (void)type;

  const Step at = step();

  if (at == Step::KeySet &&
      !chalkwalk::ninjam::conventions::extractKeyAnnouncement(text).empty()) {
    advance(Step::KeySet);
    return;
  }

  if (at == Step::Shaken && isShake(text))
    advance(Step::Shaken);
}

void TutorBot::noteDiagnostic(InputCheck::Reading reading) {
  const char *line = nullptr;
  {
    std::lock_guard<std::mutex> sl(stateMutex);

    if (reading == lastReading) {
      ++agreeingIntervals;
    } else {
      lastReading = reading;
      agreeingIntervals = 1;
    }

    const auto row = (std::size_t)reading;
    if (reading == InputCheck::Reading::Playing ||
        agreeingIntervals < kIntervalsToAgree || saidDiagnostic[row])
      return;

    saidDiagnostic[row] = true;
    line = lineFor(reading);
  }

  if (line != nullptr && client)
    client->sendChat(line);
}

void TutorBot::onIntervalReceived(const std::string &from, int,
                                  const float *left, const float *right,
                                  int numSamples) {
  if (!active.load() || numSamples <= 0)
    return;

  double rate = 0.0;
  {
    std::lock_guard<std::mutex> sl(stateMutex);
    if (!owner.empty() && from != owner)
      return; // Somebody else playing must not carry the newcomer forward.
    rate = sampleRate;
  }

  const auto reading = InputCheck::read(left, right, numSamples, rate);

  // Checked for as long as the tutor is here, and not only during the two
  // steps that are gated on hearing you.
  //
  // Section 7 says that after step 2 the tutor never listens for anything
  // ELSE, which is a rule about scope rather than about when to stop: these
  // four rows are the only thing it ever listens for, and it goes on watching
  // for them until it parts. The strict reading would make two of them dead
  // code -- quiet and clipping both let the thread through, so it is past step
  // 2 within two intervals and a row needing three in a row could never fire.
  // The cost of the wider reading is bounded by construction: four lines, once
  // each, and then the tutor leaves.
  noteDiagnostic(reading);

  // Whether the THREAD moves is the separate question, and it belongs to the
  // two steps that are about hearing you.
  const Step at = step();
  if (at != Step::FirstPlayed && at != Step::SecondPlayed)
    return;

  if (!wentOut(reading))
    return;

  if (at == Step::FirstPlayed)
    advance(Step::FirstPlayed);
  else
    advance(Step::SecondPlayed);
}
