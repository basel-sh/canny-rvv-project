#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <cstdio>  // Dropping down to pure C standard I/O for emulator stability

using namespace std;

// --- Helper Functions for File I/O (Pure C) ---
bool readRawImage(const string& filename, vector<uint8_t>& image, int width, int height) {
    FILE* file = fopen(filename.c_str(), "rb");
    if (!file) {
        perror("KERNEL READ ERROR"); // Forces Linux to tell us exactly why it failed
        return false;
    }
    fread(image.data(), 1, width * height, file);
    fclose(file);
    return true;
}

bool writeRawImage(const string& filename, const vector<uint8_t>& image, int width, int height) {
    FILE* file = fopen(filename.c_str(), "wb");
    if (!file) {
        perror("KERNEL WRITE ERROR");
        return false;
    }
    fwrite(image.data(), 1, width * height, file);
    fclose(file);
    return true;
}

// --- Stage 1: Gaussian Blur (Templated for Phase 2 Compliance) ---
template <typename PixelType, typename AccumulatorType>
void applyGaussianBlur(const vector<PixelType>& input, vector<PixelType>& output, int width, int height) {
    const int kernel[5][5] = {
        {1, 4, 7, 4, 1},
        {4, 16, 26, 16, 4},
        {7, 26, 41, 26, 7},
        {4, 16, 26, 16, 4},
        {1, 4, 7, 4, 1}
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
                        AccumulatorType pixel_val = input[ny * width + nx];
                        sum += pixel_val * kernel[ky + 2][kx + 2];
                    }
                }
            }
            output[y * width + x] = static_cast<PixelType>(sum / kernel_sum);
        }
    }
}

// --- Stage 2 & 3: Sobel, Magnitude, and Direction ---
// Added 'use_l2_norm' flag to satisfy Phase 2 requirements
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

            // --- THE PHASE 2 L1/L2 MAGNITUDE FIX ---
            int32_t mag = 0;
            if (use_l2_norm) {
                // L2 Norm: Euclidean distance (More accurate, but square roots are slow on hardware)
                mag = static_cast<int32_t>(std::round(std::sqrt(gx * gx + gy * gy)));
            } else {
                // L1 Norm: Manhattan distance (Fast absolute addition)
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

// --- Main Execution ---
int main() {
    const int width = 512;
    const int height = 512;
    const size_t img_size = width * height;

    vector<uint8_t> input_img(img_size);
    vector<uint8_t> blurred_img(img_size);
    vector<uint8_t> mag_img_L1(img_size);
    vector<uint8_t> dir_img_L1(img_size);
    vector<uint8_t> mag_img_L2(img_size);
    vector<uint8_t> dir_img_L2(img_size);

    cout << "--- Canny Edge Detection: Phase 2 Scalar Baseline ---" << endl;

    // --- UPDATED: Read from the Results folder ---
    if (!readRawImage("../Results/test_image.raw", input_img, width, height)) {
        cerr << "Program Halt: Initialization Failed." << endl;
        return 1;
    }
    cout << "Loaded image successfully from Results folder." << endl;
    
    applyGaussianBlur<uint8_t, int32_t>(input_img, blurred_img, width, height);
    cout << "Gaussian Blur applied." << endl;
    
    applySobelAndMagnitude(blurred_img, mag_img_L1, dir_img_L1, width, height, false);
    cout << "Sobel Gradients (L1 Norm) applied." << endl;
    
    // --- UPDATED: Write L1 to the Results folder ---
    writeRawImage("../Results/output_magnitude_L1.raw", mag_img_L1, width, height);
    
    applySobelAndMagnitude(blurred_img, mag_img_L2, dir_img_L2, width, height, true);
    cout << "Sobel Gradients (L2 Norm) applied." << endl;
    
    // --- UPDATED: Write L2 to the Results folder ---
    writeRawImage("../Results/output_magnitude_L2.raw", mag_img_L2, width, height);

    cout << "Pipeline complete. Both L1 and L2 outputs saved to Results folder!" << endl;
    return 0;
}