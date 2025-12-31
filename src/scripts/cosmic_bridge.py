
import sys
import os
import numpy as np

# Try to import tifffile
try:
    import tifffile
except ImportError:
    print("Error: tifffile module not found. Please install it (pip install tifffile).")
    sys.exit(1)

def save(tiff_out_path, w, h, c, raw_in_path):
    try:
        # Read raw float32 data
        data = np.fromfile(raw_in_path, dtype=np.float32)
        
        expected_size = w * h * c
        if data.size != expected_size:
            # Fallback: sometimes raw file might be incomplete or size logic differs. 
            print(f"Error: Raw file size mismatch. Expected {expected_size} floats, got {data.size}.")
            sys.exit(2)
            
        # Reshape (Assuming HWC or HW based on channels)
        if c == 1:
            img = data.reshape((h, w))
        else:
            img = data.reshape((h, w, c))
            
        # Convert to 16-bit for Cosmic Clarity input
        # Clamp to 0-1
        img = np.clip(img, 0.0, 1.0)
        img = (img * 65535.0).astype(np.uint16)
        
        # Save Tiff
        # photometric='minisblack' helps for mono images in some viewers/tools
        photometric = 'minisblack' if c == 1 else 'rgb'
        tifffile.imwrite(tiff_out_path, img, photometric=photometric)
        print(f"Saved TIFF: {tiff_out_path}")
        
    except Exception as e:
        print(f"Error in save: {e}")
        sys.exit(3)

def load(tiff_in_path, raw_out_path):
    try:
        # Read Tiff
        if not os.path.exists(tiff_in_path):
             print(f"Error: Output Tiff not found: {tiff_in_path}")
             sys.exit(4)

        img = tifffile.imread(tiff_in_path)
        
        # Determine dimensions
        # tifffile can return (H, W) or (H, W, C) or (C, H, W) depending on metadata
        # We need to normalize to H W C and float32 0-1
        
        # Convert to float32
        if img.dtype == np.uint16:
            img = img.astype(np.float32) / 65535.0
        elif img.dtype == np.uint8:
            img = img.astype(np.float32) / 255.0
        else:
            # Already float?
            img = img.astype(np.float32)
            
        # Handle shapes
        # If 2D (H, W) -> C=1
        if img.ndim == 2:
            h, w = img.shape
            c = 1
        elif img.ndim == 3:
            # Check for channels first vs channels last
            d0, d1, d2 = img.shape
            if d0 <= 4 and d1 > 4 and d2 > 4:
                # CHW -> Traspose to HWC
                img = np.transpose(img, (1, 2, 0))
                h, w, c = img.shape
            else:
                # HWC
                h, w, c = img.shape
        else:
            print(f"Error: Unexpected Image Dimensions: {img.shape}")
            sys.exit(5)
            
        # Save raw
        img.tofile(raw_out_path)
        
        # Print RESULT marker for C++ to parse
        print(f"RESULT: {w} {h} {c}")
        
    except Exception as e:
        print(f"Error in load: {e}")
        sys.exit(6)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python cosmic_bridge.py <save|load> ...")
        sys.exit(1)
        
    cmd = sys.argv[1]
    
    if cmd == "save":
        # Args: script save tiff_out w h c raw_in
        if len(sys.argv) < 7:
             print("Usage: save <tiff_out> <w> <h> <c> <raw_in>")
             sys.exit(1)
        
        t_out = sys.argv[2]
        w = int(sys.argv[3])
        h = int(sys.argv[4])
        c = int(sys.argv[5])
        r_in = sys.argv[6]
        
        save(t_out, w, h, c, r_in)
        
    elif cmd == "load":
        # Args: script load tiff_in raw_out
        if len(sys.argv) < 4:
            print("Usage: load <tiff_in> <raw_out>")
            sys.exit(1)
            
        t_in = sys.argv[2]
        r_out = sys.argv[3]
        
        load(t_in, r_out)
        
    else:
        print(f"Unknown command: {cmd}")
        sys.exit(1)