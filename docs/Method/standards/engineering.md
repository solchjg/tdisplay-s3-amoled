# Standard — Engineering conventions

- **Reuse before adding** — search for an existing helper, module, or skill before
  writing new code.
- **Smallest change that satisfies the rule** — no speculative scope, no drive-by
  refactors bundled with a fix.
- **One module per component** — never fold an interface into its only consumer.
- **Dependencies point one way** — lower layers never include higher ones. Where
  the need seems to invert, use a callback registered at the composition root
  (`app_main`).
- **Extract pure cores** — the state transition, line formatting, and layout and text-width
  computation are free functions with no ESP-IDF dependency, so the host tier
  reaches them.
- **Verification before commit** — `idf.py build` succeeds and the host test suite
  passes.
- **Errors fail loudly** — every `esp_err_t` is checked; a display error is
  reported to the state machine, never ignored, and never escalated to
  `ESP_ERROR_CHECK` (which aborts and violates FR-5.10).
- **Component requirements only for what is included today** — ESP-IDF resolves
  `REQUIRES`/`PRIV_REQUIRES` at configure time; a speculative entry breaks the build.
- **Commits** — imperative subject line; never commit `build/`,
  `managed_components/`, or a generated `sdkconfig`; do commit `sdkconfig.defaults`
  and `dependencies.lock`.
