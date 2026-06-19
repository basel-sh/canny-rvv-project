#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sys/time.h>
#include <riscv_vector.h>

using namespace std;

static double elapsed_ms(struct timeval s, struct timeval e) {
    return (e.tv_sec - s.tv_sec) * 1000.0 + (e.tv_usec - s.tv_usec) / 1000.0;
}

// Helper to copy standard image into a zero-padded canvas buffer
vector<uint8_t> createPaddedImage(const vector<uint8_t>& input, int width, int height, int pad) {
    int p_width = width + 2 * pad;
    int p_height = height + 2 * pad;
    vector<uint8_t> padded(p_width * p_height, 0); // Filled with zeros [cite: 65, 119]

    for (int y = 0; y < height; y++) {
        std::memcpy(&padded[(y + pad) * p_width + pad], &input[y * width], width);
    }
    return padded;
}

// --- Phase 6: Vectorized Gaussian Blur 5x5 (No branch checks inside loops) ---
void applyGaussianBlur_RVV(const vector<uint8_t>& padded_input, vector<uint8_t>& output, int width, int height) {
    const int16_t kernel[5][5] = {
        {1, 4, 7, 4, 1}, {4, 16, 26, 16, 4}, {7, 26, 41, 26, 7}, {4, 16, 26, 16, 4}, {1, 4, 7, 4, 1}
    };
    const int32_t kernel_sum = 273;
    int p_width = width + 4; // pad = 2

    for (int y = 0; y < height; y++) {
        int x = 0;
        int pixels_left = width;
        
        while (pixels_left > 0) {
            size_t vl = __riscv_vsetvl_e8m1(pixels_left);
            vint32m4_t v_sum = __riscv_vmv_v_x_i32m4(0, vl);

            // Directly stream and compute vector lanes without nested branches
            for (int ky = -2; ky <= 2; ky++) {
                int row_offset = (y + 2 + ky) * p_width;
                for (int kx = -2; kx <= 2; kx++) {
                    int16_t weight = kernel[ky + 2][kx + 2];
                    
                    // No branch checks: Safe due to pre-padding border safety 
                    const uint8_t* ptr = &padded_input[row_offset + (x + 2 + kx)];
                    
                    vuint8m1_t v_u8 = __riscv_vle8_v_u8m1(ptr, vl);
                    vuint16m2_t v_u16 = __riscv_vwcvtu_x_x_v_u16m2(v_u8, vl);
                    vint16m2_t v_px = __riscv_vreinterpret_v_u16m2_i16m2(v_u16);
                    
                    v_sum = __riscv_vwmacc_vx_i32m4(v_sum, weight, v_px, vl);
                }
            }

            vint32m4_t v_div = __riscv_vdiv_vx_i32m4(v_sum, kernel_sum, vl);
            vint16m2_t v_out16 = __riscv_vncvt_x_x_w_i16m2(v_div, vl);
            vint8m1_t v_out8_signed = __riscv_vncvt_x_x_w_i8m1(v_out16, vl);
            vuint8m1_t v_out8 = __riscv_vreinterpret_v_i8m1_u8m1(v_out8_signed);
            
            __riscv_vse8_v_u8m1(&output[y * width + x], v_out8, vl);

            x += vl;
            pixels_left -= vl;
        }
    }
}

// --- Phase 6: Vectorized Sobel Derivation (No branch checks inside loops) ---
void applySobel_RVV(const vector<uint8_t>& padded_blurred, vector<int16_t>& Gx, vector<int16_t>& Gy, int width, int height) {
    const int16_t Kx[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
    const int16_t Ky[3][3] = {{-1, -2, -1}, {0, 0, 0}, {1, 2, 1}};
    int p_width = width + 2; // pad = 1

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

                    // Direct pipeline load from zero-padded input buffer array boundary 
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

// --- Phase 6: Vectorized Magnitude calculation (L1 Norm) ---
void computeMagnitude_RVV(const vector<int16_t>& Gx, const vector<int16_t>& Gy, vector<uint8_t>& mag, int width, int height) {
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

static bool readRaw(const char* path, vector<uint8_t>& buf, int w, int h) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fread(buf.data(), 1, (size_t)w * h, f);
    fclose(f);
    return true;
}

static void writeRaw(const char* path, const vector<uint8_t>& buf, int w, int h) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(buf.data(), 1, (size_t)w * h, f);
    fclose(f);
}

int main() {
    const int WIDTH = 512, HEIGHT = 512, N = WIDTH * HEIGHT, ITERS = 100;
    vector<uint8_t> input(N), blurred(N), mag(N);
    vector<int16_t> Gx(N), Gy(N);

    printf("=== Phase 6: Fully Optimized RVV (Pre-padded Boundaries) ===\n");
    
    if (!readRaw("../Results/test_image.raw", input, WIDTH, HEIGHT)) {
        for (int y = 0; y < HEIGHT; y++)
            for (int x = 0; x < WIDTH; x++)
                input[y * WIDTH + x] = (uint8_t)((x + y) % 256);
    }

    struct timeval t0, t1;
    double tg, ts, tm;

    // Pre-pad input configuration once outside hot loops
    vector<uint8_t> padded_input = createPaddedImage(input, WIDTH, HEIGHT, 2);

    gettimeofday(&t0, 0);
    for (int i = 0; i < ITERS; i++) applyGaussianBlur_RVV(padded_input, blurred, WIDTH, HEIGHT);
    gettimeofday(&t1, 0); tg = elapsed_ms(t0, t1) / ITERS;

    // Pre-pad blurred configuration once outside hot loops
    vector<uint8_t> padded_blurred = createPaddedImage(blurred, WIDTH, HEIGHT, 1);

    gettimeofday(&t0, 0);
    for (int i = 0; i < ITERS; i++) applySobel_RVV(padded_blurred, Gx, Gy, WIDTH, HEIGHT);
    gettimeofday(&t1, 0); ts = elapsed_ms(t0, t1) / ITERS;

    gettimeofday(&t0, 0);
    for (int i = 0; i < ITERS; i++) computeMagnitude_RVV(Gx, Gy, mag, WIDTH, HEIGHT);
    gettimeofday(&t1, 0); tm = elapsed_ms(t0, t1) / ITERS;

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
    return 0;
}