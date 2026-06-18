#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>

using namespace std;

// --- Helper Functions for File I/O ---
bool readRawImage(const string& filename, vector<uint8_t>& image, int width, int height) {
    ifstream file(filename, ios::binary);
    if (!file) return false;
    file.read(reinterpret_cast<char*>(image.data()), width * height);
    return true;
}

bool writeRawImage(const string& filename, const vector<uint8_t>& image, int width, int height) {
    ofstream file(filename, ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(image.data()), width * height);
    return true;
}
// --- Phase 2 Functions ---
// --- Stage 1: Gaussian Blur (5x5) ---
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
// --- Main Execution ---
int main() {
    // Example dimensions: 512x512
    const int width = 512;
    const int height = 512;
    const size_t img_size = width * height;

    // Memory Allocation
    vector<uint8_t> input_img(img_size);
    vector<uint8_t> blurred_img(img_size);
    vector<uint8_t> mag_img(img_size);
    vector<uint8_t> dir_img(img_size);

    cout << "--- Canny Edge Detection: Phase 2 Scalar Baseline ---" << endl;

    // 1. Read the image (Using relative paths so QEMU doesn't get lost)
    if (!readRawImage("test_image.raw", input_img, width, height)) {
        cerr << "Error: Could not open test_image.raw. Make sure the file is in this folder!" << endl;
        return 1;
    }
    
    cout << "Loaded image successfully." << endl;

    // 2. Apply Gaussian Blur
    applyGaussianBlur<uint8_t, int32_t>(input_img, blurred_img, width, height);
    cout << "Gaussian Blur applied." << endl;

    // 3. Apply Sobel, Magnitude, and Direction
    applySobelAndMagnitude(blurred_img, mag_img, dir_img, width, height);
    cout << "Sobel Gradients & Quantization applied." << endl;

    // 4. Write Output
    writeRawImage("output_magnitude.raw", mag_img, width, height);
    cout << "Pipeline complete. Output saved to output_magnitude.raw" << endl;

    return 0;
}