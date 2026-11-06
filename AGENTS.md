## STRICT CONSTRAINTS:
- Learn the relevant code and documentation before changing anything. Do not treat unverified assumptions as facts. For native-port work, read `docs/DEVELOPMENT.md` and `rescue/RESCUE_NOTES.md` first; inspect the specific code paths involved.
- `Spaceship.hta` is the behavioral reference unless the owner explicitly approves a change. Do not change gameplay rules or behavior unless the request specifically calls for it. If the intended behavior is unclear or conflicts with an owner instruction, stop and ask before coding.
- Keep changes minimal and focused. Do not rewrite entire files, bundle refactors with features, or introduce dependencies or architectural changes without approval.
- Do not change existing tests merely to make them pass. Add or update relevant tests when a code change warrants it, and preserve the intent of the existing regression and parity checks.
- Treat `tests/parity/` scripts as behavioral evidence comparing the native simulation with the HTA reference. Do not edit them to conceal a mismatch; investigate and explain any intentional difference.
- Do not hand-edit generated catalogue data in `assets/data/`. When reference content changes, use the documented `node tools/export-data.cjs` workflow and verify it with `node tools/export-data.cjs --check`.
- Do not modify or replace authored artwork under `assets/` unless the request specifically authorizes it. Follow the documented asset-generation workflows where applicable.
- Preserve historical investigation material under `rescue/`; do not edit or delete it unless explicitly requested.
- Do not commit build output or temporary artifacts. Use the documented build and test commands, and report exactly which checks were run and any that could not be run.
- Before finishing, state what changed, what was deliberately left untouched, and the checks run. A successful build or simulation test is not proof of visual or full gameplay parity.

------------

## SOURCE AUTHORITY: ABSOLUTE; NO EXCEPTIONS
1. the owner's instructions
2. Spaceship.hta
3. docs/DEVELOPMENT.md
4. docs/PLAYING.md
5. cpp/
6. tests/


## HARD RULES

-**Do not edit files without the owner's permission.** The owner of the product
   **Spaceship** is **[@Aquaslopper](https://github.com/Aquaslopper)**
   (`Aquaslopper/SPACESHIP`). Wait for explicit approval from the repository owner
   before creating, changing, moving or deleting any file. If permission is unclear,
   ask and stop; never make the edit first and explain afterwards.
   
- **Do not invent behaviour.** Trace the HTA function, event handler, data and layout
   that implement the feature first. If the reference cannot be established, say so; never fill the gap with a plausible guess.

- **Do not hide differences.** Do not silently omit content, truncate values, inert a
   control, change a cost, or shrink text to avoid overflow.
  
-**The native game is self-contained.** The C++ port works fully on its own.


