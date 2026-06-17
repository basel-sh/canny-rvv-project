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

// --- Stage 1: Gaussian Blur (5x5) ---
void applyGaussianBlur(const vector<uint8_t>& input, vector<uint8_t>& output, int width, int height) {
    const int kernel[5][5] = {
        {1, 4, 7, 4, 1},
        {4, 16, 26, 16, 4},
        {7, 26, 41, 26, 7},
        {4, 16, 26, 16, 4},
        {1, 4, 7, 4, 1}
    };
    const int kernel_sum = 273;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int32_t sum = 0;
            for (int ky = -2; ky <= 2; ky++) {
                for (int kx = -2; kx <= 2; kx++) {
                    int ny = y + ky;
                    int nx = x + kx;
                    if (ny >= 0 && ny < height && nx >= 0 && nx < width) {
                        int pixel_val = input[ny * width + nx];
                        sum += pixel_val * kernel[ky + 2][kx + 2];
                    }
                }
            }
            output[y * width + x] = static_cast<uint8_t>(sum / kernel_sum);
        }
    }
}

// --- Stage 2 & 3: Sobel, Magnitude, and Direction ---
void applySobelAndMagnitude(const vector<uint8_t>& blurred, vector<uint8_t>& magnitude_out, vector<uint8_t>& direction_out, int width, int height) {
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

            int32_t mag = abs(gx) + abs(gy);
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
    vector<uint8_t> mag_img(img_size);
    vector<uint8_t> dir_img(img_size);

    cout << "--- Canny Edge Detection: Phase 2 Scalar Baseline ---" << endl;

    // Read using standard C paths
    if (!readRawImage("test_image.raw", input_img, width, height)) {
        cerr << "Program Halt: Initialization Failed." << endl;
        return 1;
    }
    
    cout << "Loaded image successfully." << endl;
    applyGaussianBlur(input_img, blurred_img, width, height);
    cout << "Gaussian Blur applied." << endl;
    applySobelAndMagnitude(blurred_img, mag_img, dir_img, width, height);
    cout << "Sobel Gradients & Quantization applied." << endl;

    if (!writeRawImage("output_magnitude.raw", mag_img, width, height)) {
        cerr << "Program Halt: Output write failed." << endl;
        return 1;
    }
    
    cout << "Pipeline complete. Output saved to output_magnitude.raw" << endl;
    return 0;
}