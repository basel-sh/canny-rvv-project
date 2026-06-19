// ============================================================
// Phase 4 + Phase 5 Combined: Compiler Optimization Sweep
// AND Profiling / Hotspot Identification
// ------------------------------------------------------------
// This single source file serves two purposes:
//   (1) Phase 4: compile this same file at -O0/-O2/-O3/-Os/-Ofast
//       and compare the printed timing table across binaries.
//   (2) Phase 5: the per-stage timing + percentage breakdown +
//       hotspot conclusion below IS the Phase 5 profiling
//       deliverable, regardless of which -O flag built it.
//
// Uses clock_gettime(CLOCK_MONOTONIC) since it cannot jump
// backward due to system clock adjustments (more robust than
// gettimeofday for benchmarking).
// ============================================================

#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

using namespace std;

static double elapsed_ms(struct timespec s, struct timespec e) {
    double sec_diff = (double)(e.tv_sec - s.tv_sec);
    double nsec_diff = (double)(e.tv_nsec - s.tv_nsec);
    return sec_diff * 1000.0 + nsec_diff / 1e6;
}

// --- Stage 1: Gaussian Blur ---
template <typename PixelType, typename AccumulatorType>
void applyGaussianBlur(const vector<PixelType>& input, vector<PixelType>& output, int width, int height) {
    const int kernel[5][5] = {
        {1,4,7,4,1},{4,16,26,16,4},{7,26,41,26,7},{4,16,26,16,4},{1,4,7,4,1}
    };
    const AccumulatorType kernel_sum = 273;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            AccumulatorType sum = 0;
            for (int ky = -2; ky <= 2; ky++) {
                for (int kx = -2; kx <= 2; kx++) {
                    int ny = y + ky, nx = x + kx;
                    if (ny >= 0 && ny < height && nx >= 0 && nx < width)
                        sum += (AccumulatorType)input[ny*width+nx] * kernel[ky+2][kx+2];
                }
            }
            output[y*width+x] = (PixelType)(sum / kernel_sum);
        }
    }
}

// --- Stage 2: Sobel Gx/Gy ---
void applySobel(const vector<uint8_t>& blurred, vector<int16_t>& Gx, vector<int16_t>& Gy, int width, int height) {
    const int Kx[3][3] = {{-1,0,1},{-2,0,2},{-1,0,1}};
    const int Ky[3][3] = {{-1,-2,-1},{0,0,0},{1,2,1}};
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int32_t gx = 0, gy = 0;
            for (int ky = -1; ky <= 1; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    int ny = y + ky, nx = x + kx;
                    int pv = (ny >= 0 && ny < height && nx >= 0 && nx < width) ? blurred[ny*width+nx] : 0;
                    gx += pv * Kx[ky+1][kx+1];
                    gy += pv * Ky[ky+1][kx+1];
                }
            }
            Gx[y*width+x] = (int16_t)gx;
            Gy[y*width+x] = (int16_t)gy;
        }
    }
}

// --- Stage 3: Magnitude (L1 norm) ---
void computeMagnitude(const vector<int16_t>& Gx, const vector<int16_t>& Gy, vector<uint8_t>& mag, int width, int height) {
    for (int i = 0; i < width*height; i++) {
        int32_t m = abs((int32_t)Gx[i]) + abs((int32_t)Gy[i]);
        mag[i] = (uint8_t)(m > 255 ? 255 : m);
    }
}

// --- Stage 4: Direction (quantized to codes 0/1/2/3) ---
void computeDirection(const vector<int16_t>& Gx, const vector<int16_t>& Gy, vector<uint8_t>& dir, int width, int height) {
    for (int i = 0; i < width*height; i++) {
        int32_t ax = abs((int32_t)Gx[i]), ay = abs((int32_t)Gy[i]);
        uint8_t angle = 0;
        if (ax != 0 || ay != 0) {
            if      (ay*5 < ax*2)  angle = 0;
            else if (ay*5 > ax*12) angle = 2;
            else if ((Gx[i]>0 && Gy[i]>0) || (Gx[i]<0 && Gy[i]<0)) angle = 3;
            else angle = 1;
        }
        dir[i] = angle;
    }
}

// --- Image I/O ---
static bool readRaw(const char* path, vector<uint8_t>& buf, int w, int h) {
    FILE* f = fopen(path, "rb");
    if (!f) { perror(path); return false; }
    size_t got = fread(buf.data(), 1, (size_t)w*h, f);
    fclose(f);
    return got == (size_t)w*h;
}

static void writeRaw(const char* path, const vector<uint8_t>& buf, int w, int h) {
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fwrite(buf.data(), 1, (size_t)w*h, f);
    fclose(f);
}

int main(int argc, char** argv) {
    // Usage: ./phase_4_5_combined [width] [height] [input_path] [iterations]
    int WIDTH      = (argc > 1) ? atoi(argv[1]) : 512;
    int HEIGHT     = (argc > 2) ? atoi(argv[2]) : 512;
    const char* IN = (argc > 3) ? argv[3] : "../Results/test_image.raw";
    int ITERS      = (argc > 4) ? atoi(argv[4]) : 100;

    const int N = WIDTH * HEIGHT;
    vector<uint8_t> input(N), blurred(N), mag(N), dir(N);
    vector<int16_t> Gx(N), Gy(N);

    printf("=== Phase 4/5: Compiler Optimization Sweep + Profiling ===\n");
    printf("Image: %dx%d   Iterations: %d\n\n", WIDTH, HEIGHT, ITERS);

    if (!readRaw(IN, input, WIDTH, HEIGHT)) {
        printf("Could not read '%s'. Using synthetic gradient image instead.\n", IN);
        for (int y = 0; y < HEIGHT; y++)
            for (int x = 0; x < WIDTH; x++)
                input[y*WIDTH+x] = (uint8_t)((x + y) % 256);
    }

    struct timespec t0, t1;
    double tg, ts, tm, td;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < ITERS; i++) applyGaussianBlur<uint8_t, int32_t>(input, blurred, WIDTH, HEIGHT);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    tg = elapsed_ms(t0, t1) / ITERS;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < ITERS; i++) applySobel(blurred, Gx, Gy, WIDTH, HEIGHT);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ts = elapsed_ms(t0, t1) / ITERS;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < ITERS; i++) computeMagnitude(Gx, Gy, mag, WIDTH, HEIGHT);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    tm = elapsed_ms(t0, t1) / ITERS;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < ITERS; i++) computeDirection(Gx, Gy, dir, WIDTH, HEIGHT);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    td = elapsed_ms(t0, t1) / ITERS;

    writeRaw("../Results/output_magnitude_phase4_5.raw", mag, WIDTH, HEIGHT);
    writeRaw("../Results/output_direction_phase4_5.raw", dir, WIDTH, HEIGHT);

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

    printf("\nHotspot conclusion: the stages above %%total each represent\n");
    printf("candidates for RVV optimization in Phase 6. Stages contributing\n");
    printf("a small %% of total time are not worth vectorizing (Amdahl's law).\n");

    return 0;
}
