# T-Display-S3 Webserver — AI Workflow (the build contract)

Read this before any change. It is how new functionality is built and how the
documentation stays in sync with it.

## The loop

**1. Locate the contract.** Find the FSD rule the work serves in
[`../Functionality/TDisplay-S3-Webserver-FSD.md`](../Functionality/TDisplay-S3-Webserver-FSD.md). If none
exists, the work starts by defining the **WHAT** — a new, atomic, falsifiable
requirement — not by writing code.

**2. Build per the Method.** Follow `standards/` and `project/`. Reuse an existing
module or skill before adding new code. Make the smallest change that satisfies
the rule.

**3. Test — the gate.** A change is **not done** until its test exists and
passes, per [`standards/testing.md`](standards/testing.md). A bug fix writes its
regression test first, and that test must fail before the fix.

**4. Reconcile the documentation.**
- The **FSD** absorbs new or changed behaviour — verify, don't transcribe. If the
  code deviates from the spec, fix the code.
- The **user manual** absorbs anything an operator would notice.
- The **Method** changes only for a rule that is true for the whole project.
- All present-state.

**5. Verify both directions.** The implementation matches the FSD, and no FSD rule
is silently unimplemented.

## Requirement quality gate

Before a new requirement enters the FSD it must be atomic, falsifiable (precondition,
stimulus, observable response, deadline, tolerance, failure behaviour, tier), free of
weasel words, and provenance-tagged (`[user]`, `[derived]`, `[proposed]`).

## Roles

The user owns the FSD: only the user approves a requirement or a number.
`/define` writes and amends it; `/build` consumes contracts and reports spec
defects upward, never editing them. Any contradiction between planes goes to the
user.
