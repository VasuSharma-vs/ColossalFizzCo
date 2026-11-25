# Game Design Document (GDD) — Master

**Shop progression and daily schedule — 24 November 2026.** Players carry finished bottles to shelves and NPC pickup credits the shared bank automatically. Section 2 describes the current sales loop. Section 7 defines demo and full-version hours, upgrade rules, and proposed playtest values. [Shop Upgrades and Daily Schedule](https://chatgpt.com/space/page_a732c3017920819184f9220f2fbfabfd#bea5a902-5b45-46f6-ba79-ee1587fa3e63)

**Modular product architecture — 15 November 2026:** [Approved Bottle / Content / Cap specification](Modular_Product_Databases_2026-11-15.md) supersedes flavor-owned capacity and filled-SKU replacement rules. Maximum portions belong to the bottle type; current portions and immediate ownership belong to individual instances. Contents/caps select supported bottle products by stable named PID references. Preserve the Plastic + five Normal Colas + Manual Twist prototype scope and current sizes. [Implementation evidence and remaining acceptance](Modular_Product_Databases_Implementation_2026-11-15.md) are recorded separately.

**Design Name:** Colossal Fizz Co

**Bottle dynamics update — 12–13 November 2026:** The designer approved separate
empty bottle and liquid mass, with **0.05 kg** as the editable plastic shell
default, **20 ml per portion** and per-content density. Total mass follows actual
remaining volume; this supersedes the previous 80 kg bottle tuning without
resizing any actor. Original projectile launches use server-seeded editable
distance/speed/direction/acceleration, with liquid depleted by travel progress and
physics handoff at apex or distance limit. Actual pressure drives locally smoothed
plastic morphs. Stable ASM/ISM identity, canonical state, cash and server authority
remain required. [Current implementation and limitations](Bottle_Systems_Update_2026-11-12.md)
distinguishes tested behavior from pending visual/remote and retained-actor reuse
acceptance. The generic mass policy for other prop classes remains unchanged.

**Current prototype rocket choice — 12 November 2026:** The designer approved
the original **BP_Cola Rocket Projectile Component flight** for now: tipping,
dragging and erratic projectile flight. This temporarily supersedes the earlier
Chaos-thrust powered-flight requirement for the prototype. Pressure thresholds,
managed caps, liquid/pressure accounting and stable ISIDs remain. The physics
mode is retained as an option. See [the implementation/validation record](Original_Bottle_Rocket_2026-11-12.md);
the older physical-flight tests do not accept the new default.

**Legacy Working Title:** Midnight Fizz: Giant Thirst (*ColossalFizzCo Engine Build*)

**Genre:** Multiplayer Co-op / Physics-Based Factory Simulation / Time Management Comedy  
**Target Platform:** PC (Steam), Console  
**Target Audience:** Fans of *Overcooked*, *PlateUp!*, *Lethal Company*, *Deep Rock Galactic*, *Viscera Cleanup Detail*  

**Scale and mass decision — 11 November 2026:** The current NPC, player and Cola sizes in `/Game/FirstPerson/Lvl_FirstPerson` are approved and must remain unchanged. The player retains the existing default character size to preserve movement, collision, camera and interaction mechanics. The small-player feeling comes from the surrounding giant world. Earlier 25 cm player targets and fixed patron/player multipliers are superseded by Section 3. NPCs and movable actors use gameplay masses below the player's mass; visible size does not imply greater mass.

**Browser-chat reconciliation — 11 November 2026:** The decision record covers the Normal Bulk Cola Container, closure distinctions, art direction, persistence and the earlier scanner and physical cash flow. The eleven excluded legacy PSOs, including PSO_FoodContainer, remain outside current scope. [Chat decisions and conflict register](Chat_Design_Decisions_2026-11-11.md)

**Latest prototype scope — 11 November 2026, flight mode updated 12 November:** **Plastic Bottles + the five Normal Colas + Normal Caps (Manual Twist) only.** All other bottle materials, cola categories and cap types are excluded from the prototype. Hits, shaking, drops and physical impacts increase internal pressure; exceeding the bottle's maximum tolerance triggers a cola rocket release. The current powered-flight path is the original BP_Cola Rocket Projectile Component behavior, per the decision above; the fully physics-driven option is retained. See [the approved pressure-system scope and acceptance requirements](Prototype_Plastic_Bottle_Pressure_2026-11-11.md). This resolves the earlier prototype material/closure question in favor of plastic/manual twist without changing current bottle, player or NPC sizes. Other material/cap descriptions below are full-game reference only.

**Latest equipment decision — 11 November 2026:** [Equipment Design Specification](Equipment_Design_Specification.md) adds five planned categories; only the **Manual Wheel Rotator** is demo equipment from that catalog. Two players cooperate: one repeatedly presses **G** to hold the cap down with gradually declining effectiveness, while the other rotates it closed. No electricity is required. Cap Gun, Automatic Fill and Cap Machine, Pressure Regulator and Automatic Bottle Cleaner are full-game-only. Essential Normal bulk supply, hoses/dispenser and filling infrastructure remain in scope. [Equipment integration and conflicts](Equipment_Design_Integration_2026-11-11.md) flags input, transport, weight, decay/rotation and multiplayer-session details; this is a design addition, not implementation/test acceptance.

## Current Bottle-Contents Scope and Economy

The [Bottle Contents Catalog](Bottle_Contents_Catalog.md) is the current working taxonomy: **21 types**, comprising Normal (5), Toxic (4), Black Magic (4), Radioactive (4) and Explosive (4). The **demo includes only Lemonade, Mint, Sweet, Spicy and Zero Calories Cola**; all other categories remain future scope.

Approved Normal base-price relationship: **Lemonade < Mint < Sweet < Spicy = Zero Calories**. Spicy and Zero Calories are jointly most expensive. Numeric content prices and type-specific properties/effects are not approved by the catalog. Existing prototype numeric settings remain provisional.

The full-game price constraints, including Explosive Colas overlapping the Black Magic range, are recorded in the catalog. Preserve its partial ordering rather than inventing undefined price relationships. Older formula names, numeric formula tables and named effect descriptions elsewhere are proposals where they conflict with this catalog; they do not expand demo scope or approve effects.

---

## 1. Executive Summary & Vision

### 1.1 The High Concept
Players use the existing default-sized first-person character to operate a frantic, high-pressure cola bottling workshop in a world built primarily for colossal creatures. They feel small relative to the customers, counters, racks and surrounding structures; the player character itself is not resized into a 25 cm miniature.

Your customers are **towering Mountain Giants, sinister Bog Witches, ironbound Golems, and smaller woodland imps**. Cola bottles retain their approved size from `Lvl_FirstPerson`. Oversized production spaces create logistical challenges through distance, reach, awkward dimensions and teamwork, rather than an assumption that every giant object must outweigh the player.

### 1.2 Core Pillars
1. **Physics-Driven Production:**
   Manage hot, pressurized supply, filling, bottle condition and closure integrity. Shaking, impacts, temperature and pressure can create handling risks according to configured material/containment rules. Overfilling or leaving a bottle uncapped does not automatically trigger a timed explosion or rocket launch.
2. **The "David & Goliath" Scale Twist:**  
   Preserve the current player, NPC and Cola sizes from `Lvl_FirstPerson`. Scale and arrange most of the environment for giant customers, while the default-sized player remains visibly small by comparison. Transport equipment addresses distance, stability and handling; NPCs and movable props remain lighter than the player.
3. **Micro-Vehicles & Shop Logistics:**  
   In the full version, players use miniature carts, forklifts, tuggers and mobile capping rigs to haul bulk cylinders, move crates and close bottles on the move.
4. **Cooperative Production and Service:**
   Coordinate dispensing, compatible bottle sealing, cooling and physical shelf stocking so NPCs can collect finished drinks. Manual, automatic and material-specific closures follow their own rules. The demo uses the Manual Wheel Rotator in Section 2.5.
5. **Shared Prosperity & Shared Chaos:**  
   Multiplayer features a **100% shared shop, shared bank account and shared inventory**. A teammate's mistake is everyone's disaster. A successful rush funds supplies and upgrades to equipment, shelves and handling. The full version adds vehicle progression and more products.

---

### 1.3 Art Direction

Use semi-realistic stylized 3D with **Victorian steampunk, dark fantasy and alchemy** influences. Machinery, bottle hardware and physical coins must remain functional and readable. The browser assistant's phrase “Stylized Victorian Alchemy” is a useful working descriptor, not separately approved branding. Concept-art descriptions do not authorize changing existing player, NPC or bottle sizes. Existing supplied assets remain the visual reference; generated images were not independently reviewed for this update.

## 2. The Core Gameplay Loop

```mermaid
flowchart TD
    A["1. Buy empty bottles, compatible caps and pre-mixed Normal Cola"] --> B["2. Container to hose/nozzle to filling equipment to bottle"]
    B --> C["3. Seal with compatible closure and cool hot cola"]
    C --> D["4. Carry finished bottles to shelves"]
    D --> E["5. NPC collects displayed stock"]
    E --> F["6. Credit the shared bank automatically"]
    F --> G["7. Reinvest in supplies and upgrades"]
    G --> A
```

### 2.1 Step-by-Step Breakdown

1. **Purchase supplies:** Keep the approved $0 starting shared bank and existing starter-stock count. Prototype supplies are empty plastic bottles, Normal manual twist caps and pre-mixed Normal Cola at configurable prototype supply costs. Existing prototype product/material settings and labels need alignment without changing bottle size or resetting identity/ownership. Raw-ingredient processing belongs to the full game.
2. **Fill bottles:** Route supply through the Normal Bulk Cola Container, hose/dispenser and separate filling-equipment stage. Transfer fractional quantities under server authority and deduct only accepted transfer. Preserve bottle identity, Strength/damage and container history; initialize the new liquid's own creation/expiry lifecycle.
3. **Seal and cool:** Two players operate the Manual Wheel Rotator to secure the Normal manual twist cap on the plastic bottle. One repeatedly presses G to apply downward holding force, whose effectiveness declines with time even under continued pressing; the other progressively rotates the cap closed. Insufficient holding before closure permits cola escape through existing pressure/leakage mechanics. Releasing hold after proper sealing does not reopen the cap. Cool the hot product toward its configured optimum temperature. There is no mandatory uncapped eruption countdown. Exact station inputs/tuning and practical cooling remain open; no new warm-product sale lock is implied.
4. **Stock shelves:** Players physically carry finished bottles from production to shelves and place them using the existing Prop Manager and shelf services at the approved bottle dimensions. Players carry one full bottle at a time.
5. **Sell through NPC pickup:** During opening hours, NPCs find the products they want and collect displayed bottles. Each completed pickup finishes the sale at the shelf and credits the shared bank once. The existing bucket return, departure, NPC recycling and purchased-goods retirement lifecycle follows, with sale and ownership history retained.
6. **Reinvest:** Spend shared bank funds on supplies and the equipment, shelf and handling upgrades in Section 7.

Scanner interaction, locked bills, physical tender, change and ATM deposit are retired from this shop loop. Final bank-credit rounding and how the existing post-sale expiry adjustments apply to a direct shelf pickup remain open. [Earlier scanner and checkout design](Scanner_Checkout_Design_Update_2026-11-10.md) · [Earlier ATM accounting](ATM_Cash_Accounting_Proposal.md)

### 2.2 Normal Bulk Cola Container

The demo uses **one bulk container type**, the **Normal Bulk Cola Container**, for the five Normal flavors. Opening its valve immediately produces a powerful, high-pressure, fire-hose-like stream of hot cola. The source must not break from its own internal pressure. In the demo it needs **no electricity** and has **no progressive container heating**; hot liquid output remains enabled. Final capacity, flow, pressure and output temperature are not specified. The current 40-portion source is provisional configuration, not final balance.

The four-stage path is **container → flexible hose/dispenser nozzle → interchangeable bottle filling equipment → bottle(s)**. Glass-specific nozzles, four-bottle attachments and pressure-control pumps are later-design examples; their exact behaviors are not automatically demo requirements. Reuse the existing C++ liquid-transfer/Prop Manager services rather than creating a competing quantity or identity system. Handling uncaptured spray/spillage requires a defined policy.

For the full game, electricity-dependent container cooling, low-voltage consequences, progressive heating and a coal-fired steam generator are planned for later design. Additional bulk-container types are also deferred. Internal Cylinder/Tank asset names may remain for compatibility.

### 2.3 Pricing and Quality Boundary

At matching nominal capacity, the full-bottle base price comes from configured nominal portions and the fixed portion rate. Apply `Clamp(ContentLevel, 0, 1)` proportionally, then the approved symmetric temperature discount, capped at 20%, relative to the cola's configured optimum. Nominal fill caps selling value, and the existing expiry penalty uses the full-bottle base price. Account for bottle purchase cost and recycling value separately. Underfill reduces revenue, and warm bottles remain eligible for purchase at the temperature-adjusted price. [Pricing rules](Scanner_Checkout_Design_Update_2026-11-10.md)

### 2.4 Plastic Bottle Pressure and Rocket Prototype

Hits, shaking, drops and physical impacts increase internal pressure in the existing base/interaction pressure model. Each plastic bottle has a maximum pressure tolerance. **When `InternalPressure > MaximumPressureTolerance`, trigger pressure release and rocket propulsion from escaping cola.** The designer's 12 November prototype choice uses `BP_Cola`'s original Rocket Projectile Component flight, including its tipping/dragging/erratic movement and flight-plan synchronization. Keep the separate physics-thrust mode available for later development. Exhaustion of liquid/pressure ends powered movement through the component's falling handoff. This is not a glass-fragment event, an automatic pressure-release cap or a capping countdown. The prior fully physics-driven powered-flight requirement is temporarily superseded for the prototype.

The server owns pressure accumulation, threshold evaluation, release and authoritative rocket motion; preserve ISIDs and existing Prop Manager state/pooling/persistence rules. Required validation covers impact/shake/drop buildup, below/equal/above tolerance, oriented flight/collisions, duplicate-event protection, manual cap/content state, server/remote synchronization and repeated bottle reuse. Pressure gains, threshold values, sealed-cap escape mechanism, held/uncapped/empty cases and propulsion/discharge/end-state tuning remain open in the [prototype pressure specification](Prototype_Plastic_Bottle_Pressure_2026-11-11.md). These tests are required, not already passed.

### 2.5 Equipment Catalog and Cooperative Sealing

The complete [equipment specification](Equipment_Design_Specification.md) defines the following catalog. Suggested names are provisional; exact prices, dimensions/weights, operating speeds, voltages/power, pressure ranges and variant counts are not final.

| Equipment | Availability | Main requirements |
| --- | --- | --- |
| Manual Wheel Rotator | Demo and full game | Cheapest cap-closing option; large, extremely low portability; two players; manual power, no electricity. One player repeatedly presses G to hold the cap down, the other rotates it closed. Holding effectiveness deteriorates even with continued pressing. Carrier required to relocate; demo transport arrangement remains open. |
| Cap Gun | Full game only | Expensive, highly portable, handheld, one player; high-voltage electricity. F1 wheel-gun-inspired cap tightening; excessive torque can damage/break caps. |
| Automatic Fill and Cap Machine | Full game only | Most expensive current catalog type, very high voltage; grabbable hose-connected nozzles with separate fill and close inputs. Variant nozzle count changes size, portability, operating demand and throughput. Overfill/over-tightening use existing bottle/cap failures. |
| Pressure Regulator | Full game only | Raises or lowers supplied liquid pressure within variant limits. Fuel/electric versions; wider ranges cost more and can consume more energy. |
| Automatic Bottle Cleaner | Full game only | Cleans dirty bottles over several seconds, with capacity variants. Energy source/duration/capacities open. Cleaning does not repair Strength; reuse still requires empty, clean and Strength > 70%. |

Electrically powered machines expose **RequiredVoltage**, **PowerConsumption** and **OperatingPressureRange** (MinimumOperatingPressure/MaximumOperatingPressure, or not applicable when pressure is unused). Shared data also supports type/variant, price, portability, size, required players, energy source, applicable fuel consumption, capacity and operating speed. Manual equipment requires no electrical connection; rotator PowerConsumption is 0. Specific low-voltage effects remain deferred with the electrical system.

The rotator's time pressure is declining **holding effectiveness**, not an inevitable bottle eruption timer. Inadequate hold before closure permits leakage; the bottle's separately approved pressure-tolerance rule still governs its rocket reaction. Preserve the current bottle/player/NPC sizes, existing pressure/cap/liquid services and approved mass policy. Numeric weight and force/pressure conversion are unresolved. G already serves grab gestures, and carrier-required relocation touches deferred vehicles; both require integration decisions rather than silent key changes, a solo bypass or an assumed full vehicle implementation. See [the equipment conflict register and required tests](Equipment_Design_Integration_2026-11-11.md#5-conflicts-and-unresolved-dependencies).

---

## 3. The Scale Twist: Tiny People, Giant World

### 3.0 Approved Reference and Scale Categories

`/Game/FirstPerson/Lvl_FirstPerson` is the authoritative visual and spatial reference. Preserve the current NPC, player and Cola sizes, including their existing component transforms. Do not change the player to 25 cm, normalize giant props to human proportions, or reduce bottle/cap mesh geometry to make equipment fit. Prototype copies must match the approved reference rather than preserve accidental reduced transforms.

Measured reference values are verification aids, not instructions to resize an actor:

| Reference | Current measured size |
| :--- | :--- |
| Default player collision capsule | Approximately 192 cm tall, radius 34 cm, component scale 1 |
| Existing giant NPC collision capsule | Approximately 1,056 cm tall, radius 204 cm; existing character proportions preserved |
| `SM_PlasticBottle` supplied plastic shell | Approximately 186 cm tall at native mesh scale 1; replaces the historical `Cola1` mesh without changing placed actor scales |
| Existing reference bottles | Actor scale 1 in `Lvl_FirstPerson` |

**Giant-scale environment:** bottles and matching caps, counters, storage/display racks, shop furniture, structures and most other environmental props. The earlier prototype's cash, coins, scanners and checkout equipment also use giant scale. Use the reference bottles and giant characters to determine proportions, spacing and capacity.

**Player-scale exceptions:** player vehicles/cars, bottle-filling controls and equipment, cleaning equipment, specialized tools and machinery intended for direct player operation. Their controls, seats, grips and working positions fit the unchanged player while their hoses, mounts and delivery interfaces can reach giant bottles.

Provide appropriate ramps, stairs, walkways, loading positions and reachable controls. Adjust placement, clearance, collision, scan volumes, interaction points and sight lines to the approved object sizes. Preserve the default player camera and mechanics; solve accessibility in the equipment and environment instead of shrinking bottles or the character.

| Customer Archetype | Height / Scale | Personality & Demands | Failure Consequence |
| :--- | :--- | :--- | :--- |
| **Mountain Giant** | Authored giant scale using the approved level reference | Craves massive quantities of ice-cold Heavy Metal Colas. Impatient and heavy-handed. | Slams fists onto counter, launching players, bottles, and tools into the air. |
| **Bog Witch** | Authored patron scale; no fixed player-height multiplier | Demands exotic, cursed beverages (Spider Cola, Toxic Swamp Fizz). Cackles eerily. | Casts hexes on workstations, reversing conveyor belts or turning ice into hot magma. |
| **Stone Golem** | Authored giant scale using the approved level reference | Drinks high-viscosity, heavy-pressure colas. Cannot tolerate warm drinks. | Emits ground tremors that agitate all un-capped or un-settled bottles in the shop. |
| **Forest Goblin / Pixie** | Smaller authored archetype; final variant dimensions require separate authoring | Fast, hyperactive, demands rapid Sugar-Crush mini-bottles. | Jumps onto the counter, knocking bottles over or stealing loose caps. |

### 3.1 Countertop Navigation & Spatial Challenges
Because the environment is built for giants around the unchanged player:
- Bottling stations are elevated; players use kick-ladders, pneumatic lifts, and counter ramps.
- Any cooperative carrying or handling restrictions are explicit item mechanics based on grip, size or stability. They must not depend on a prop outweighing the player.
- Player tools fit the existing character. Hoses can reach giant fittings; tension limits and reactions are tuned independently of visible size.

### 3.2 Vast Workshop Navigation & Micro-Vehicle Logistics
In the full version, vehicles improve traversal, bulk transport and throughput across the giant workshop, including its counters, floors, cellars and serving tables. Section 7 covers vehicle upgrades, tanker hauling and deliveries.
- **The Vehicle Fleet:** The team uses motorized micro-machinery:
  - **Flatbed Mules / Karts:** Drivable transport karts with magnetized beds to securely carry crates of empty bottles, ice blocks, and finished products across the shop.
  - **Micro-Forklifts:** Player-sized hydraulic lifters used to transport, hoist and dock bulky Cola cylinders. Gameplay mass stays below the player baseline; final values are configurable tuning.
  - **The Mobile Capper Buggy:** A motorized cart equipped with an overhead pneumatic capping ram that allows a driver to speed down a line of filled bottles, torquing caps in rapid succession like an F1 pit-crew on wheels.
- **Logistical Hazards:** Speeding with a bed full of agitated bottles can cause bottles to tip over and detonate! Sharp turns, sudden braking, and collisions with teammates or loose caps introduce chaotic driving physics.

### 3.3 Gameplay Mass Is Independent of Visual Scale

- Preserve the player's current default movement/physics configuration as the reference mass. Do not resize or rebalance the player to accommodate giant scenery.
- NPCs and other movable/simulated actors must be lighter than the main player. For each such actor, the configured gameplay mass must satisfy `0 < ActorMass < PlayerReferenceMass`.
- Large meshes must not acquire excessive gameplay mass automatically from their scaled collision volume or realistic material density. Use explicit, configurable actor/product mass tuning where necessary.
- Static floors, walls, racks and fixed structures remain stationary collision geometry. A lower gameplay-mass rule does not make fixed scenery movable.
- Visual size, collision dimensions, gameplay mass, handling restrictions and thermal mass are separate concerns. This rule does not change capacity, liquid quantity, pressure, strength, temperature behavior or approved prices.
- The server owns physics and authoritative mass configuration. The same product/NPC configuration must survive spawning, pool reuse, ASM/ISM transitions, multiplayer synchronization and save/load without retaining a previous pooled actor's mass.
- Exact per-type mass values require tuning below the unchanged player baseline; obsolete 50–200 kg and 10x-player-mass targets are withdrawn. This is an approved design requirement, not a claim that all current assets already satisfy it.

---

## 4. Multiplayer Co-op Systems

### 4.1 Shared Architecture
- **Shared Bank:** The server owns the bank and transaction history. Each completed NPC pickup is paired with one authoritative bank credit, including when a notification is delivered twice. Placing a bottle on a shelf makes it available for sale. The earlier prototype's physical coin identities and deposit/withdrawal rules are documented in the [ATM accounting design](ATM_Cash_Accounting_Proposal.md).
- **Shared Storage & Vehicle Pool:** Supplies such as empty bottles, gas tanks and caps are accessible to all teammates. The full version also shares vehicles.
- **Shared Customer Anger Meter:** If orders back up, the tavern reputation plummets, causing giants to smash tables or walk out.

### 4.2 Natural Emergent Player Roles
While any player can perform any task, high efficiency requires spontaneous specialization:
1. **The Pumpmaster:** Operates the cola cylinders and fuel nozzles, mastering precision fill cutoffs.
2. **The Pit-Stop Capper / Mobile Mechanist:** Operates the stationary capping bench. The full version adds mobile capping on the assembly line.
3. **The Logistics Driver / Forklift Pilot:** In the full version, hauls bulk cylinders from delivery chutes, docks tanks and transports bottle crates across the giant shop floor.
4. **The Cryo-Runner:** Manages ice supplies, dips bottles into coolers, and monitors temperature dials.
5. **The Stocker & Prep Runner:** Replaces empty bottles at stations, carries finished bottles to shelves and replenishes supplies for the next rush.

---

## 5. Risk, Agitation & Disaster System

**Current authority:** The prototype uses [plastic/Normal Cola/manual twist caps only](Prototype_Plastic_Bottle_Pressure_2026-11-11.md). Interaction-generated internal pressure exceeding the plastic bottle's maximum tolerance triggers cola-powered, orientation-dependent rocket propulsion. Keep normalized base/interaction pressure and Strength separate. Overfill alone and cap timing are not automatic failure triggers. The numeric agitation schedule and named flavor disasters below are **historical proposals**, not approved balance or Normal-demo requirements. Toxic gas, spiders, hallucinations and other type-specific effects remain deferred; a Spicy label does not approve fire mechanics.

Soda bottles in *Midnight Fizz* possess a dynamic **Agitation Meter ($A$)**:
$$A \in [0.0, 100.0]$$

### 5.1 Agitation Triggers
- **Running while carrying:** $+5\,A/\text{sec}$
- **Jumping or falling from height:** $+25\,A$
- **Throwing or catching a bottle:** $+35\,A$
- **Bumping into furniture or dropped bottles:** $+15\,A$
- **Overfilling past 100%:** $+50\,A$ instantly

### 5.2 Catastrophic Failure Modes (When Agitation exceeds Threshold)
Depending on the Cola Formula contained inside:
1. **Ballistic Rocket Missile:** The cap blows off, propelling the bottle at extreme velocities around the tavern with dynamic bouncing, leaving fizz trails and knocking over players and stations.
2. **Violent Detonation:** The bottle shatters into flying shrapnel, creating sticky puddles that slow player movement.
3. **Structural Fragility:** The bottle remains intact but micro-fractured; any slight subsequent tap will instantly shatter it.
4. **Toxic Gas Cloud:** Green effervescent smoke erupts, causing players in the radius to cough, drop carried items, and lose screen visibility.
5. **Spider Swarm Infestation:** The bottle bursts into dozens of creepy crawlers that run across the counter, tangling hoses and startling players.
6. **Hangover / Dizzy Haze:** Releases psychedelic fumes that invert player movement controls and induce drunken ragdoll swaying.

---

## 6. Bottle Reusability & Stage Progression

Reuse requires **Strength > 70%, clean and completely empty**, plus material-specific opening/closure constraints. Preserve the bottle's physical history on refill. Caps cannot be recycled. Plastic is not categorically single-use: existing core design allows up to 40% recycling recovery. Glass cracks and shatters; plastic and metal dent/deform rather than automatically shattering. Metal is not an indestructible bolt-lock keg; the current can uses a welded lid, with 20% recycling recovery and unresolved reopening/intact-can reuse details.

| Material | Closure distinction |
| --- | --- |
| Plastic | Manual twist, auto-lock at full Content Level 1.0, or auto-lock plus limited pressure release; exact thresholds/rates remain tunable. |
| Glass | Hammer-installed cylindrical wooden stopper, or pressure-release cap **without full-bottle auto-lock**. No change to existing bottle size is authorized. |
| Metal can | Place compatible lid and weld; welding tools, timing, quality and reopening are not finalized. |

Only plastic and the Normal manual twist cap are included in the prototype. Other rows and automatic/pressure-release plastic variants remain full-game reference; glass fragments and welding are not prototype requirements. Future washing/return stations and additional vessel progression must be specified separately. The current prototype's approved departure recycling/retirement policy remains in place. See [core material/cap rules](Core_Gameplay_Design_Decisions.md#34-cap-types) and the [conflict register](Chat_Design_Decisions_2026-11-11.md#5-conflicts-and-disposition).

---

## 7. Daily Schedule and Shop Progression

### 7.1 Demo Day

The demo uses the five Normal Colas. Give players three game hours to prepare and ten game hours to trade. Customer demand peaks around noon.

| Game time | Shop state | Player activity |
| --- | --- | --- |
| 06:00–09:00 | Closed for preparation | Clean, obtain supplies, prepare bottles and stock shelves. |
| 09:00–19:00 | Open | Fill and prepare bottles, replace filled bottles at stations with empties, carry finished stock to shelves and replenish supplies as NPCs buy. |
| Around 12:00 | Peak demand | Keep shelves stocked through the busiest part of trading. |
| 19:00 | Closing | Pause new purchases. Customers who already paid can finish drinking and dispose of their bottles. |

In the opening level, the demo day runs for three hours of play including a 10-second closing transition. The clock holds at 19:00 during the transition, then advances to 06:00 the next day in the same shop. The noon rush runs from 12:00 to 13:00.

### 7.2 Full-Version Day and Night Cleanup

The shop opens at 09:00 and trades for nineteen game hours, passing through midnight before closing at 04:00 the next day. Customers predominantly seek the drinks listed for each time band. Keep arrival intensity and product preferences configurable. Preview upcoming demand so players can prepare shelf stock ahead of a change.

| Game time | Duration | Main demand or activity |
| --- | --- | --- |
| 09:00–12:00 | 3 hours | Healthy drinks |
| 12:00–16:00 | 4 hours | Cold drinks |
| 16:00–19:00 | 3 hours | Shakes and smoothies |
| 19:00–00:00 | 5 hours | Liquor and beer |
| 00:00–04:00 | 4 hours | Witch potions, nuclear, ghost and toxic drinks |
| 04:00–09:00 | 5 hours | Closed for night cleanup and morning preparation |

Before the 09:00 opening, players must clear evidence of the night business. Decontaminate radiation, capture escaped ghosts, kill or remove spiders, and clean toxic spills and other hazardous residue. A proposed readiness check uses editable thresholds for each hazard and a compact indicator that shows the amount or count left. The consequence of discovery at 09:00 is open.

### 7.3 Upgrade Purchases

Players use money from the shared bank to buy upgrades. Rank 0 is the starting state. Each purchase requires the previous rank in its branch and the displayed cost. Branches progress independently. Equipment ranks belong to the upgraded instance, handling ranks belong to the player profile, and the shelf limit belongs to the shop.

The values below are proposed playtest settings. Every purchasable rank needs a money price based on actual sales margin and time saved. Percentage increments are additive against the original base value. Rates use the existing liquid portion unit.

### 7.4 Proposed Equipment and Shelf Balance

| Equipment | Branch | UI | Starting value | Per purchase | Proposed maximum |
| --- | --- | --- | --- | --- | --- |
| Flow controller | Supply Manifold | IN | 1 input | +1 input | 4 inputs |
| Flow controller | Distribution Manifold | OUT | 1 output | +1 output | 12 outputs |
| Flow controller | Flow Chamber | FLOW | 1 portion/s each for total intake and total output | +1 portion/s to each total | 8 portions/s each |
| Flow controller | Intake Assist | SUCTION | 0 strength | +0.25 strength | 1.50 strength |
| Flow controller | Lever Mechanism | LEVER | Hold to run | Next control behavior | Independent Valve Control |
| Single-bottle filling table | Filling Nozzle | FILL | 1 portion/s | +1 portion/s | 4 portions/s |
| Hose | Inlet Coupling | IN | 1 portion/s | +1 portion/s | 8 portions/s |
| Hose | Outlet Coupling | OUT | 1 portion/s | +1 portion/s | 8 portions/s |
| Hose | Pipe Reinforcement | PRESSURE | Base pressure tolerance | +20% of base | 200% of base |
| Tanker | Pressure Vessel | STORAGE | Base pressure rating and storage capacity | +25% of each base | 200% of each base |
| Shop | Shelf Expansion | SHELVES | 1 placeable shelf | +1 placeable shelf | 4 shelves in the first demo test |

- **Flow controller.** The five proposed branches contain 30 total purchases. Tune the proposed Flow Chamber inlet-tolerance bonus independently of throughput at +10% of base per rank, up to 170%. The proposed Intake Assist rule uses `OriginalLoad / (1 + Strength)` once on the connected inlet path, leaving 40% of the original modeled pressure load at strength 1.50. Availability of cola and rated throughput still govern transfer.
- **Flow limits.** Delivered flow is constrained by the source output, the controller's allocated share, both hose endpoint limits and filling-table acceptance. Keep total allocation within the controller's shared flow budget. Hose inlet and outlet coupling upgrades each change throughput. Show the smaller endpoint limit in the upgrade UI and track pressure tolerance separately.
- **Tanker.** Higher pressure capability permits more stored liquid through the game's compressed-storage rule. Store pressure rating and maximum portions as paired values per rank. A purchase raises allowed capacity while retaining the actual liquid amount. Derive actual pressure from the existing fill and pressure rules. The same hose throughput and demo bulk-source protections described in Section 2.2 apply at higher storage ranks.
- **Shelves.** Progression raises the maximum number of physical shelves the shop can place. Bottle slots are defined per shelf. Keep the shop limit editable for available floor space and NPC paths. The map-supported limit and whether a rank includes a physical shelf remain open.

#### Flow Controller Lever Ranks

| Rank | Name | Behavior |
| --- | --- | --- |
| 0 | Manual Lever | Hold to run. Release to close. |
| 1 | Locking Lever | Press E to run. Press E again to close. |
| 2 | Bottle Cutoff | When any active bottle reaches its fill target, close the whole controller. A player restarts it manually. |
| 3 | Independent Valve Control | Each outlet closes when its bottle is full and reopens when a valid replacement is correctly seated and ready. Other outlets can continue filling. |

Offer the first toggle early in the playtest so a player can restock shelves while filling continues. Lever upgrades govern filling. Players still replace empties, replenish supplies and carry finished bottles to shelves.

#### Manual Cola Filling Machine Upgrades

`BP_ManualColaFillingMachine` has four upgrade branches alongside nozzle throughput. The cap and seal capacities apply to each machine's trays. Player carrying limits are defined in Section 7.5.

| Upgrade | Starting value | Effect | Maximum |
| --- | --- | --- | --- |
| Cap Tray Capacity | 1 cap | Increase the number of caps the machine can hold | 100 caps |
| Seal Tray Capacity | 1 seal | Increase the number of seals the machine can store | 100 seals |
| Built-in Pressure Regulator | Rank 0 with base filling pressure | Each purchase adds 0.05 gameplay pressure units of reduction in the bottle during filling | 100 purchasable ranks, for 5.00 units of configured reduction |
| Built-in Temperature Regulator | Base filling behavior | Reduce the cola temperature by 0.02 per upgrade while filling | Open |

Each pressure regulator rank costs more money than the previous rank. The starting price and price increases are open balance decisions. Section 2.5 covers the standalone full-game pressure regulator. The tray capacity increases per rank and the temperature regulator's units and maximum are also open.

### 7.5 Player Handling

Each player can buy 15 ranks across four independent paths. Rank 0 is the free starting state. Each price below is the starting playtest cost for the rank shown.

| Rank | Empty Bottle Handling · EMPTY | Cap Handling · CAPS | Seal Handling · SEALS | Hand Stability · STABILITY |
| --- | --- | --- | --- | --- |
| 0 | 1 empty bottle | 1 cap | 1 seal | 0% less movement shake · ×1.00 |
| 1 | 2 empty bottles · $20 | 3 caps · $15 | 3 seals · $15 | 10% less · ×0.90 · $15 |
| 2 | 3 empty bottles · $45 | 6 caps · $30 | 6 seals · $30 | 20% less · ×0.80 · $30 |
| 3 | — | 9 caps · $50 | 9 seals · $50 | 30% less · ×0.70 · $50 |
| 4 | — | 12 caps · $75 | 12 seals · $75 | 40% less · ×0.60 · $75 |
| 5 | — | — | — | 50% less · ×0.50 · $105 |

**Carrying.** The player carries one hand group at a time. Empty bottles, caps and seals form groups of their own type. A partially filled or full bottle takes the hand by itself. Use the bottle's existing empty-state tolerance to identify loaded bottles. Hold **G** while aiming at another bottle, another cap while holding caps, or another seal while holding seals to attempt a pickup. Capacity checks reject extra items and keep them in the world. Holding **G** toward a placement point places the current item, and double-tapping **G** drops it. At a filling station, **E** seats one bottle at a time. A successful placement, drop or station handoff makes the next item available in hand. Check the hand group before liquid transfer so a held batch cannot become multiple loaded bottles. Placing or using a cap or seal reduces the held count. Capacity purchases take effect immediately and retain items already held. Seal duration and strength come from the seal itself.

**Stability.** Multiply movement-induced carrying shake by the rank's multiplier once. The resulting shake feeds the bottle's movement agitation and the associated pressure gain. Maximum rank halves that shake under identical movement. Collision impulses, drops, existing internal pressure and other pressure sources follow their own rules. The menu shows the percentage of shake removed.

**Purchases and saves.** The shared shop bank pays for ranks owned by the purchasing player. The server validates the next rank, prerequisite, configured price and available funds, then charges and grants the rank once. Replicated PlayerState ranks drive the player's effective values and menu. Save the four ranks under the player's stable profile identity in the shop save and recalculate their effects on load. Profiles without saved ranks start at rank 0. Each rank definition records the branch, required previous rank, editable price and resulting capacity or stability multiplier.

**Player menu.** Press **H** to open the player upgrade menu. Select one of the four tabs to see its current value, next value and price, or its maximum rank. The menu displays `FULL: 1` as a fixed carrying rule.

### 7.6 Proposed Full-Version Vehicle Balance

The full version adds suspension, engine and fuel upgrades to support heavier tanker hauling and cola deliveries.

| Upgrade | UI | Per purchase | Proposed maximum rank effect |
| --- | --- | --- | --- |
| Suspension | SUSPENSION | Reduce transmitted movement shock by 10 percentage points of base | 50% of original movement shock |
| Engine Power | POWER | +20% of base engine output | 200% of base output |
| Fuel Mileage | MILEAGE | +10% of base distance per fuel unit | 150% of base mileage |

Tune engine power, permitted loaded-tanker mass and fuel mileage independently. Set any change to permitted mass explicitly on the rank. Loaded weight and route conditions can affect fuel consumption. Suspension reduces shock transmitted to the tanker or cargo during movement, with crashes governed by the separate collision rules.

### 7.7 Implementation and Playtesting

The opening level defaults to the three-hour demo clock. Set `ClockMode=RealWorld` in `Config/DefaultGame.ini` to use the host's local time at its normal pace. Full-version trading continues through midnight until the 04:00 close, when the next business day begins with cleanup and preparation.

- Expose a small set of useful equipment, shelf and handling ranks in the first demo playtest. Measure the full route from empty bottle to NPC pickup before tuning higher ranks. Compare preparation time, shelf space, walking and restocking time, peak arrivals and output backlog. Time the full-version cleanup window independently of the demo day.
- Store editable upgrade definitions with branch, rank, cost, prerequisite and stat effects. Save purchased ranks with the relevant equipment identity, player or shop, plus the clock phase and unresolved cleanup state. Use server-authoritative purchases and sale credits.
- Store demo and full-version time bands separately with shop state, arrival intensity and product weights. Treat midnight as part of the same full-version trading day until the 04:00 closing event. Represent the bands as wraparound intervals or as elapsed time since the 09:00 business-day start.

| Proposed Blueprint data or asset | Responsibility |
| --- | --- |
| `ST_EquipmentUpgradeRow`, `DT_EquipmentUpgrades`, `ST_PurchasedUpgradeState` | Define upgrade ranks and track purchases. |
| `ST_ShopTimeBand`, `DT_DemoShopSchedule`, `DT_FullShopSchedule` | Define shop hours, demand and product preferences by time band. |
| `BP_ShopTimeManager`, `BP_ShopDemandManager`, `BP_NightCleanupManager` | Track clock and rollover, customer arrivals and preferences, and full-version cleanup progress. |

### 7.8 Future Win and Loss Conditions

Revenue quotas, one-to-five-star shift ratings, rent and loss from bankruptcy or shop destruction remain future concepts. Potential rating inputs include fill accuracy, each drink's configured optimum temperature, restocking speed and shop damage.
