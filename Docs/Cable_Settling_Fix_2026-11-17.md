# Cable floor friction and self-contact settling

The native `UCollisionCableComponent` now maintains resting friction and stable self-contact. This applies to `BP_Cable_CollisionSafe`, the supply hose and the typed hose actors that use this component.

## Causes and changes

Floor friction previously used a contact normal that was populated only when a point penetrated a surface. Once cached collision constraints held it just outside that surface, the normal could disappear and friction stopped applying. Friction also ran once per component tick as a velocity multiplier, without static resistance to positional solver drift.

Friction now uses persistent nearby point/span contacts each simulation substep, including a floor reached during integration before length or self-contact constraints lift a point slightly. `CollisionFriction` is a Coulomb coefficient: small tangential movement is held within the available normal load, and stronger movement slides against bounded resistance. A value of zero permits free sliding. The existing 0–1 range and default 0.28 remain. Floor friction also resists the tangential part of repair from another contact, preventing stacked spans from creeping sideways. Repair cancellation is tracked separately from physical deceleration so it cannot create reverse velocity. Lifted points do not stick to an old floor plane.

Self-collision previously called `WakePoint` on every overlapping solver pass, even for already-awake points. This restarted their quiet timers. A small extra separation bias repeatedly nudged touching spans, and turning off smooth world recovery allowed self-contact repair to become velocity.

Self-contact now has a small contact tolerance, stable normals near coincident spans, and a finite repair allowance for overlap that already existed at the start of the substep. Only that initial overlap repair is excluded from velocity; normal reactions to new gravity or movement remain physical and stop the incoming motion. Resting contact preserves quiet timers. Relative closing velocity is removed with an inelastic response. Static/sliding friction constrains total relative tangential displacement within the actual normal correction's load budget, which is spent only once per substep. Cancelling existing repair changes the repair bookkeeping without introducing reverse momentum. Position and velocity corrections account for support surfaces: a lower span blocked by the floor cannot consume a downward correction that only the upper span can perform. Floor friction runs before and after self-contact friction with one shared Coulomb limit per point and surface normal. This lets self-contact see the actual supported movement and lets the floor resist the resulting reaction without multiplying friction. A final self-separation check catches overlaps created by other constraints before permitting sleep. Meaningful impacts still wake sleeping points.

Runtime transform notifications now use the existing displacement/rotation thresholds instead of waking on every notification. Sleeping points record their neighboring span lengths. Distance constraints wake them for new strain relative to that settled shape, excess absolute error, or real neighbor velocity; an unchanged accepted residual no longer restarts sleep, while small deliberate pulls can still propagate.

## Controls

- **Collision Friction:** surface/self-contact friction; increasing it increases resistance, not the time before sleep.
- **Enable Sleeping / Sleep Delay:** stop simulation work after the cable has physically settled. Deliberate grabs, impulses and changed supporting geometry still wake it.
- **Sleep Velocity Threshold:** speed accepted as quiet. The native default is 1 cm/s; raising it is not a substitute for contact stability.
- **Self Collision Thickness:** centerline separation; zero uses Cable Width.
- **Self Collision Iterations:** contact correction passes. The fix supports both the default one-pass setting and the selected cable's four passes.

The inspected editor instance had friction 1, damping 1, width 23 cm, length 1200 cm, 12 segments, four self-collision iterations and a fixed start/free end. Its sleeping velocity threshold was 500 cm/s. These designer settings were inspected without changing them; the repair is in the native solver.

## Verification

New regressions cover horizontal floor drift, static friction on a five-degree slope, deliberate sliding impulses, a wide coiled cable, crossed-span separation and settling, and wake/movement after impulse and grab. Drift checks run with sleeping disabled. Both rotating line/sweep modes and full collision queries are covered.

Editor build **15610** succeeded. All **58/58** selected cable, hose, manual-filling and liquid-transfer regressions passed (56 clean, two with existing hose/rocket log warnings; zero failures or skipped tests).

- Flat and wide coiled floor fixtures: **0.00000 cm** measured horizontal drift over the final two seconds in all three query modes, with sleeping disabled.
- Crossed spans on the floor: **0.00000 cm** late drift in rotating line/sweep modes and **0.00132 cm** in full-query mode. Maximum full-query frame movement was **0.00253 cm**. Ten-centimeter self separation remained within the configured tolerance, and all cases subsequently slept normally.
- Five-degree slope: late drift at most **0.01152 cm**; a deliberate impulse still moved the cable **36–38 cm**. Impulse wake, grab/lift, neighbor wake and connected-pulling regressions passed.

Build and test evidence is written to `Saved/CableSettling_20261117/Build.log` and `Saved/CableSettling_20261117/Report/index.json`. Temporary solver diagnostics were removed before the final build and test run. These are headless native checks; a rendered live playtest has not been performed. Restart UE5 to load the new native module before testing the editor instance.
