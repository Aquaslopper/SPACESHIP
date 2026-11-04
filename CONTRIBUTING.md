# Welcome aboard

Thank you for spending a little time with Spaceship Command. A clear bug report,
a careful playthrough or a small, well-tested fix all help the ship along.

## Report something that feels wrong

[Open a bug report](https://github.com/Aquaslopper/SPACESHIP/issues/new?template=bug_report.yml)
and include:

- **Which edition and build?** Native Windows EXE or original HTA; include the
  commit/build link if you have it, plus your Windows version.
- **What did you do?** A short sequence of clicks is especially useful. Name the
  room, target and command, and say whether work or a cooldown was active.
- **What did you expect, and what happened?** If a click unexpectedly advanced the
  game, include the action count before and after. Cooldowns count actions, not seconds.
- **Can it be repeated?** A screenshot or a small reproduction is welcome.

Keep a backup before testing an imported voyage. If sharing a save would help,
review it first: native saves can retain the original imported snapshot. Remove
anything you do not want published; do not share private paths or personal details.

## Suggest a change

A short [issue](https://github.com/Aquaslopper/SPACESHIP/issues/new) explaining the
problem you want to solve is a good place to start. The intended mood is calm and
considered: clear feedback, meaningful decisions, and room to think rather than
pressure to keep clicking.

For larger changes, describe the idea before investing in a broad rewrite. The
[manifesto](Spaceship%20manifesto.pdf) and [concept notes](spaceship%20concept.pdf)
are useful context, not a claim that every idea is already implemented.

## Make a contribution

1. Read the [development guide](docs/DEVELOPMENT.md) and [AGENTS.md](AGENTS.md).
2. Keep the change focused. Separate presentation work from gameplay changes.
3. Preserve the original HTA, artwork and design documents during the native port
   unless the change explicitly concerns them.
4. Add regression coverage when changing action timing, resource use, reservations
   or save compatibility. Free interactions must not advance the world.
5. Run the relevant checks and explain what you tested in your pull request.
   Note anything that still needs manual Windows testing.

The native conversion is still in progress. Please keep
[NATIVE-PORT.md](NATIVE-PORT.md) accurate: a feature being present is not the same as
full behavioral parity with the reference.
