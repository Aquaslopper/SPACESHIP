# Rescue notes: what to keep

The files in [`rescue/`](./) are a historical investigation, not a current release checklist. They preserve screenshots, detailed source comparisons and earlier agent mistakes. Keep them intact: some claims and statuses were superseded by later repairs and the owner's decisions, so check the code and subsequent history before acting on them.

Useful lessons and entry points:

- **Follow the owner's instructions.** The [Developer Note](Developer%20Note) records a failure where agents replaced explicit instructions with assumptions. Ask when the intended behavior is uncertain; never invent a correction.
- **The action clock is gameplay, not wall time.** Only committed actions advance the world and cooldowns. Free browsing, repainting, blocked clicks and time spent waiting must not do so. Existing targeted guards include `cpp/tests/spam_audit_tests.cpp`, `cpp/tests/draw_probe.cpp`, and the parity scripts in `tests/parity/`. Cargo salvage repetition is intentionally retained. These tests cover specific paths, not every possible action.
- **The line-by-line ledgers are worth keeping.** The repository contains ledgers for HTA lines [1–2000](first_2000_line_ledger.csv) and [2001–4000](lines_2001_4000_line_ledger.csv), plus focused audits of functions elsewhere. The ledgers themselves do not establish a complete 6000-line or whole-game parity review.
- **A build isn't a visual or behavioral comparison.** The [code audit](code_vs_code_audit.md), [health audit](health_source_audit.md), source line ledgers and [damage-control log](damagecontrol_report.md) are useful leads. Check the current implementation and test scopes; older statuses are historical. CPU drawing probes do not replace testing the real window and GPU.
- **Protect startup and input.** `cpp/tests/raylib_config_guard.c` checks raylib frame, input, JPEG and PNG flags inside the real library build; `tools/check-imgui-input.sh` and `tools/check-draw.sh` exercise bounded input and drawing paths.
- **Preserve the reference and intentional changes separately.** `Spaceship.hta` is the behavioral reference unless the owner explicitly chooses otherwise. The owner has since requested paid, morale-improving crew conversations, a lighter palette and a new ship-art direction. Older rescue text demanding visual 1:1 or different conversation behavior does not override those decisions.

The [rescue index](damagcontrol_index.md), [recovery proposal](RECOVERY_PLAN.md) and [repair instructions](REPAIR_AND_RESCUE.md) remain available for historical context. They have not been revalidated in full. For building and tests see [development](../docs/DEVELOPMENT.md).
