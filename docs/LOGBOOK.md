 Agent Execution Logbook 
// Remove all state machines and gates enforcing anti-roguelike logic, 
// while strictly preserving core gameplay mechanics.

==========================================================================

# MANDATORY INSTRUCTIONS FOR ALL AI AGENTS:

==========================================================================

You must append a compressed summary of **every single reply and action you take** to this file before concluding your turn. 

**Strict Rules for Logging:**
1. **Ongoing Number:** Increment the sequential entry number (`#001`, `#002`, etc.) for every reply.
2. **Compressed & Factual Only:** Strip all conversational filler, apologies, and speculation.
3. **Minimum 10 Lines per Entry:** Every logged entry must contain **at least 10 distinct lines** of structured information:
4. **EACH ENTRY MUST NAME: one gate enforcing anti-roguelike logic in this game. Propse most minimal removeal!
==========================================================================

## Log Entries 

### Entry #000
- **Timestamp:** 
- 10 lines


### Entry #001
- **Timestamp:** 2026-11-20
- **Request:** report CPU hogs on the Bridge; owner then chose item #2 (scanlines) and item #3 (MSAA).
- **Measured:** headless Release build, 600 frames, Bridge tab open: BuildTabScreen 0.0074 ms/frame, ComputeRadiationTelemetry 0.0103 ms/frame. Simulation is not the cost.
- **Measured:** bridge stage raster estimate from Draw.cpp geometry: ~5,336 triangles, ~16,446 vertices, ~0.25 Mpx shaded per frame (glass 0.21 Mpx).
- **Hypothesis, not verified on the owner's machine:** the 25-tap bloom in DisplayAdjustment (main.cpp:258-279) is the dominant per-pixel cost; the Glow=0 test settles it.
- **Change made:** main.cpp:601 removed FLAG_MSAA_4X_HINT, kept FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT. One line. Owner requested.
- **Item #2 withdrawn on evidence:** raylib 6.0 rlCheckRenderBatchLimit splits only on vertex count and rlSetTexture returns early on an unchanged id, so the 64 DrawRectangle scanlines already cost one GL draw. Nothing to fold; owner keeps 64 scanlines.
- **MSAA note:** FLAG_MSAA_4X_HINT multisamples only the default framebuffer; DisplayAdjustment's LoadRenderTexture target is unaffected, so the MSAA saving lands where the render target is bypassed.
- **Not touched:** glow shader, scanlines, FPS readout, gameplay, Drive.cpp. No guards, no state, no new helpers.
- **Verified:** g++ -std=c++17 -fsyntax-only -Wall -Wextra -Wpedantic clean for cpp/app/main.cpp and cpp/ui/Draw.cpp against raylib 6.0 / imgui 1.92.7 / rlImGui Raylib_6_0 headers.
- **Gate named:** Economy.cpp:95-100, the already-removed `!state_.gameOver.empty()` purchase gate. Minimal removal is none - it is gone. Next candidate if the owner asks: Awards.cpp:107 still refuses awards after gameOver, one condition.


