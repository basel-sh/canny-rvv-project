#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <gtest/gtest.h>

using namespace std;

// ============================================================================
// --- Phase 2 Functions ---
// (Included directly in this file so you don't have to deal with header linking issues)
// ============================================================================

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

// ============================================================================
// --- GOOGLE TESTS (Phase 3.1 Quality Test Suite) ---
// ============================================================================

// ----------------------------------------------------------------------------
// 1. Gaussian Blur Tests
// ----------------------------------------------------------------------------

// Test (a): A uniform image should stay uniform after blurring.
// Rubric requirement: Allow +/- 1 tolerance due to integer rounding operations.
TEST(GaussianTest, UniformImageStaysUniform) {
    const int W = 64, H = 64;
    vector<uint8_t> input(W * H, 128); // uniform gray image at pixel value 128
    vector<uint8_t> output(W * H, 0);

    applyGaussianBlur<uint8_t, int32_t>(input, output, W, H);

    // FIX: Only check the inner pixels, avoiding the 2-pixel zero-padded boundary
    // because border pixels are naturally dimmed by zero-padding
    for (int y = 2; y < H - 2; y++) {
        for (int x = 2; x < W - 2; x++) {
            EXPECT_NEAR(output[y * W + x], 128, 1) << "Inner pixel at (" << x << "," << y << ") deviated";
        }
    }
}

// Test (b): Blurring an all-black image should produce all-black.
// Literal rubric requirement: "Blurring an all-black image should produce all-black."
TEST(GaussianTest, AllBlackImageStaysBlack) {
    const int W = 32, H = 32;
    vector<uint8_t> input(W * H, 0);    // completely black image
    vector<uint8_t> output(W * H, 255); // fill output with 255 to verify the function zeroes it

    applyGaussianBlur<uint8_t, int32_t>(input, output, W, H);

    // Entire image (including borders) must be 0 because (0 x any kernel value = 0)
    for (int i = 0; i < W * H; i++) {
        EXPECT_EQ(output[i], 0) << "Pixel at index " << i << " is not black!";
    }
}

// Test (c): A single bright pixel (impulse) should spread to neighbors symmetrically.
// Literal rubric requirement: "A single bright pixel (impulse) should spread to neighbors symmetrically."
TEST(GaussianTest, ImpulseResponseSpreadsSymmetrically) {
    const int W = 17, H = 17; // odd size chosen so there is an exact center pixel at (8,8)
    vector<uint8_t> input(W * H, 0);
    vector<uint8_t> output(W * H, 0);

    input[8 * W + 8] = 255; // place a bright white pixel exactly at the center
    applyGaussianBlur<uint8_t, int32_t>(input, output, W, H);

    // Check 1: intensity must decrease gradually the further we go from the center
    EXPECT_GT(output[8 * W + 8], output[8 * W + 9]);  // center > immediate right neighbor
    EXPECT_GT(output[8 * W + 9], output[8 * W + 10]); // immediate neighbor > farther neighbor

    // Check 2 (horizontal, vertical and diagonal symmetry): most important rubric condition
    EXPECT_EQ(output[8 * W + 7], output[8 * W + 9]);   // right value equals left value
    EXPECT_EQ(output[7 * W + 8], output[9 * W + 8]);   // top value equals bottom value
    EXPECT_EQ(output[7 * W + 7], output[9 * W + 9]);   // upper diagonal equals lower diagonal
}

// ----------------------------------------------------------------------------
// 2. Sobel Kernel Tests
// ----------------------------------------------------------------------------

// Test (a): A perfectly uniform image has no edges so gradient must be zero everywhere.
TEST(SobelTest, UniformImageZeroGradient) {
    const int W = 64, H = 64;
    vector<uint8_t> input(W * H, 100); // perfectly stable uniform gray image
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

// Test (b): Detect a vertical edge and confirm the angle direction is 0 degrees.
TEST(SobelTest, VerticalEdgeDetected) {
    const int W = 32, H = 32;
    vector<uint8_t> input(W * H, 0);
    vector<uint8_t> mag(W * H, 0);
    vector<uint8_t> dir(W * H, 0);

    // Create a sharp vertical edge: left half black (0), right half white (255)
    for (int y = 0; y < H; y++) {
        for (int x = W/2; x < W; x++) {
            input[y * W + x] = 255;
        }
    }

    applySobelAndMagnitude(input, mag, dir, W, H);

    int edge_x = W/2; // the dividing line between black and white
    for (int y = 1; y < H - 1; y++) {
        EXPECT_GT(mag[y * W + edge_x], 0) << "Expected edge at column " << edge_x;
        EXPECT_EQ(dir[y * W + edge_x], 0) << "Expected 0 degree direction for vertical edge";
    }
}

// Test (c): Detect a horizontal edge and confirm the angle direction is 90 degrees.
TEST(SobelTest, HorizontalEdgeDetected) {
    const int W = 32, H = 32;
    vector<uint8_t> input(W * H, 0);
    vector<uint8_t> mag(W * H, 0);
    vector<uint8_t> dir(W * H, 0);

    // Create a sharp horizontal edge: top half black (0), bottom half white (255)
    for (int y = H/2; y < H; y++) {
        for (int x = 0; x < W; x++) {
            input[y * W + x] = 255;
        }
    }

    applySobelAndMagnitude(input, mag, dir, W, H);

    int edge_y = H/2; // the dividing line between black and white
    for (int x = 1; x < W - 1; x++) {
        EXPECT_GT(mag[edge_y * W + x], 0) << "Expected edge at row " << edge_y;
        EXPECT_EQ(dir[edge_y * W + x], 90) << "Expected 90 degree direction for horizontal edge";
    }
}

// Test (d): Detect a diagonal edge and confirm the angle direction is 45 degrees.
TEST(SobelTest, DiagonalEdgeDetected) {
    const int W = 32, H = 32;
    vector<uint8_t> input(W * H, 0);
    vector<uint8_t> mag(W * H, 0);
    vector<uint8_t> dir(W * H, 0);

    // Create a diagonal line: pixels where (x >= y) become white
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

// ----------------------------------------------------------------------------
// 3. Magnitude Tests (L1 vs L2 Norm)
// ----------------------------------------------------------------------------

// Test (a): Mathematical comparison to confirm L1 is always >= L2 geometrically.
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

// Test (b): Run both functions (L1 and L2) on a random image and confirm no crash and non-zero output.
// Literal rubric requirement: "Both L1 and L2 methods should produce nonzero output on a random image.
// Neither should crash or produce all-zeros."
TEST(MagnitudeTest, NonZeroOnRandomImageAndNoCrash) {
    const int W = 32, H = 32;
    vector<uint8_t> input(W * H);
    
    // Fill with deterministic noise to stimulate the edge detector
    for (int i = 0; i < W * H; i++) {
        input[i] = (i * 17) % 256;
    }

    vector<uint8_t> mag_l1(W * H, 0);
    vector<uint8_t> dir_l1(W * H, 0);
    vector<uint8_t> mag_l2(W * H, 0);
    vector<uint8_t> dir_l2(W * H, 0);

    // 1. Robustness check: confirm L1 computation does not crash
    ASSERT_NO_THROW(applySobelAndMagnitude(input, mag_l1, dir_l1, W, H, false));
    
    // 2. Robustness check: confirm L2 square root computation does not crash
    ASSERT_NO_THROW(applySobelAndMagnitude(input, mag_l2, dir_l2, W, H, true));

    // 3. Output verification: sum pixel values to confirm neither output is all-zeros
    long sum_l1 = 0, sum_l2 = 0;
    for (int i = 0; i < W * H; i++) {
        sum_l1 += mag_l1[i];
        sum_l2 += mag_l2[i];
    }
    EXPECT_GT(sum_l1, 0) << "L1 function produced a dead all-zero image!";
    EXPECT_GT(sum_l2, 0) << "L2 function produced a dead all-zero image!";
}