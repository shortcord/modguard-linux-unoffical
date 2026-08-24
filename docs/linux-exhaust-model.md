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
(`exhaust_model.hpp:68-85`). The private state includes `throttle_smoothed_`,
`rpm_smoothed_`, `boost_` (turbo lag), `previous_throttle_`, `puff_`
(transient over-fuel), `shift_transient_`, `session_peak_`,
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
- The idle fraction is fixed at `0.10F`.

### 2.3 Boost lag (turbo dynamics)

- `boost_demand = throttle_smoothed * (0.4 + 0.6 * rpm_norm)` models turbo
  pressure that rises with throttle and RPM.
- `boost_` is a first-order lag with `tau_boost = 1.2F`. This makes the boost
  lag the throttle.

### 2.4 RPM factor

Below the idle fraction, `rpm_factor = 1.0F`. Above idle, it lerps from `1.6F`
(lugging) down to `0.7F` (high RPM). Lugging the engine worsens the smoke.
High RPM reduces the smoke (`exhaust_model.cpp:75-80`).

### 2.5 Effective load

- `air = rpm_norm * (0.3 + 0.7 * boost_)` approximates the available air.
- `load = clamp(throttle_smoothed / max(air, 0.15), 0, 1.5)` is the demanded
  fuel relative to the air. A value above 1.0 means over-fueling.

### 2.6 Steady-state load smoke

- Below 50% load, the smoke is near zero: `smoke_load = 0.02 * load`.
- Above 50% load, the smoke rises sharply:
  `smoke_load = 0.01 + (load - 0.5)^1.8 * 0.9`.
- The result is multiplied by `rpm_factor`.

### 2.7 Transient puff (turbo-lag over-fueling)

- `dthrottle = max(0, throttle - previous_throttle) / dt` is the throttle rate.
- `puff_target = clamp(dthrottle * 0.4 + max(0, load - 1.0) * 1.5, 0, 1)`.
- `puff_ = max(puff_target, decay(puff_, dt, 1.5))` holds the puff at its peak.
  It lets the puff decay with a 1.5 s time constant.

### 2.8 Gear-shift puff

When `gear` changes, `shift_transient_` is set to `1.0F`. Otherwise it decays
with `tau = 0.3F` (`exhaust_model.cpp:104-110`).

### 2.9 Combination

```
smoke  = smoke_load
smoke += puff_ * 0.7 * throttle_transient_strength
smoke += shift_transient_ * 0.5 * shift_weight
if speed < 2 and throttle > 0.5 and gear >= 1:
    smoke += 0.15 * throttle * launch_weight
```
(`exhaust_model.cpp:115-120`)

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

### 2.11 Throttle and RPM gates

Diesel smoke comes from over-fueling at low RPM under high throttle
(`exhaust_model.cpp:136-145`):

- `throttle_gate = throttle_smoothed ^ throttle_gate_power`.
- `rpm_gate = (1 - clamp(rpm_norm / rpm_lean_fraction, 0, 1)) ^ rpm_lean_power`.

The smoke leans out sharply as RPM rises through `rpm_lean_fraction` of the
limit.

### 2.12 Final scaling

```
smoke *= load_gate * throttle_gate * rpm_gate
smoke *= intensity * final_smoke_multiplier
smoke *= load_boost * load_sensitivity + (1 - load_sensitivity)
```
(`exhaust_model.cpp:145-153`)

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

`reset()` (`exhaust_model.cpp:31`) zeroes all smoothing, boost, puff,
shift-transient, session-peak, and previous-gear state. It is called on:

- Engine off (every `update` with `engine_enabled == false`).
- Pause (`on_pause` in `plugin.cpp`).
- Frame-start timer restart or backward time jump.

This prevents stale state from producing smoke after a save load, a pause, or
an engine restart.

---

## 4. Configuration schema

All keys live under the case-sensitive `[RealisticSmoke]` section. Missing or
invalid values use the safe defaults shown. The ranges are enforced by
`sanitize()` in `config.cpp:108-126`. Booleans accept `1/true/yes/on` and
`0/false/no/off` (case-insensitive).

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

### 4.4 Throttle and RPM shaping

| Key | Type | Default | Range | Meaning |
| --- | --- | --- | --- | --- |
| `ThrottleGatePower` | float | `1.5` | `[0.1, 5]` | How strongly demand follows the throttle. |
| `RpmLeanFraction` | float | `0.6` | `[0.1, 1]` | RPM fraction at which smoke reaches zero. |
| `RpmLeanPower` | float | `1.2` | `[0.1, 5]` | Sharpness of the high-RPM lean-out. |

### 4.5 Smoothing and transients

| Key | Type | Default | Range | Meaning |
| --- | --- | --- | --- | --- |
| `AttackSmoothness` | float | `0.3` | `>= 0` | Throttle rise time constant (seconds). |
| `ReleaseSmoothness` | float | `0.3` | `>= 0` | Throttle fall time constant (seconds). |
| `ThrottleTransientStrength` | float | `0.7` | any finite | Strength of the throttle-rate puff. |
| `ThrottleRiseThreshold` | float | `0.05` | `[0, 1]` | (Reserved. Parsed and clamped. Not currently used in the pipeline.) |
| `ThrottleTransientDecay` | float | `0.1` | `>= 0` | (Reserved. Parsed and clamped. Not currently used in the pipeline.) |
| `ShiftWeight` | float | `1.0` | any finite | Weight of the gear-shift puff. |
| `LaunchWeight` | float | `1.0` | any finite | Weight of the stationary-launch puff. |

### 4.6 Event and peak

| Key | Type | Default | Range | Meaning |
| --- | --- | --- | --- | --- |
| `EventStrength` | float | `1.0` | any finite | (Reserved. Parsed. Not currently used in the pipeline.) |
| `SustainedThrottleStrength` | float | `0.5` | any finite | (Reserved. Parsed. Not currently used in the pipeline.) |
| `SessionPeakPercent` | float | `0.85` | `[0, 1]` | Per-frame decay fraction of the session peak. |

### 4.7 Throttle curve and component weights

| Key | Type | Default | Range | Meaning |
| --- | --- | --- | --- | --- |
| `ThrottleCurve` | int | `45` | `[-1, 100]` | Parsed and clamped. (The pipeline uses `throttle_gate_power` for throttle shaping. This key is retained for schema compatibility with `MODGuard_RE.md`.) |
| `ThrottleWeight` | float | `1.0` | any finite | (Reserved. Parsed. Not currently used in the pipeline.) |
| `RpmWeight` | float | `1.0` | any finite | (Reserved. Parsed. Not currently used in the pipeline.) |
| `SpeedWeight` | float | `1.0` | any finite | (Reserved. Parsed. Not currently used in the pipeline.) |
| `AccelerationWeight` | float | `1.0` | any finite | (Reserved. Parsed. Not currently used in the pipeline.) |
| `GearWeight` | float | `1.0` | any finite | (Reserved. Parsed. Not currently used in the pipeline.) |
| `ClutchWeight` | float | `1.0` | any finite | (Reserved. Parsed. Not currently used in the pipeline.) |
| `CargoWeight` | float | `1.0` | any finite | (Reserved. Parsed. Not currently used in the pipeline.) |
| `TractionAccelerationWeight` | float | `1.0` | any finite | (Reserved. Parsed. Not currently used in the pipeline.) |
| `LoadEffortWeight` | float | `1.0` | any finite | (Reserved. Parsed. Not currently used in the pipeline.) |
| `FreeRevWeight` | float | `1.0` | any finite | (Reserved. Parsed. Not currently used in the pipeline.) |

> **Reserved keys.** Several keys in section 4.6 and 4.7 are parsed and
> sanitized. This keeps the INI schema compatible with `MODGuard_RE.md`. The
> current Linux pipeline does not read them. They are accepted without error.
> They are clamped to their safe ranges. They must not be presented as active
> behavior in user-facing documentation.

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
of per-input terms. It then assigns one of three particle-size buckets
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