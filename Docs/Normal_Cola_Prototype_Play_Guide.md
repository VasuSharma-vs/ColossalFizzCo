# Normal Cola prototype play guide

**Current build — 14 November 2026: native14119 / Game70.** Plastic-bottle migration is installed; native65/65, packaged core13/13 and late-join13/13 checks pass. See [migration status, evidence and pending class decisions](Cola1_Plastic_Migration_2026-11-14.md) and [the DOCX checkpoint](Cola1_Plastic_Migration_2026-11-14.docx). Use Saved/Builds/NormalColaPrototype_14119/Windows/ColossalFizzCo.exe. Older build numbers below are historical.


Updated 14 November 2026. This guide covers plastic bottles, the five Normal Colas and Normal manual twist caps. Current player, customer and bottle sizes are preserved.

## Current build

Candidate 14118 is at `Saved/Builds/NormalColaPrototype_14118/Windows/ColossalFizzCo.exe`. Keep the complete Windows folder together. The default map is `/Game/NormalColaPrototype/Lvl_NormalColaPrototype`; the Validation map contains the automated driver and is for QA.

Candidate14118 passed the packaged three-customer multiplayer loop, banking, manual manufacture, stocking and pressure flight with75 ms configured lag and2 percent loss. Read [the current implementation and acceptance report](Bottle_Prototype_Progress_and_Questions_2026-11-14.docx) for exact evidence. Automated checks do not replace hands-on input, appearance and handling acceptance.

## Controls

- WASD and mouse: move and look.
- Hold G: pick up or position a prop. Double-tap G: drop it.
- E: interact with the workbench, checkout or ATM. Look toward the station controls and stand within their interaction reach.
- Escape: close the terminal and release active production controls.
- Hold F in the manual filling terminal: open the valve. Release it to stop.
- R in the capping terminal: advance manual twist closure. The safe solo method below does not require another player.

Powered rocket flight owns the bottle movement. Wait for thrust to finish before trying to grab that bottle again.

## Serve the first customer

The shared bank starts at $0. Use the existing starter shelf stock for initial sales.

1. A giant customer takes a bucket, collects bottles and queues. Wait for the customer to dock the bucket.
2. Place each requested bottle in SCAN. Scanning is automatic. The live list removes bottles taken out; Save preserves selected identities outside the volume.
3. Check content, temperature and expiry. Replace unwanted bottles before locking the bill.
4. Open checkout with E and choose **Finalize and lock bill**. The customer supplies physical coins in PAYMENT.
5. If change is due, move the correct existing shop-owned coins to CHANGE. Complete the paid checkout only when payment and change match the bill.
6. The customer collects its purchase, returns the bucket and leaves. Temporarily blocked paid customers wait and retry their exit; clearing the passage lets them continue.

Customers pay exactly when the register cannot make change. Rounded payments require sufficient real change coins. The register starts empty.

## Bank and buy supplies

Sale proceeds remain physical shop cash until deposited. They are not also credited directly to the bank.

Move shop-owned coins into the ATM deposit tray and deposit them. All players share the resulting bank balance. Withdrawals debit that same bank and produce physical coins. Supported denominations are $1, $2, $5, $10, $20, $50 and $100.

Use the supply terminal to buy empty plastic bottles, Normal caps and pre-mixed Normal bulk cola. Deliveries need free output space. Prices are configurable prototype values, not final balance. Any teammate can continue the shared checkout.

## Fill, cap and restock

1. Bring a Normal bulk container within 400 cm of a manual machine. Open it with E and select **Connect single supply pipe**. Several machines may share a source; only transferred contents are deducted.
2. Put an empty eligible bottle in the holder and select **Align and seat in bottle holder**.
3. Hold F or the filling-lever button. Release to stop. Closing the menu, walking away or losing connection also releases the valve. Filling stops at nominal capacity.
4. Use the safe solo sequence: leave the bottle open until residual pressure is **0.10 or lower**, attach a compatible Normal cap and tap R until fully closed.
5. The faster alternative uses the one-time **three-second temporary seal**. Attach and close the cap before it expires. Expiry while still pressurized and unsecured can eject the cap and launch the bottle.
6. A helper can supply cap-holding force with G while another player twists with R. Use explicit role buttons if several bottles are eligible.
7. Carry the full sealed bottle into a shelf stock area. Shelf registration is automatic.

Demo supplies are pre-mixed. Raw-ingredient recipes and advanced cola categories are outside this prototype. A physical hose/dispenser presentation and the full hot-source cooling workflow remain separate equipment integration work.

## Pressure, saving and acceptance

Gentle carrying is intended to be safe. Strong impacts, acceleration, running, shaking and spinning can build pressure. During a pressure launch, Rocket Projectile Component controls powered motion, liquid drains and mass updates. Normal physics takes over afterward.

The prototype uses its own autosave slot and a 60-second interval. Unfinished checkouts are canceled on reload; completed receipts and committed financial state persist. Existing original-world saves remain separate.

See the DOCX report for exact build/test evidence, known warnings, unresolved balance decisions and the remaining manual G/GG/hold-G, E/F/R, host/client and performance checks.

## Archived guidance from earlier builds

The original guide is preserved below as history. Its former build paths, acceptance boundaries and mandatory two-player closure instructions are superseded by the current sections above.

# Normal Cola prototype play guide

## Manual filling update — 13 November 2026

Use the current Editor build. Earlier packaged archives do not contain this update. The existing raw-material rack still provides ten empty plastic bottles, ten Normal caps and one full Normal bulk container. The starter workbench is now a manual machine, and the supply terminal sells additional manual machines for a configurable **$50** each. Carry and place purchased machines using the existing **G** controls; each machine has its own bottle holder and one supply pipe. No electricity is required.

1. Bring a Normal bulk container within 400 cm of the machine. Press **E**, then **Connect single supply pipe** for that container. Several machines can connect to the same source; each valve draws only the quantity actually transferred.
2. Place a plastic bottle in the holder area and choose **Align and seat in bottle holder**. Misaligned or carried bottles cannot be filled.
3. **Hold F** or hold the filling-lever button with the mouse. Release the key/button to stop immediately. Closing the terminal, losing focus, walking away or losing connection also releases the valve. Filling can resume with the retained contents. Nominal full capacity remains the safety limit.
4. Choose a sealing method:
   - **Fast/risky:** stop filling, apply the **three-second temporary seal**, attach a Normal cap and tap **R** until fully closed. The temporary seal is available once per fill. It prevents leakage and retains residual pressure; it cannot be refreshed by repeated presses. Proper closure cancels its expiry hazard. Letting it expire with significant pressure and an unsecured cap can eject the cap and trigger the existing bottle rocket.
   - **Slow/safe:** leave the bottle open until displayed residual pressure is **0.10 or lower**, then attach the Normal cap and tap **R** to finish. No second player or temporary seal is required.
5. Carry the finished bottle to a display shelf. Existing registration, NPC selection, checkout, liquid/mass/temperature updates and bottle/cap persistence remain in use.

A helper can maintain cap pressure with **G** while another player twists with **R**, prepare the next bottle, handle completed goods, or operate another machine. One player can cycle through several machines, leaving earlier bottles to vent. Each machine holds one bottle for filling; the sequence across machines is entirely up to the players.

Initial tuning is one portion/second (four seconds for a normal bottle), 0.60 residual pressure per full bottle, 0.15 pressure/second natural venting, a three-second temporary seal and a 0.40-second server lever timeout. These values are editable prototype settings. Temporary-seal deadlines use the persistent manager clock, so saving and loading cannot restart their countdowns.

See [the supplied design](Normal_Cola_Manual_Filling_Design.md) and [the manual-filling validation record](Manual_Cola_Filling_2026-11-13.md) for exact acceptance results. The dated notes below describe earlier builds and their former mandatory two-player closure.

**Checkout E repair, Editor 11205:** enter Play, climb the front player access
deck, look at the counter near its front controls and press E. The old 150 cm
prop trace missed this giant desk; a station-specific C++ query now uses the
existing 350 cm control reach. Actual E opens the menu in standalone and on the
listen-server host. Remote testing currently hits a Prop Manager initialization
disconnect (`ReliableBufferOverflow`); see [the repair record](Checkout_Interaction_Fix_2026-11-12.md).
This repair is not in the older packaged 11193 candidate described below.

**Current acceptance boundary, 12 November:** the two-peer instrumented service/RPC loop seals a genuinely full bottle and passes sales, ATM banking, supplies and supported restocking. Staged-impact pressure flight and desktop role/G/R routing pass. Clean-start giant navigation passes three early starts, including the playable map without a test driver. The cooked standalone now starts after a repaired bottle-component template, but desktop inspection timed out; packaged multiplayer and performance remain pending. A complete player-driven manufacturing/carry session, shake/drop, late join/adverse networking, and the specified hot bulk supply/hose/cooling workflow still need acceptance. Current scope is plastic bottles, five Normal Colas and Normal manual twist caps. Preserve the reference character and bottle sizes.

## Open the playable world

The latest packaged candidate is `Saved/PrototypeResume_20261112/Packaged_11193/PackageArchive/Windows/ColossalFizzCo.exe`. It opens the player-facing prototype map. The native Game runtime is 11193; the newer 11194 Editor build adds editor-only five-flavor filling coverage. This candidate passes instrumented packaged multiplayer and settled late join with both no emulation and verified 75 ms outgoing lag / 2% packet loss per peer. Human input and the hot hose/cooling workflow remain pending.

From the project directory, start two local game windows with:

```powershell
& ./Scripts/StartNormalColaPrototype.ps1 -ArchiveRoot 'Saved/PrototypeResume_20261112/Packaged_11193/PackageArchive' -Mode LocalPair
```

For two computers, use `-Mode Host` on the host, and `-Mode Client -ServerAddress '<host address>'` on the other computer. These modes target the same player-facing map and require the matching package on both machines. LocalPair is a convenient two-window check; cap sealing still requires two distinct player characters. No automated validation driver runs in this map. Normal play progress is isolated in `Saved/PrototypePlay/Host` and `Client`, separate from the original world saves and disposable test saves. Use `-WhatIf` to preview a launch without starting either game. The launcher is parse/preview-checked; its full player-driven session remains unaccepted.

Open `/Game/NormalColaPrototype/Lvl_NormalColaPrototype` and press Play. For multiplayer, use a listen-server PIE session with two players. The separate `_Validation` world contains an opt-in test driver; it is not the player-facing demo.

The bank and register start empty. The 356 approved starter bottles provide the first sales, so serve customers and deposit their coins before purchasing supplies. The prototype has its own `NormalColaPrototypeAutosave` slot and a 60-second autosave interval. Existing `ShopAutosave` progress belongs to the original world.

## Controls and first sale

- Move with WASD and look with the mouse.
- Hold G to pick up or place a prop at the aimed location. Double-tap G to drop it.
- Look at the workbench, checkout or ATM and use E to interact.
- Use the terminal buttons for purchases, production and billing. Escape closes a terminal.
- In the manual-capping terminal with one eligible bottle, the first G claims the holding role and contributes force in the same request. Keep pressing G; a different player presses R to claim rotation and advance it. Use the explicit role buttons to select between multiple bottles. Start holding before rotation to avoid unnecessary leakage. R is provisional prototype tuning; Escape leaves your role. These contextual inputs do not change ordinary G grab controls outside the terminal. The atomic-claim service/RPC repair passes on both peers with a full bottle; complete human manufacture remains pending.

These keys are read from the actual `/Game/Input/IMC_Default` mappings. Saved Blueprint carry callbacks have automation coverage; a complete human hardware-input acceptance pass is still required.

1. A giant customer takes a bucket, collects Normal Cola from shelves and joins the checkout queue.
2. At the front, the customer puts the bucket at the checkout. Place each requested bottle in the SCAN area. The live list preserves each bottle identity and groups matching products.
3. Inspect content, temperature and expiry. Remove/replace unwanted products before locking the invoice. Save preserves selected bottles after they leave the scanner volume.
4. Use **Finalize and lock bill**. The customer provides physical coins in PAYMENT.
5. If change is due, move existing shop-owned coins into CHANGE. Use **Complete paid checkout** once tender and change match the bill. Customers pay exactly when the register cannot make change.
6. The customer collects purchased bottles, returns the bucket at the exit and leaves. The counter can serve the next customer.

Sale proceeds remain physical shop cash. They are not also credited to the bank. Move shop-owned coins into the ATM deposit tray to convert them into shared bank funds. The ATM terminal withdraws the selected denomination from that same bank into real coins.

## Reinvest and manufacture

1. At the workbench, buy an empty plastic bottle, a Normal manual twist cap and a pre-mixed Normal Bulk Cola Container. Prototype costs are configurable: $4, $0.50 and $20 respectively. Deliveries use the supported outboard platform; a full area rejects the order without charging the bank. Clear delivered goods to make room.
2. Place the items in the labeled Bottle, Cap and Cylinder input areas. Close the terminal while moving physical items, then interact again.
3. Select **Fill from** the desired Normal Cola cylinder. Filling stops at normal full capacity; a four-portion bottle uses 10% of a forty-portion cylinder.
4. Attach the compatible physical cap. Two distinct players then use the Manual Wheel Rotator: one maintains holding force with G presses, and the other advances rotation with R. Holding effectiveness declines over time; insufficient hold permits accounted liquid leakage before closure. A completed seal remains closed after releasing hold. An open bottle is not ready for NPC shelf selection.
5. Place the finished bottle within a shelf stock area. Shelf stock registers automatically; customers can select the new product.
6. Repeat sales, ATM deposits and purchases. The five demo contents are Lemonade, Mint, Sweet, Spicy and Zero-Calorie.

There is no raw-ingredient mixing in this demo; it remains a later full-game feature. Empty bottles must meet the approved strength, cleanliness and empty-content requirements for reuse.

The current fill control uses the existing direct source-to-filler service. The specified physical hose/dispenser and hot-source cooling workflow are still pending and must not be mistaken for completed equipment.

## Plastic pressure

Impacts and shaking increase persistent agitation pressure. Strictly exceeding
the plastic bottle's strength-adjusted tolerance ejects its physical cap. Per the
designer's latest choice, Editor **11207** defaults to the original **BP_Cola
Rocket Projectile Component** tipping/dragging/erratic flight. Actual liquid is
consumed and pressure vents; exhaustion ends powered movement with a physics
falling handoff. The editable **Cola > Pressure > Rocket > Use Original Rocket
Flight** option can select the retained Chaos-thrust mode instead. This option
selects flight behavior; it does not bypass the pressure/cap launch conditions.

The new original-flight native regression passes cleanly; physical-mode coverage
also passes. Full suite: 57/58, with the existing liquid-height assertion failing.
Visual handling, actual remote flight, late join and adverse networking require
acceptance. The older 11175 two-peer physical-thrust result is historical evidence
for the optional mode, not validation of this new default. The packaged 11193
candidate does not contain this change. Read [the current rocket record](Original_Bottle_Rocket_2026-11-12.md).

## Current validation boundary

See [the current checkpoint](Prototype_Progress_2026-11-12.md) for exact build numbers and evidence; the older validation record retains historical results. Automated native tests and the two-player driver establish only their exercised financial, manufacturing, shopping and persistence paths. They do not replace a full player-driven keyboard/controller, pressure-failure, packaged-build or performance pass.
