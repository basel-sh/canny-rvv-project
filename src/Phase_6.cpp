#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <time.h>
#include <riscv_vector.h>

using namespace std;

// =============================================================================
// Direct QEMU/Linux Syscall Interface (Bypasses broken Newlib File I/O)
// =============================================================================
#define SYS_clock_gettime  113
#define SYS_openat          56
#define SYS_close           57
#define SYS_read            63
#define SYS_write           64

#define AT_FDCWD           -100
#define O_RDONLY             0
#define O_WRONLY             1
#define O_CREAT           0x40
#define O_TRUNC          0x200

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC      1
#endif

static long rv_syscall(long num, long a0, long a1, long a2, long a3 = 0) {
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

static int rv_clock_gettime(clockid_t clk, struct timespec* tp) {
    return (int)rv_syscall(SYS_clock_gettime, (long)clk, (long)tp, 0);
}

static int rv_open(const char* path, int flags, int mode = 0) {
    return (int)rv_syscall(SYS_openat, AT_FDCWD, (long)path, flags, mode);
}
static void rv_close(int fd)  { rv_syscall(SYS_close, fd, 0, 0); }
static long rv_read (int fd, void* buf, size_t n) { return rv_syscall(SYS_read,  fd, (long)buf, (long)n); }
static long rv_write(int fd, const void* buf, size_t n) { return rv_syscall(SYS_write, fd, (long)buf, (long)n); }

static double elapsed_ms(const struct timespec& s, const struct timespec& e) {
    return (e.tv_sec - s.tv_sec) * 1000.0 + (e.tv_nsec - s.tv_nsec) / 1.0e6;
}

// Fixed Image I/O utilizing explicit low-level syscall wrappers
static bool readRaw(const char* path, uint8_t* buf, int w, int h) {
    int fd = rv_open(path, O_RDONLY);
    if (fd < 0) {
        rv_write(1, "CRITICAL ERROR: Could not open file: ", 37);
        rv_write(1, path, __builtin_strlen(path));
        rv_write(1, "\n", 1);
        return false;
    }
    rv_read(fd, buf, (size_t)w * h);
    rv_close(fd);
    return true;
}

static void writeRaw(const char* path, const uint8_t* buf, int w, int h) {
    int fd = rv_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    rv_write(fd, buf, (size_t)w * h);
    rv_close(fd);
}

// Canvas padding utility function
void createPaddedImage(const uint8_t* input, uint8_t* padded, int width, int height, int pad) {
    int p_width = width + 2 * pad;
    for (int y = 0; y < height; y++) {
        std::memcpy(&padded[(y + pad) * p_width + pad], &input[y * width], width);
    }
}

// --- Phase 6: Vectorized Gaussian Blur 5x5 ---
void applyGaussianBlur_RVV(const uint8_t* padded_input, uint8_t* output, int width, int height) {
    const int16_t kernel[5][5] = {
        {1, 4, 7, 4, 1}, {4, 16, 26, 16, 4}, {7, 26, 41, 26, 7}, {4, 16, 26, 16, 4}, {1, 4, 7, 4, 1}
    };
    const int32_t kernel_sum = 273;
    int p_width = width + 4;

    for (int y = 0; y < height; y++) {
        int x = 0;
        int pixels_left = width;
        
        while (pixels_left > 0) {
            size_t vl = __riscv_vsetvl_e8m1(pixels_left);
            vint32m4_t v_sum = __riscv_vmv_v_x_i32m4(0, vl);

            for (int ky = -2; ky <= 2; ky++) {
                int row_offset = (y + 2 + ky) * p_width;
                for (int kx = -2; kx <= 2; kx++) {
                    int16_t weight = kernel[ky + 2][kx + 2];
                    const uint8_t* ptr = &padded_input[row_offset + (x + 2 + kx)];
                    
                    vuint8m1_t v_u8 = __riscv_vle8_v_u8m1(ptr, vl);
                    vuint16m2_t v_u16 = __riscv_vwcvtu_x_x_v_u16m2(v_u8, vl);
                    vint16m2_t v_px = __riscv_vreinterpret_v_u16m2_i16m2(v_u16);
                    
                    v_sum = __riscv_vwmacc_vx_i32m4(v_sum, weight, v_px, vl);
                }
            }

           // Replace slow hardware division with fast multiply and right shift: (sum * 240) >> 16
            vint32m4_t v_mult = __riscv_vmul_vx_i32m4(v_sum, 240, vl);
            vint32m4_t v_div = __riscv_vsra_vx_i32m4(v_mult, 16, vl);
            vint16m2_t v_out16 = __riscv_vncvt_x_x_w_i16m2(v_div, vl);
            vint8m1_t v_out8_signed = __riscv_vncvt_x_x_w_i8m1(v_out16, vl);
            vuint8m1_t v_out8 = __riscv_vreinterpret_v_i8m1_u8m1(v_out8_signed);
            
            __riscv_vse8_v_u8m1(&output[y * width + x], v_out8, vl);

            x += vl;
            pixels_left -= vl;
        }
    }
}

// --- Phase 6: Vectorized Sobel Derivation ---
void applySobel_RVV(const uint8_t* padded_blurred, int16_t* Gx, int16_t* Gy, int width, int height) {
    const int16_t Kx[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
    const int16_t Ky[3][3] = {{-1, -2, -1}, {0, 0, 0}, {1, 2, 1}};
    int p_width = width + 2;

    for (int y = 0; y < height; y++) {
        int x = 0;
        int pixels_left = width;

        while (pixels_left > 0) {
            size_t vl = __riscv_vsetvl_e8m1(pixels_left);
            vint16m2_t v_gx = __riscv_vmv_v_x_i16m2(0, vl);
            vint16m2_t v_gy = __riscv_vmv_v_x_i16m2(0, vl);

            for (int ky = -1; ky <= 1; ky++) {
                int row_offset = (y + 1 + ky) * p_width;
                for (int kx = -1; kx <= 1; kx++) {
                    int16_t kx_w = Kx[ky + 1][kx + 1];
                    int16_t ky_w = Ky[ky + 1][kx + 1];

                    if (kx_w == 0 && ky_w == 0) continue;

                    const uint8_t* ptr = &padded_blurred[row_offset + (x + 1 + kx)];
                    
                    vuint8m1_t v_u8 = __riscv_vle8_v_u8m1(ptr, vl);
                    vint16m2_t v_px = __riscv_vreinterpret_v_u16m2_i16m2(__riscv_vwcvtu_x_x_v_u16m2(v_u8, vl));

                    if (kx_w != 0) v_gx = __riscv_vmacc_vx_i16m2(v_gx, kx_w, v_px, vl);
                    if (ky_w != 0) v_gy = __riscv_vmacc_vx_i16m2(v_gy, ky_w, v_px, vl);
                }
            }

            __riscv_vse16_v_i16m2(&Gx[y * width + x], v_gx, vl);
            __riscv_vse16_v_i16m2(&Gy[y * width + x], v_gy, vl);

            x += vl;
            pixels_left -= vl;
        }
    }
}

// --- Phase 6: Vectorized Magnitude calculation ---
void computeMagnitude_RVV(const int16_t* Gx, const int16_t* Gy, uint8_t* mag, int width, int height) {
    int total_elements = width * height;
    int idx = 0;

    while (total_elements > 0) {
        size_t vl = __riscv_vsetvl_e16m2(total_elements);

        vint16m2_t v_gx = __riscv_vle16_v_i16m2(&Gx[idx], vl);
        vint16m2_t v_gy = __riscv_vle16_v_i16m2(&Gy[idx], vl);

        vint16m2_t v_zero = __riscv_vmv_v_x_i16m2(0, vl);
        vint16m2_t v_neg_gx = __riscv_vsub_vv_i16m2(v_zero, v_gx, vl);
        vint16m2_t v_abs_gx = __riscv_vmax_vv_i16m2(v_gx, v_neg_gx, vl);

        vint16m2_t v_neg_gy = __riscv_vsub_vv_i16m2(v_zero, v_gy, vl);
        vint16m2_t v_abs_gy = __riscv_vmax_vv_i16m2(v_gy, v_neg_gy, vl);

        vint16m2_t v_mag16 = __riscv_vadd_vv_i16m2(v_abs_gx, v_abs_gy, vl);

        vint16m2_t v_max = __riscv_vmv_v_x_i16m2(255, vl);
        vint16m2_t v_sat16 = __riscv_vmin_vv_i16m2(v_mag16, v_max, vl);

        vint8m1_t v_out8_signed = __riscv_vncvt_x_x_w_i8m1(v_sat16, vl);
        vuint8m1_t v_out8 = __riscv_vreinterpret_v_i8m1_u8m1(v_out8_signed);

        __riscv_vse8_v_u8m1(&mag[idx], v_out8, vl);

        idx += vl;
        total_elements -= vl;
    }
}

int main() {
    const int WIDTH = 512, HEIGHT = 512, N = WIDTH * HEIGHT, ITERS = 100;

    // Use 64-byte aligned alloc blocks exactly like your friend's code
    uint8_t* input         = (uint8_t*)aligned_alloc(64, N * sizeof(uint8_t));
    uint8_t* blurred       = (uint8_t*)aligned_alloc(64, N * sizeof(uint8_t));
    uint8_t* mag           = (uint8_t*)aligned_alloc(64, N * sizeof(uint8_t));
    int16_t* Gx            = (int16_t*)aligned_alloc(64, N * sizeof(int16_t));
    int16_t* Gy            = (int16_t*)aligned_alloc(64, N * sizeof(int16_t));

    int p_width_g = WIDTH + 4;
    int p_height_g = HEIGHT + 4;
    uint8_t* padded_input  = (uint8_t*)aligned_alloc(64, p_width_g * p_height_g * sizeof(uint8_t));

    int p_width_s = WIDTH + 2;
    int p_height_s = HEIGHT + 2;
    uint8_t* padded_blurred = (uint8_t*)aligned_alloc(64, p_width_s * p_height_s * sizeof(uint8_t));

    printf("=== Phase 6: Fully Optimized RVV (Direct Syscall Implementation) ===\n");
    
    // Note: Since you are inside the 'src' folder, the relative path must be '../Results/...'
    if (!readRaw("../Results/test_image.raw", input, WIDTH, HEIGHT)) {
        return 1; 
    }
    printf("Successfully loaded real asset via direct filesystem ecall.\n");

    struct timespec t0, t1;
    double tg, ts, tm;

    // Clean padding context sweeps
    std::memset(padded_input, 0, p_width_g * p_height_g);
    createPaddedImage(input, padded_input, WIDTH, HEIGHT, 2);

    rv_clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < ITERS; i++) applyGaussianBlur_RVV(padded_input, blurred, WIDTH, HEIGHT);
    rv_clock_gettime(CLOCK_MONOTONIC, &t1); tg = elapsed_ms(t0, t1) / ITERS;

    std::memset(padded_blurred, 0, p_width_s * p_height_s);
    createPaddedImage(blurred, padded_blurred, WIDTH, HEIGHT, 1);

    rv_clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < ITERS; i++) applySobel_RVV(padded_blurred, Gx, Gy, WIDTH, HEIGHT);
    rv_clock_gettime(CLOCK_MONOTONIC, &t1); ts = elapsed_ms(t0, t1) / ITERS;

    rv_clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < ITERS; i++) computeMagnitude_RVV(Gx, Gy, mag, WIDTH, HEIGHT);
    rv_clock_gettime(CLOCK_MONOTONIC, &t1); tm = elapsed_ms(t0, t1) / ITERS;

    writeRaw("../Results/output_magnitude_phase6.raw", mag, WIDTH, HEIGHT);

    double total = tg + ts + tm;
    printf("+-----------------------+----------+-----------+\n");
    printf("| Stage (RVV)           |  ms/iter |  %%total   |\n");
    printf("+-----------------------+----------+-----------+\n");
    printf("| Gaussian 5x5          | %8.3f |  %6.1f%%  |\n", tg, 100.0 * tg / total);
    printf("| Sobel Gx/Gy           | %8.3f |  %6.1f%%  |\n", ts, 100.0 * ts / total);
    printf("| Magnitude (L1)        | %8.3f |  %6.1f%%  |\n", tm, 100.0 * tm / total);
    printf("+-----------------------+----------+-----------+\n");
    printf("| TOTAL                 | %8.3f |  100.0%%   |\n", total);
    printf("+-----------------------+----------+-----------+\n");

    free(input); free(blurred); free(mag); free(Gx); free(Gy);
    free(padded_input); free(padded_blurred);
    return 0;
}