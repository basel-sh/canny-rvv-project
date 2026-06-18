# Canny Edge Detection with RISC-V Vector (RVV) Optimization

A complete engineering workflow for developing, validating, and preparing a **Canny Edge Detection** application for **RISC-V Vector Extension (RVV)** acceleration.

The project begins with verification of the RVV toolchain and emulation environment, proceeds through implementation of a mathematically correct scalar baseline of the Canny pipeline, and concludes with modular unit testing using Google Test.

The ultimate objective is to establish a trusted baseline before moving toward vectorized acceleration and performance optimization on RISC-V hardware.

---

## Project Overview

This repository is divided into three major phases:

| Phase   | Purpose                                                    |
| ------- | ---------------------------------------------------------- |
| Phase 1 | Verify RVV compiler, intrinsics, and QEMU vector emulation |
| Phase 2 | Implement a complete scalar Canny Edge Detection pipeline  |
| Phase 3 | Validate correctness using automated unit testing          |

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
│   ├── output_magnitude_L1.raw
│   ├── output_magnitude_L2.raw
│   ├── final_edges_L1.png
│   └── final_edges_L2.png
│
├── scripts/
│   └── view_output.py
│
└── src/
    ├── Phase_1.cpp
    ├── rvv_verify.cpp
    ├── Phase_2.cpp
    └── Phase_3.cpp
```

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
