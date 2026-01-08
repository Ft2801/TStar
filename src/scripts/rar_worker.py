import sys
import os
import argparse
import numpy as np
import tifffile
import time

try:
    import onnxruntime as ort
except ImportError:
    print("Error: onnxruntime not installed. Please run: pip install onnxruntime")
    sys.exit(1)

def _hann2d(n):
    w = np.hanning(n).astype(np.float32)
    return (w[:, None] * w[None, :])

def _tile_indices(n, patch, overlap):
    stride = patch - overlap
    if patch >= n:
        return [0]
    idx, pos = [], 0
    while True:
        if pos + patch >= n:
            idx.append(n - patch)
            break
        idx.append(pos); pos += stride
    return sorted(set(idx))

def _pad_C_HW(arr, patch):
    # arr is (C, H, W)
    C, H, W = arr.shape
    pad_h = max(0, patch - H)
    pad_w = max(0, patch - W)
    if pad_h or pad_w:
        arr = np.pad(arr, ((0,0),(0,pad_h),(0,pad_w)), mode="edge")
    return arr, H, W

def _get_model_required_patch(sess):
    try:
        shp = sess.get_inputs()[0].shape # e.g. [1, 1, 512, 512] or ['N','C',512,512]
        if len(shp) >= 2:
            h = shp[-2]
            w = shp[-1]
            if isinstance(h, int) and isinstance(w, int) and h == w and h > 0:
                return int(h)
    except:
        pass
    return None


def _preserve_border(dst: np.ndarray, src: np.ndarray, px: int = 10) -> np.ndarray:
    """
    Copy a px-wide ring from src to dst to avoid artifacts.
    Expects (C, H, W) layout.
    """
    if px <= 0: return dst
    if dst.shape != src.shape: return dst

    h, w = dst.shape[1:3] # C, H, W
    px = int(max(0, min(px, h // 2, w // 2)))
    if px == 0: return dst

    # top & bottom
    dst[:, :px, :]  = src[:, :px, :]
    dst[:, -px:, :] = src[:, -px:, :]
    # left & right
    dst[:, :, :px]  = src[:, :, :px]
    dst[:, :, -px:] = src[:, :, -px:]
    return dst


def run_onnx_tiled(session, img, patch_size=512, overlap=64):
    # Prepare Input: Ensure (C, H, W) float32 [0..1]
    # Input img: (H, W, C) or (H, W) read from Tiff
    if img.ndim == 2:
        # (H,W) -> (1,H,W)
        arr = img[np.newaxis, ...]
    elif img.ndim == 3:
        # (H,W,C) -> (C,H,W)
        arr = img.transpose(2, 0, 1)
    
    # Store original dtype info if needed, but we assume float input from bridge
    arr = arr.astype(np.float32)
    
    # Pad
    arr_padded, H0, W0 = _pad_C_HW(arr, patch_size)
    C, H, W = arr_padded.shape
    
    win = _hann2d(patch_size)
    out_buf = np.zeros_like(arr_padded, dtype=np.float32)
    wgt_buf = np.zeros_like(arr_padded, dtype=np.float32)
    
    hs = _tile_indices(H, patch_size, overlap)
    ws = _tile_indices(W, patch_size, overlap)
    
    inp_name = session.get_inputs()[0].name
    
    total_tiles = len(hs) * len(ws) * C
    processed = 0
    
    print(f"INFO: Processing {total_tiles} tiles...")
    
    last_pct = -1
    for c in range(C):
        for i in hs:
            for j in ws:
                # Extract patch (1, P, P)
                patch = arr_padded[c:c+1, i:i+patch_size, j:j+patch_size]
                # Add batch dim -> (1, 1, P, P)
                inp = np.ascontiguousarray(patch[np.newaxis, ...])
                
                # Inference
                res = session.run(None, {inp_name: inp})[0] # (1, 1, P, P)
                res = np.squeeze(res, axis=0) # (1, P, P)
                
                # Accumulate
                out_buf[c:c+1, i:i+patch_size, j:j+patch_size] += res * win
                wgt_buf[c:c+1, i:i+patch_size, j:j+patch_size] += win
                
                processed += 1
                pct = int(processed / total_tiles * 100)
                if pct > last_pct:
                     print(f"Progress: {pct}%")
                     sys.stdout.flush()
                     last_pct = pct

    # Normalize
    wgt_buf[wgt_buf == 0] = 1.0
    arr_padded = out_buf / wgt_buf
    
    # Restore
    res_final = arr_padded[:, :H0, :W0] # Crop padding
    res_final = np.clip(res_final, 0.0, 1.0)
    
    # Preserve Border (10px) - Pass original array reference (which might be modified) 
    # Actually wait, 'arr' was transposed/modified at start. 
    # But 'arr' before padding is the correct reference for shape.
    # 'arr' is (C, H, W). res_final is (C, H, W).
    res_final = _preserve_border(res_final, arr, px=10)

    # Transpose back to HWC for saving
    if res_final.shape[0] == 1:
        # (1, H, W) -> (H, W) or (H, W, 1) usually tifffile handles 2D
        return res_final[0]
    else:
        # (C, H, W) -> (H, W, C)
        return res_final.transpose(1, 2, 0)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--patch", type=int, default=512)
    parser.add_argument("--overlap", type=int, default=64)
    parser.add_argument("--provider", default="CPU")
    args = parser.parse_args()
    
    if not os.path.exists(args.input):
        print(f"Error: Input file not found {args.input}")
        sys.exit(1)
        
    # Load Input
    try:
        data = tifffile.imread(args.input).astype(np.float32)
        print(f"DEBUG: Input Shape={data.shape} Min={data.min()} Max={data.max()} Mean={data.mean()}")
    except Exception as e:
        # Fallback to astropy if tifffile fails (unlikely for proper tiff but safety)
        try:
            from astropy.io import fits
            with fits.open(args.input) as hdul:
                data = hdul[0].data.astype(np.float32)
        except:
            print(f"Error: Failed to load input image: {e}")
            sys.exit(1)
            
    # Setup ONNX Providers
    available = ort.get_available_providers()
    providers = []
    
    # If user explicitly requests a provider, prioritize it (but fallback to CPU)
    if args.provider == "DirectML" and "DmlExecutionProvider" in available:
        providers.append("DmlExecutionProvider")
    elif args.provider == "CUDA" and "CUDAExecutionProvider" in available:
        providers.append("CUDAExecutionProvider")
    elif args.provider == "CoreML" and "CoreMLExecutionProvider" in available:
        providers.append("CoreMLExecutionProvider")
        
    # Always append CPU as fallback
    if "CPUExecutionProvider" in available:
        providers.append("CPUExecutionProvider")
        
    print(f"INFO: Requesting providers: {providers}")
    
    try:
        sess = ort.InferenceSession(args.model, providers=providers)
    except Exception as e:
        print(f"Error: Failed to load model: {e}")
        sys.exit(1)

    # Check for fixed patch size
    fixed_patch = _get_model_required_patch(sess)
    patch_size = args.patch
    if fixed_patch is not None:
        print(f"INFO: Model enforces fixed patch size: {fixed_patch}")
        patch_size = fixed_patch
        
    # Run
    start = time.time()
    try:
        result = run_onnx_tiled(sess, data, patch_size, args.overlap)
    except Exception as e:
        print(f"Error: Inference failed: {e}")
        # Print full traceback for debugging if needed
        import traceback
        traceback.print_exc()
        sys.exit(1)
        
    dt = time.time() - start
    print(f"INFO: Inference finished in {dt:.2f}s")
    
    print(f"DEBUG: Output Shape={result.shape} Min={result.min()} Max={result.max()} Mean={result.mean()}")

    print(f"DEBUG: Output Shape={result.shape} Min={result.min()} Max={result.max()} Mean={result.mean()}")

    # Save
    if args.output.endswith(".raw"):
         # Write dimensions to stdout or assume caller knows W, H, C?
         # C++ knows the input size, and output size == input size.
         # Just write raw bytes.
         result.tofile(args.output)
         print("RESULT: Saved RAW")
    else:
         tifffile.imwrite(args.output, result)
         print("RESULT: Saved TIFF")

if __name__ == "__main__":
    main()
