# T-Display-S3 Webserver — Documentation

Three planes, three questions, three readers. Every sentence belongs to exactly
one of them.

| Plane | Question | Directory | Reader |
|---|---|---|---|
| **WHAT** | What must be true of the system? | [`Functionality/`](Functionality/) | Anyone judging whether it is correct — reviewer, tester, future maintainer |
| **HOW** | How is it built and changed? | [`Method/`](Method/) | Whoever writes the next change, human or agent |
| **OPERATE** | How do I install and run it? | [`UserDocumentation/`](UserDocumentation/) | Whoever installs, drives, or recovers the running system |

**Authority order: the FSD defines the target; the Method defines the way there.**
On conflict the FSD wins on *what must be true*, the Method wins on *how to get
there*. User documentation describes the system as built — if it disagrees with
either it is stale, which means reality or the spec moved.

None of the three carries history or rationale narrative. Those live in `git log`.

## Where to start

| If you are… | Read |
|---|---|
| Making a change | [`Method/AI-Workflow.md`](Method/AI-Workflow.md) — the loop every change follows |
| Judging correctness | [`Functionality/TDisplay-S3-Webserver-FSD.md`](Functionality/TDisplay-S3-Webserver-FSD.md) — requirements, state model, verification contracts |
| Installing or running it | [`UserDocumentation/User-Manual.md`](UserDocumentation/User-Manual.md) |
| Wondering why | `git log` — a decision leaves its effect in a plane and its reason in the commit |

## Routing a new sentence

Ask in order; the first yes wins:

1. Externally observable and must be true → **Functionality**
2. Constrains how code is written or verified → **Method**
3. Tells a human how to run or recover the system → **UserDocumentation**
4. About collaborating with an AI assistant → `CLAUDE.md`, which is not a plane
5. Why a past decision was made → the commit message

Two questions settle the hard cases. *Could a black-box tester verify it?* — yes
means WHAT. *Would it survive a rewrite in another language?* — no means HOW.

## There is no decisions file

A settled decision leaves its **effect** as present-state content in the plane
it governs — a load-bearing "must not" becomes a stated negative requirement —
and its reason in the commit message.

## Writing rules

**Present-state.** Write what is true now, in the present tense. No history, no
rationale narrative, no temporal comparison. The FSD's lifecycle block is the one
sanctioned dated record.

**One canonical home.** Every fact is stated once. Anywhere else that needs it,
link.
