
#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sys/time.h>
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
    vector<uint8_t> input(N),blurred(N),mag(N),dir(N);
    vector<int16_t> Gx(N),Gy(N);

    printf("=== Phase 4: Compiler Optimization Sweep ===\n");
    printf("Image: %dx%d   Iterations: %d\n\n",WIDTH,HEIGHT,ITERS);

    if(!readRaw("../Results/test_image.raw",input,WIDTH,HEIGHT)) {
        printf("Using synthetic gradient image.\n");
        for(int y=0;y<HEIGHT;y++)
            for(int x=0;x<WIDTH;x++)
                input[y*WIDTH+x]=(uint8_t)((x+y)%256);
    }

    struct timeval t0,t1;
    double tg,ts,tm,td;

    gettimeofday(&t0,0);
    for(int i=0;i<ITERS;i++) applyGaussianBlur<uint8_t,int32_t>(input,blurred,WIDTH,HEIGHT);
    gettimeofday(&t1,0); tg=elapsed_ms(t0,t1)/ITERS;

    gettimeofday(&t0,0);
    for(int i=0;i<ITERS;i++) applySobel(blurred,Gx,Gy,WIDTH,HEIGHT);
    gettimeofday(&t1,0); ts=elapsed_ms(t0,t1)/ITERS;

    gettimeofday(&t0,0);
    for(int i=0;i<ITERS;i++) computeMagnitude(Gx,Gy,mag,WIDTH,HEIGHT);
    gettimeofday(&t1,0); tm=elapsed_ms(t0,t1)/ITERS;

    gettimeofday(&t0,0);
    for(int i=0;i<ITERS;i++) computeDirection(Gx,Gy,dir,WIDTH,HEIGHT);
    gettimeofday(&t1,0); td=elapsed_ms(t0,t1)/ITERS;

    writeRaw("../Results/output_magnitude_phase4.raw",mag,WIDTH,HEIGHT);
    writeRaw("../Results/output_direction_phase4.raw",dir,WIDTH,HEIGHT);

    double total=tg+ts+tm+td;
    printf("+-----------------------+----------+-----------+\n");
    printf("|  Stage                |  ms/iter |  %%total   |\n");
    printf("+-----------------------+----------+-----------+\n");
    printf("|  Gaussian 5x5         | %8.3f |  %6.1f%%  |\n",tg,100.0*tg/total);
    printf("|  Sobel Gx/Gy          | %8.3f |  %6.1f%%  |\n",ts,100.0*ts/total);
    printf("|  Magnitude (L1)       | %8.3f |  %6.1f%%  |\n",tm,100.0*tm/total);
    printf("|  Direction            | %8.3f |  %6.1f%%  |\n",td,100.0*td/total);
    printf("+-----------------------+----------+-----------+\n");
    printf("|  TOTAL                | %8.3f |  100.0%%   |\n",total);
    printf("+-----------------------+----------+-----------+\n");
    return 0;
}
