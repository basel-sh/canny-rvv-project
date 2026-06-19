import numpy as np

width, height = 512, 512
img = np.zeros((height, width), dtype=np.uint8)

# White rectangle on black background (clean edges for Sobel to detect)
img[100:400, 150:350] = 255

img.tofile("Results/test_image.raw")
print(f"Wrote Results/test_image.raw ({width}x{height}, {width*height} bytes)")
