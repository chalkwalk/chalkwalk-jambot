# chalkwalk-jambot -- Principles

> Twelve rules the band is built on. When a proposal conflicts with one, the
> proposal bends.
>
> `docs/BOT-CHAT.md` is the long form: what the bots would say, how they are
> addressed, what each voice plays. This file is the short one, and it exists
> because those arguments are spread over two thousand lines and the same half
> dozen ideas keep being rediscovered inside them.
>
> **Citing.** `JAMBOT §N`, stable anchors. Antiphon's own `PRINCIPLES §N` and
> `fence #N` are a different, separate numbering; a bot library that ran on
> another host would keep these and lose those.

## North Star

**A band for one player who wanted to jam and found nobody there.**

Not a composer, not a backing track, not a demonstration of generative music.
Everything below follows from there being exactly one person in the room whose
evening this is.

Where two principles pull against each other, the reading that leaves that
person playing -- rather than listening, waiting, or negotiating -- wins.

---

## §1 The room has the floor

The band never takes musical or social space. It can be given it.

It does not propose a tempo, a key or a chart. It does not start a vote. It
does not bring in another player unasked. It does not step forward to solo.
Every one of those was decided separately and they are all the same rule, which
is why it is first: the band supplies what is missing and never competes for
what is not.

Being *given* the floor is different and is allowed -- `band, take a solo` is a
person handing it over, and a handover the person initiated is one they can
hear coming.

## §2 Every part is a theme and its variations

Nobody in this band improvises freely. Each voice states something and then
reshapes it: the kick a figure, the bass a riff, the lead a tune.

This is Schoenberg's *developing variation*, and it is borrowed on purpose
because the transformation set is finite and enumerable -- transposition,
inversion, retrograde, augmentation and diminution, fragmentation, sequence,
ornamentation, displacement. Cheap, deterministic, composable, and applicable
to any part rather than only to a melody.

A voice generating fresh material every interval is not playing a part. It is
soloing, and §1 says it may not.

## §3 Prominence is continuous, never a role

There are no solos, no handoffs and no turns. There is a per-voice prominence
that drifts slowly, bounded so the band never all surges or all vanishes.

A player becomes more present over several intervals and recedes over several
more, and everyone else fits around it. Nothing is announced, because a role
handoff is a signal and §4 says a signal costs an interval nobody has.

## §4 The interval is the unit, and the delay is the instrument

Everything the band can do that a human cannot comes from holding a whole
interval at once: it has your complete phrase, ending and all, while a human
listener is still hearing it unfold (`BOT-CHAT.md` 14). Everything it cannot do
comes from the same fact.

**No mechanism may require perceiving a boundary sooner than an interval.**
That single test rejects turn-taking, call-and-response inside an interval, and
song form -- verse and chorus do not fit inside an interval and do not align
across intervals, so a form imposed on top is a structure only the band can
hear.

## §5 One harmony for the whole room

The band never varies the key or the chart. Not per section, not per voice, not
ever.

You hear the band an interval late, so any structure they play is rotated
against the structure you are playing over. A rotation of the same chords is
the same chords; different chords per section would have you soloing over a
progression you cannot hear.

## §6 The same seed plays the same music

Every decision is a deterministic function of the room seed, the interval index,
and **the room's inputs** -- the key, the chart, the tempo, and eventually what
the band heard. No wall clock, no entropy, no hidden state. `shake` is the only
reroll, and it is a person asking.

The inputs clause is load-bearing rather than a hedge. A band that reacts to you
is not deterministic in the seed alone, and it is not meant to be; what it must
stay is deterministic in the seed *and what it was given*, so a test can hand it
a reading and get the same music twice.

This is what makes the band testable at all, and it is why every number here can
be asserted rather than sampled.

## §7 The figure returns; the performance never does

Repetition is identical in **what** is played and never in **how**. A phrase
that returns played exactly the same way twice is a loop; played fractionally
differently, it is a band.

Held per session, per section, or per interval -- `BotBand::Hold` names the
three, and the split is what lets a figure come back without the audio being
bit-identical.

## §8 A bot is an ordinary client

It connects over the wire like anybody else, and it must be as easy to get rid
of as any player. No bot may reach into a host's UI, and nothing may become the
one path that cannot be dismissed.

These can be pointed at a stranger's server. The failure to design against is a
bot nobody can evict, playing to a room that never asked for it.

## §9 Say less than you could

One answer, not four. A bot speaks when addressed, when it acts, or when the
room changes -- and the band as a whole says a thing once.

Speech is the loudest thing in a practice room. The budget is not politeness;
it is the difference between a player and a status readout.

## §10 It never judges what you played

Analysis of what arrives may bias what the band generates. It may never form an
opinion about your playing, and it may never say one.

The one thing a bot may conclude from your audio is whether it could be heard
at all, which is a fault report and not a review (`BOT-CHAT.md` 7).

## §11 Analysis biases the generator; it never replaces it

When the band listens, what it gets is a handful of plain numbers -- density,
register, how active, where the energy sits -- and those move existing
decisions: a pulse count, a register, whether to rest, how busy the lead is.

The generator stays in charge. A band that echoed you would be a mirror, and a
mirror is not somebody to play with.

## §12 Cheap and legible over clever

> Sources for the claims in this file are in `SOURCES.md`. A principle with a
> source is one somebody can argue with; a principle without one is a
> preference.

No machine learning, no model weights, no data files. Every stage is a small
deterministic function somebody can read and a test can pin.

A genre is a point in a parameter space -- density, syncopation, register,
swing -- not a code path. "Play like a jazz band" moves numbers.
