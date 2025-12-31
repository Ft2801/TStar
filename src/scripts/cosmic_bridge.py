import sys
import os
import numpy as np

# Try to import tifffile, typical in astrophotography envs
try:
    import tifffile
except ImportError:
    print("Error: tifffile module not found. Please install it (pip install tifffile).")
    sys.exit(1)

def write_tiff(raw_path, width, height, channels, out_path):
    try:
        # Read raw float32 data
        data = np.fromfile(raw_path, dtype=np.float32)
        
        expected_size = width * height * channels
        if data.size != expected_size:
            print(f"Error: Raw file size mismatch. Expected {expected_size} floats, got {data.size}.")
            sys.exit(2)
            
        # Reshape
        # ImageBuffer is usually interleaved or planar? 
        # C++ ImageBuffer is typically Interleaved (RGBRGB) or Planar (RRRGGGBBB)?
        # Checking ImageBuffer.cpp: usually internal storage is vector<float>.
        # If I dump vector<float>, I need to know the layout.
        # Assuming Interleaved for now, but I will verify in C++.
        # Update: ImageBuffer in this project seems to handle data as flat vector.
        # standard load/save usually implies specific structure.
        # Let's assume C++ dumps it exactly as it holds it.
        # SimpleTiffReader usually outputs CHW or HWC?
        # Standard OpenCV/Stb is HWC.
        # I'll modify C++ to write HWC Interleaved for simplicity, or just handle what it has.
        
        # Let's assume HWC (Height, Width, Channels) for now.
        if channels == 1:
            img = data.reshape((height, width))
        else:
            img = data.reshape((height, width, channels))
            
        # Convert to 16-bit for maximum compatibility with Cosmic Clarity
        # (It seems to struggle with Float32 inputs in this environment)
        img = np.clip(img, 0.0, 1.0)
        img = (img * 65535.0).astype(np.uint16)
        
        # Write using tifffile
        tifffile.imwrite(out_path, img)
        print(f"Successfully converted {raw_path} to {out_path} (16-bit) using tifffile.")
        
    except Exception as e:
        print(f"Exception in write_tiff: {e}")
        sys.exit(3)

if __name__ == "__main__":
    if len(sys.argv) < 6:
        print("Usage: python cosmic_bridge.py <raw_path> <width> <height> <channels> <out_path>")
        sys.exit(1)
        
    raw = sys.argv[1]
    w = int(sys.argv[2])
    h = int(sys.argv[3])
    c = int(sys.argv[4])
    out = sys.argv[5]
    
    write_tiff(raw, w, h, c, out)
