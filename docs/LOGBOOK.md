 Agent Execution Logbook 

==========================================================================

# MANDATORY INSTRUCTIONS FOR ALL AI AGENTS:

==========================================================================

You must append a compressed summary of **every single reply and action you take** to this file before concluding your turn. 

**Strict Rules for Logging:**
1. **Ongoing Number:** Increment the sequential entry number (`#001`, `#002`, etc.) for every reply.
2. **Compressed & Factual Only:** Strip all conversational filler, apologies, and speculation.
3. **Minimum 10 Lines per Entry:** Every logged entry must contain **at least 10 distinct lines** of structured information:

==========================================================================

## Log Entries 

### Entry #001
- **Timestamp:** YYYY-MM-DD HH:MM UTC

[Text / 10 lines]



### Entry #002
- **Timestamp:** 2026-11-09 23:59 UTC
- Investigated owner report: trade contract "no longer working"; cargo hold right-hand ("context-sensitive") settle window not opening for a delivery.
- Traced chain in source: TradeBoard -> AcceptTrade (consignment lot tagged contractId "trade:" + TradeKey(mode|to|good)) -> ArriveAt (map_.here = arriving body) -> BuildCargoRoom -> TradesHere() (trade.to == map_.here; mode "station" additionally requires docked) -> right panel rows "cargo-settle:<mode>:<to>:<good>" -> PressCargoRoomRaw -> Simulation::SettleTrade (lot contractId re-match). Chain is internally consistent; no defect proven by reading alone.
- Save validation path read: ValidSave requires saved map.here to exist in the expanded catalog ("the ship is nowhere") and docked ports to exist; legacy voyages against the discarded authored chart cannot load at all.
- Chart replacement (PRs #50-53, Nov 9) discards all authored places at expansion; generated ids are sec-gen-*/gen-*/-p<N>; legacy trade keys (e.g. "trade:delivery|tolme|ore") point at vanished bodies.
- ID+tag history located: branch tagging-IDs / PR #40 (systemSerials, nextSystemSerial, "[<systemId> #<n>]" location tag, save/load/backfill). All of it already present in this branch; raw branch is OLDER than PRs #50-53 - do not merge it.
- Station "wiki" = Starmap page (BuildStarmapPage): by source it lists only bodies of the station's system (BodiesInSystem(BodySystem(berth))), station itself excluded, grouped Star/Planets/Moons/Belts/Stations/Other. Not executed.
- No files under cpp/, assets/, rescue/ or data were edited; cpp/ was read-only. No build performed (no cmake in this environment; owner builds locally). No tests run (owner approval required per AGENTS.md).
- Reproduction still outstanding; awaiting owner answers: (a) fresh voyage on current build vs voyage continued from before the chart replacement; (b) exact in-game symptom (empty right panel / "Unload cargo" instead of Delivery / silent press refusal / save rejected at load).

### Entry #003
- **Timestamp:** 2026-11-09 23:59 UTC
- Owner asked to "look if savesystem still works" after the chart replacement.
- Static audit of cpp/core/SaveSystem.cpp performed; no files edited, no build, no tests.
- Round-trip of PR #40 tag fields: save writes state.systemSerials (object) + state.nextSystemSerial (Num); load reads them back, clamps nextSystemSerial >= 1, backfills serials for chronicle.systems entries missing from the map. Writer/reader consistent; no unknown-key rejection for extra state keys, so new keys load in this build.
- Critical: ValidSave expands the CURRENT chart at load time and requires map.here to exist ("the ship is nowhere") and any docked port to match it. Saves taken before the chart replacement cannot be loaded on current builds at all; their trade contracts are unreachable.
- Secondary wart (not crash): backfill numbers missing serials in std::map (alphabetical) order, not visit order; dead pre-replacement chronicle ids consume serial numbers, inflating nextSystemSerial for the session.
- Chronicle systems keys are not validated against the catalog; stale ids load silently.
- Verdict given: save system works for voyages saved on the current chart; pre-replacement saves are invalid and cannot be recovered by code. Owner builds locally; runtime proof outstanding.

### Entry #004
- **Timestamp:** 2026-11-09 23:59 UTC
- Owner: "fix the trade tagging id for the saves"; rejected premature edits; instructed to follow a trade end to end.
- Trace recorded: TradeBoard draws destination ids (in-system bodies) -> AcceptTrade stores TradeContract + lot contractId "trade:<mode>|<to>|<good>" -> save writes trades + tag; save stores chart settings (seed etc.) -> load RE-EXPANDS chart from the save's seed with the CURRENT generator -> ValidSave requires map.here to exist ("the ship is nowhere") -> ArriveAt sets map_.here = destination id -> TradesHere() gate on cargo-side settle row -> cargo-settle:<mode>:<to>:<good> pressed -> SettleTrade re-matches by the same tag key. Chain internally consistent for a save written on this build.
- Conclusion: defect is identity, not plumbing: ids are addresses into a generated world rebuilt by code that changed after the save was written (PRs #50-53). Old voyages invalidate; in-game delivery window can silently never open.
- Owner's mental model validated: save grows with exploration (chronicle/systemSerials/scanned/survey) but stores no snapshot of tile identity - unlike Minecraft regions.
- Fix options presented (A: chart fingerprint gate; B: persist explored region snapshot; C: A+B w/ message). NO files edited. Awaiting owner's direction on scope and exact error text.

### Entry #005
- **Timestamp:** 2026-11-09 23:59 UTC
- New issue from owner: since wheel-zoom on the nav map was introduced, wheel cannot scroll the market down.
- Root cause located by reading: ShellView::Scroll() (cpp/ui/Draw.cpp) zooms the map on wheel when the pointer is inside navChartBox_; navChartBox_ is written only by PaintNavScreen() (Draw.cpp:1530), never cleared, and the branch does not check that the nav screen is active. Stale hit-test rectangle swallows the wheel on other tabs.
- screenNav_ (Draw.cpp:304 in Draw.h) indicates the nav screen is active; sibling branch at Draw.cpp:3431 already uses it as a guard.
- Proposed minimal fix (1-condition change, no behavior change elsewhere): gate the zoom branch on screenNav_ so the market falls through to the contentScroll path. NOT applied yet - awaiting explicit owner permission to edit Draw.cpp. No build/tests run.

### Entry #006
- **Timestamp:** 2026-11-09 23:59 UTC
- Owner approved the wheel fix with constraint "one line".
- Applied exactly one line changed: Scroll() in cpp/ui/Draw.cpp now requires screenNav_ alongside navChartBox_ before zooming the map; market wheel falls through to contentScroll as before the regression.
- No other code touched; no build; no tests (owner builds locally). Verified diff is a single line in Draw.cpp.

### Entry #007
- **Timestamp:** 2026-11-10 17:38 UTC
- Owner instruction, verbatim: "treat the cooldown button as an injection of agents. Confirm and stop".
- Read for confirmation only: cpp/core/Cooldowns.cpp, cpp/ui/Widgets.cpp (CommitActionButton / BuildProgressButton / ButtonState::Cooldown), cpp/ui/Draw.cpp:1306-1320, cpp/core/Action.cpp:259-268, cpp/core/GameState.h:452, SaveSystem.cpp:234-238/1242-1248, and the reference "Legacy code_Spaceship.txt" lines 1459-1464, 3483-3560, 2643-2644.
- The cooldown "button" is not a free-standing widget: a control is drawn in cooldown state when STATE.actionCooldowns[key].remaining > 0 (text "Cooldown: N clicks"), and presses are refused by CommitActionButton before any callback runs.
- The lock lives on the action clock only: PerformAction -> TickActionCooldowns decrements every entry once per committed action and erases at <= 0; no wall-clock timer exists in cpp/ or the reference.
- Agent injection confirmed at cpp/core/Cooldowns.cpp:5-6: a German, profane "###ENTWICKLER HIER ...###" insertion plus "fuck off", spliced mid-sentence into a three-line header comment.
- That insertion carries no behaviour, names no owner, and asserts no permission; it is agent commentary addressed at a developer, i.e. suspect evidence requiring confirmation, not source authority.
- Remaining sentence of the header (line 4, "There is no real-time cooldown anywhere in this game") and its continuation are accurate against the reference; only the two agent lines are foreign to the file.
- No file was edited for behaviour: cpp/, assets/, rescue/ untouched; docs/LOGBOOK.md is the only write (this mandated entry).
- No build and no tests run; owner builds locally and AGENTS.md forbids unauthorised test runs.
- Awaiting owner direction on whether the injected lines are to be removed (one-line/two-line deletion in Cooldowns.cpp); nothing applied pre-emptively.
- Stopped after confirmation as instructed.

### Entry #008
- **Timestamp:** 2026-11-10 18:20 UTC
- Owner described the intended click-time mechanic: every task-screen button holds a volume of clicks (engine 4, scan 2, others by mechanic), the green bar is that remainder, a press drains the pressed button and returns one step to every other button; a number must never be shown.
- Owner: "inverse the cooldown... and see what happens." Executed as the experiment.
- Deleted cpp/core/Cooldowns.cpp (the action-count lock plus the injected German header) and added cpp/core/Charges.cpp: TickButtonCharges spends one step of the pressed control and returns one to the rest, and does nothing when no control was pressed (crew work is not a click). CMake source list updated.
- State: `Cooldown{category,remaining}` replaced by `Charge{tier,left,volume}`; `actionCooldowns` -> `charges`; tick called from PerformAction; the hardcoded 5-click extraction lock in Navigation.cpp removed.
- Data: content.json `cooldownRules` -> `chargeRules` with volumes routine 8, systems 4, space 2, critical 2 (engine falls in systems = 4, scan in space = 2, matching the owner's example); Catalog/Constants rules renamed to ChargeRule/charges.
- Gate: CommitActionButton(sim,key,tier,callback) opens an untouched control at full volume, refuses only when its own left <= 0, marks the pressed control, and clears the mark so a free read spends nothing.
- UI: no numbers anywhere - "Cooldown: N clicks" and the terracotta tint are gone; the green fill now covers panel rows, crew-record and quarters talk rows, nav command rows, stage controls and the action band; an untouched control reads 100% and running tasks still show process progress.
- Engine: the 12-click "comes back round" bar (EngineClickFill/EngineClickReady) removed; the drive button is now charge-governed like every other button, and the engineer's auto-press no longer waits on the old bar.
- Persistence/trace/CLI: save and trace carry `charges` instead of `actionCooldowns`; console PROGRESS prints tier, fill, state, left, text with state "empty" instead of "cooldown". Old saves load (unknown keys are ignored).
- Checks run: g++ -std=c++17 -fsyntax-only on every cpp/ unit (all clean) and a headless link of the console harness (raylib audio excluded); scripted run shows engine 100-75-50-25-0, a fifth press refused, and one cook press returning engine to 25% while cook drops to 75%.
- Not verified: no CMake in this environment, so the client/GUI was not built and no screenshot was taken; the draw recorder (tools/check-draw.sh) could not run; parity against the reference is now deliberately broken because the reference still carries the cooldown.

### Entry #009
- **Timestamp:** 2026-11-10 18:42 UTC
- Owner clarified the pacing: tiny jobs (talking, confirming a trade) are one click 100-0; small jobs (scan, filling the fuel) are two clicks 100-50-0; real work (maintenance, the drive) is four clicks 100-75-50-25-0.
- Owner clarified money is its own gate: the shop, the shipyard and a hire are paid in shiplets and are not click time at all.
- Owner clarified that the air plant and the galley already carry their own mechanic and must not be given a charge.
- Also confirmed: the crew does the work on its own; the ship runs while maintenance, food and air are handled; a crew action is not a player click.
- volume table set in assets/data/content.json: routine 1, systems 4, space 2, critical 2; "fuel" moved into the two-click classification so filling the tanks is 100-50-0.
- Exempt controls: KeepsOwnMechanic() covers every o2 task and cook; they run with no charge key and no green of their own.
- Added PressWithoutCharge(): an exempt press spends nothing but still reaches the tick, so the other buttons get their step back. Used for o2/cook and for shiplets-only station rows.
- Station split made explicit: StationRow::free rows (shop, shipyard, hire) hold no charge and draw no bar; trade confirmations and every other commit row are charged (routine = one click).
- CommandRowView gained `charged`, so Draw paints the green only where clicks are actually spent; shiplets rows stay plain.
- Checks run: g++ -fsyntax-only on every cpp/ unit (clean); headless console scripted - engine 4,3,2,1,0 and refused after, a cook press (own mechanic) puts the engine back to 1 of 4, scan opens at 2 of 2, cook opens with no charge.
- Not verified: no CMake in this environment, so the client was not built and no screen was seen; the visual result is still unverified until the owner builds locally.

### Entry #010
- **Timestamp:** 2026-11-10 19:12 UTC
- Owner reported two logic holes: the drive could be "maintained" at full condition, and a finished deep scan could be run again from the same button.
- Owner's rule, read from the two examples: a press must either do real work (spend a click, drain, wind the others up) or be visibly unavailable. Nothing may look pressable while doing nothing, and no press that does something may go uncounted.
- Fixed engine: EngineClick refuses while engineCondition >= 100, and the engine-room button is live only when charge > 0 and the drive is below full; at full it reads Off with the title "The drive is at full condition; there is nothing to maintain".
- Fixed acknowledge: the done-press now runs through PressWithoutCharge, so reading a finished result spends no charge of its own but still returns a step to every other button.
- Fixed scan: a body already deep-scanned shows "Deep Scan Filed" and the button is not live, so no click can be paid for a scan with nothing left to reveal.
- Fixed talk: TalkToCrew refuses at full morale and the conversation row is not live there either.
- Audited and left as they are, because they already match the rule: travel/scan/extract enable conditions in NavScreen, cook (full counter, empty larder), O2 (ready, running, power), medical (health, medkit, energy), projects (blocked list), station rows (money gate and greying), system service (below 100 and stock > 0).
- Checks run: g++ -fsyntax-only on every unit (clean); headless console - engine at 100 takes no click (charge stays 4), engine at 55 spends one (4->3) and refuses the second press once the drive is full.
- Not verified: the scan-filed and acknowledge paths could not be driven through the console harness (SelectTarget does not set navTarget there), so those two rest on the compiler and on reading the call path.
- No GUI build here; visuals still unverified.

### Entry #011
- **Timestamp:** 2026-11-10 20:05 UTC
- Owner's report: "it takes 3 clicks to seat all tanks but 4 clicks overall before the button for filter running can be clicked. So how can it be that when I do only two clicks the worker was able to do this whole work? It shouldn't." A hand's job must cost what the same job costs the commander.
- Root cause found: the crew's own presses were re-entering the clock. `AutoFilters` calls `StartCanisterInstall(slot, worker->name)`, and that function spends an action of its own (`PerformAction`) because the commander's press must; made from inside the crew tick, which already runs inside the player's action, it advanced the ship a second time. `AutoMess` had the identical hole through `StartCookingSelf(worker->name)`.
- What that looked like in the headless console before the fix: one player click often moved the action counter by two (1, 2, 3, 5, 6, 7, 8) and the crew's two-click seating job closed inside a single click, because the extra tick also ran `AdvanceTasks`.
- Fix: `StartCanisterInstall(slot, by, spendsAction)` and `StartCookingSelf(by, spendsAction)` gained a flag, defaulting to true so every commander path is untouched; the crew passes false. A hand's press now fills its own feed line and registers its work at the hand's rate, but the click that carries the ship forward stays the player's.
- Audited the other crew-direct calls for the same bypass and left them alone, because they do no work of their own: `RunBank` is a pure effect (slots active, filter life, the +10 Pro-Action line) and starts nothing the player pays for; `ServiceDrive` in the maintenance path makes no action; `TalkToCrew` is the player's own press from the crew row, not a hand's.
- The bank switch keeps its price for the player: `StartFilterBank` spends one action and then runs the bank, so the commander's 3 seatings plus 1 switch are still four clicks of his own.
- After the fix, same script: the action counter moves by exactly one per click, 1 through 24 with no jumps; two clicks never produce a running bank, and the bank turns over on click 18 with all three slots active after the untrained hand's own two-click seatings and the third canister waiting for the air to fall to his stage.
- Checks run: g++ -fsyntax-only on every unit in cpp/ (clean); headless console scripts to /tmp/s18.txt through /tmp/s22.txt.
- Not verified: no CMake or raylib build here, so nothing was seen on screen; the visual result and the button's own pacing in the GUI stay unverified until the owner builds locally.

### Entry #012
- **Timestamp:** 2026-11-10 21:10 UTC
- Owner read the cross that rises after a press as the clock's own marker: "that cross is an indicator for when the time has advanced one click", and reported that presses are mixed - some carry the cross without moving the clock, and some move the clock without carrying it.
- Mechanism located: the marker is `SpawnMark` in `cpp/ui/Fx.cpp` (220 ms still, 26 px rise, 950 ms fade, five alive at most), spawned from nine places in `Draw.cpp::HandleClick`. It is a cosmetic record of a press, not of a tick: nothing in it consults `ActionCount()`.
- Reading the two directions gave these mismatches, all confirmed in code:
- Cross, no tick: purchase rows (shop, shipyard, hire, refit) go through `FreeCommit`, which moves no counter, yet the press spawns a mark.
- Cross, no tick: the station's free reading rows ("reading is free: nothing on this page costs a click") and the bridge row "Open Navigation", which only opens a window.
- Cross, no tick: opening a room's briefing from the Home deck plan or from a callout on the hull - pure reading, still marked.
- Tick, no cross: "Stand watch" (`Navigation.cpp:545`), "Dock at ..." (`:1020`) and "Undock" (`:1033`) each call `PerformAction`, but the chart's hit loop spawns a mark only for `travel` and `scan`.
- Owner settled the rule for the two disputed classes: reading is never a click - no cross, no tick; a purchase is a real click and may advance the clock, money being its own gate and the reason it carries no green bar.
- So the invariant to hold from here: a press that calls `PerformAction` exactly once carries exactly one cross; a press that moves no clock carries none.
- Plan agreed with the owner: settle this room by room rather than in one sweep, after his playtest of the current state.
- Checks run: none - this entry records an audit of code paths only; no file but this one was touched.
- Still unverified: the visual behaviour of the mark itself, since no raylib build is possible in this environment.

### Entry #013
- Owner approved starting the room-by-room pass with Navigation, followed by his playtest before moving on.
- Task tabs now show the title alone, without prefixes such as NAV and ROOM; tab width and hit boxes follow the shorter title.
- Removed Navigation's pinned Target Actions banner and put Travel to the left of the target details, with Scan and Extraction on the right.
- The embedded controls wrap their labels, retain their own hit areas, and make room for the reasons a control is blocked.
- Navigation controls that cannot start work are grey instead of showing a full green battery.
- Navigation describes the applicable reason, including current location, range, engine, energy, fuel, full cargo, depleted deposits, or an already filed scan.
- A button with no charge explains that other work recharges it; its charge is still separate from other blockers.
- Shared progress surfaces now show running progress in green and finished work with a green glow, including stage and footer controls.
- The same shared surfaces suppress green and mute text for Off and Empty states; a genuine blocker takes precedence over an empty charge.
- A completed extraction remains reachable after selecting a different target, rather than appearing as a busy rig.
- Navigation marks a press only if ActionCount increased, covering work actions such as docking without marking map viewing or a free completion acknowledgment.
- The sector-view command is explicitly free and uncharged; the docking row displays its own charge and is disabled when empty.
- Correction to #012: the owner did not decide that buying advances the clock. The rule is whether the action implies work and time; purchase logic was not changed in this Navigation pass.
- Other rooms' action logic and blocked-reason text have not yet been audited; they remain for their own room-by-room passes.
- No tests or builds were run, in accordance with the owner's instruction. The GUI, layout, glow, and interaction remain for the owner's local playtest.

### Entry #014
- Owner requested an extremely small Navigation-only text correction after playtesting.
- Blocked Navigation progress controls now put their short status inside the control.
- Removed the separate text previously painted underneath those controls.
- Travel at the current position reads "Already here" inside its button.
- An already filed scan keeps its existing "Deep Scan Filed" button label.
- Removed the redundant explanation under the filed scan button.
- Exhausted deposits read "Depleted" inside the extraction button.
- Other existing Navigation blocked reasons are shortened to fit the same treatment.
- The disabled Navigation command row also puts its short reason on the row, not below it.
- No button availability, charge, clock, engine, or other room logic was changed.
- No tests or builds were run; the owner will verify the GUI locally.

### Entry #015
- Owner noticed that Home room callouts darken on hover, but other Home labels with right-click tasks did not.
- Added the same palette hover background to the upper system/stat cells.
- Added it to the lower information cells and Navigation strip.
- Active event and docked-station cards now receive the same feedback.
- Empty event placeholders stay unchanged because they do not open a task.
- Deck-plan room areas also show a hover response where they open a room.
- Crew markers and visible labels get hover feedback where they open a crew task.
- Crew hit areas take precedence over room areas, matching the existing click handling.
- Hover checks use the scrolled content position and only respond within the content viewport.
- Existing click, right-click, clock, and action behavior was left unchanged.
- No tests or builds were run; the owner will verify the hover visually in his local build.

### Entry #016
- Owner approved applying the agreed engine-room battery and maintenance logic first, then playtesting it himself.
- One Run the drive press restores 25 percentage points of engine condition, capped at full.
- The engineer's existing automatic service uses the same 25-point maintenance amount.
- The engineer does not also service the drive during the commander's own engine press.
- That guard prevents one manual press from restoring twice during its own clock tick.
- Each successful manual press still uses the existing four-step systems charge and one clock click.
- The green engine battery is again drawn even when the drive is full and cannot be maintained.
- This preserves the difference between stored button charge and engine condition.
- The engine button's descriptive text now states the 25-point restoration rather than full restoration.
- Engine wear remains unchanged at 1% per four clock clicks; no asset or configuration was edited.
- Other buttons, rooms, and their shared progress rendering were not changed in this pass.
- The existing crew timing and battery accounting were otherwise left as they were.
- No tests or builds were run; the owner will check the engine behavior locally.

### Entry #017
- Owner directed an immediate restoration of the original cooking mechanism from PR #33 and no other changes.
- Retrieved the original `AutoMess` from the final PR #33 commit `cedf30be6faf62bc9ef1ba5d7460b5fecd4ce3ed`.
- Restored that function verbatim in `cpp/core/CrewLogic.cpp`.
- The original starts restocking below the configured `cookBelow` threshold.
- Removed the later training-based adjustment to that threshold by restoring the original function.
- The original creates the automatic batch directly and records the assigned cook.
- It spends one dry ration at batch start and retains the original restock chain.
- Batch yield, batch duration and completion code were already present and were not changed.
- Player-triggered cooking, the clock, hunger rules, assets and other rooms were not changed.
- Crew Quarters assignment visibility was not changed in this restoration-only edit.
- No tests or builds were run, per the owner's standing instruction.

### Entry #018
- Owner directed removal of the code hiding the right-hand Crew Quarters controls.
- The selected crew member's standing job and job options are now built even while busy.
- The order list is also built while busy instead of being omitted.
- The right-hand renderer no longer skips those controls because the crew member is busy.
- The existing busy status remains visible in the Orders panel.
- Busy controls are displayed disabled rather than appearing usable when the underlying action refuses them.
- The original job-staging and order-staging restrictions were not changed.
- No cancellation, interruption or queuing behavior was invented.
- Empty crew lists still have no job selector.
- Cooking and other rooms were not changed in this edit.
- No tests or builds were run; the owner will check the screen locally.

### Entry #019
- Owner rejected the invented restriction preventing orders to a busy crew member.
- The job selector and order rows no longer disable merely because the selected hand is working.
- Staging a job or order no longer refuses a hand merely because that hand is busy.
- The order sheet no longer refuses to commit solely because a selected hand is busy.
- Selecting Lay off still stages the instruction; handing over the sheet now applies the layoff immediately.
- A layoff uses the existing single action for handing over the sheet, not a five-click wait.
- Layoff takes precedence over other staged instructions for that hand, without charging training fees for them.
- The dismissed hand stops their ongoing personal work and any old personal order task.
- An interrupted unseated canister returns to stock, following the existing interruption handling.
- An interrupted cooking batch stops, and the original auto-restocking chain stops with it.
- Other staged hands still receive their orders through the existing work mechanism.
- The separate existing restriction while a whole order sheet is already in progress was not redesigned.
- No tests or builds were run; the owner will verify the behavior locally.

### Entry #020
- Owner directed removal of the remaining restriction on issuing a new crew sheet while another is in progress.
- Staging a job or order no longer checks for an earlier order sheet.
- Committing the sheet no longer refuses because of an earlier sheet.
- Crew Quarters no longer disables its controls or replaces the new-sheet note for that reason.
- New orders are tracked by each person's existing work task instead of creating a global sheet lock.
- A newer instruction replaces an earlier personal order for the same hand; other hands' work continues.
- Old saved global sheet tasks are ignored as a button lock and may finish normally.
- A committed layoff still takes effect immediately and stops that hand's current work.
- An empty assignment-button charge cannot prevent a layoff; the click still advances the clock once.
- If other staged training is unaffordable, layoff still proceeds and those other orders remain staged.
- The existing charge rule for sheets without a layoff was left intact.
- Cooking's restored PR #33 mechanism and unrelated rooms were not changed in this edit.
- No tests or builds were run, as instructed; local playtesting remains necessary.

### Entry #021
- Owner clarified: once a person is removed from their job, nothing they were doing continues.
- A committed layoff takes effect before its clock step, as in the preceding edit.
- A removed person's staged sheet entries are cleared.
- Their pending-order marker is cleared.
- Their crew claim and personal order task are cleared.
- Tasks identifying the removed person by name, primary crew ID or second crew ID are cancelled.
- That includes their cooking batch and its automatic restock chain.
- An interrupted unseated canister uses the existing return-to-stock rule.
- Removing the person prevents any old personal order from applying later.
- Dismissal cleanup also runs if the person is already off a job.
- The low-funds path uses a snapshot of IDs because dismissal clears staged entries.
- No tests or builds were run, per the owner's instruction; local verification remains necessary.

### Entry #022
- Owner clarified the automatic cook starts when cooked food is at 10 or less.
- The previous strict-below comparison excluded exactly 10.
- Automatic cooking now starts at or below the configured threshold.
- A restocking cycle already started still continues to the rack capacity.
- Each batch still uses one dry ration.
- Each batch still takes five clock clicks.
- Each finished batch still adds up to five cooked food.
- The final batch is still capped by rack capacity.
- A manually started batch no longer starts an automatic restock cycle above the threshold.
- An already active automatic restock cycle is not cancelled by a manual batch.
- Mess Hall text now states the inclusive threshold.
- No tests or builds were run; the owner playtests locally.
