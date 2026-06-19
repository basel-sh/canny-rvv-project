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
   both get *slower* at `-O2` before improving sharply at `-O3` (Section
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
| Direction | 5.251 | 69.535 | 3.541 | 3.846 | — | — |
| **TOTAL** | **175.089** | **157.822** | **21.029** | **21.448** | **162.530** | **129.435** |
| Binary size | 398 KB | 383 KB | 384 KB | 384 KB | — | — |

¹ **Not yet measured.** RVV Direction-stage timing and binary size for all
five builds were not captured in this benchmark run. These need to be filled
in before this report is final — see Section 6 (Open Items).

---

## 3. Stage-by-Stage Discussion

### 3.1 Gaussian Blur (5×5 convolution)

This stage is the largest single contributor to the RVV vs. `-O3`
regression discussed in Section 4.

- `-O0 → -O2`: 118.5 ms → 35.9 ms (**3.30×**).
- `-O2 → -O3`: 35.9 ms → 13.1 ms (further **2.74×**).
- Auto-vectorization: 13.4 ms — essentially the same as plain `-O3`
  (13.1 ms), marginally slower. Close enough that this should not be
  over-read as a meaningful difference without more samples (Section 6).
- RVV intrinsics: 124.5 ms at VLEN=128, 96.8 ms at VLEN=256 — **9.5× and
  7.4× slower than `-O3` scalar respectively.** Improves with VLEN as
  expected (more lanes per `vsetvli` call, fewer strip-mining iterations),
  but remains far above the scalar `-O3` time at both tested VLEN values.

### 3.2 Sobel Gx/Gy

- `-O0 → -O2`: 48.9 ms → 24.9 ms (**1.97×**).
- `-O2 → -O3`: 24.9 ms → 4.4 ms (**5.68×**) — a clear improvement, unlike
  Gaussian's more modest `-O2→-O3` gain. `-O3`'s additional unrolling and
  vectorization passes appear to help this kernel more than Gaussian's.
- Auto-vectorization: 4.2 ms — essentially matching `-O3` (4.4 ms).
- RVV: 33.9 ms at VLEN=128, 28.8 ms at VLEN=256 — **7.75× and 6.58× slower
  than `-O3` scalar.** Sobel shows the same qualitative pattern as Gaussian
  (RVV losing badly to `-O3`), even though Sobel's RVV/scalar gap is
  somewhat smaller than Gaussian's.

### 3.3 Magnitude (L1)

- **`-O0 → -O2` is a regression, not an improvement**: 2.4 ms → 27.5 ms
  (**11.3× slower**). This is the opposite of every other stage's
  `-O0→-O2` direction and needs explanation before this report is final —
  see Section 6. A plausible cause is that `-O0`'s number is itself
  artificially low rather than `-O2` being abnormally slow (see the next
  bullet), but this hasn't been confirmed.
- `-O2 → -O3`: 27.5 ms → 0.019 ms — a drop of roughly three orders of
  magnitude. This is **not credible as real per-iteration work** for an
  L1-norm computation over a 512×512 image; it is far faster than even the
  RVV hardware-vectorized version (4.2 ms at VLEN=128). The likely
  explanation is dead-code elimination at `-O3`: if the magnitude output is
  not consumed by anything the compiler considers observable, `-O3` can
  legally delete most or all of the actual computation. **This needs to be
  verified before being reported as a real result** — see Section 6. If
  `-O3` is eliminating the computation, it raises the question of whether
  `-O0`'s unusually low 2.4 ms figure (lower than `-O2`'s 27.5 ms, which is
  backwards) might reflect a similar issue rather than a real measurement.
- RVV (4.2 / 3.9 ms at VLEN 128/256) is a believable result on its own
  terms — consistent with L1 magnitude being a simple add-of-absolute-values
  that vectorizes well — but cannot be meaningfully compared to the
  suspect `-O3`/Auto-vec numbers until Section 6's Item 3 is resolved.

### 3.4 Direction

- **`-O0 → -O2` is also a regression here**: 5.3 ms → 69.5 ms
  (**13.2× slower**) — the same backwards pattern seen in Magnitude (3.3),
  and on the same two stages. This is unlikely to be coincidence; both
  regressions appearing on the same two stages, while Gaussian and Sobel
  behave normally at `-O2`, suggests a cause specific to how Magnitude and
  Direction are computed or measured, not general `-O2` misbehavior. Worth
  investigating before drawing any conclusion from the `-O0` or `-O2`
  Direction/Magnitude numbers (Section 6).
- `-O2 → -O3`: 69.5 ms → 3.5 ms (**19.6×**) — Direction recovers sharply at
  `-O3`, consistent with whatever caused the `-O2` regression being
  specific to `-O2`'s optimization choices rather than a permanent property
  of the Direction code.
- No RVV implementation has been benchmarked for this stage. Whether
  Direction was deliberately left scalar (a defensible call, given it's a
  small fraction of total time at `-O3`/Auto-vec: 3.5–3.8 ms out of
  ~21 ms total) or simply not yet ported to RVV is an open item — see
  Section 6. The report should not claim one or the other without
  confirming which it is.

---

## 4. Primary Finding: RVV Underperforms Compiler Optimization

Across the full pipeline:

| Configuration | Total (ms/iter) | vs. -O3 |
|---|---|---|
| -O3 (scalar) | 21.029 | 1.00× (baseline) |
| Auto-vectorized | 21.448 | 0.98× (2% slower) |
| RVV, VLEN=128 | 162.530 | 0.13× (7.73× slower) |
| RVV, VLEN=256 | 129.435 | 0.16× (6.16× slower) |

Hand-written RVV is slower than plain `-O3` at both tested vector lengths,
and the gap closes only by increasing VLEN, not by anything in the RVV
implementation itself improving. This is the opposite of the expected
outcome for hand-vectorized intrinsics and is treated here as the central
result to investigate, not a footnote.

**Gaussian is responsible for the bulk of this gap**: it is 76.6% of total
RVV time at VLEN=128 and 74.8% at VLEN=256 (vs. 62.3% of `-O3` scalar
time), and RVV Gaussian time (96.8–124.5 ms) is 7.4–9.5× the `-O3` scalar
Gaussian time (13.1 ms) — a larger gap than Sobel's 6.6–7.75×, making
Gaussian the higher-priority target if the RVV implementation is revisited.

### Candidate explanations

1. **`vsetvli` placement / strip-mining granularity.** If vector length is
   reconfigured more often than necessary — e.g. inside an inner loop rather
   than once per strip — the fixed per-call overhead of vector configuration
   could dominate at small VLEN, which matches the observation that the RVV
   penalty shrinks (but does not disappear) as VLEN grows from 128 to 256.
2. **LMUL selection.** A 5×5 kernel with widened accumulation (u8→i16/i32)
   needs register-pressure tradeoffs the scalar compiler doesn't have to
   make. A poorly chosen LMUL could mean far more loop iterations per row
   than necessary at low VLEN.
3. **Redundant memory traffic.** A direct 25-tap-per-pixel implementation of
   the 2D 5×5 Gaussian re-reads heavily overlapping input data across
   adjacent output pixels. A true Gaussian kernel is separable (5+5 taps via
   two 1D passes instead of 25 taps via one 2D pass); if the RVV kernel does
   the full 2D convolution while the scalar/auto-vec path benefits from a
   pattern GCC recognizes more cheaply, that alone could explain a multi-×
   gap.
4. **GCC may simply be doing something the hand-written kernel isn't** —
   confirmed by checking `-fopt-info-vec-all` output for the Gaussian loop
   against the actual RVV intrinsic sequence side by side.


---

## 5. Binary Size

| Build | Size |
|---|---|
| -O0 | 398 KB |
| -O2 | 383 KB |
| -O3 | 384 KB |
| Auto-vec | 384 KB |
| RVV | - |

`-O0` produces the largest binary despite being the slowest — expected,
since `-O0` preserves the most debug-friendly, unoptimized code structure
and does no dead-code elimination or inlining-driven size reduction. Size is
essentially flat from `-O2` onward (383–384 KB), suggesting binary size is
not a useful differentiator between optimization levels for this workload;
the report should not over-read significance into a 1 KB difference.

---

## 6. Open Items (must be resolved before this report is final)

1. **RVV Direction stage**: timing not measured. Determine whether this is
   because Direction was deliberately left scalar, or because it has not yet
   been ported — and update Section 3.4 and the table accordingly.
2. **Binary size for all five builds**: not measured for this run. Run the
   `ls -la` / `size` check used in earlier benchmark runs against each of
   `-O0`, `-O2`, `-O3`, Auto-vec, and RVV builds.
3. **Magnitude/Direction `-O0→-O2` regression (Sections 3.3, 3.4)**: both
   stages get *slower* at `-O2` than at `-O0`, by 11.3× and 13.2×
   respectively — the only two stages in the table that behave this way.
   This needs an actual explanation, not speculation. Worth checking: (a)
   whether the `-O0` numbers for these two stages are themselves unusually
   low rather than `-O2` being unusually slow, (b) whether something in how
   these two stages are timed differs from Gaussian/Sobel's timing, (c)
   `-fopt-info-vec-all` / disassembly comparison between `-O0` and `-O2` for
   both stages specifically.
4. **Magnitude dead-code-elimination check (Section 3.3)**: confirm whether
   the near-zero `-O3`/Auto-vec magnitude times (0.019 ms, 0.016 ms) reflect
   real elimination of unused computation, and if so, document how output
   correctness was verified despite the benchmark loop not exercising the
   full computation.
5. **Gaussian RVV regression (Section 4)**: review the actual RVV
   intrinsic implementation against the four candidate explanations in
   Section 4. This is the most important open item — it is very likely a
   specific, fixable issue (vsetvli placement, LMUL, or redundant loads)
   rather than a fundamental limitation of RVV for this workload, and the
   report's credibility depends on this section reflecting an actual
   diagnosis rather than speculation.
6. **Disassembly excerpts**: the project deliverables call for disassembly
   excerpts in the optimization report. None are included yet — add
   relevant excerpts (e.g. the vectorized Gaussian inner loop at `-O3`
   auto-vec vs. the hand-written RVV version, and the `-O0` vs `-O2`
   comparison for Magnitude/Direction from Item 3) once those items are
   investigated, since the same excerpts needed for diagnosis are the ones
   the report should show.
7. **VLEN=512**: not tested in this run (only 128 and 256 are present).
   Add if a VLEN=512 measurement becomes available, to match the broader
   VLEN sweep the project spec asks for.

---

## 7. Correctness Note

This report covers performance only. It assumes — but does not itself
demonstrate — that all configurations above produce numerically equivalent
output to the scalar baseline. That equivalence (scalar vs. RVV, and across
tested VLEN values) should be confirmed via the Phase 6 assert-based QEMU
equivalence tests, with a pointer to those test results included here or in
the README before this report is considered complete.