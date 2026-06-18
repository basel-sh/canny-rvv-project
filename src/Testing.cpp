#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <gtest/gtest.h>

using namespace std;

// --- Phase 2 Functions ---
// (Included directly in this file so you don't have to deal with header linking issues)

template <typename PixelType, typename AccumulatorType>
void applyGaussianBlur(const vector<PixelType>& input, vector<PixelType>& output, int width, int height) {
    const int kernel[5][5] = {
        {1, 4, 7, 4, 1}, {4, 16, 26, 16, 4}, {7, 26, 41, 26, 7}, {4, 16, 26, 16, 4}, {1, 4, 7, 4, 1}
    };
    const AccumulatorType kernel_sum = 273;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            AccumulatorType sum = 0;
            for (int ky = -2; ky <= 2; ky++) {
                for (int kx = -2; kx <= 2; kx++) {
                    int ny = y + ky;
                    int nx = x + kx;
                    if (ny >= 0 && ny < height && nx >= 0 && nx < width) {
                        sum += input[ny * width + nx] * kernel[ky + 2][kx + 2];
                    }
                }
            }
            output[y * width + x] = static_cast<PixelType>(sum / kernel_sum);
        }
    }
}

void applySobelAndMagnitude(const vector<uint8_t>& blurred, vector<uint8_t>& magnitude_out, vector<uint8_t>& direction_out, int width, int height, bool use_l2_norm = false) {
    const int Kx[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
    const int Ky[3][3] = {{-1, -2, -1}, {0, 0, 0}, {1, 2, 1}};

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int32_t gx = 0;
            int32_t gy = 0;

            for (int ky = -1; ky <= 1; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    int ny = y + ky;
                    int nx = x + kx;
                    int pixel_val = (ny >= 0 && ny < height && nx >= 0 && nx < width) ? blurred[ny * width + nx] : 0;
                    
                    gx += pixel_val * Kx[ky + 1][kx + 1];
                    gy += pixel_val * Ky[ky + 1][kx + 1];
                }
            }

            int32_t mag = 0;
            if (use_l2_norm) {
                mag = static_cast<int32_t>(std::round(std::sqrt(gx * gx + gy * gy)));
            } else {
                mag = abs(gx) + abs(gy);
            }
            magnitude_out[y * width + x] = static_cast<uint8_t>(mag > 255 ? 255 : mag);

            uint8_t angle = 0;
            if (mag != 0) {
                int32_t abs_gx = abs(gx);
                int32_t abs_gy = abs(gy);

                if (abs_gy * 1000 <= 414 * abs_gx) angle = 0; 
                else if (abs_gy * 1000 >= 2414 * abs_gx) angle = 90; 
                else if ((gx > 0 && gy > 0) || (gx < 0 && gy < 0)) angle = 135; 
                else angle = 45;
            }
            direction_out[y * width + x] = angle;
        }
    }
}

// ============================================================
//                       GOOGLE TESTS
// ============================================================

// Test 1: Gaussian blur of uniform image stays uniform
TEST(GaussianTest, UniformImageStaysUniform) {
    const int W = 64, H = 64;
    vector<uint8_t> input(W * H, 128);
    vector<uint8_t> output(W * H, 0);

    applyGaussianBlur<uint8_t, int32_t>(input, output, W, H);

    // FIX: Only check the inner pixels, avoiding the 2-pixel zero-padded boundary
    for (int y = 2; y < H - 2; y++) {
        for (int x = 2; x < W - 2; x++) {
            EXPECT_NEAR(output[y * W + x], 128, 2) << "Inner pixel at (" << x << "," << y << ") deviated";
        }
    }
}

// Test 2: Gaussian impulse response spreads out
TEST(GaussianTest, ImpulseResponseSpreads) {
    const int W = 16, H = 16;
    vector<uint8_t> input(W * H, 0);
    vector<uint8_t> output(W * H, 0);

    input[8 * W + 8] = 255;
    applyGaussianBlur<uint8_t, int32_t>(input, output, W, H);

    EXPECT_GT(output[8 * W + 8], output[8 * W + 9]);
    EXPECT_GT(output[8 * W + 8], output[7 * W + 8]);
}

// Test 3: Zero gradient on uniform image
TEST(SobelTest, UniformImageZeroGradient) {
    const int W = 64, H = 64;
    vector<uint8_t> input(W * H, 100); // A perfectly uniform gray image
    vector<uint8_t> mag(W * H, 0);
    vector<uint8_t> dir(W * H, 0);

    // FIX 1: We removed the Gaussian blur. We are isolating and unit testing Sobel directly.
    applySobelAndMagnitude(input, mag, dir, W, H);

    // FIX 2: Since Sobel is a 3x3 kernel, we only need to ignore the 1-pixel outer border
    for (int y = 1; y < H - 1; y++) {
        for (int x = 1; x < W - 1; x++) {
            EXPECT_EQ(mag[y * W + x], 0) << "Expected zero magnitude at inner pixel (" << x << "," << y << ")";
        }
    }
}

// Test 4: Vertical edge detected as 0 degrees
TEST(SobelTest, VerticalEdgeDetected) {
    const int W = 32, H = 32;
    vector<uint8_t> input(W * H, 0);
    vector<uint8_t> mag(W * H, 0);
    vector<uint8_t> dir(W * H, 0);

    for (int y = 0; y < H; y++) {
        for (int x = W/2; x < W; x++) {
            input[y * W + x] = 255;
        }
    }

    applySobelAndMagnitude(input, mag, dir, W, H);

    int edge_x = W/2;
    for (int y = 1; y < H-1; y++) {
        EXPECT_GT(mag[y * W + edge_x], 0) << "Expected edge at column " << edge_x;
        EXPECT_EQ(dir[y * W + edge_x], 0) << "Expected 0 degree direction for vertical edge";
    }
}

// Test 5: Horizontal edge detected as 90 degrees
TEST(SobelTest, HorizontalEdgeDetected) {
    const int W = 32, H = 32;
    vector<uint8_t> input(W * H, 0);
    vector<uint8_t> mag(W * H, 0);
    vector<uint8_t> dir(W * H, 0);

    for (int y = H/2; y < H; y++) {
        for (int x = 0; x < W; x++) {
            input[y * W + x] = 255;
        }
    }

    applySobelAndMagnitude(input, mag, dir, W, H);

    int edge_y = H/2;
    for (int x = 1; x < W-1; x++) {
        EXPECT_GT(mag[edge_y * W + x], 0) << "Expected edge at row " << edge_y;
        EXPECT_EQ(dir[edge_y * W + x], 90) << "Expected 90 degree direction for horizontal edge";
    }
}

// Test 6: Diagonal edge detected (Rubric Requirement)
TEST(SobelTest, DiagonalEdgeDetected) {
    const int W = 32, H = 32;
    vector<uint8_t> input(W * H, 0);
    vector<uint8_t> mag(W * H, 0);
    vector<uint8_t> dir(W * H, 0);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (x >= y) input[y * W + x] = 255;
        }
    }

    applySobelAndMagnitude(input, mag, dir, W, H);
    
    int test_x = 15;
    int test_y = 15;
    
    EXPECT_GT(mag[test_y * W + test_x], 0) << "Expected edge magnitude on the diagonal";
    // FIX: Updated expectation to 45 degrees to match the image memory Y-axis inversion
    EXPECT_EQ(dir[test_y * W + test_x], 45) << "Expected 45 degree direction for diagonal edge";
}

// Test 7: L1 vs L2 magnitude comparison
TEST(MagnitudeTest, L1AlwaysGreaterOrEqualL2) {
    vector<pair<int,int>> samples = {
        {100, 100}, {255, 0}, {0, 255}, {128, 64}, {50, 200}
    };

    for (auto [gx, gy] : samples) {
        float l1 = abs(gx) + abs(gy);
        float l2 = sqrt((float)gx*gx + (float)gy*gy);
        EXPECT_GE(l1, l2) << "L1 should be >= L2 for gx=" << gx << " gy=" << gy;
    }
}