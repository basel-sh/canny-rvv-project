#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sys/time.h>
#include <algorithm>

using namespace std;

static double elapsed_ms(struct timeval s, struct timeval e) {
    return (e.tv_sec-s.tv_sec)*1000.0 + (e.tv_usec-s.tv_usec)/1000.0;
}

template <typename PixelType, typename AccumulatorType>
void applyGaussianBlur(const vector<PixelType>& input, vector<PixelType>& output, int width, int height) {
    const int kernel[5][5] = {
        {1,4,7,4,1},{4,16,26,16,4},{7,26,41,26,7},{4,16,26,16,4},{1,4,7,4,1}
    };
    const AccumulatorType kernel_sum = 273;
    for (int y=0; y<height; y++) {
        for (int x=0; x<width; x++) {
            AccumulatorType sum=0;
            for (int ky=-2; ky<=2; ky++) {
                for (int kx=-2; kx<=2; kx++) {
                    int ny=y+ky, nx=x+kx;
                    if (ny>=0&&ny<height&&nx>=0&&nx<width)
                        sum+=(AccumulatorType)input[ny*width+nx]*kernel[ky+2][kx+2];
                }
            }
            output[y*width+x]=(PixelType)(sum/kernel_sum);
        }
    }
}

void applySobel(const vector<uint8_t>& blurred, vector<int16_t>& Gx, vector<int16_t>& Gy, int width, int height) {
    const int Kx[3][3]={{-1,0,1},{-2,0,2},{-1,0,1}};
    const int Ky[3][3]={{-1,-2,-1},{0,0,0},{1,2,1}};
    for (int y=0; y<height; y++) {
        for (int x=0; x<width; x++) {
            int32_t gx=0,gy=0;
            for (int ky=-1; ky<=1; ky++) {
                for (int kx=-1; kx<=1; kx++) {
                    int ny=y+ky,nx=x+kx;
                    int pv=(ny>=0&&ny<height&&nx>=0&&nx<width)?blurred[ny*width+nx]:0;
                    gx+=pv*Kx[ky+1][kx+1];
                    gy+=pv*Ky[ky+1][kx+1];
                }
            }
            Gx[y*width+x]=(int16_t)gx;
            Gy[y*width+x]=(int16_t)gy;
        }
    }
}

void computeMagnitude(const vector<int16_t>& Gx, const vector<int16_t>& Gy, vector<uint8_t>& mag, int width, int height) {
    for (int i=0; i<width*height; i++) {
        int32_t m=abs((int32_t)Gx[i])+abs((int32_t)Gy[i]);
        mag[i]=(uint8_t)(m>255?255:m);
    }
}

void computeDirection(const vector<int16_t>& Gx, const vector<int16_t>& Gy, vector<uint8_t>& dir, int width, int height) {
    for (int i=0; i<width*height; i++) {
        int32_t ax=abs((int32_t)Gx[i]),ay=abs((int32_t)Gy[i]);
        uint8_t angle=0;
        if (ax!=0||ay!=0) {
            if      (ay*5<ax*2)  angle=0;
            else if (ay*5>ax*12) angle=2;
            else if ((Gx[i]>0&&Gy[i]>0)||(Gx[i]<0&&Gy[i]<0)) angle=3;
            else angle=1;
        }
        dir[i]=angle;
    }
}

// --- BONUS: True Edge Tracking & Non-Maximum Suppression ---
void applyNMSAndThreshold(const vector<uint8_t>& mag, const vector<uint8_t>& dir, vector<uint8_t>& out, int width, int height, uint8_t low_thresh = 20, uint8_t high_thresh = 80) {
    std::fill(out.begin(), out.end(), 0);
    vector<int> strong_edges; // Stack to keep track of strong edges

    // Step 1: NMS and Initial Categorization
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int idx = y * width + x;
            uint8_t current_mag = mag[idx];
            
            if (current_mag == 0) continue; 

            uint8_t n1 = 0, n2 = 0;
            uint8_t angle = dir[idx];

            if (angle == 0) { 
                n1 = mag[idx - 1]; n2 = mag[idx + 1];
            } else if (angle == 2) { 
                n1 = mag[idx - width]; n2 = mag[idx + width];
            } else if (angle == 1) { 
                n1 = mag[idx - width + 1]; n2 = mag[idx + width - 1];
            } else if (angle == 3) { 
                n1 = mag[idx - width - 1]; n2 = mag[idx + width + 1];
            }

            // Non-Maximum Suppression
            if (current_mag >= n1 && current_mag >= n2) {
                if (current_mag >= high_thresh) {
                    out[idx] = 255; // Definite Strong Edge
                    strong_edges.push_back(idx); // Push to stack for tracing
                } else if (current_mag >= low_thresh) {
                    out[idx] = 128; // Mark temporarily as a "Weak" edge
                }
            }
        }
    }

    // Step 2: True Edge Tracking (Flood Fill Hysteresis)
    // Arrays representing the 8 surrounding neighbor directions
    int dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    int dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

    while (!strong_edges.empty()) {
        int idx = strong_edges.back();
        strong_edges.pop_back();

        int y = idx / width;
        int x = idx % width;

        // Look at all 8 surrounding pixels
        for (int i = 0; i < 8; i++) {
            int nx = x + dx[i];
            int ny = y + dy[i];
            
            // Boundary check
            if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                int n_idx = ny * width + nx;
                
                // If the neighbor is a weak edge, PROMOTE it to strong, and push it to the stack
                // so we can keep tracing down the line from there!
                if (out[n_idx] == 128) {
                    out[n_idx] = 255; 
                    strong_edges.push_back(n_idx); 
                }
            }
        }
    }

    // Step 3: Cleanup
    // Any pixel still marked as 128 never connected to a strong edge. Delete them.
    for (int i = 0; i < width * height; i++) {
        if (out[i] == 128) {
            out[i] = 0;
        }
    }
}

static bool readRaw(const char* path, vector<uint8_t>& buf, int w, int h) {
    FILE* f=fopen(path,"rb");
    if(!f){perror(path);return false;}
    fread(buf.data(),1,(size_t)w*h,f);
    fclose(f);
    return true;
}

static void writeRaw(const char* path, const vector<uint8_t>& buf, int w, int h) {
    FILE* f=fopen(path,"wb");
    if(!f){perror(path);return;}
    fwrite(buf.data(),1,(size_t)w*h,f);
    fclose(f);
}

int main() {
    const int WIDTH=512,HEIGHT=512,N=WIDTH*HEIGHT,ITERS=100;
    vector<uint8_t> input(N),blurred(N),mag(N),dir(N),final_edges(N);
    vector<int16_t> Gx(N),Gy(N);

    printf("=== Canny Edge Detection Pipeline (with NMS Bonus) ===\n");

    if(!readRaw("../Results/test_image.raw",input,WIDTH,HEIGHT)) {
        printf("Using synthetic gradient image.\n");
        for(int y=0;y<HEIGHT;y++)
            for(int x=0;x<WIDTH;x++)
                input[y*WIDTH+x]=(uint8_t)((x+y)%256);
    }

    applyGaussianBlur<uint8_t,int32_t>(input,blurred,WIDTH,HEIGHT);
    applySobel(blurred,Gx,Gy,WIDTH,HEIGHT);
    computeMagnitude(Gx,Gy,mag,WIDTH,HEIGHT);
    computeDirection(Gx,Gy,dir,WIDTH,HEIGHT);
    
    // Call the newly added bonus function
    applyNMSAndThreshold(mag, dir, final_edges, WIDTH, HEIGHT, 20, 70); // You can adjust the low and high thresholds as needed

    // Write the output to the Results folder
    writeRaw("../Results/output_final_bonus.raw", final_edges, WIDTH, HEIGHT);
    
    printf("Pipeline complete. Razor-sharp edges saved to Results/output_final_bonus.raw\n");

    return 0;
}