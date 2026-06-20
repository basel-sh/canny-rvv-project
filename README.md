# Canny Edge Detection with RISC-V Vector (RVV) Optimization

A complete engineering workflow for developing, validating, and preparing a **Canny Edge Detection** application for **RISC-V Vector Extension (RVV)** acceleration.

The project begins with verification of the RVV toolchain and emulation environment, proceeds through implementation of a mathematically correct scalar baseline of the Canny pipeline, and concludes with modular unit testing using Google Test. The final phases introduce aggressive compiler optimization profiling, bare-metal RVV intrinsic vectorization, and advanced algorithmic filtering.

The ultimate objective is to establish a trusted baseline before moving toward vectorized acceleration and performance optimization on RISC-V hardware.

---

## Project Overview

This repository is divided into several major phases:

| Phase   | Purpose                                                    |
| ------- | ---------------------------------------------------------- |
| Phase 1 | Verify RVV compiler, intrinsics, and QEMU vector emulation |
| Phase 2 | Implement a complete scalar Canny Edge Detection pipeline  |
| Phase 3 | Validate correctness using automated unit testing          |
| Phase 4 | Compiler optimization sweep and benchmark timing (-O0 to -O3) |
| Phase 5 | Profiling and hotspot identification (Amdahl's Law)        |
| Phase 6 | Bare-metal RVV Intrinsic Optimization (Vectorization)      |
| Phase 7 | Final Analysis, VLEN Sweeps, and Documentation             |
| Bonus   | Non-Maximum Suppression (NMS), Hysteresis, and CI/CD Pipeline |

---

## Repository Structure

```text
canny-rvv-project/
│
├── build/
│   └── Generated executables
│
├── Results/
│   ├── PCB.jpg
│   ├── test_image.raw
│   ├── output_final_bonus.raw
│   ├── output_magnitude_L1.raw
│   ├── output_magnitude_L2.raw
│   ├── final_edges_L1.png
│   ├── final_edges_L2.png
│   └── final_edges_bonus.png
│
├── scripts/
│   └── view_output.py
│
└── src/
    ├── Phase_1.cpp
    ├── rvv_verify.cpp
    ├── Phase_2.cpp
    ├── Phase_3.cpp
    ├── Phase_4_5.cpp
    ├── Phase_6.cpp
    └── Phase_bonus.cpp
---

## Project Objectives

The project was developed to:

- Verify RVV compiler and emulator support
- Learn and validate RVV intrinsics
- Build a correct scalar baseline implementation
- Compare L₁ and L₂ gradient magnitude calculations
- Validate algorithm correctness using unit tests
- Prepare the codebase for future RVV vectorization

---

# Phase 1 — RVV Environment Verification

## Purpose

Before vectorizing any image processing algorithm, it is essential to verify that:

- The RVV compiler works correctly
- RVV intrinsics compile successfully
- QEMU correctly emulates RVV hardware
- Vector registers operate as expected

---

## File: `Phase_1.cpp`

A simple RVV smoke test.

### What it does

1. Creates two integer arrays
2. Loads them into RVV vector registers
3. Performs vector addition
4. Stores the result back to memory
5. Prints the final output

Example:

```text
[1 2 3 4]
+
[10 20 30 40]
=
[11 22 33 44]
```

This confirms:

- Vector loads work
- Vector arithmetic works
- Vector stores work
- RVV instructions execute correctly


| Stage | -O0 | -O2 | -O3 | Auto-vec | RVV (VLEN=128) | RVV (VLEN=256) |
|---|---|---|---|---|---|---|
| Gaussian 5×5 | 118.491 | 35.882 | 13.094 | 13.423 | 124.453 | 96.773 |
| Sobel Gx/Gy | 48.914 | 24.862 | 4.375 | 4.163 | 33.889 | 28.783 |
| Magnitude (L1) | 2.433 | 27.544 | 0.019 | 0.016 | 4.189 | 3.879 |
| Direction | 5.251 | 69.535 | 3.541 | 3.846 | — | — |
| **TOTAL** | **175.089** | **157.822** | **21.029** | **21.448** | **162.530** | **129.435** |
| Binary size | — | — | — | — | — | — |

---

## File: `rvv_verify.cpp`

A complete RVV validation suite.

### Included Tests

#### Test 1 — Dynamic Vector Length

Validates:

```cpp
__riscv_vsetvl_e32m1()
```

Ensures proper strip-mining behavior.

#### Test 2 — Integer Vector Addition

Validates:

```cpp
__riscv_vadd_vv_i32m1()
```

Confirms correct integer arithmetic.

#### Test 3 — Floating Point Vector Addition

Validates:

```cpp
__riscv_vfadd_vv_f32m1()
```

Confirms floating-point vector operations.

### Example Output

```text
[PASS] test_vsetvl
[PASS] test_vadd_i32
[PASS] test_vfadd_f32

Result: 3/3 tests passed

RVV environment is ready!
```

---

## Compile & Run Phase 1

Move into the source directory:

```bash
cd src
```

### Run Phase_1.cpp

```bash
riscv64-unknown-elf-g++ -march=rv64gcv -mabi=lp64d -O2 -std=c++17 Phase_1.cpp -o ../build/rvv_verify

/home/basel/rvv/qemu/build/qemu-riscv64 -cpu rv64,v=true,vlen=128,elen=64 ../build/rvv_verify
```

### Run rvv_verify.cpp

```bash
riscv64-unknown-elf-g++ -march=rv64gcv -mabi=lp64d -O2 -std=c++17 rvv_verify.cpp -o ../build/rvv_verify

/home/basel/rvv/qemu/build/qemu-riscv64 -cpu rv64,v=true,vlen=128,elen=64 ../build/rvv_verify
```

---

# Phase 2 — Scalar Canny Edge Detection Pipeline

## Purpose

This phase implements a complete scalar baseline for edge detection.

The scalar version serves as the ground-truth reference before RVV optimization.

---

## Processing Pipeline

```text
Input PCB Image
       │
       ▼
Convert to RAW Grayscale
       │
       ▼
Gaussian Blur (5×5)
       │
       ▼
Sobel Gradient Calculation
       │
       ▼
Gradient Magnitude
       │
       ├── L1 Norm
       │
       └── L2 Norm
       │
       ▼
Generate Edge Maps
```

---

## Stage 1 — Image Preparation

The PCB image is converted into a raw grayscale image:

```text
512 × 512 pixels
8-bit grayscale
```

Input:

```text
Results/PCB.jpg
```

Generated:

```text
Results/test_image.raw
```

---

## Stage 2 — Gaussian Blur

A 5×5 Gaussian filter is applied to reduce noise and improve edge detection stability.

Kernel normalization factor:

```text
273
```

---

## Stage 3 — Sobel Gradient Computation

The Sobel operator computes:

```text
Gx → Horizontal Gradient
Gy → Vertical Gradient
```

These gradients are used to determine edge strength and direction.

---

## Stage 4 — Gradient Magnitude

Two magnitude calculation methods are implemented.

### L₁ Norm (Manhattan Distance)

```math
|G_x| + |G_y|
```

Advantages:

- Faster
- No square root
- Better suited for embedded systems

---

### L₂ Norm (Euclidean Distance)

```math
\sqrt{G_x^2 + G_y^2}
```

Advantages:

- More mathematically accurate
- Standard edge magnitude metric

---

## Stage 5 — Direction Quantization

Gradient directions are classified into:

```text
0°
45°
90°
135°
```

---

## Generated Outputs

Raw outputs:

```text
Results/output_magnitude_L1.raw
Results/output_magnitude_L2.raw
```

Visual outputs:

```text
Results/final_edges_L1.png
Results/final_edges_L2.png
```

---

## Compile & Run Phase 2

(optional) add the disared pic in the results folder

Move into the source directory:

```bash
cd src
```

### Step 1 — Convert PCB Image

```bash
convert ../Results/PCB.jpg -resize 512x512! -colorspace gray -depth 8 gray:../Results/test_image.raw
```

### Step 2 — Compile

```bash
g++ -O3 Phase_2.cpp -o scalar_native
```

### Step 3 — Run

```bash
./scalar_native
```

### Step 4 — Generate PNG Outputs

```bash
python3 ../scripts/view_output.py
```

---

# Phase 3 — Google Test Validation

## Purpose

This phase verifies the correctness of the implemented image-processing algorithms using automated unit testing.

The tests ensure that each processing stage behaves exactly as expected.

---

## Included Tests

### Gaussian Filter Tests

#### Uniform Image Test

Verifies that a uniform image remains unchanged after filtering.

#### Impulse Response Test

Verifies proper Gaussian spreading behavior.

---

### Sobel Tests

#### Uniform Image

Expected:

```text
Zero Gradient
```

#### Vertical Edge

Expected:

```text
Direction = 0°
```

#### Horizontal Edge

Expected:

```text
Direction = 90°
```

#### Diagonal Edge

Expected:

```text
Direction = 45°
```

---

### Magnitude Validation

Verifies the mathematical property:

```text
L1 ≥ L2
```

for all tested gradient pairs.

Mathematically:

```math
|G_x| + |G_y| \ge \sqrt{G_x^2 + G_y^2}
```

---

## Compile & Run Phase 3

Move into the source directory:

```bash
cd src
```

### Compile

```bash
g++ Phase_3.cpp -lgtest -lgtest_main -pthread -o test_verify
```

### Run

```bash
./test_verify
```

---

# Complete Workflow

```text
PHASE 1
│
├── Verify RVV Toolchain
├── Verify RVV Intrinsics
└── Verify QEMU RVV Emulation
│
▼
PHASE 2
│
├── Load PCB Image
├── Gaussian Blur
├── Sobel Gradients
├── L1 Magnitude
├── L2 Magnitude
└── Generate Edge Maps
│
▼
PHASE 3
│
├── Unit Testing
├── Gaussian Validation
├── Sobel Validation
└── Magnitude Validation
│
▼
READY FOR RVV OPTIMIZATION
```

---

# Results

The project successfully:

- Verified the RVV software stack
- Built a fully functional scalar Canny Edge Detection pipeline
- Generated and compared L₁ and L₂ edge maps
- Validated algorithm correctness using Google Test
- Established a trusted baseline for future RVV acceleration

---

# Future Work

The next stage of the project is to replace the scalar image-processing kernels with RVV vectorized implementations and compare:

- Execution time
- Speedup
- Scalability
- Correctness against the validated scalar baseline

This will provide a quantitative evaluation of the benefits of RISC-V Vector Extensions for image-processing workloads.

---

# Phase 4 & 5 — Compiler Optimization Sweep and Profiling

## Purpose

Before writing custom vector instructions, it is critical to understand the limits of the compiler and identify the true bottlenecks in the program. 

**Phase 4 (Compiler Optimization Sweep)** evaluates the performance of the baseline scalar code across different GCC optimization levels (`-O0`, `-O2`, `-O3`) and analyzes the compiler's auto-vectorization capabilities.

**Phase 5 (Profiling)** applies **Amdahl's Law** to determine the percentage of execution time consumed by each stage of the Canny pipeline, identifying the "hotspots" that will yield the highest speedup when manually vectorized.

---

## Key Engineering Techniques Implemented

To ensure highly accurate benchmarking inside the QEMU emulator, several advanced Embedded Systems techniques were utilized:

1. **Direct System Calls (Syscall via `ecall`)**: Bypassed standard C++ timing/I/O libraries that fail or behave inaccurately in QEMU. Instead, direct inline assembly (`ecall`) was used to communicate directly with the host Linux kernel for accurate `clock_gettime(CLOCK_MONOTONIC)` measurements and raw file I/O.
2. **Memory Alignment**: Image buffers were allocated using `aligned_alloc(64, ...)` to ensure 64-byte alignment, satisfying strict compiler requirements for auto-vectorization and preparing the data structures for Phase 6 RVV intrinsics.
3. **Iterative Averaging**: Because QEMU is not cycle-accurate, each pipeline stage was placed in a 100-iteration loop. The accumulated wall-clock time was averaged to absorb OS scheduling noise and provide stable benchmarks.

---

## Official Benchmarking Results

After running the automated optimization sweep through QEMU, we collected the following performance metrics. Execution times represent the average wall-clock time per iteration (in milliseconds) over 100 iterations.

| Stage | `-O0` (Baseline) | `-O2` | `-O3` (Max Scalar) | Auto-vectorization |
| :--- | :--- | :--- | :--- | :--- |
| **Gaussian 5x5** | 71.3 ms | 22.7 ms | 9.0 ms | 9.0 ms |
| **Sobel Gx/Gy** | 34.5 ms | 15.3 ms | 2.9 ms | 2.9 ms |
| **Magnitude (L1)** | 2.1 ms | 12.3 ms | 0.01 ms | 0.01 ms |
| **Direction** | 4.8 ms | 39.3 ms | 2.7 ms | 2.6 ms |
| **TOTAL TIME** | **112.6 ms** | **89.6 ms** | **14.6 ms** | **14.5 ms** |
| **Binary Size** | 110 KB | 109 KB | 110 KB | 110 KB |
| **Vector Instructions**| 0 | 0 | 0 | 184 |

---

## Engineering Analysis & Profiling (Amdahl's Law)

Based on the collected data, we can draw several critical conclusions before moving to bare-metal RVV programming:

1. **The Power of the Compiler (`-O0` vs `-O3`)**
   Relying purely on GCC's `-O3` scalar optimizations yielded a massive **~7.7x overall speedup** (from 112.6 ms down to 14.6 ms) without altering a single line of C++ code.

2. **The Failure of Auto-Vectorization**
   Applying the `-ftree-vectorize` flag successfully forced the compiler to generate **184 vector instructions**. However, the execution time only dropped by a negligible 0.1 ms (from 14.6 ms to 14.5 ms). 
   *Conclusion:* The compiler struggles to efficiently auto-vectorize complex 2D memory access patterns (like sliding convolution windows). To get true RVV performance, we must write the vector instructions manually.

3. **Hotspot Identification**
   Looking at the `-O3` baseline profile, we calculated the exact percentage of time spent in each stage:
   * **Gaussian 5x5:** ~61.6% (9.0 ms)
   * **Sobel Gx/Gy:** ~19.8% (2.9 ms)
   * **Direction & Magnitude:** ~18.6% combined
   
   *Engineering Decision:* Over **81% of the execution time** is trapped inside the Gaussian and Sobel convolution filters. Therefore, Phase 6 will focus entirely on manually vectorizing these two specific kernels to achieve the highest possible return on investment.

---

## Compile & Run Phase 4 & 5

The benchmarking process is fully automated via the `Makefile`. Move into the root directory of the project:

```bash
cd canny-rvv-project
1. Run Baseline (No Optimization)

Bash



make sweep_O0

2. Run Moderate Optimization

Bash



make sweep_O2

3. Run Aggressive Optimization

Bash



make sweep_O3

4. Run Auto-Vectorization Analysis

Bash



make sweep_autovec


PHASE 1
│
├── Verify RVV Toolchain
├── Verify RVV Intrinsics
└── Verify QEMU RVV Emulation
│
▼
PHASE 2
│
├── Load PCB Image
├── Gaussian Blur
├── Sobel Gradients
├── L1 Magnitude
├── L2 Magnitude
└── Generate Edge Maps
│
▼
PHASE 3
│
├── Unit Testing
├── Gaussian Validation
├── Sobel Validation
└── Magnitude Validation
│
▼
PHASE 4 & 5
│
├── Compiler Optimization Sweep (-O0 to -O3)
├── Auto-Vectorization Analysis
├── Performance Profiling
└── Hotspot Identification (Amdahl's Law)
│
▼
READY FOR BARE-METAL RVV OPTIMIZATION (PHASE 6)