from PIL import Image
import os

def convert_raw_to_png(raw_filename, png_filename):
    """Reads a 512x512 raw byte stream and saves it as a PNG."""
    try:
        with open(raw_filename, "rb") as f:
            raw_data = f.read()
            
        # Verify the file size is exactly 512x512 bytes
        if len(raw_data) != 262144:
            print(f"Warning: {raw_filename} is {len(raw_data)} bytes, expected 262144 bytes.")
            
        img = Image.frombytes('L', (512, 512), raw_data)
        img.save(png_filename)
        print(f"Successfully generated: {png_filename}")
        
    except FileNotFoundError:
        print(f"Error: {raw_filename} not found. Did the C++ code finish running?")

# --- Execution ---
print("--- Generating Edge Maps ---")

# Process the L1 (Manhattan Distance) Output
convert_raw_to_png("../Results/output_magnitude_L1.raw", "../Results/final_edges_L1.png")

# Process the L2 (Euclidean Distance) Output
convert_raw_to_png("../Results/output_magnitude_L2.raw", "../Results/final_edges_L2.png")

# Bonus:
convert_raw_to_png("../Results/output_final_bonus.raw", "../Results/final_edges_bonus.png")

#Phase_6 optimization
convert_raw_to_png("../Results/output_magnitude_phase6.raw", "../Results/final_edges_phase6.png")

print("--- Done ---")