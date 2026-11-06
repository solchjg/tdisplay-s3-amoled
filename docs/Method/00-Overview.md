# T-Display-S3 AMOLED Webserver — Method (HOW)

The **build contract**: the rules for how this project is built and changed.
Entry point for any task: **[`AI-Workflow.md`](AI-Workflow.md)**.

| Path | Contents |
|------|----------|
| `AI-Workflow.md` | The loop every change follows. Read it before touching code. |
| `standards/` | Portable rules — engineering and testing. |
| `project/layout.md` | Project bindings: layers and source layout, panel bring-up, test builds, toolchain, bench. |
| `project/wiring/` | Fritzing sketches and checks for every wired subsystem. |
| `project/*-circuit.md` | Add-on circuits: the 4-20 mA loop (§29), RS232 (§30) and the proposed water probe. |
| `project/blues-cellular.md` | The Notecard hardware, budgets and bring-up for the cellular uplink (§33). |
| `project/lte-m-modem.md`, `project/crowpanel-epaper.md` | Evaluations of other hardware: the direct LTE-M modem and the e-paper screen. |

A rule belongs here when it constrains **how the code is written, structured, or
verified** without being observable from outside the running system. If a
black-box tester could verify it, it is a requirement and belongs in the FSD.

The plane map, authority order, and routing rule are in [`../00-Overview.md`](../00-Overview.md).
