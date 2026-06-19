#include <iostream>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace std;

bool readRawImage(const string& filename, uint8_t* image, int width, int height) {
    FILE* file = fopen(filename.c_str(), "rb");
    if (!file) {
        perror("KERNEL READ ERROR");
        return false;
    }
    fread(image, 1, width * height, file);
    fclose(file);
    return true;
}

bool writeRawImage(const string& filename, const uint8_t* image, int width, int height) {
    FILE* file = fopen(filename.c_str(), "wb");
    if (!file) {
        perror("KERNEL WRITE ERROR");
        return false;
    }
    fwrite(image, 1, width * height, file);
    fclose(file);
    return true;
}

template <typename PixelType, typename AccumulatorType>
void applyGaussianBlur(const PixelType* input, PixelType* output, int width, int height) {
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

void applySobelAndMagnitude(const uint8_t* blurred, uint8_t* magnitude_out, uint8_t* direction_out, int width, int height, bool use_l2_norm = false) {
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

int main() {
    const int width = 512;
    const int height = 512;
    const size_t img_size = width * height;

    uint8_t* input_img = (uint8_t*)aligned_alloc(64, img_size * sizeof(uint8_t));
    uint8_t* blurred_img = (uint8_t*)aligned_alloc(64, img_size * sizeof(uint8_t));
    uint8_t* mag_img_L1 = (uint8_t*)aligned_alloc(64, img_size * sizeof(uint8_t));
    uint8_t* dir_img_L1 = (uint8_t*)aligned_alloc(64, img_size * sizeof(uint8_t));
    uint8_t* mag_img_L2 = (uint8_t*)aligned_alloc(64, img_size * sizeof(uint8_t));
    uint8_t* dir_img_L2 = (uint8_t*)aligned_alloc(64, img_size * sizeof(uint8_t));

    cout << "--- Canny Edge Detection: Phase 2 Scalar Baseline ---" << endl;

    if (!readRawImage("../Results/test_image.raw", input_img, width, height)) {
        cerr << "Program Halt: Initialization Failed." << endl;
        return 1;
    }
    cout << "Loaded image successfully from Results folder." << endl;
    
    applyGaussianBlur<uint8_t, int32_t>(input_img, blurred_img, width, height);
    cout << "Gaussian Blur applied." << endl;
    
    applySobelAndMagnitude(blurred_img, mag_img_L1, dir_img_L1, width, height, false);
    cout << "Sobel Gradients (L1 Norm) applied." << endl;
    
    writeRawImage("../Results/output_magnitude_L1.raw", mag_img_L1, width, height);
    
    applySobelAndMagnitude(blurred_img, mag_img_L2, dir_img_L2, width, height, true);
    cout << "Sobel Gradients (L2 Norm) applied." << endl;
    
    writeRawImage("../Results/output_magnitude_L2.raw", mag_img_L2, width, height);

    cout << "Pipeline complete. Both L1 and L2 outputs saved to Results folder!" << endl;

    free(input_img);
    free(blurred_img);
    free(mag_img_L1);
    free(dir_img_L1);
    free(mag_img_L2);
    free(dir_img_L2);

    return 0;
}