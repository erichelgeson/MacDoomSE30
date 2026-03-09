# Doom SE/30 — Optimization Ideas

Prioritized list of remaining optimization opportunities. Update as ideas are tried,
confirmed, or ruled out. Mark status with: `[ ]` untried, `[x]` done, `[-]` tried/rejected.

---

## High Impact / Tractable

- [x] **Colormap/lighting skip per column** — `rw_scale` steps slowly across a seg so
  adjacent columns often land on the same `walllights[index]`. Track last index and
  skip `dc_colormap` + `mono_cm` recompute when unchanged. ~20 cycles saved per
  unchanged column. Low risk, small change. (2026-03-08)

- [-] **Double-buffer blit (MOVEM.L assembly)** — Hand-rolled 68030 MOVEM.L loop
  for the 12,800-byte memcpy flip. Assembly was correct but Basilisk II doesn't
  trigger display refresh from MOVEM.L writes to video RAM (dirty-page tracking
  issue). Snow showed HUD fine but savings marginal (~1–2 ticks on 7–28 total).
  Rejected 2026-03-09.

- [ ] **Profile `logic` cost** — `P_Ticker` sometimes hits 20–25 ticks. Sector specials,
  moving floors, and line actions all run every game tic regardless of rendering load.
  Add a profiling split to identify the hotspot.

---

## Medium Impact

- [ ] **R_ScaleFromGlobalAngle — reduce FixedDiv calls** — Called twice per seg
  (start + end scale). Each call contains a FixedDiv (~50 cycles). 36 segs/frame =
  72 divisions just for wall scale. Could approximate the end scale from the start
  or cache recent results.

- [ ] **HUD rendering cost** — Spikes 60–120 ticks during menu transitions, 4–16 ticks
  during gameplay. Menu responsiveness TODO likely tied to this. Profile and reduce
  redundant redraws.

- [ ] **R_CheckBBox tighter angle rejection** — Currently does full clip array scan per
  BSP node. Could add a screen-space angle rejection early-out for nodes clearly
  off to one side (before the clip array walk).

---

## Bigger Projects / Longer Term

- [ ] **Pre-dithered WAD textures** — Offline-convert all textures to 1-bit, eliminating
  `COLMONO_GRAY` entirely (32-cycle double table-lookup per row). Biggest single
  remaining win but requires building the WAD converter tool. Would also eliminate
  the colormap lookup chain.

- [ ] **Sky/fog interaction** — Sky ceiling is special-cased and currently gets fogged
  out when fog_scale is set. Needs a separate fog path for sky sectors so the sky
  remains visible. See also TODO.md.

- [ ] **Smaller view window** — Could try a narrower viewport (fewer columns per seg)
  but QUAD mode is already small — diminishing returns and visual quality degrades
  quickly.

---

## Already Done / Rejected

- [x] **FixedMul inline macro** — Replaced function call with macro (Phase 2, 2026-02-26)
- [x] **FixedDiv2 → long long** — Eliminated FPU dependency (Phase 2, 2026-02-26)
- [x] **Direct 1-bit framebuffer renderers** — R_DrawColumn_Mono etc., skip intermediate buffer (Phase 4, 2026-03-01)
- [x] **68030 ASM pixel macros** — COLMONO_GRAY / COLMONO_BIT with SWAP, BSET/BCLR (2026-03-01)
- [x] **Flat floor/ceiling (solidfloor)** — Eliminates span drawing entirely (2026-03-01)
- [x] **Double-buffering** — Eliminates mid-frame flicker (2026-03-02)
- [x] **halfline rendering** — Render even rows only; odd rows copied (2026-03-02)
- [x] **iscale linear interpolation** — Replaces per-column 32-bit divide with linear step (2026-03-02)
- [x] **Affine texture column stepping** — Replaces per-column FixedMul with addition (2026-03-02)
- [x] **Visplane mark skip** — Suppress floor/ceiling visplane writes when solidfloor=1 (2026-03-02)
- [x] **QUAD mode (detailLevel=2)** — 4px-wide columns, nibble framebuffer writes (2026-03-07)
- [x] **BSP bbox fog culling** — Prune entire BSP subtrees beyond fog distance (2026-03-08)
- [x] **Seg-level fog fast path** — Skip texture/colfunc for fully-fogged segs (2026-03-08)
- [x] **QUAD nibble precomputed table** — Replace QUAD_NIBBLE 4×CMP/shift/OR with 1 table lookup (2026-03-08)
- [x] **Colormap/lighting index cache** — Skip `dc_colormap` update when `walllights[index]` unchanged across adjacent columns (2026-03-08)
- [-] **Pre-dithered texture columns (Option D)** — Tried and removed 2026-03-02: -3.7% FPS (slower) and visually broken (texture-space Bayer becomes perspective-warped spirals in screen space)
