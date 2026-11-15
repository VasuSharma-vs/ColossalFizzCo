# Colossal Cola Co. — Normal Cola Manual Bottle Filling Loop

## Objective

Update the current manual Normal Cola filling system so that a single player can complete the entire bottle-filling process, while two players can cooperate to increase production speed.

The system should also allow players to purchase multiple manual filling machines and create their own production strategies.

## 1. Manual Filling Machine

The manual filling machine connects to the Normal Bulk Cola Container through a single liquid supply pipe.

Each machine can fill one bottle at a time.

The machine includes:

- **Bottle Holder:** Holds the bottle in the correct position beneath the filling nozzle.
- **Filling Nozzle:** Dispenses Normal Cola into the bottle.
- **Manual Filling Lever:** The player pulls and holds the lever to dispense cola. Releasing the lever immediately stops filling.
- **Temporary Seal Button:** Applies a temporary seal to the filled bottle, preventing cola from leaking for a few seconds and giving the player time to close the cap.

The machine does not require electricity.

## 2. Bottle Filling Process

1. The player picks up an empty plastic bottle.
2. The player places and properly aligns the bottle beneath the filling nozzle.
3. Once aligned, the player pulls and holds the filling lever.
4. Normal Cola starts flowing into the bottle.
5. The player monitors the liquid level and releases the lever when the bottle reaches the desired fill level.
6. The filled bottle initially contains high residual pressure, making immediate cap closure risky.
7. The player must choose between two methods of closing the bottle.

## 3. Method A — Fast but Risky (Temporary Seal)

1. Immediately after filling, the player presses the Temporary Seal Button.
2. The machine applies a temporary seal that prevents cola from leaking for a few seconds.
3. During this limited time, the player must close the Normal Cap using the manual twisting mechanism.
4. If the player closes the cap properly before the temporary seal expires, the bottle is successfully sealed.
5. If the player fails to close the cap properly within the available time, the temporary seal expires.
6. The remaining pressure can cause a pressure-release event, potentially turning the bottle into a rocket.

This method allows faster production but requires speed, coordination, and precision.

## 4. Method B — Slow but Safe (Natural Pressure Release)

1. After filling, the player leaves the bottle unsealed.
2. The bottle gradually releases its remaining internal pressure.
3. The player waits until the pressure settles to a safe level.
4. Once the pressure has settled, the player can close the Normal Cap without significant pressure resistance or pressure pop.
5. The bottle is successfully sealed.

This method is slower but significantly reduces the risk of pressure-related accidents.

## 5. Single-Player Gameplay

A single player can operate the entire production process independently.

**Workflow:**

Place Bottle → Align Bottle → Pull Filling Lever → Fill Cola → Release Lever → Apply Temporary Seal or Wait for Pressure Release → Close Cap → Finished Bottle

The player can choose between fast, risky sealing and slow, safe sealing depending on the situation.

## 6. Two-Player Cooperative Gameplay

Two players can divide the work to increase production speed.

**Player 1 — Filling Operator**
- Places and aligns empty bottles.
- Operates the filling lever.
- Monitors the liquid level.
- Stops filling at the desired level.
- Prepares the next bottle.

**Player 2 — Sealing Operator**
- Applies the temporary seal.
- Closes the Normal Cap before the seal expires.
- Handles completed bottles.

Both players can work simultaneously, creating a faster and more efficient production line.

## 7. Multiple Manual Filling Machines

Players can purchase and install multiple manual filling machines.

There should be no forced production sequence. Players should be free to arrange machines and develop their own workflow.

**Example: Four-Machine Solo Production Strategy**

1. Fill Bottle A using Machine 1.
2. Leave Bottle A to release pressure naturally.
3. Move to Machine 2 and fill Bottle B.
4. Leave Bottle B to release pressure.
5. Repeat the process with Machines 3 and 4.
6. Return to Machine 1 after Bottle A's pressure has settled.
7. Close Bottle A's cap safely.
8. Repeat the process for the remaining bottles.

This allows players to use multiple machines to compensate for the waiting time required for pressure release.

Players should also be able to combine both methods, using temporary seals on some bottles while allowing others to release pressure naturally.

## 8. Pressure and Failure Mechanics

- Filling increases the bottle's internal pressure.
- Releasing the filling lever stops the incoming liquid flow.
- An unsealed bottle gradually releases its residual pressure.
- Applying a temporary seal prevents leakage and temporarily retains the remaining pressure.
- The temporary seal only lasts a few seconds.
- If the cap is properly closed before the temporary seal expires, the bottle is successfully sealed.
- If the temporary seal expires while significant pressure remains, the bottle can experience a pressure-release failure.
- An improperly closed cap can cause the bottle to launch like a rocket.
- Once the bottle's pressure has settled, the player can close the cap easily without a significant pressure pop.
- The existing bottle pressure, cap integrity, and rocket physics systems should be reused rather than creating a completely separate physics system.

## 9. Demo Scope

The first playable demo will include:

- Plastic bottles only.
- Normal Cola only.
- Normal Cap (Manual Twist) only.
- Manual bottle filling machine.
- Manual filling lever.
- Temporary seal mechanism.
- Natural pressure release.
- Pressure-related bottle rocket failures.
- Single-player operation.
- Two-player cooperative operation.
- Support for purchasing and operating multiple manual filling machines.

Automatic filling equipment and other advanced machinery are not required for this demo.

## 10. Design Goal

The goal is to create a simple but flexible manual manufacturing system that rewards player creativity, teamwork, timing, and production optimization.

A single player should be able to operate a complete Cola production line, while two players should be able to increase production efficiency through cooperation.

Players should be free to choose their own strategy instead of being forced into one production method.

**Core Gameplay Loop:**

Empty Bottle → Align → Fill Cola → Manage Pressure → Close Cap → Finished Product

**Main Design Principle:**

Fast production introduces greater pressure-related risks, while slower production allows safer bottle sealing. Multiple machines and cooperative gameplay allow players to find creative ways to improve efficiency.