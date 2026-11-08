# Trade Contracts and the Station Star-Wiki

A design list for the station trade system. This describes the intent, the
player-facing behaviour, and what the code has to guarantee for it to be real.
It is not a claim that any of it is built.

---

## 1. The problem this replaces

- Every station currently renders the same window: the same 7-good shelf, the
  same yard, the same hiring hall. Only the name and a price multiplier change.
- Measured before this design: 30 authored ports, **8 distinct price baskets**,
  9 ports sharing one basket, and one shared market list with no per-port filter.
- The authored chart left most systems with no berth at all, so the sameness was
  structural, not cosmetic.

The fix is not "make each station look different". It is **each station knows
its own system, and its trade offers are derived from that system.**

---

## 2. The starmap panel (star-wiki)

- Every station holds its **own starmap**, presented like a wiki page for the
  system the station sits in.
- It sits beside the shop in the station window, as one of the cards.
- **It fills itself.** A station generated in a system writes every planet in
  that system into its own page: name, kind, and the system's own reading.
- The player can browse it: pick a planet, read it. That is the goodie.
- Underneath, this page is the **mechanism**: it is where a station learns the
  names and contents of the bodies around it, and the trade offers are then
  built out of that knowledge instead of out of a global list.

---

## 3. The station window, as four cards

The window already walks left → middle → right. The design keeps that walk:

| Left (cards) | Middle (what the card holds) | Right (the one item + its button) |
|---|---|---|
| Market | supplies and goods | the inspector, price, commit |
| Sell cargo and survey data | everything aboard a counter will take | the inspector, price, commit |
| **Star-map** | **the planets of this station's system** | **that planet's page** |
| **Trade contracts** | **Delivery / Freelance / Station-to-station** | **the offers of the chosen kind** |
| Shipyard and crew | refits, rooms, hiring | the inspector, price, commit |

Opening a card and picking an item stay free. Only commitments spend actions.

---

## 4. The three trade kinds

The Trade contracts card opens the middle panel with exactly three routes:

### 4.1 Delivery trade

- The right panel lists **deliveries available at this station**.
- Accepting one: **one click, +1 on the clock**, the button greys out.
- The player carries the goods and hands them over at the destination.
- **Paid and finished at the destination.**
- The starmap shows a **green pathfinder to the delivery point.**

### 4.2 Freelance (buy goods and sell them)

- The right panel lists **different requests** drawn from this station's system.
- Accepting: **one click, +1 on the clock**, the button greys out.
- Then the player must do the actual trade:
  **buy the goods → deliver them → sell them at the destination.**
- The green path is drawn to the target destination.

### 4.3 Station-to-station trade

- The right panel shows **every station visited so far**.
- Picking one activates the trade between the two stations.
- The click **advances the clock +1** and the button greys out.
- The fiction is a **data trade** — survey data moved between stations. It is a
  name for the trade, not a separate resource the player has to manage.
- Because both ends are different stations, **the green path is on one map level
  up**: the player leaves the system chart and sees it on the sector chart.

---

## 5. Re-randomising the offers

- Trade offers **mix and re-randomise every 200 clicks.**
- **This never touches accepted work.** A contract the player has taken on
  survives the re-roll and stays completable; only the unaccepted offer board
  rotates. The player is never punished mid-run for a clock they did not watch.
- The rotation is deterministic per station, so the board does not flicker and a
  re-roll cannot produce the same list it just replaced.

---

## 6. The green route on the starmap

- Any live trade run draws a **green path** between the berth the goods came from
  and the berth they are owed to.
- Same system: drawn on the **system** chart.
- Station to another station: drawn on the **sector** chart, one level up.
- It is derived from live state, so it disappears the moment the run is paid.

---

## 7. What the economy has to guarantee

This is the part that decides whether the design is real or decorative:

- **A run must be able to pay a margin.** Today buy is `ceil(value × f × 1.2)`
  and sell is `value × f`, so `f` cancels and every crossing loses ~20% even
  between two differently-priced stations. Freelance cannot exist on that.
- Therefore: a berth's **export commodity** is what its own basket values
  least, and its **buyer** is the reachable berth that values it most. Cargo
  bought at the exporter and sold at the importer must clear a profit after the
  crossing.
- A delivery pays a **fee for the crossing**, not for the cargo: the goods are
  the shipper's, the fee is fixed when the contract is signed.
- Contract freight cannot be sold at a counter and cannot be jettisoned for
  profit; the run is the price.

---

## 8. What stays separate and untouched

- The **18 authored story contracts** stay exactly as they are.
- The **repeatable commissions** stay exactly as they are.
- Trade contracts are a third, independent set. They share the clock and the
  hold, nothing else.

---

## 9. Open points still to settle

1. **How many offers per kind** at one berth: one per kind, or a short list.
2. **What the star-map lists**: planets only, or moons and belts too.
3. **What counts as "visited"** for the station-to-station list: docked at least
   once, or just flown through the system.
4. **Whether the star-map page shows a planet's scan reading when it is already
   scanned**, or always shows the public reading.
