# Working on the ship

[← Back to the README](../README.md)

The native edition uses C++17 and CMake 3.20 or newer. The Windows interface is
Win32/GDI; the portable console and tests use the same `Voyage` simulation.
Linux builds the console and tests, not a graphical client.

## Build locally

Run these commands from the repository root, with a C++17 compiler and CMake
installed. On Windows, use Visual Studio's C++ desktop build tools.

### Windows · native application

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\spaceship.exe
```

The post-build step copies `music/`, `sounds/`, `saves/` and the distribution
notes beside the EXE. Keep them together when testing or sharing a build.

### Linux · shared simulation and console

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/spaceship_console
```

The simulation/catalog is compiled in. Building the C++ targets does not require
Node or the HTA. nlohmann/json 3.11.3 is vendored with its
[MIT license](../cpp/vendor/LICENSE.json). The Windows build uses the static MSVC
runtime; optional WAV audio uses Windows `waveOut`.

## Find your bearings

| Path | Responsibility |
| --- | --- |
| `cpp/include/voyage.hpp` | Shared simulation API |
| `cpp/src/voyage.cpp` | Actions, work, world ticks, validation and native saves |
| `cpp/src/voyage_crew.cpp` | Crew orders, automation, relationships and navigation rules |
| `cpp/src/voyage_import.cpp` | Transactional import of supported reference saves |
| `cpp/src/voyage_views.cpp` | Readouts, choices and command availability |
| `cpp/src/windows_native.cpp` | Native Windows controls, views and dispatch |
| `cpp/src/runtime_files.cpp` | Portable save locations and optional WAV validation |
| `cpp/include/windows_audio.hpp` | Native music/effects playback |
| `cpp/tests/voyage_tests.cpp` | Native regression and differential tests |
| `Spaceship.hta` | Preserved original game and behavioral reference |
| `tests/*.test.cjs` | Dependency-free reference-edition regression tests |
| `tools/` | Development-only data/audio exporters |
| `runtime/` | Audio and save-folder notes copied into the Windows distribution |

## Keep the clock honest

A committed gameplay action increments the counter, ticks **already-active**
cooldowns, then advances the world. New work and the source control's cooldown
start afterward. Invalid actions and blocked repeats must roll back without a
tick. Free browsing, staging, settings and acknowledgements stay free.

Cooldowns are per control, not a global debounce or a real-time timer. Watch has a
15-action cooldown; unused arguments must not create an alternative key for a
global control. Audio must never advance the simulation.

Long world traces use an explicit test-fixture clock driver so they can compare
with the reference's direct world ticks. Separate tests exercise actual wrapped
controls, repeated-click rejection and watch's recovery through 15 other gameplay
clicks. Keep those two kinds of tests distinct.

See [AGENTS.md](../AGENTS.md) for the fuller contributor invariants and
[NATIVE-PORT.md](../NATIVE-PORT.md) for the current verification scope.

## Run the reference and export checks

With Node installed, run from the repository root:

```sh
node tools/export-native-data.cjs --check
node tools/export-native-audio.cjs --check
```

Then run the reference suites:

```sh
# POSIX shell
for test in tests/*.test.cjs; do node "$test" || exit; done
```

```powershell
# PowerShell
Get-ChildItem tests/*.test.cjs | ForEach-Object {
  node $_.FullName
  if ($LASTEXITCODE -ne 0) { throw "Reference test failed: $_" }
}
```

The exporters evaluate the original game **at development time only**. Run them
without `--check` to regenerate their committed outputs after an intentional
reference change. The executable does not load JavaScript, Node, the HTA, or the
test fixtures at runtime.

The [Game checks workflow](../.github/workflows/native-cpp.yml) runs reference
checks and native Linux/Windows builds, then publishes `spaceship-windows-x64`.
Windows also creates real controls and tests repeat-click blocking in
`spaceship.exe --smoke-test`. A green run does **not** replace a manual Windows
visual, audible, accessibility or full-playthrough check.
