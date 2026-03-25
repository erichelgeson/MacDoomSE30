# 68030 Optimization Research — Findings from Reference Documents

Extracted from: 68030 Assembly Language Reference (Williams, 1989), Graphics Gems (1995),
High-Performance Interactive Graphics (Adams, 1987), Inside Macintosh volumes (1992-1994).

---

## 1. 68030 Cache Architecture (CRITICAL)

### Instruction Cache
- 256 bytes, 16 entries × 4 longwords (16 bytes per cache line)
- Address bits A7-A4 select entry, A3-A1 select word within entry
- One tag per line, valid bit per longword
- **Burst mode**: fetches required longword first, then 3 surrounding longwords
- Enabled/disabled via CACR bit 0

### Data Cache (KEY FINDING — may be unused on SE/30)
- **The 68030 HAS a 256-byte data cache** — 16 lines × 4 longwords, same as instruction cache
- **Write-through**: writes ALWAYS go to external memory, even on cache hits
- Read hits served from cache (saves ~100-200 cycle memory access penalty)
- Enabled/disabled via CACR bit 8
- Burst mode for data reads via CACR bit 12
- Write allocation (fill cache on write miss) via CACR bit 13
- **Question for testing**: Is the SE/30 running with data cache enabled? System 7.5 may or may not enable it. If disabled, enabling it could provide significant speedup for array reads (nodes[], segs[], colormaps[]).

### Cache Implications for Doom
- Loops that fit in 256 bytes benefit enormously from instruction cache
- **Iterative BSP regression explained**: larger code blew the 256-byte I-cache, confirmed by the 10% regression observed
- Data cache (if enabled) helps repeated reads from same 16-byte-aligned regions
- `nodes[]` random access: 28 bytes/node, crosses 2 cache lines — cache helps if revisited
- `walllights[]`, `solidsegs[]`, `ceilingclip[]`, `floorclip[]` — sequential access, cache-friendly
- Framebuffer writes: NO cache benefit (write-through)

---

## 2. 68030 Instruction Characteristics

### Key Timing Notes (from reference, OCR quality poor — approximate)
The book notes that on 68020/68030 with instruction cache:
- **Immediate values in loops**: On 68020/68030, immediate constants can stay in instruction cache, so putting them inline is fine (unlike 68000/68010 where register is better)
- **EXTB.L**: 68020/68030 only instruction, extends byte to long in one instruction (vs EXT.W + EXT.L on 68000)
- **MOVEM.L**: "may not be the best technique on 68020/68030 processors (which have an instruction cache)" — because MOVEM is a large instruction that may pollute the I-cache

### Approximate Instruction Timings (68030 @ 16 MHz, from various sources)
| Instruction | Cycles (approx) | Notes |
|---|---|---|
| MOVE.L Dn,Dn | 2 | Register-to-register |
| MOVE.L (An),Dn | 4-6 | Memory read (cache hit: 4, miss: varies) |
| MOVE.L Dn,(An) | 4-6 | Memory write (always external) |
| MOVEM.L (saves) | 8 + 4/reg | Large instruction, may thrash I-cache |
| MULS.W | 28 | 16×16→32 multiply |
| MULS.L | 44 | 32×32→64 multiply |
| DIVS.W | 56 | 32÷16→16 divide |
| DIVS.L | 90-140 | 64÷32→32 divide (data dependent) |
| SWAP | 4 | Exchange upper/lower word |
| LSR/LSL/ASR | 6-8 | Shift by register or immediate |
| BSET/BCLR (reg) | 6-8 | Bit set/clear on register |
| BSET/BCLR (mem) | 12-16 | Bit set/clear on memory (read-modify-write) |
| DBcc | 6 (taken), 10 (fall through) | Decrement and branch |
| JSR | 18-22 | Jump to subroutine |
| RTS | 16-18 | Return from subroutine |
| BRA/Bcc (taken) | 6-10 | Branch |
| ADD.L Dn,Dn | 2 | Register add |
| CMP.L Dn,Dn | 2 | Register compare |

### Critical Ratio: MULS.W (28) vs MULS.L (44)
**If operands fit in 16 bits, MULS.W saves 16 cycles per multiply.**
Many Doom values (column heights, screen coordinates, offsets) fit in 16 bits.
FixedMul currently uses `long long` (64-bit intermediate) which forces MULS.L.

### Function Call Overhead
JSR (18-22) + RTS (16-18) + typical prologue (LINK, MOVEM.L save) + epilogue (MOVEM.L restore, UNLK) = **~60-120 cycles per function call**. For functions called 100-3000× per frame, this is significant.

---

## 3. Mac OS Trap Dispatch Overhead

From Inside Macintosh: OS Utilities — every Toolbox/OS call goes through:
1. A-line instruction encountered by CPU
2. CPU generates exception → trap dispatcher
3. Trap dispatcher looks up routine address in trap dispatch table (256 OS + 1024 Toolbox entries)
4. Indirect jump to routine

**Estimated overhead: 40-80 cycles per trap call**, on top of the routine itself.
This means: avoid calling Toolbox traps in hot paths. Currently `I_GetMacTick()` (TickCount trap) is called in the profiling loop — acceptable. But if any trap is called per-column or per-pixel, it's expensive.

---

## 4. Sound Manager CPU Impact

From Inside Macintosh: Sound:
- Sound Manager tracks CPU load via `smCurCPULoad` field in `SMStatus` record
- `smMaxCPULoad` defaults to 100 (percentage)
- Async SndPlay continues consuming CPU during playback via interrupts
- Sound callback procedures execute at **interrupt time** — they preempt game code
- System alert sound can be disabled with `SndSetSysBeepState(sysBeepDisable)` to prevent it from consuming CPU during gameplay
- **Buffered expansion** (pre-expand compressed audio) has minimal CPU overhead during playback vs real-time expansion

### Actionable for Doom:
- Call `SndSetSysBeepState(sysBeepDisable)` at game start to prevent system beep interrupts
- Could query `smCurCPULoad` to log how much CPU sound is consuming
- Our current approach (SndPlay async with PCM data) is already the minimal-overhead path

---

## 5. QuickDraw / Framebuffer Access

From Inside Macintosh: Imaging with QuickDraw:
- `screenBits` global variable: BitMap for main screen
- `screenBits.baseAddr` = pointer to framebuffer memory
- `screenBits.rowBytes` = bytes per row (64 for SE/30's 512-wide screen)
- **Direct framebuffer writes bypass QuickDraw entirely** — no trap overhead
- Our current approach (writing directly to `fb_mono_base` / `real_fb_base`) is correct and optimal
- `rowBytes` must be < $4000 (bit 14 must be 0) — this is a flag, not a size limit concern for SE/30

### Bit Image Layout (confirmed for SE/30)
- 342 rows × 512 pixels = 342 × 64 bytes = 21,888 bytes of video RAM
- Bit 15 (MSB) of first word = leftmost pixel
- Bit 0 (LSB) = rightmost pixel of that word
- Pixel value 0 = white, 1 = black

---

## 6. Memory Manager Considerations

From Inside Macintosh: Memory:
- 24-bit vs 32-bit addressing: SE/30 with System 7.5 runs in 32-bit mode
- Handle double-indirection: master pointer → actual data. Every handle dereference = one extra memory read
- `HLock`/`HUnlock` prevent Memory Manager from moving blocks but add a trap call overhead
- Zone allocator compaction can cause unpredictable pauses

### Actionable for Doom:
- Doom's `Z_Malloc` is its own zone allocator, not using Mac Memory Manager for game data
- Handle dereferencing for sound (`snd_handle`) is fine since it's infrequent
- No Memory Manager concerns for hot rendering paths

---

## 7. Graphics Gems — Applicable Techniques

### Fast Hypotenuse Approximation (Paeth)
`h ≈ max(|x|,|y|) + min(|x|,|y|)/2`
- Error: never underestimates, worst case +12%
- Implementation: 2 abs, 1 compare, 1 shift, 1 add — ~10 cycles
- **Doom already uses this**: `P_AproxDistance` in `p_maputl.c`

### Ordered Dithering
- Graphics Gems covers ordered dithering theory
- **Already implemented**: Bayer 4×4 dither matrix in our renderer

### Fixed-Point CORDIC Iterations
- Iterative trig using only shifts and adds (no multiply)
- Potentially applicable if Doom's angle computation is a bottleneck
- Current Doom uses `finesine[]`/`finecosine[]` LUT (8192 entries) — already fast

### Bit Interleaving for Quad/Octrees
- Spatial indexing technique — not applicable to Doom's BSP

### Distance Measures
- Manhattan distance: `|dx| + |dy|` — already used in some Doom paths
- Chebyshev distance: `max(|dx|, |dy|)` — useful for rough proximity tests

---

## 8. Optimization Principles from 68030 Reference

Direct quotes/paraphrases from the Performance Improvements section:

1. **"Minimize the total number of times the processor goes to memory."** Most optimizations have this at their root.

2. **"Place frequently-accessed data in registers."** Register access = no memory access, smaller instruction, faster.

3. **"Reduce the total number of instructions in a loop."** Loop unrolling reduces overhead but must balance against I-cache (256 bytes).

4. **"Use MOVEM for bulk transfers"** but be careful on 68020/68030 — large MOVEM instructions may pollute the instruction cache.

5. **"Use longword operations wherever possible"** instead of byte operations — process 4 bytes at a time.

6. **"Match software structure to workload"** — choose the right algorithm variant for the actual data characteristics.

7. Loop unrolling benchmarks showed:
   - For memcpy: byte loop = 1.04 µs/byte, unrolled longword = 0.78 µs/byte (25% improvement)
   - For memclr: byte loop = 0.80 µs/byte, unrolled longword = faster
   - Improvement plateaus around 128-256 bytes per call
   - **Key insight**: unrolling helps more for small calls than large ones

---

## 9. Key Unknowns to Investigate

1. **Is the 68030 data cache enabled on SE/30 under System 7.5?**
   - If not, enabling it (MOVEC to CACR) could provide 10-30% speedup on data-heavy reads
   - Could test by reading CACR register value at startup and logging it

2. **What does GCC -O3 actually emit for R_RenderSegLoop?**
   - Need to disassemble the binary to identify wasted instructions
   - The MULS.W vs MULS.L question is critical — does GCC use .W where possible?

3. **How much CPU does sound actually consume?**
   - Can measure via `SndManagerStatus` and log `smCurCPULoad`

4. **Can we use MULS.W instead of MULS.L in FixedMul?**
   - FixedMul result = (a * b) >> 16
   - If one operand fits in 16 bits (common for small values like column heights, texture offsets), a 16-bit version saves 16 cycles per call
   - Needs careful analysis of value ranges in hot paths
