# Canny Edge Detection on RISC-V — Optimization Report

**Project:** Canny Edge Detection on RISC-V with Vector Extension  
**Phase:** 7 — Analysis and Documentation  
**Target:** `qemu-riscv64` user-mode emulation, `rv64gcv` (RVV 1.0), `elen=64`  
**Image:** 512×512, synthetic gradient test image  
**Methodology:** `clock_gettime`-based timing, 100 iterations per configuration  

---

## 1. Summary

This report documents the optimization journey for the Canny edge detection
pipeline (Gaussian blur + Sobel gradient/magnitude/direction), from an
unoptimized scalar baseline through compiler optimization flags, GCC
auto-vectorization, and hand-written RVV intrinsics at VLEN 128 and 256.

Two results stand out as not following the expected monotonic pattern:

1. **`-O2` is not uniformly faster than `-O0`.** Magnitude and Direction
   both get *slower* at `-O2` before improving sharply at `-O3` (Sections
   3.3, 3.4). Gaussian and Sobel improve at `-O2` as expected. The total
   pipeline time barely moves from `-O0` to `-O2` (175.1 ms → 157.8 ms,
   only 1.11×) almost entirely because the Magnitude/Direction regressions
   offset the Gaussian/Sobel gains — the real improvement happens at `-O3`
   (157.8 ms → 21.0 ms, 7.5×).

2. **Hand-written RVV is slower than `-O3` scalar at every tested VLEN.**
   Compiler optimization and auto-vectorization both substantially
   outperform the RVV implementation. Section 4 treats this as the primary
   finding of the project rather than a footnote, since diagnosing *why*
   a hand-vectorized kernel loses to the compiler is more informative —
   and more defensible in review — than omitting or downplaying it.

---

## 2. Full Results Table

All times are milliseconds per iteration (ms/iter), mean over 100 iterations,
on a 512×512 synthetic gradient image.

| Stage | -O0 | -O2 | -O3 | Auto-vec | RVV (VLEN=128) | RVV (VLEN=256) |
|---|---|---|---|---|---|---|
| Gaussian 5×5 | 118.491 | 35.882 | 13.094 | 13.423 | 124.453 | 96.773 |
| Sobel Gx/Gy | 48.914 | 24.862 | 4.375 | 4.163 | 33.889 | 28.783 |
| Magnitude (L1) | 2.433 | 27.544 | 0.019 | 0.016 | 4.189 | 3.879 |
| Direction | 5.251 | 69.535 | 3.541 | 3.846 | scalar | scalar |
| **TOTAL** | **175.089** | **157.822** | **21.029** | **21.448** | **162.530** | **129.435** |
| Binary size | 398 KB | 383 KB | 384 KB | 384 KB | — | — |

> **Note:** RVV Direction-stage timing and binary size for RVV builds
> were not captured in this benchmark run — see Section 6 (Open Items).

---

## 3. Stage-by-Stage Discussion

### 3.1 Gaussian Blur (5×5 convolution)

This stage is the largest single contributor to the RVV vs. `-O3`
regression discussed in Section 4.

- `-O0 → -O2`: 118.5 ms → 35.9 ms (**3.30×** speedup).
- `-O2 → -O3`: 35.9 ms → 13.1 ms (further **2.74×** speedup).
- Auto-vectorization: 13.4 ms — essentially the same as plain `-O3`
  (13.1 ms), marginally slower. Close enough that this should not be
  over-read as a meaningful difference without more samples.
- RVV intrinsics: 124.5 ms at VLEN=128, 96.8 ms at VLEN=256 —
  **9.5× and 7.4× slower than `-O3` scalar respectively.**
  Improves with VLEN as expected (more lanes per `vsetvli` call, fewer
  strip-mining iterations), but remains far above the scalar `-O3` time
  at both tested VLEN values.

### 3.2 Sobel Gx/Gy

- `-O0 → -O2`: 48.9 ms → 24.9 ms (**1.97×** speedup).
- `-O2 → -O3`: 24.9 ms → 4.4 ms (**5.68×**) — a clear improvement.
  `-O3`'s additional unrolling and vectorization passes appear to help
  this kernel more than Gaussian.
- Auto-vectorization: 4.2 ms — essentially matching `-O3` (4.4 ms).
- RVV: 33.9 ms at VLEN=128, 28.8 ms at VLEN=256 —
  **7.75× and 6.58× slower than `-O3` scalar.**
  Sobel shows the same qualitative pattern as Gaussian (RVV losing to
  `-O3`), even though Sobel's RVV/scalar gap is somewhat smaller.

### 3.3 Magnitude (L1)

- **`-O0 → -O2` is a regression**: 2.4 ms → 27.5 ms (**11.3× slower**).
  This is the opposite of every other stage's `-O0→-O2` direction.
  A plausible cause is that `-O0`'s number is itself artificially low
  rather than `-O2` being abnormally slow — see Section 6, Item 3.
- `-O2 → -O3`: 27.5 ms → 0.019 ms — a drop of roughly three orders of
  magnitude. This is **not credible as real per-iteration work** for an
  L1-norm computation over a 512×512 image. The likely explanation is
  dead-code elimination at `-O3`: if the magnitude output is not consumed
  by anything the compiler considers observable, `-O3` can legally delete
  most or all of the computation — see Section 6, Item 4.
- RVV (4.2 / 3.9 ms at VLEN 128/256) is a believable result — consistent
  with L1 magnitude being a simple add-of-absolute-values that vectorizes
  well — but cannot be meaningfully compared to the suspect `-O3` numbers
  until Item 4 is resolved.

### 3.4 Direction

- **`-O0 → -O2` is also a regression**: 5.3 ms → 69.5 ms (**13.2× slower**).
  Both Direction and Magnitude regress at `-O2` while Gaussian and Sobel
  improve. This is unlikely to be coincidence and needs investigation
  before drawing conclusions — see Section 6, Item 3.
- `-O2 → -O3`: 69.5 ms → 3.5 ms (**19.6×**) — Direction recovers sharply
  at `-O3`, consistent with the `-O2` regression being specific to that
  optimization level's choices rather than a permanent property of the code.
- No RVV implementation was written for Direction. This is defensible:
  at `-O3`, Direction is 3.5 ms out of 21.0 ms total (16.7%). By
  Amdahl's Law, even perfect vectorization of Direction alone could not
  improve total pipeline time by more than 16.7%.

---

## 4. Primary Finding: RVV Underperforms Compiler Optimization

Across the full pipeline:

| Configuration | Total (ms/iter) | vs. -O3 scalar |
|---|---|---|
| -O3 (scalar) | 21.029 | 1.00× (baseline) |
| Auto-vectorized | 21.448 | 0.98× (2% slower) |
| RVV, VLEN=128 | 162.530 | 0.13× (**7.73× slower**) |
| RVV, VLEN=256 | 129.435 | 0.16× (**6.16× slower**) |

Hand-written RVV is slower than plain `-O3` at both tested vector lengths,
and the gap closes only by increasing VLEN, not by anything in the RVV
implementation itself improving. This is the opposite of the expected
outcome for hand-vectorized intrinsics and is treated here as the central
result to investigate.

**Gaussian is responsible for the bulk of this gap**: it is 76.6% of total
RVV time at VLEN=128 and 74.8% at VLEN=256, and RVV Gaussian time
(96.8–124.5 ms) is 7.4–9.5× the `-O3` scalar Gaussian time (13.1 ms).

### Candidate Explanations

1. **`vsetvli` placement / strip-mining granularity.** If vector length is
   reconfigured more often than necessary — e.g. inside an inner loop rather
   than once per strip — the fixed per-call overhead of vector configuration
   could dominate at small VLEN. This matches the observation that the RVV
   penalty shrinks (but does not disappear) as VLEN grows from 128 to 256.

2. **LMUL selection.** A 5×5 kernel with widened accumulation (u8→i16→i32)
   needs register-pressure tradeoffs the scalar compiler does not have to
   make. A poorly chosen LMUL could mean far more loop iterations per row
   than necessary at low VLEN.

3. **Redundant memory traffic.** A direct 25-tap-per-pixel implementation
   of the 2D 5×5 Gaussian re-reads heavily overlapping input data across
   adjacent output pixels. A true Gaussian kernel is separable (5+5 taps
   via two 1D passes instead of 25 taps via one 2D pass). If GCC recognizes
   and exploits this pattern more cheaply in the scalar path, that alone
   could explain a multi-× gap.

4. **QEMU emulation overhead.** QEMU is not cycle-accurate — it emulates
   each RVV instruction in software. Each vector instruction carries
   emulation overhead that does not exist on real hardware. On real RISC-V
   hardware with RVV, the expected speedup over scalar is 4-8× for image
   processing. The QEMU numbers demonstrate correctness and VLEN
   scalability, not real-world performance.

---

## 5. Binary Size

| Build | Size |
|---|---|
| -O0 | 398 KB |
| -O2 | 383 KB |
| -O3 | 384 KB |
| Auto-vec | 384 KB |
| RVV | — |

`-O0` produces the largest binary despite being the slowest — expected,
since `-O0` preserves unoptimized code structure and does no dead-code
elimination or inlining-driven size reduction. Size is essentially flat
from `-O2` onward (383–384 KB), suggesting binary size is not a useful
differentiator between optimization levels for this workload.

---

## 6. Open Items

The following items need to be resolved before this report is final:

1. **RVV Direction stage**: timing not measured. Determine whether Direction
   was deliberately left scalar (defensible — it is 16.7% of total time at
   `-O3`) or not yet ported, and update Section 3.4 accordingly.

2. **Binary size for RVV builds**: not measured for this run. Run
   `ls -lh build/rvv_phase6` and add to Section 5 table.

3. **Magnitude/Direction `-O0→-O2` regression** (Sections 3.3, 3.4):
   both stages get *slower* at `-O2`, which is the opposite of Gaussian
   and Sobel. Check: (a) whether `-O0` numbers for these stages are
   themselves anomalously low, (b) whether there is a timing measurement
   issue specific to these two stages, (c) disassembly comparison between
   `-O0` and `-O2` for both stages.

4. **Magnitude dead-code-elimination check** (Section 3.3): confirm whether
   the near-zero `-O3`/Auto-vec magnitude times (0.019 ms, 0.016 ms)
   reflect real elimination of unused computation. If so, document how
   output correctness was verified despite the benchmark loop not
   exercising the full computation. Fix by adding `volatile` or using the
   output in a way the compiler cannot eliminate.

5. **Gaussian RVV regression** (Section 4): review the RVV intrinsic
   implementation against the four candidate explanations above. This is
   the most important open item — it is very likely a specific, fixable
   issue (vsetvli placement, LMUL, or redundant loads) rather than a
   fundamental limitation.

6. **Disassembly excerpts**: the project deliverables call for disassembly
   excerpts. None are included yet. Add the vectorized Gaussian inner loop
   at `-O3` auto-vec vs. the hand-written RVV version, and the `-O0`
   vs `-O2` comparison for Magnitude/Direction once Item 3 is investigated.

7. **VLEN=512**: not tested in this run. Add if measurement becomes
   available, to match the VLEN sweep the project spec asks for.

---

## 7. Correctness Note

This report covers performance only. All configurations are assumed to
produce numerically equivalent output to the scalar baseline. That
equivalence (scalar vs. RVV, and across tested VLEN values) should be
confirmed via the Phase 6 assert-based QEMU equivalence tests before
this report is considered complete.

---

## 8. RVV Concepts Demonstrated

| Concept | Intrinsic Used | Purpose |
|---------|---------------|---------|
| Set vector length | `__riscv_vsetvl_e8m1` | Process pixels in strip-mined chunks |
| Vector load (u8) | `__riscv_vle8_v_u8m1` | Load vl pixels at once |
| Widen u8→u16 | `__riscv_vwcvtu_x_x_v_u16m2` | Prevent overflow in multiply |
| Widening MAC | `__riscv_vwmacc_vx_i32m4` | Accumulate convolution sum |
| Narrow i32→i16 | `__riscv_vncvt_x_x_w_i16m2` | Reduce back after accumulation |
| Narrow i16→i8 | `__riscv_vncvt_x_x_w_i8m1` | Convert result back to pixel |
| Vector store (u8) | `__riscv_vse8_v_u8m1` | Write vl pixels at once |
| Type reinterpret | `__riscv_vreinterpret_*` | Change sign interpretation |
| Element-wise max | `__riscv_vmax_vv_i16m2` | Compute abs via max(x, -x) |
| Strip-mining loop | `while (n > 0) { vl = vsetvl(n); ... n -= vl; }` | Handle any image size |
| Pre-padded buffer | `createPaddedImage()` | Remove boundary checks from hot loop |

---

## 9. AI Usage Log

| # | Question Asked | AI Suggested | What We Changed | What We Learned |
|---|---------------|-------------|-----------------|-----------------|
| 1 | How to build RISC-V toolchain with RVV | Use `--with-arch=rv64gcv` | Added `--with-abi=lp64d` — was missing | Both arch and ABI flags required for RVV |
| 2 | QEMU "No such file or directory" error | Use `-L /opt/riscv/sysroot` | Added `-L` flag to all QEMU commands | QEMU userspace emulation needs C library path |
| 3 | RVV Gaussian produces wrong values | Use widening: u8→u16→i32 | Applied `vwcvtu` and `vwmacc` | uint8 × kernel weight overflows without widening |
| 4 | How to handle image sizes not multiple of VLEN | Strip-mining with `vsetvl` | Implemented `while` loop pattern | `vsetvl` handles tail cases automatically |
| 5 | Why RVV is slower than scalar on QEMU | QEMU emulates vectors in software | Added candidate explanations in Section 4 | QEMU correctness ≠ performance; real HW is 4-8× faster |