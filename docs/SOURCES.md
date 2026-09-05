# chalkwalk-jambot -- Sources

> What was read to design this band, and where to find it again.
>
> `PRINCIPLES.md` and `NON-GOALS.md` cite this file when a rule came from
> somewhere rather than from us. A principle with a source is one somebody can
> argue with; a principle without one is a preference.

## The rule about including a document

**Cite always. Vendor only where the licence plainly permits it.**

A copy in the repository is worth more than a URL -- links rot, paywalls
appear, and a design argument that cannot be re-read is a design argument
nobody can check. So where a source is licensed for redistribution, the
document itself belongs in `docs/sources/` beside this file.

**Today that directory is empty, and the entries below say why in each case.**
Most of what was read is open to *read* and silent about redistribution, which
is not the same permission. Where a source is only linked, the entry records
the specific claim taken from it, so the claim survives the link.

---

## Musical design

### Developing variation (Schoenberg, after Brahms)

The idea that every part is a theme and its variations -- `PRINCIPLES.md`
`JAMBOT §2` -- is Schoenberg's, and the term is his: "the succession of motive
forms produced through the variation of the basic motive", creating "something
which can be compared to development, to growth". The *Grundgestalt* is the
basic shape; variation moves a motive **incrementally, away from or toward the
original**, which is exactly the shape a jam has and a song form does not.

- <https://en.wikipedia.org/wiki/Developing_variation> -- CC BY-SA 4.0, so it
  *could* be vendored; not done, because it is an encyclopaedia entry that will
  be better next year than the copy would be.
- Sirman, *Developing Variations: An Analytical and Historical Perspective* --
  <https://www.diva-portal.org/smash/get/diva2:310395/FULLTEXT01.pdf>. Thesis,
  no redistribution licence stated. Linked only.

### The transformation set

That the operators are finite and enumerable -- transposition, inversion,
retrograde, augmentation and diminution, fragmentation, sequence, ornamentation,
displacement -- is what makes `JAMBOT §2` buildable rather than aspirational.
Fragmentation as "deleting notes or replacing notes with rests" is the
formulation worth keeping, because it is implementable as written.

- <https://zimmer.fresnostate.edu/~sasanr/Music/Music-Theory/Melodic-Transformation.htm>
  -- course notes, no licence stated. Linked only.
- *MelodyVis: Visual Analytics for Melodic Patterns in Sheet Music* --
  <https://arxiv.org/pdf/2407.05427>. Names the same operators as analysis
  primitives; check the arXiv licence before vendoring, as it varies per paper.

### Prominence as a continuum, not a role

`JAMBOT §3` -- that a player becomes present over several intervals rather than
taking a turn -- comes from how group improvisation actually behaves: a
performance "can begin with a strong solo mounting quickly in dynamics and
density, which prompts the entry of other musicians". The **back-channelling**
work matters as much: the non-soloing player is still contributing, which is
the argument that an accompanist is doing something rather than waiting.

- *Group Performance Paradigms in Free Improvisation*, Organised Sound --
  <https://www.cambridge.org/core/journals/organised-sound/article/group-performance-paradigms-in-free-improvisation/3D3C4382343E9903B32AEB1BE5473A48>.
  Paywalled. Linked only.
- *Perception of 'Back-Channeling' Nonverbal Feedback in Musical Duo
  Improvisation* -- <https://www.ncbi.nlm.nih.gov/pmc/articles/PMC4473276/>.
  PMC open access; licence is per-article, usually CC BY. **A candidate for
  vendoring** if the specific article's licence allows.

### Genre as a parameter space

`JAMBOT §12` -- that "play like a jazz band" moves numbers rather than selecting a code
path -- rests on genre differences being measurable: note density, syncopation,
onset distribution within the bar, register. Blues, classical and jazz show
higher median note density; folk and soul are more homogeneous and lower, around
3-5 notes per second.

- *Syncopation and Groove in Polyphonic Music*, Music Perception --
  <https://online.ucpress.edu/mp/article/39/5/503/182325/Syncopation-and-Groove-in-Polyphonic-MusicPatterns>.
  Paywalled. Linked only.
- *A corpus-based analysis of syncopated patterns in ragtime*, Kirlin --
  <https://www.cs.rhodes.edu/~kirlinp/papers/kirlin20corpus.pdf>. Author copy;
  no licence stated.

---

## Systems we are deliberately not

Both are good, both are cited in `NON-GOALS.md`, and both are the wrong shape
for a room where one person came to play.

### GenJam (Biles) -- `jambot fence #2`

An interactive genetic algorithm that trades fours and eights with a human: it
listens to the last four bars, maps them into its chromosome representation,
mutates, and plays the result. The *mutate what you just heard* idea is exactly
right for an interval-delayed room. The **turn-taking** is what we refuse.

- <https://genjam.org/wp-content/uploads/2019/07/genjamasa97.pdf> -- author's
  site, no redistribution licence. Linked only.

### The Continuator (Pachet) -- `jambot fence #6`

A variable-order Markov model of the player's own style, presented as reflexive
interaction -- deliberately "an imperfect mirror". It continues you rather than
taking turns with you, which is closer to what we want than GenJam is.

We refuse it because a model of you is a mirror, and a mirror is not somebody to
play with -- and because weights are not readable (`JAMBOT §12`).

- <https://www.francoispachet.fr/the-continuator-musical-interaction-with-style-pachet-2003/>
  -- author's site; the journal version is paywalled. Linked only.

---

## Sound sources

### SoundFont banks

Recorded here because a bank is *shipped*, which makes its licence a fact about
the product rather than about the reading.

- **MuseScore_General.sf3** -- **MIT**, about 36 MB. Lineage is documented:
  FluidR3 by Frank Wen (2000-02), mono conversion by Michael Cowgill (2014-17),
  adapted by S. Christian Collins (2018-19), with named contributions from Ethan
  Winer and Michael Schorsch. <https://musescore.org/en/node/317991>
- **GeneralUser GS** -- permissive for use, **not recommended for
  redistribution here.** Its own documentation states the author "cannot be 100%
  sure where all of the samples originated" and flags this as a concern for use
  in a software product. Free to use; we would be shipping it, which is a
  different question.
  <https://github.com/mrbumpy409/GeneralUser-GS/blob/main/documentation/README.md>

`chalkwalk-soundfont` is where the loading of these lives, with its own patched
FluidLite; this entry is about which bank and why, not about how it is read.
