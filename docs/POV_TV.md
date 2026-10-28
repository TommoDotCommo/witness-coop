# Optional Back TV ("POV TV")

This document details the design, rendering architecture, wire protocol, privacy model,
frame budget, and test suite for the optional "POV TV" strapped to the adventurer's back.

---

## 1. Design Study: Rendering Approaches & Feasibility

The objective is to display a player's point of view on a small TV prop mounted to the
back of their adventurer model, viewable by other players in the co-op session.

Three approaches were evaluated:

### (a) Real Second Camera Render to Low-Resolution Texture
- **Engine Mechanics**: The Witness (Steam build 11370412, `witness64_d3d11.exe`) uses a monolithic
  in-engine rendering pipeline. It manages culling, shadow cascades, clustered light assignments,
  puzzle panel state, and post-processing targets globally for a single primary camera (`base + 0x6304C0`).
- **Feasibility & Constraints**: The engine provides no external viewport or multi-camera rendering API.
  Re-invoking the internal scene draw calls outside the main loop disrupts engine shadow/occlusion
  buffers, risks D3D11 render target contention, and would double scene draw calls per visible TV,
  violating the strict frame budget.
- **Verdict**: Not feasible without extensive reverse-engineering of the engine's internal scene
  graph and multi-pass pipeline.

### (b) Periodically Captured Snapshot Stream (Chosen Approach)
- **Mechanics**: Capture the sender's rendered backbuffer after presentation via D3D11 staging textures,
  downsample to 128x96 RGB332, compress with run-length encoding (RLE), and stream at 5 Hz over UDP
  to authorized subscribers.
- **Feasibility & Frame Budget**:
  - The mod already maintains an asynchronous swapchain capture path (`VideoFrame` in `proxy/video.cpp`)
    using a 2-frame pipelined staging texture (`Map(..., D3D11_MAP_FLAG_DO_NOT_WAIT)`).
  - Downsampling from 480x270 to 128x96 takes **~40 microseconds** of CPU time on an encoder thread.
  - Bandwidth is bounded: 128x96 = 12,288 bytes uncompressed; with RLE typically 2–6 KB per frame.
  - At 5 Hz and capped at 2 subscribers, network throughput is under 40 KB/s.
  - On the viewer, updating the 128x96 D3D11 texture takes less than 0.001 ms of GPU time.
- **Visual Fidelity**: Shows exactly what the sender sees (including panel tracing, cursors, and puzzle lines)
  without state desynchronization.
- **Verdict**: Fully feasible, robust, and well within CPU, GPU, and network budgets.

### (c) Stylised Fallback & CRT Aesthetics (Implemented Companion)
- **Mechanics**: When no live feed is available (sender disconnected, viewer disabled, or consent withheld),
  the TV renders dynamic CRT static noise at 5 Hz, combined with an authentic phosphor collapse/expansion
  power-on/off animation.
- **Heading Mini-Map**: Follow-up candidate for offline/compass mode if heading telemetry is desired without video.
- **Verdict**: Implemented as the default unlinked state when live feed is inactive.

### Why Local Camera-Pose Rendering on the Receiver is NOT Feasible
Syncing only camera pose and rendering the remote player's view locally on the receiver client has two
critical flaws in this engine:
1. Re-executing the entire scene render on the receiver client requires reverse-engineering the engine's
   internal culling, light clustering, and shadow hierarchy.
2. Even if executable, local rendering would reflect the *receiver's* local puzzle progress and panel states,
   not what the remote player is actually interacting with.

---

## 2. 3D Prop & Secondary Motion Model

The TV prop is procedurally generated in `proxy/tv/mesh.h` for the bundled adventurer model:
- **Geometry**:
  - Main CRT shell (olive-slate composite: 72, 81, 77) with rounded beveling.
  - Front trim bezel (dark charcoal: 27, 30, 29).
  - Channel and volume rotary brass dials.
  - Speaker grille slots and power indicator LED (emissive green when powered, dark when off).
  - Screen quad positioned on the rear face (`z = -0.481m`), pointing backwards (`n = (0, 0, -1)`).
- **Harness & Straps**:
  - Mounting bracket strapped to the rear pack (`z = -0.31m`).
  - Dual leather shoulder straps (80, 51, 29) with brass buckles passing over the adventurer's shoulders
    to the front chest plate.
  - Moving attachment points dynamically adjust ribbon length to maintain tension as the bracket sways.
- **Skeletal Binding**:
  - All vertices bind to joint `chest` with weight 1.0, inheriting chest animation from walk/run clips.
- **Secondary Dynamics (`wtv::Motion`)**:
  - Second-order damped harmonic spring model:
    $$\ddot{x} + 2\zeta\omega\dot{x} + \omega^2 x = F(\text{speed})$$
    where $\omega^2 = 90$, $2\zeta\omega = 18$ ($\zeta \approx 0.95$, near critical damping).
  - Fixed substep integration ($h \le 1/240$ s) guarantees stability across frame spikes or teleports.
  - Physical limits: sway angle clamped to $\pm 0.16$ rad ($\approx 9^\circ$), velocity clamped to $\pm 1.0$ m/s.
  - Antenna oscillation: secondary higher-frequency wobble ($2.7\times$ phase) added to body sway.
  - Power transition: linear CRT phosphor collapse/expansion over 333 ms.

---

## 3. Screen Shader & Lighting Integration

In `proxy/model_gpu.cpp`:
- The screen quad vertex has `col.a = 255`, signaling the shader to sample the TV texture rather than flat color.
- `FrameConst.light[3]` is set to `1.0` during screen rendering:
  $$c = \text{base} \times \text{tint} \times (\text{light.w} + (1 - \text{light.w}) d)$$
  This makes the TV screen 100% emissive, independent of environmental lighting angles.
- Scene depth testing remains fully active (`OMSetRenderTargets` depth buffer hook): if the adventurer stands
  behind a wall or tree, the TV screen is properly occluded.
- Non-blocking GPU occlusion queries (`D3D11_QUERY_OCCLUSION` with `ASYNC_GETDATA_DONOTFLUSH`) monitor
  actual visible screen pixels without stalling the render thread.

---

## 4. Privacy & Consent Architecture

Privacy is enforced through strict opt-in controls:
1. **Double Opt-In**:
   - Viewer setting: `pov_tv=0|1` (default 0).
   - Sender consent: `pov_tv_consent=0|1` (default 0).
   - The viewer toggle shows the prop. Without sender consent, its screen shows static and no POV snapshots are sent.
2. **On-Screen HUD Notice**:
   - When consent is active, a prominent indicator appears on screen:
     `SHARING MY TV POV [F11 STOP]  [F10 back TV]`
   - When viewer mode is active without consent:
     `BACK TV ON [F10 OFF]  [F11 share my POV]`
3. **Instantaneous Revocation**:
   - Pressing F11 or disabling consent immediately sends `kind=2` (denied/revoked) packets to all subscribers.
   - Senders immediately drop subscriptions and cease capture (`TvWanted() == false`).
   - Receivers immediately erase the frame buffer (`reset()`) and increment ticket counters to reject
     in-flight packets. The screen instantly reverts to CRT static.
4. **Zero Persistent Storage**:
   - Captured frames exist only in transient memory during transmission and display; no frames are written to disk.

---

## 5. Wire Protocol & Network Security

Two UDP datagram formats are used:

### Control Datagram (`WTU1`, 24 bytes)
| Offset | Type | Field | Description |
|---|---|---|---|
| 0 | char[4] | Magic | `"WTU1"` |
| 4 | uint32 | From | Sender player ID |
| 8 | uint32 | To | Destination player ID |
| 12 | uint32 | Ticket | Session ticket (increments on target change or revoke) |
| 16 | uint32 | Kind | 0 = Cancel, 1 = Request, 2 = Denied/Revoked |
| 20 | uint32 | Checksum | FNV-1a hash of bytes 0–19 |

### Data Frame Datagram (`WTT1`, 28 + length bytes)
| Offset | Type | Field | Description |
|---|---|---|---|
| 0 | char[4] | Magic | `"WTT1"` |
| 4 | uint32 | From | Sender player ID |
| 8 | uint32 | Ticket | Ticket matching active request |
| 12 | uint32 | Sequence | Frame sequence number |
| 16 | uint8 | ChunkIdx | Chunk index (0 .. Count - 1) |
| 17 | uint8 | ChunkCount | Total chunks (max 12) |
| 18 | uint8 | Mode | 0 = Raw RGB332, 1 = RLE compressed |
| 19 | uint8 | Reserved | Must be 0 |
| 20 | uint32 | DataLen | Length of chunk payload |
| 24 | uint8[] | Payload | Compressed pixel data (max 1024 bytes) |
| End - 4 | uint32 | Checksum | FNV-1a hash of preceding packet bytes |

### Security & Ingress Protections
- Ingress rate limiter: max 24 chunks ($2\times \text{MaxChunks}$) per 200 ms window before checksum or decode.
- Untrusted length validation: strictly checks `len <= 1024`, `count <= 12`, `idx < count`.
- Sequence wrap-around: standard signed 32-bit distance checks `(int32_t)(seq - last) > 0`.
- Stale picture rejection: chunks older than 800 ms are dropped.
- Lease expiry: subscribers must renew every 500 ms; leases expire after 1500 ms.
- Subscriber limit: hard cap of at most 2 concurrent subscribers per sender.

---

## 6. Controls & Commands

- **Config (`witness_coop.cfg`)**:
  - `pov_tv=1`: Enable viewing the nearest player's back TV (default 0).
  - `pov_tv_consent=1`: Consent to streaming your POV to subscribers (default 0).
- **Keyboard Shortcuts**:
  - `F10`: Toggle Back TV viewer on/off.
  - `F11`: Toggle POV streaming consent on/off.
  - `J` (`key_tv_deploy`): Aim at a nearby player's backpack to deploy a stationary
    4 m × 2.5 m audience screen, 3 m behind their deployment pose. Press again to retract.
    The bottom is 2 m above the deployment ground plane. Source consent still applies.
  - `K` (`key_tv_seat`): Aim at one of the three wooden audience stools within 2.5 m
    to sit facing the screen. Press again, move, jump, or press Escape to stand.
    Retraction/disconnection also releases the seat. Standing restores the entry pose.
    Seats share the screen's ground plane; use a flat, open deployment area.
    A held Sit emote and lowered eye position synchronize the seated avatar without
    lowering its model origin a second time (the gesture already lowers the pelvis). Occupied stools are excluded once the
    other player's seated pose arrives; simultaneous seating is best effort.
- **In-Process Commands (`witness_cmd.txt`)**:
  - `tv 0|1`: Enable/disable viewer.
  - `tvconsent 0|1`: Enable/disable consent.
  - `tvdeploy [player]`, `tvretract`: Deploy the aimed/named source, or retract.
  - `tvseat`, `tvstand`: Use the aimed stool, or exit the current seat.
  - `tvstate`: Dump metadata to `witness_tv.json`.

---

## 7. Verification & Automated Tests

### Game-Free Contract Tests (T0)
- `python3 tools/test_tv.py` (and `make -C proxy/core test`):
  - Fuzzes packet reassembly with 100,000 malformed packets.
  - Tests 500 sessions under packet loss, jitter, reordering, and retransmissions.
  - Wire, consent, same-sequence heartbeat/replay, local-only audience capture,
    ownership destruction, bounded diagnostics, seating placement and mesh mutants.
  - A 14-second heartbeat simulation checks every 100 ms without accepting flicker.
  - Fake COM references exercise texture/SRV destruction through 1,000 map erasures.
  - Numerical spring-damper stability and NaN-safety verified.

### Live Two-Client Verification (T2)
- `tools/qa.sh tv` (`tools/qa_tv.py`):
  - Starts two game clients (Alpha and Beta).
  - Beta stands 2.5m behind Alpha and enables TV (`F10`).
  - Asserts static noise when consent is off.
  - Alpha consents (`F11`); asserts Beta receives live snapshots matching Alpha's frame hash.
  - Asserts visible screen pixels via GPU occlusion query.
  - Turns Alpha's camera; verifies texture update on Beta's TV.
  - Revokes consent on Alpha; verifies instantaneous screen blanking on Beta.
  - Includes mutant `no_partner_video=1` which fails the live assertion.

### Live Three-Client Audience Verification

`python3 tools/qa_tv3.py` uses three isolated clients through the resource provider.
It checks uninterrupted deployment/render/live pictures for 14 seconds, visible
screen pixels on both viewers, exact source frame hashes, late join, stationary
placement while the source moves, consent revoke/re-enable, aimed seating and
synchronized eye/Sit pose, movement and retraction exits, source changes, a local
source audience with zero network subscriptions, and resource cleanup. Captures
and process bridge snapshots are saved under ignored `local/tv3-evidence/`.
