# Linux Exhaust Demand Model

This document describes the `exhaust_demand_v0` model. The model is implemented
in `modguard-linux/src/exhaust_model.{hpp,cpp}`. This document also describes
the `[RealisticSmoke]` configuration schema. The schema is parsed by
`modguard-linux/src/config.{hpp,cpp}`. This document is the implementation
reference for the demand calculation and every configurable value.

The source of truth is the source code. Use `../reverse-enginnering/MODGuard_RE.md` only for the
analyzed Windows plugin formula. The Linux model is a clean-room reimplementation.
It does not reproduce the Windows bucket assignment (see section 6).

---

## 1. Data structures

All structs live in `modguard::` (`exhaust_model.hpp`).

### 1.1 `ExhaustConfig`

This struct holds every `[RealisticSmoke]` setting. The defaults are defined
inline (`exhaust_model.hpp:8-45`). They mirror the sanitize ranges in
`config.cpp:108`. The full key table is in section 4.

### 1.2 `ExhaustInputs`

Per-frame telemetry feeds into the model (`exhaust_model.hpp:47-58`):

| Field | Type | Default | Source |
| --- | --- | --- | --- |
| `throttle` | `float` | `0.0F` | `truck.effective.throttle` |
| `rpm` | `float` | `0.0F` | `truck.engine.rpm` |
| `rpm_limit` | `float` | `2500.0F` | `truck` config `rpm.limit` |
| `speed` | `float` | `0.0F` | `truck.speed` |
| `clutch` | `float` | `0.0F` | `truck.effective.clutch` |
| `acceleration` | `float` | `0.0F` | magnitude of `truck.local.acceleration.linear` |
| `cargo_mass` | `float` | `0.0F` | `job` config `cargo.mass` |
| `gear` | `int32_t` | `0` | `truck.displayed.gear` |
| `engine_enabled` | `bool` | `false` | `truck.engine.enabled` |
| `cargo_loaded` | `bool` | `false` | `job` config `cargo.loaded` |

### 1.3 `ExhaustOutput`

This struct is returned by `ExhaustModel::update` (`exhaust_model.hpp:60-66`):

| Field | Type | Meaning |
| --- | --- | --- |
| `demand` | `float` | Smoke demand clamped to `[0,1]`. The particle hook uses this value as its admission threshold. |
| `raw_demand` | `float` | Unclamped demand. The model retains this value for diagnostics only. |
| `throttle_smoothed` | `float` | Attack/release-smoothed throttle. |
| `rpm_normalized` | `float` | Smoothed RPM divided by the RPM limit. |
| `session_peak` | `float` | Decaying session peak of `demand`. |

### 1.4 `ExhaustModel`

This is a stateful model. It has `update(inputs, delta_seconds)` and `reset()`
(`exhaust_model.hpp:68-89`). The private state includes `throttle_smoothed_`,
`rpm_smoothed_`, `boost_` (turbo lag), `previous_throttle_`, `puff_`
(transient over-fuel), `shift_transient_`, `launch_envelope_`,
`free_rev_envelope_`, `steady_envelope_`, `session_peak_`,
`previous_gear_`, and `have_previous_gear_`.

---

## 2. Update pipeline

`ExhaustModel::update` (`exhaust_model.cpp:44`) runs the stages below. The
inputs are clamped to safe ranges. An engine-off condition returns zero and
resets the state.

### 2.1 Time and throttle smoothing

- `dt` is clamped to `[0, 0.25]` seconds. This survives frame spikes.
- `throttle` is clamped to `[0,1]`.
- A first-order low-pass smooths the throttle. The time constant `tau` is
  `attack_smoothness` when the throttle rises. It is `release_smoothness` when
  the throttle falls. Each value is floored at `0.001F`. The smoothing
  coefficient is `alpha = 1 - exp(-dt / tau)` (`exhaust_model.cpp:55-59`).

### 2.2 RPM normalization

- `rpm_limit` falls back to `2500.0F` if it is invalid or `<= 1.0F`.
- RPM is low-pass smoothed with `tau = 0.3F`. This damps telemetry jitter.
- `rpm_norm = clamp01(rpm_smoothed / rpm_limit)`.

### 2.3 Boost lag (turbo dynamics)

- `boost_demand = throttle_smoothed * (0.4 + 0.6 * rpm_norm)` models turbo
  pressure that rises with throttle and RPM.
- `boost_` is a first-order lag with `tau_boost = 1.2F`. This makes the boost
  lag the throttle.

### 2.5 Effective load

- `air = rpm_norm * (0.3 + 0.7 * boost_)` approximates the available air.
- `load = clamp(throttle_smoothed / max(air, 0.15), 0, 1.5)` is the demanded
  fuel relative to the air. A value above 1.0 means over-fueling.

### 2.6 Steady-state load smoke

- Below 50% load, the smoke is near zero: `smoke_load = 0.02 * load`.
- Above 50% load, the smoke rises sharply:
  `smoke_load = 0.01 + (load - 0.5)^1.8 * 0.9`.

### 2.7 Transient puff (turbo-lag over-fueling)

- `dthrottle = max(0, throttle - previous_throttle) / dt` is the throttle rate.
- `puff_target = clamp(dthrottle * 0.4 + max(0, load - 1.0) * 1.5, 0, 1)`.
- `puff_ = max(puff_target, decay(puff_, dt, 1.5))` holds the puff at its peak.
  It lets the puff decay with a 1.5 s time constant.

### 2.8 Envelopes and gear-shift puff

- When `gear` changes, `shift_transient_` is set to `1.0F`. Otherwise it decays
  with `tau = 0.3F` (`exhaust_model.cpp:104-110`).
- The launch envelope rises to `1.0F` when speed is below `1.0F` and throttle
  is above `0.5F`. It decays with `tau = 1.5F`. A floor cut removes the tail.
  This follows the launch event in the Windows formula.
- The free-rev envelope rises while the engine runs, the throttle is below
  `0.05F`, and the gear is neutral. It decays with `tau = 0.5F`.
- The steady envelope follows the smoothed throttle with a rise time constant
  of `8 s`. It releases at once. It drives the sustained-throttle event.

### 2.9 Combination

```
smoke  = smoke_load
smoke += puff_ * 0.7 * throttle_transient_strength
smoke += shift_transient_ * 0.5 * shift_weight
smoke += launch_envelope * launch_weight
```

Then the weighted additive terms from the Windows formula enter (section
6.3 of `../reverse-enginnering/MODGuard_RE.md`):

```
exponent = 1 + ThrottleCurve / 100  (-1 selects 1.0)
smoke += throttle_smoothed ^ exponent * SustainedThrottleStrength / 100
smoke += rpm_norm * rpm_weight
smoke += clamp(speed / 30, 0, 1) * speed_weight
smoke += accel_norm * (acceleration_weight + traction_acceleration_weight)
smoke += clutch_norm * clutch_weight
smoke += (cargo_mass / 30000 if cargo_loaded else 0) * cargo_weight
smoke += clamp(load, 0, 1) * load_effort_weight
smoke += free_rev_envelope * free_rev_weight
```

`accel_norm` is the clamped acceleration magnitude divided by `9.81`.
`free_rev_envelope` rises while the engine runs, the throttle is below `0.05`,
and the gear is neutral. The sustained-throttle event adds
`steady_envelope * event_strength` when `steady_envelope` passes
`sustained_throttle_strength`. The steady envelope rises with a time constant
of `8 s` and releases at once.

> **Hypotheses.** The binary derivation of `load_effort` was not recovered.
> The Linux model maps it to the computed fuel-to-air demand. The role of
> `EventStrength` was not recovered. The Linux model follows the
> sustained-throttle event in `docs/01-reverse-engineering.md`. Both terms are
> labeled as hypotheses.

(`exhaust_model.cpp:113-171`)

### 2.10 Gross-weight load gate

The SCS SDK exposes no direct vehicle-weight channel. The truck weight and the
empty-trailer weight are configured statically. The cargo is read from
telemetry (`exhaust_model.cpp:122-134`):

- `payload = (trailer_mass_kg + cargo_mass) if cargo_loaded else 0`.
- `gross = truck_mass_kg + payload`.
- `gross_norm = clamp01(gross / gross_weight_reference_kg)`.
- `load_gate = lerp(load_baseline, 1.0, gross_norm ^ load_curve_power)`.

A light, unloaded truck stays near `load_baseline`. A heavy, loaded combination
approaches the full gate.

### 2.11 Throttle gate

Diesel smoke comes from over-fueling under heavy throttle with little boost
available (`exhaust_model.cpp:187-189`):

- `throttle_gate = throttle_smoothed ^ throttle_gate_power`.

The weighted terms enter before this gate. At zero throttle the gate
multiplies the smoke to zero. A free-revving idle therefore stays clean.

### 2.12 Final scaling

```
smoke *= load_gate * throttle_gate
smoke *= intensity * final_smoke_multiplier
smoke *= load_boost * load_sensitivity + (1 - load_sensitivity)
```
(`exhaust_model.cpp:198-206`)

`load_boost` lerps from `load_boost_min` to `load_boost_max` across the load
gate. A loaded, working truck can reach far more smoke than a light truck.

### 2.13 Continuous smoke and hook disable

- If `continuous_smoke` is true, the smoke is floored at
  `0.05 * intensity * final_smoke_multiplier` while the engine runs
  (`exhaust_model.cpp:155-158`).
- If `smoke_hook` is false, the smoke is forced to zero. The hook then admits
  nothing even if it is installed (`exhaust_model.cpp:159-161`).

### 2.14 Session peak

`session_peak_ = max(session_peak_ * session_peak_percent, smoke)` decays the
peak each frame by the configured fraction. It latches any new high
(`exhaust_model.cpp:163`).

### 2.15 Return

`return {clamp01(smoke), smoke, throttle_smoothed_, rpm_norm, session_peak_}`.
`demand` is the clamped value. `raw_demand` is the unclamped value for
diagnostics.

---

## 3. Reset behavior

`reset()` (`exhaust_model.cpp:37`) zeroes all smoothing, boost, puff,
shift-transient, envelope, session-peak, and previous-gear state. It is
called on:

- Engine off (every `update` with `engine_enabled == false`).
- Pause (`on_pause` in `plugin.cpp`).
- Frame-start timer restart or backward time jump.

This prevents stale state from producing smoke after a save load, a pause, or
an engine restart.

---

## 4. Configuration schema

All keys live under the case-sensitive `[RealisticSmoke]` section. Missing or
invalid values use the safe defaults shown. The ranges are enforced by
`sanitize()` in `config.cpp:167-198`. Booleans accept `1/true/yes/on` and
`0/false/no/off` (case-insensitive).

You can change these keys while the game runs. The plugin applies a saved
change on the next simulation frame. See section 6.2 of
`linux-plugin-architecture.md` for the reload contract.

### 4.1 Hook and behavior

| Key | Type | Default | Range / accepted | Meaning |
| --- | --- | --- | --- | --- |
| `SmokeHook` | bool | `true` | bool | Enables in-process particle control. `0` keeps telemetry and diagnostics active but admits no particles. |
| `ContinuousSmoke` | bool | `false` | bool | Keeps a faint baseline while the engine runs. |

### 4.2 Global scaling

| Key | Type | Default | Range | Meaning |
| --- | --- | --- | --- | --- |
| `Intensity` | float | `0.3` | any finite | Master smoke scale. |
| `FinalSmokeMultiplier` | float | `0.8` | any finite | Final multiplier applied after all gates. |
| `LoadSensitivity` | float | `1.0` | any finite | Strength of the heavy-load top-end boost. |

### 4.3 Weight and load gate

| Key | Type | Default | Range | Meaning |
| --- | --- | --- | --- | --- |
| `TruckMassKg` | float | `8000` | `[0, 50000]` | Curb weight of the truck. |
| `TrailerMassKg` | float | `6000` | `[0, 50000]` | Weight of the trailer when attached. |
| `LoadCurvePower` | float | `1.8` | `[0.1, 10]` | Steepness of the gross-weight smoke curve. |
| `GrossWeightReferenceKg` | float | `36000` | `[1, 1000000]` | Gross weight at which the load gate saturates. |
| `LoadBaseline` | float | `0.15` | `[0, 1]` | Smoke fraction for an unloaded, empty truck. |
| `LoadBoostMin` | float | `0.6` | `[0, 100]` | Low end of the heavy-load emission boost. |
| `LoadBoostMax` | float | `2.6` | `[0, 100]` | High end of the heavy-load emission boost. |

### 4.4 Throttle shaping

| Key | Type | Default | Range | Meaning |
| --- | --- | --- | --- | --- |
| `ThrottleGatePower` | float | `1.5` | `[0.1, 5]` | How strongly demand follows the throttle. |

### 4.5 Smoothing and transients

| Key | Type | Default | Range | Meaning |
| --- | --- | --- | --- | --- |
| `AttackSmoothness` | float | `0.3` | `>= 0` | Throttle rise time constant (seconds). |
| `ReleaseSmoothness` | float | `0.3` | `>= 0` | Throttle fall time constant (seconds). |
| `ThrottleTransientStrength` | float | `0.7` | any finite | Strength of the throttle-rate puff. |
| `ThrottleRiseThreshold` | float | `0.05` | `[0, 1]` | (Reserved. Parsed and clamped. Not currently used in the pipeline.) |
| `ThrottleTransientDecay` | float | `0.1` | `>= 0` | (Reserved. Parsed and clamped. Not currently used in the pipeline.) |
| `ShiftWeight` | float | `1.0` | any finite | Weight of the gear-shift puff. |
| `LaunchWeight` | float | `1.0` | `>= 0` | Weight of the launch envelope. It fires when speed is below `1` m/s and throttle is above `0.5`. |

### 4.6 Event and peak

| Key | Type | Default | Range | Meaning |
| --- | --- | --- | --- | --- |
| `EventStrength` | float | `1.0` | `>= 0` | Weight of the sustained-throttle event. (Hypothesis. See below.) |
| `SustainedThrottleStrength` | float | `0.5` | `>= 0` | Threshold that the steady envelope must pass. It also scales the curved-throttle term. |
| `SessionPeakPercent` | float | `0.85` | `[0, 1]` | Per-frame decay fraction of the session peak. |

### 4.7 Throttle curve and component weights

| Key | Type | Default | Range | Meaning |
| --- | --- | --- | --- | --- |
| `ThrottleCurve` | int | `45` | `[-1, 100]` | Exponent `1 + value/100` of the curved-throttle term. `-1` selects `1.0`. |
| `ThrottleWeight` | float | `1.0` | any finite | (Reserved. Parsed. Not currently used in the pipeline.) |
| `RpmWeight` | float | `1.0` | `>= 0` | Weight of `rpm_norm`. |
| `SpeedWeight` | float | `1.0` | `>= 0` | Weight of `speed / 30 m/s`, clamped to `[0,1]`. |
| `AccelerationWeight` | float | `1.0` | `>= 0` | Weight of the acceleration fraction. |
| `ClutchWeight` | float | `1.0` | `>= 0` | Weight of the clutch fraction. |
| `CargoWeight` | float | `1.0` | `>= 0` | Weight of `cargo.mass / 30000` while cargo is loaded. |
| `TractionAccelerationWeight` | float | `1.0` | `>= 0` | Extra weight on the acceleration fraction. |
| `LoadEffortWeight` | float | `1.0` | `>= 0` | Weight of the computed fuel-to-air demand. (Hypothesis. See below.) |
| `FreeRevWeight` | float | `1.0` | `>= 0` | Weight of the free-rev envelope. |

> **Removed keys.** `RpmLeanFraction`, `RpmLeanPower`, and `GearWeight` are
> no longer part of the schema. The loader ignores them. Old files that set
> them still load.

> **Hypothesis keys.** The binary derivation of `load_effort` was not
> recovered. The Linux model multiplies `LoadEffortWeight` with its computed
> fuel-to-air demand. The role of `EventStrength` was not recovered. The Linux
> model uses it as the weight of a sustained-throttle event. Both labels come
> from `docs/01-reverse-engineering.md`. Do not present them as confirmed
> binary behavior.

---

## 5. File locations

The plugin reads `modguard_linux.ini` from the applicable game mod directory:

```
~/.local/share/American Truck Simulator/mod/modguard_linux.ini
~/.local/share/Euro Truck Simulator 2/mod/modguard_linux.ini
```

Set `MODGUARD_CONFIG` to an explicit file path to override this location. A
fully commented template with every key and its default is at
`modguard-linux/modguard_linux.ini.example`.

---

## 6. Relationship to the Windows formula

The Windows plugin (see `../reverse-enginnering/MODGuard_RE.md` section 6) computes a weighted sum
of per-input terms. The Linux model now applies the recovered weighted terms
from section 6.3. The curved-throttle term, the component weights, and the
free-rev envelope follow the recovered formula. The launch envelope follows
the recovered launch event. The model adds these terms before its own gates.

Two terms are labeled as hypotheses: `load_effort` and `EventStrength`. Their
roles in the binary were not recovered. See section 4.7.

The Windows plugin then assigns one of three particle-size buckets
(`100`, `2500`, `10000`). The Linux model does **not** reproduce the bucket
assignment. Those values are the original plugin's RPM-limit minimum, default,
and maximum. They are not a smoke-output scale.

The Linux model instead produces a continuous `demand` in `[0,1]`. The particle
hook uses this value as a 24-bit admission threshold (see
`linux-particle-hook.md`). `raw_demand` is retained for diagnostics. The
unclamped shape can be observed without affecting particle control.

---

## 7. Related documents

- `linux-plugin-architecture.md` — how the model is driven by telemetry and
  how its output is published.
- `linux-particle-hook.md` — how `demand` becomes an admission threshold.
- `../reverse-enginnering/MODGuard_RE.md` — the analyzed Windows plugin formula (research
  reference only).