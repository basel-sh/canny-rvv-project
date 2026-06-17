from PIL import Image

# Read the raw 512x512 byte stream
try:
    with open("/home/salah/Canny_edge/output_magnitude.raw", "rb") as f:
        raw_data = f.read()

    # Reconstruct the image and save it as a PNG
    img = Image.frombytes('L', (512, 512), raw_data)
    img.save("final_edges.png")
    print("Successfully saved final_edges.png!")
except FileNotFoundError:
    print("Error: output_magnitude.raw not found. The C++ code hasn't generated it yet!")