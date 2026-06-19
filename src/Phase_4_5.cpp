// =============================================================================
//  Phase_4.cpp  —  Canny Edge Detection: Compiler Optimization Sweep
//  Compiles with: riscv64-unknown-elf-g++ -march=rv64gcv -mabi=lp64d -std=c++17
//  Runs on:       qemu-riscv64 -cpu rv64,v=true,vlen=128,elen=64
// =============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>      // printf only (goes through QEMU semihosting / syscall)
#include <cstring>     // memset
#include <cstdlib>     // abs, aligned_alloc, free
#include <time.h>      // struct timespec, clockid_t

// -----------------------------------------------------------------------------
//  QEMU user-mode syscall layer
//
//  QEMU user-mode intercepts every "ecall" instruction and forwards it to the
//  host Linux kernel.  This means:
//    - fopen / fread / fwrite  would also work (newlib wraps them into ecall)
//    - BUT clock_gettime via newlib may not link cleanly on bare-metal newlib
//  So we call the Linux syscalls directly to be safe and explicit.
//
//  Why not rdtime CSR?
//    QEMU's virtual timer ticks at 10 MHz and does NOT reflect instruction
//    execution time.  The Linux clock_gettime syscall returns the host's real
//    CLOCK_MONOTONIC — accurate wall-clock time — making relative comparisons
//    (O0 vs O2 vs O3 vs RVV) valid and consistent.
// -----------------------------------------------------------------------------

// RISC-V Linux syscall numbers (from linux/unistd.h for riscv)
#define SYS_clock_gettime  113
#define SYS_openat          56
#define SYS_close           57
#define SYS_read            63
#define SYS_write           64

#define AT_FDCWD           -100   // dirfd: use current working directory
#define O_RDONLY             0
#define O_WRONLY             1
#define O_CREAT           0x40
#define O_TRUNC          0x200

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC      1
#endif

// Issue a Linux syscall via the RISC-V "ecall" instruction.
// QEMU intercepts ecall and forwards it to the host kernel.
static long rv_syscall(long num, long a0, long a1, long a2, long a3 = 0)
{
    register long r_num asm("a7") = num;
    register long r_a0  asm("a0") = a0;
    register long r_a1  asm("a1") = a1;
    register long r_a2  asm("a2") = a2;
    register long r_a3  asm("a3") = a3;
    asm volatile(
        "ecall"
        : "+r"(r_a0)
        : "r"(r_num), "r"(r_a1), "r"(r_a2), "r"(r_a3)
        : "memory"
    );
    return r_a0;
}

// clock_gettime — forwarded to host via QEMU syscall translation
static int rv_clock_gettime(clockid_t clk, struct timespec* tp)
{
    return (int)rv_syscall(SYS_clock_gettime, (long)clk, (long)tp, 0);
}

// File I/O — forwarded to host filesystem via QEMU syscall translation.
// This is how Phase_4 reads test_image.raw that was generated in Phase 2.
static int rv_open(const char* path, int flags, int mode = 0)
{
    return (int)rv_syscall(SYS_openat, AT_FDCWD, (long)path, flags, mode);
}
static void rv_close(int fd)  { rv_syscall(SYS_close, fd, 0, 0); }
static long rv_read (int fd, void*       buf, size_t n) { return rv_syscall(SYS_read,  fd, (long)buf, (long)n); }
static long rv_write(int fd, const void* buf, size_t n) { return rv_syscall(SYS_write, fd, (long)buf, (long)n); }

// Elapsed time in milliseconds between two timespec samples
static double elapsed_ms(const struct timespec& s, const struct timespec& e)
{
    return (e.tv_sec - s.tv_sec) * 1000.0
         + (e.tv_nsec - s.tv_nsec) / 1.0e6;
}

// =============================================================================
//  Image I/O
//
//  Raw grayscale format: exactly width*height bytes, one byte per pixel.
//  No headers, no compression — eliminates library dependencies.
//  Buffers use aligned_alloc(64, ...) so the compiler can vectorize
//  loads/stores and RVV intrinsics can use aligned load instructions.
// =============================================================================

// Read a raw grayscale image from the host filesystem via QEMU syscall.
// Returns true on success.  On failure prints the path and returns false.
static bool readRaw(const char* path, uint8_t* buf, int w, int h)
{
    int fd = rv_open(path, O_RDONLY);
    if (fd < 0) {
        // Print the path so the user knows which file is missing
        rv_write(1, path, __builtin_strlen(path));
        rv_write(1, ":\n", 2);
        return false;
    }
    rv_read(fd, buf, (size_t)w * h);
    rv_close(fd);
    return true;
}

// Write a raw grayscale image to the host filesystem via QEMU syscall.
static void writeRaw(const char* path, const uint8_t* buf, int w, int h)
{
    int fd = rv_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        rv_write(1, path, __builtin_strlen(path));
        rv_write(1, ": write failed\n", 15);
        return;
    }
    rv_write(fd, buf, (size_t)w * h);
    rv_close(fd);
}

// =============================================================================
//  Pipeline Stage 1: Gaussian Blur (5x5, sigma≈1.0)
//
//  Template parameters:
//    PixelType      — uint8_t  (input/output pixel type)
//    AccumType      — int32_t  (accumulator; prevents overflow during
//                               multiply-accumulate: max = 255*41*25 ≈ 260K)
//
//  Boundary handling: zero-padding (out-of-bounds pixels treated as 0).
//  This is the simplest approach and makes vectorization easier in Phase 6.
//
//  Integer kernel coefficients sum to 273.  Each pixel is multiplied by its
//  coefficient, accumulated into AccumType, then divided by 273 and clamped.
// =============================================================================

template <typename PixelType, typename AccumType>
void applyGaussianBlur(const PixelType* __restrict__ input,
                             PixelType* __restrict__ output,
                       int width, int height)
{
    // Standard 5x5 Gaussian kernel (integer approximation, sigma≈1.0)
    static const int kernel[5][5] = {
        { 1,  4,  7,  4,  1},
        { 4, 16, 26, 16,  4},
        { 7, 26, 41, 26,  7},
        { 4, 16, 26, 16,  4},
        { 1,  4,  7,  4,  1}
    };
    const AccumType kernel_sum = 273;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            AccumType sum = 0;
            for (int ky = -2; ky <= 2; ky++) {
                for (int kx = -2; kx <= 2; kx++) {
                    int ny = y + ky, nx = x + kx;
                    // Zero-padding: pixels outside the image boundary = 0
                    if (ny >= 0 && ny < height && nx >= 0 && nx < width)
                        sum += (AccumType)input[ny * width + nx]
                             * kernel[ky + 2][kx + 2];
                }
            }
            output[y * width + x] = (PixelType)(sum / kernel_sum);
        }
    }
}

// =============================================================================
//  Pipeline Stage 2: Sobel Gradient (Gx, Gy)
//
//  Applies two 3x3 Sobel kernels to the blurred image.
//  Output: two separate int16_t arrays (Structure-of-Arrays layout).
//
//  Why int16_t?  Max Sobel output for 8-bit pixels on a 3x3 kernel:
//    max|Gx| = 4*255 = 1020  →  fits in int16_t (range ±32767)
//
//  Why SoA (separate Gx, Gy arrays)?
//    When vectorizing magnitude computation, SoA lets us load consecutive
//    Gx values with a single vector load.  AoS would require gather ops.
// =============================================================================

void applySobel(const uint8_t*  __restrict__ blurred,
                      int16_t* __restrict__ Gx,
                      int16_t* __restrict__ Gy,
                int width, int height)
{
    static const int Kx[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
    static const int Ky[3][3] = {{-1,-2,-1}, { 0, 0, 0}, { 1, 2, 1}};

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int32_t gx = 0, gy = 0;
            for (int ky = -1; ky <= 1; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    int ny = y + ky, nx = x + kx;
                    // Zero-padding for boundary pixels
                    int pv = (ny >= 0 && ny < height && nx >= 0 && nx < width)
                           ? blurred[ny * width + nx] : 0;
                    gx += pv * Kx[ky + 1][kx + 1];
                    gy += pv * Ky[ky + 1][kx + 1];
                }
            }
            Gx[y * width + x] = (int16_t)gx;
            Gy[y * width + x] = (int16_t)gy;
        }
    }
}

// =============================================================================
//  Pipeline Stage 3: Gradient Magnitude (L1 norm)
//
//  L1 norm: |Gx| + |Gy|
//    - Integer only, fast
//    - Slight overestimate of diagonal edges (vs true Euclidean)
//    - Clamped to [0, 255] (no normalization pass needed for L1)
//
//  Note: A proper two-pass normalization (find max, then scale) would give
//  better dynamic range but requires reading the data twice.  For Phase 4
//  benchmarking the simple clamp is sufficient; Phase 6 RVV will use the
//  two-pass approach with vredmax reduction.
// =============================================================================

void computeMagnitude(const int16_t* __restrict__ Gx,
                      const int16_t* __restrict__ Gy,
                            uint8_t* __restrict__ mag,
                      int width, int height)
{
    for (int i = 0; i < width * height; i++) {
        int32_t m = abs((int32_t)Gx[i]) + abs((int32_t)Gy[i]);
        mag[i] = (uint8_t)(m > 255 ? 255 : m);
    }
}

// =============================================================================
//  Pipeline Stage 4: Gradient Direction (quantized to 4 values)
//
//  Direction is quantized to: 0=horizontal, 1=diagonal/, 2=vertical, 3=diagonal\.
//
//  No atan2() needed — integer cross-multiplication instead:
//    tan(22.5°) ≈ 0.414 ≈ 2/5   →  ay*5 < ax*2  means angle < 22.5° (mostly horizontal)
//    tan(67.5°) ≈ 2.414 ≈ 12/5  →  ay*5 > ax*12 means angle > 67.5° (mostly vertical)
//
//  This avoids floating-point division — a common embedded optimization.
// =============================================================================

void computeDirection(const int16_t* __restrict__ Gx,
                      const int16_t* __restrict__ Gy,
                            uint8_t* __restrict__ dir,
                      int width, int height)
{
    for (int i = 0; i < width * height; i++) {
        int32_t ax = abs((int32_t)Gx[i]);
        int32_t ay = abs((int32_t)Gy[i]);
        uint8_t angle = 0;
        if (ax != 0 || ay != 0) {
            if      (ay * 5  < ax * 2)  angle = 0;   // mostly horizontal
            else if (ay * 5  > ax * 12) angle = 2;   // mostly vertical
            else if ((Gx[i] > 0 && Gy[i] > 0) ||
                     (Gx[i] < 0 && Gy[i] < 0)) angle = 3;  // diagonal '\'
            else                                angle = 1;  // diagonal '/'
        }
        dir[i] = angle;
    }
}

// =============================================================================
//  main — Benchmark each pipeline stage across 100 iterations
// =============================================================================

int main()
{
    const int WIDTH  = 512;
    const int HEIGHT = 512;
    const int N      = WIDTH * HEIGHT;
    const int ITERS  = 100;

    // Allocate 64-byte aligned buffers.
    // aligned_alloc(64, ...) required for RVV aligned load intrinsics in Phase 6.
    uint8_t*  input   = (uint8_t* )aligned_alloc(64, (size_t)N * sizeof(uint8_t));
    uint8_t*  blurred = (uint8_t* )aligned_alloc(64, (size_t)N * sizeof(uint8_t));
    int16_t*  Gx      = (int16_t* )aligned_alloc(64, (size_t)N * sizeof(int16_t));
    int16_t*  Gy      = (int16_t* )aligned_alloc(64, (size_t)N * sizeof(int16_t));
    uint8_t*  mag     = (uint8_t* )aligned_alloc(64, (size_t)N * sizeof(uint8_t));
    uint8_t*  dir_buf = (uint8_t* )aligned_alloc(64, (size_t)N * sizeof(uint8_t));

    printf("=== Phase 4: Compiler Optimization Sweep ===\n");
    printf("Image: %dx%d   Iterations: %d\n\n", WIDTH, HEIGHT, ITERS);

    // -------------------------------------------------------------------------
    //  Load test image generated by Phase 2 image generator.
    //  Path is relative to the project root (where make is run from).
    //  On failure: fall back to a synthetic gradient so benchmarking still runs.
    // -------------------------------------------------------------------------
    if (!readRaw("Results/test_image.raw", input, WIDTH, HEIGHT)) {
        printf("Using synthetic gradient image.\n\n");
        for (int y = 0; y < HEIGHT; y++)
            for (int x = 0; x < WIDTH; x++)
                input[y * WIDTH + x] = (uint8_t)((x + y) % 256);
    }

    // -------------------------------------------------------------------------
    //  Benchmark each stage using clock_gettime(CLOCK_MONOTONIC) via ecall.
    //  QEMU forwards this to the host's real monotonic clock.
    //  Running 100 iterations stabilises the measurement (QEMU is not
    //  cycle-accurate; wall-clock averages are the meaningful metric).
    // -------------------------------------------------------------------------
    struct timespec t0, t1;
    double tg, ts, tm, td;

    // --- Gaussian Blur ---
    rv_clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < ITERS; i++)
        applyGaussianBlur<uint8_t, int32_t>(input, blurred, WIDTH, HEIGHT);
    rv_clock_gettime(CLOCK_MONOTONIC, &t1);
    tg = elapsed_ms(t0, t1) / ITERS;

    // --- Sobel ---
    rv_clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < ITERS; i++)
        applySobel(blurred, Gx, Gy, WIDTH, HEIGHT);
    rv_clock_gettime(CLOCK_MONOTONIC, &t1);
    ts = elapsed_ms(t0, t1) / ITERS;

    // --- Magnitude ---
    rv_clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < ITERS; i++)
        computeMagnitude(Gx, Gy, mag, WIDTH, HEIGHT);
    rv_clock_gettime(CLOCK_MONOTONIC, &t1);
    tm = elapsed_ms(t0, t1) / ITERS;

    // --- Direction ---
    rv_clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < ITERS; i++)
        computeDirection(Gx, Gy, dir_buf, WIDTH, HEIGHT);
    rv_clock_gettime(CLOCK_MONOTONIC, &t1);
    td = elapsed_ms(t0, t1) / ITERS;

    // Save outputs to Results/ for visual verification
    writeRaw("Results/output_magnitude_phase4.raw", mag,     WIDTH, HEIGHT);
    writeRaw("Results/output_direction_phase4.raw",  dir_buf, WIDTH, HEIGHT);

    // -------------------------------------------------------------------------
    //  Print results table
    // -------------------------------------------------------------------------
    double total = tg + ts + tm + td;
    printf("+-----------------------+----------+-----------+\n");
    printf("|  Stage                |  ms/iter |  %%total   |\n");
    printf("+-----------------------+----------+-----------+\n");
    printf("|  Gaussian 5x5         | %8.3f |  %6.1f%%  |\n", tg, 100.0*tg/total);
    printf("|  Sobel Gx/Gy          | %8.3f |  %6.1f%%  |\n", ts, 100.0*ts/total);
    printf("|  Magnitude (L1)       | %8.3f |  %6.1f%%  |\n", tm, 100.0*tm/total);
    printf("|  Direction            | %8.3f |  %6.1f%%  |\n", td, 100.0*td/total);
    printf("+-----------------------+----------+-----------+\n");
    printf("|  TOTAL                | %8.3f |  100.0%%   |\n", total);
    printf("+-----------------------+----------+-----------+\n");

    free(input); free(blurred); free(Gx); free(Gy); free(mag); free(dir_buf);
    return 0;
}