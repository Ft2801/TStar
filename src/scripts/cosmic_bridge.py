
"""
Cosmic Clarity ONNX Bridge for TStar

Usage:
  python cosmic_bridge.py process <params.json> <input.raw> <output.raw>
  python cosmic_bridge.py save    <tiff_out> <w> <h> <c> <raw_in>
  python cosmic_bridge.py load    <tiff_in> <raw_out>
"""
import sys
import os
import json
import math
import numpy as np

try:
    import tifffile
except ImportError:
    print("Error: tifffile not found. pip install tifffile")
    sys.exit(1)

try:
    import onnxruntime as ort
except ImportError:
    ort = None  # only needed for 'process' command


# ============================================================================
#  Color-space helpers  (BT.601 YCbCr, same as saspro)
# ============================================================================
_M_RGB2YCBCR = np.array([
    [ 0.299,      0.587,      0.114    ],
    [-0.168736,  -0.331264,   0.5      ],
    [ 0.5,       -0.418688,  -0.081312 ],
], dtype=np.float32)

_M_YCBCR2RGB = np.array([
    [1.0,  0.0,       1.402    ],
    [1.0, -0.344136, -0.714136 ],
    [1.0,  1.772,     0.0      ],
], dtype=np.float32)


def extract_luminance(rgb):
    """RGB (H,W,3) -> Y (H,W), Cb (H,W), Cr (H,W).  Cb/Cr offset by +0.5."""
    rgb = np.asarray(rgb, dtype=np.float32)
    ycbcr = rgb @ _M_RGB2YCBCR.T
    return ycbcr[..., 0], ycbcr[..., 1] + 0.5, ycbcr[..., 2] + 0.5


def merge_luminance(y, cb, cr):
    """Y (H,W), Cb (H,W), Cr (H,W) -> RGB (H,W,3).  Cb/Cr were offset +0.5.
    Clips each component to [0,1] BEFORE reconstruction (saspro exact)."""
    y = np.asarray(y, dtype=np.float32)
    cb = np.asarray(cb, dtype=np.float32)
    cr = np.asarray(cr, dtype=np.float32)
    
    # CRITICAL: clip Y, Cb, Cr separately BEFORE doing inverse transform
    y = np.clip(y, 0.0, 1.0)
    cb = np.clip(cb, 0.0, 1.0)
    cr = np.clip(cr, 0.0, 1.0)
    
    # Now do inverse transform
    ycbcr = np.stack([y, cb - 0.5, cr - 0.5], axis=-1)
    rgb = ycbcr @ _M_YCBCR2RGB.T
    return np.clip(rgb, 0.0, 1.0)


# ============================================================================
#  Tiling helpers  (chunk_size=256, overlap=64, border_ignore=16)
# ============================================================================
CHUNK  = 256
OVERLAP = 64
BORDER = 16
PAD_MULT = 16


def pad_to_mult(arr2d, mult=PAD_MULT):
    """Reflect-pad H,W to multiples of `mult`. Returns (padded, orig_h, orig_w)."""
    h, w = arr2d.shape[:2]
    ph = (mult - h % mult) % mult
    pw = (mult - w % mult) % mult
    if ph == 0 and pw == 0:
        return arr2d, h, w
    if arr2d.ndim == 2:
        return np.pad(arr2d, ((0, ph), (0, pw)), mode='reflect'), h, w
    return np.pad(arr2d, ((0, ph), (0, pw), (0, 0)), mode='reflect'), h, w


def split_chunks(plane2d, chunk=CHUNK, overlap=OVERLAP):
    """Split 2-D array into overlapping chunks. Yields (chunk, row, col)."""
    H, W = plane2d.shape
    step = chunk - overlap
    for i in range(0, H, step):
        for j in range(0, W, step):
            ei, ej = min(i + chunk, H), min(j + chunk, W)
            yield plane2d[i:ei, j:ej], i, j


def stitch_chunks(chunks, shape, border=16):
    """Stitch overlapping chunks with border-ignore weights (SASPRO EXACT)."""
    H, W = shape[0], shape[1]
    stitched = np.zeros((H, W), dtype=np.float32)
    weights  = np.zeros((H, W), dtype=np.float32)

    for chunk, i, j in chunks:
        h, w = chunk.shape
        if h <= 0 or w <= 0: continue

        bh = min(border, h // 2)
        bw = min(border, w // 2)

        y0 = i + bh
        y1 = i + h - bh
        x0 = j + bw
        x1 = j + w - bw

        if y1 <= y0 or x1 <= x0: continue

        inner = chunk[bh:h-bh, bw:w-bw]

        # Clip destination
        yy0 = max(0, y0); yy1 = min(H, y1)
        xx0 = max(0, x0); xx1 = min(W, x1)

        if yy1 <= yy0 or xx1 <= xx0: continue

        # Clip source
        sy0 = yy0 - y0; sy1 = sy0 + (yy1 - yy0)
        sx0 = xx0 - x0; sx1 = sx0 + (xx1 - xx0)

        dst_slice = (slice(yy0, yy1), slice(xx0, xx1))
        src_slice = (slice(sy0, sy1), slice(sx0, sx1))

        stitched[dst_slice] += inner[src_slice]
        weights[dst_slice] += 1.0

    stitched /= np.maximum(weights, 1.0)
    return stitched


def split_chunks_rgb(img3, chunk=CHUNK, overlap=OVERLAP):
    """Split H,W,3 image. Yields (chunk_hwc, row, col)."""
    H, W, _ = img3.shape
    step = chunk - overlap
    for i in range(0, H, step):
        for j in range(0, W, step):
            ei, ej = min(i + chunk, H), min(j + chunk, W)
            yield img3[i:ei, j:ej], i, j


def stitch_chunks_rgb(chunks, shape, border=16):
    """Stitch overlapping RGB chunks with border-ignore weights (SASPRO EXACT)."""
    H, W, C = shape
    stitched = np.zeros((H, W, C), dtype=np.float32)
    weights  = np.zeros((H, W, 1), dtype=np.float32)

    for chunk, i, j in chunks:
        h, w, _ = chunk.shape
        if h <= 0 or w <= 0: continue

        bh = min(border, h // 2)
        bw = min(border, w // 2)

        y0 = i + bh
        y1 = i + h - bh
        x0 = j + bw
        x1 = j + w - bw

        if y1 <= y0 or x1 <= x0: continue

        inner = chunk[bh:h-bh, bw:w-bw]

        yy0 = max(0, y0); yy1 = min(H, y1)
        xx0 = max(0, x0); xx1 = min(W, x1)

        if yy1 <= yy0 or xx1 <= xx0: continue

        sy0 = yy0 - y0; sy1 = sy0 + (yy1 - yy0)
        sx0 = xx0 - x0; sx1 = sx0 + (xx1 - xx0)
        
        # Slicing for RGB
        dst_slice = (slice(yy0, yy1), slice(xx0, xx1))
        src_slice = (slice(sy0, sy1), slice(sx0, sx1))

        stitched[dst_slice] += inner[src_slice]
        weights[dst_slice] += 1.0

    stitched /= np.maximum(weights, 1.0)
    return stitched


# ============================================================================
#  ONNX inference helpers
# ============================================================================
def _pick_io(session):
    """Return (img_input_name, psf_input_name_or_None, output_name)."""
    ins = session.get_inputs()
    out_name = session.get_outputs()[0].name
    img_name = ins[0].name
    psf_name = None
    for inp in ins:
        rank = len(inp.shape) if inp.shape else 0
        if rank == 4:
            img_name = inp.name
        elif rank in (1, 2):
            psf_name = inp.name
    if len(ins) == 1:
        psf_name = None
    return img_name, psf_name, out_name


def _infer_2d(session, chunk2d, psf01=None):
    """Run model on a 2-D plane.  Input tiled to (1,3,Hp,Wp). Returns 2-D."""
    # Match Saspro: Use _pad2d_to_multiple logic (reflect, mult=16)
    chunk_p, h0, w0 = pad_to_mult(chunk2d.astype(np.float32), mult=16)
    
    # (Hp,Wp) -> (1,1,Hp,Wp) -> (1,3,Hp,Wp)
    inp = chunk_p[np.newaxis, np.newaxis, :, :].astype(np.float32)
    inp = np.tile(inp, (1, 3, 1, 1))

    img_name, psf_name, out_name = _pick_io(session)
    feeds = {img_name: inp}
    if psf_name is not None and psf01 is not None:
        feeds[psf_name] = np.array([[float(psf01)]], dtype=np.float32)
    elif psf_name is not None and psf01 is None:
        # Default PSF if model needs it but none provided (e.g. denoise model wrongly loaded as sharpen?)
        feeds[psf_name] = np.array([[0.5]], dtype=np.float32)

    out = session.run([out_name], feeds)[0]
    # (1,3,Hp,Wp) -> channel 0  (same as saspro)
    if out.ndim == 4:
        y = out[0, 0]
    elif out.ndim == 3:
        y = out[0]
        if y.shape[0] in (1, 3):
             y = y[0]
    else:
        y = out
    
    # Return unclipped float32 to preserve peaks
    return y[:h0, :w0]


def _infer_rgb(session, chunk_hwc):
    """Run model on an RGB chunk (H,W,3). Returns (H,W,3)."""
    # Match Saspro: pad each channel (H,W,3) to multiple of 16
    chunk_p, h0, w0 = pad_to_mult(chunk_hwc.astype(np.float32), mult=16)
    
    # (Hp,Wp,3) -> (3,Hp,Wp) -> (1,3,Hp,Wp)
    inp = np.transpose(chunk_p, (2, 0, 1))[None, ...].astype(np.float32)

    img_name, _, out_name = _pick_io(session)
    out = session.run([out_name], {img_name: inp})[0]
    
    # (1,3,Hp,Wp) -> (3,Hp,Wp) -> (Hp,Wp,3)
    if out.ndim == 4:
        y = out[0]
    else:
        y = out
    
    y = np.transpose(y, (1, 2, 0))
    # Return unclipped
    return y[:h0, :w0]


def blend(orig, processed, amount):
    a = float(np.clip(amount, 0.0, 1.0))
    return (1.0 - a) * orig + a * processed


def stretch_image_unlinked_rgb(image_rgb, target_median=0.25):
    """Apply MTF stretch per-channel (saspro exact formula with safe-guards).
    Returns (stretched, orig_min, orig_meds) for later unstretch.
    """
    x = image_rgb.astype(np.float32, copy=True)
    orig_min = float(np.min(x))
    x -= orig_min
    
    t = float(target_median)
    channels = 1 if x.ndim == 2 else x.shape[2]
    orig_meds = []
    
    for ch in range(channels):  # FIX: renamed from 'c' to avoid shadowing global 'c'
        if x.ndim == 2:
            plane = x
        else:
            plane = x[..., ch]
        
        m0 = float(np.median(plane))
        orig_meds.append(m0)
        
        if m0 != 0.0:
            # CRITICAL: use np.where() safe-guard like saspro denoise_engine
            # If |denom| < 1e-12, keep original value (don't divide)
            denom = m0 * (t + plane - 1.0) - t * plane
            res = np.where(np.abs(denom) > 1e-12, ((m0 - 1.0) * t * plane) / denom, plane)
            if x.ndim == 2:
                x = res
            else:
                x[..., ch] = res
    
    x = np.clip(x, 0.0, 1.0)
    return x, orig_min, orig_meds


def unstretch_image_unlinked_rgb(image_rgb, orig_meds, orig_min, was_mono):
    """Inverse of stretch_image_unlinked_rgb with safe-guards (saspro exact)."""
    y = image_rgb.astype(np.float32, copy=True)
    t = float(0.25)  # target_median
    
    channels = 1 if y.ndim == 2 else y.shape[2]
    
    for ch in range(channels):  # FIX: renamed from 'c' to avoid shadowing global 'c'
        m0 = float(orig_meds[ch])
        
        if m0 != 0.0:
            if y.ndim == 2:
                yc = y
            else:
                yc = y[..., ch]
            
            # Inverse formula with safe-guard
            denom = t * (m0 - 1.0 + yc) - yc * m0
            num = yc * m0 * (t - 1.0)
            res = np.where(np.abs(denom) > 1e-12, num / denom, yc)
            
            if y.ndim == 2:
                y = res
            else:
                y[..., ch] = res
    
    y += float(orig_min)
    y = np.clip(y, 0.0, 1.0)
    
    if was_mono and y.ndim == 3 and y.shape[2] == 3:
        y = np.mean(y, axis=2, keepdims=True)
    return y


def encode_psf(psf_radius, lo=1.0, hi=8.0):
    """Encode PSF radius [1..8] to [0..1] via log2, matching saspro."""
    return float(np.clip(
        (math.log2(psf_radius) - math.log2(lo)) / (math.log2(hi) - math.log2(lo)),
        0.0, 1.0
    ))


def _is_triplicated_mono(rgb_image, eps=1e-7):
    """Check if an RGB image is actually mono (all channels equal).
    This detects when a mono image was tripled for processing.
    Matches saspro denoise_engine._is_triplicated_mono()
    """
    if rgb_image.ndim != 3 or rgb_image.shape[2] != 3:
        return False
    r = rgb_image[..., 0]
    g = rgb_image[..., 1]
    b = rgb_image[..., 2]
    return (np.max(np.abs(r - g)) <= eps) and (np.max(np.abs(r - b)) <= eps)


def _every(n, total=0, interval=10):
    """Always return True - print every single chunk without filtering."""
    return True


# ============================================================================
#  Main processing  (called by TStar C++)
# ============================================================================
def process(params_file, raw_in, raw_out):
    if ort is None:
        print("Error: onnxruntime not found. pip install onnxruntime", flush=True)
        sys.exit(1)

    with open(params_file) as f:
        params = json.load(f)

    w      = int(params['width'])
    h      = int(params['height'])
    c      = int(params['channels'])
    mode   = params.get('mode', 'sharpen')
    mdir   = params.get('models_dir', '')
    use_gpu = bool(params.get('use_gpu', False))

    print(f"[Bridge] Image {w}x{h}x{c}  mode={mode}", flush=True)

    # Read raw float32
    data = np.fromfile(raw_in, dtype=np.float32)
    if data.size != w * h * c:
        print(f"Error: expected {w*h*c} floats, got {data.size}", flush=True)
        sys.exit(2)
    img = data.reshape((h, w, c)) if c > 1 else data.reshape((h, w, 1))
    # Removed initial clip to preserve HDR data (stars > 1.0) for correct color recovery
    img = img.astype(np.float32)

    was_mono = (c == 1)
    
    # CRITICAL: Determine if input is ACTUALLY mono (not tripled mono for processing)
    # This will be used throughout processing to make correct decisions
    actual_mono = was_mono  # Will be refined after initial processing
    
    # Debug: check input ranges
    print(f"[Bridge] Input: min={float(np.min(img)):.4f} max={float(np.max(img)):.4f} med={float(np.median(img)):.4f}", flush=True)

    # Determine threshold based on mode
    if mode == 'denoise':
        stretch_threshold = 0.05
    else:
        stretch_threshold = 0.08  # Standard for sharpen/both
    
    # Saspro alignment: Check stretch metric on ORIGINAL image first (global median)
    metric_val = float(np.median(img - np.min(img)))
    stretch_needed = (metric_val < stretch_threshold)
    print(f"[Bridge] Stretch metric={metric_val:.6f} needed={stretch_needed}", flush=True)

    # Sanity Check for HDR data: if not stretching, ensure range is 0-1 compatible
    # If the user passes raw linear data that is > 1.0 (e.g. 60000.0) but metric fails (unlikely for linear data)
    # or if metric says "No Stretch", we assume inputs are normalized.
    # However, if max > 1.0 and we DON'T stretch, models will fail.
    # We should normalize 16-bit data if it looks like it (max > 255 typically)
    img_max = float(np.max(img))
    if not stretch_needed and img_max > 2.0:
        print(f"[Bridge] WARNING: Input max={img_max} but stretch not requested. Models expect 0..1.", flush=True)
        print(f"[Bridge] Normalizing by dividing by 65535.0 (Assuming 16-bit)", flush=True)
        img /= 65535.0
        # Re-evaluate stretch needed on normalized data?
        # Typically if it was > 2.0, it was certainly linear 16-bit or similar.
        # Let's force re-check.
        metric_val = float(np.median(img - np.min(img)))
        stretch_needed = (metric_val < stretch_threshold)
        print(f"[Bridge] Re-evaluated metric={metric_val:.6f} needed={stretch_needed}", flush=True)

    if stretch_needed:
        print(f"[Bridge] Applying stretch to original...", flush=True)
        if was_mono:
            stretched_core, orig_min, orig_meds = stretch_image_unlinked_rgb(img[..., 0], target_median=0.25)
        else:
            stretched_core, orig_min, orig_meds = stretch_image_unlinked_rgb(img, target_median=0.25)
    else:
        print(f"[Bridge] No stretch needed", flush=True)
        if was_mono:
            stretched_core = img[..., 0]
        else:
            stretched_core = img
        orig_min = None
        orig_meds = None

    # Apply Border (Saspro: padded with median values)
    if was_mono:
        # stretched_core is 2D from above
        med_val = float(np.median(stretched_core))
        stretched = np.pad(stretched_core, ((16,16), (16,16)), mode='constant', constant_values=med_val)
    else:
        # Color: pad each channel with the median of THIS channel
        chans = []
        for cidx in range(3):
            plane = stretched_core[..., cidx]
            p_med = float(np.median(plane))
            p_pad = np.pad(plane, ((16,16), (16,16)), mode='constant', constant_values=p_med)
            chans.append(p_pad)
        stretched = np.stack(chans, axis=-1)

    print(f"[Bridge] Prepared: min={float(np.min(stretched)):.4f} max={float(np.max(stretched)):.4f}", flush=True)

    # Ensure 3-channel for processing
    if was_mono:
        img3 = np.stack([stretched, stretched, stretched], axis=-1)
    else:
        img3 = stretched

    # ONNX providers
    providers = ['CPUExecutionProvider']
    if use_gpu:
        try:
            avail = ort.get_available_providers()
            print(f"[Bridge] Available ONNX providers: {avail}", flush=True)
            if 'DmlExecutionProvider' in avail:
                providers.insert(0, 'DmlExecutionProvider')
            elif 'CUDAExecutionProvider' in avail:
                providers.insert(0, 'CUDAExecutionProvider')
        except Exception as e:
            print(f"[Bridge] Error checking providers: {e}", flush=True)
    
    print(f"[Bridge] Selected providers: {providers}", flush=True)

    sess_opts = ort.SessionOptions()
    sess_opts.intra_op_num_threads = max(1, os.cpu_count() or 4)

    result = img3.copy()
    print(f"[Bridge] Before sharpen: min={float(np.min(result)):.4f} max={float(np.max(result)):.4f} med={float(np.median(result)):.4f}", flush=True)

    # ---------------------------------------------------------------- SHARPEN
    if mode in ('sharpen', 'both'):
        sharpen_mode   = params.get('sharpen_mode', 'Both')
        stellar_amt    = float(params.get('stellar_amount', 0.5))
        ns_amt         = float(params.get('nonstellar_amount', 0.5))
        ns_psf         = float(params.get('nonstellar_psf', 3.0))
        auto_psf       = bool(params.get('auto_psf', True))
        sep_channels   = bool(params.get('separate_channels_sharpen', False))

        # Extract luminance (or per-channel)
        if sep_channels and not was_mono:
            planes = [result[..., ch] for ch in range(3)]
            labels = ['R', 'G', 'B']
        else:
            y_plane, cb, cr = extract_luminance(result)
            planes = [y_plane]
            labels = ['Y']
            print(f"[Bridge] Extracted Y: min={float(np.min(y_plane)):.4f} max={float(np.max(y_plane)):.4f} med={float(np.median(y_plane)):.4f}", flush=True)

        sharpened_planes = []
        for plane, label in zip(planes, labels):
            # --- Stellar ---
            if sharpen_mode in ('Both', 'Stellar Only') and stellar_amt > 0:
                model_path = os.path.join(mdir, 'deep_sharp_stellar_AI4.onnx')
                if os.path.exists(model_path):
                    print(f"[Bridge] Stellar sharpen ({label}) amount={stellar_amt:.2f}", flush=True)
                    sess = ort.InferenceSession(model_path, sess_options=sess_opts, providers=providers)
                    out_chunks = []
                    total = sum(1 for _ in split_chunks(plane))
                    for idx, (ch, i, j) in enumerate(split_chunks(plane), start=1):
                        y = _infer_2d(sess, ch)
                        out_chunks.append((blend(ch, y, stellar_amt), i, j))
                        if _every(idx, total, interval=10):
                            print(f"[Bridge]   Stellar {label} chunk {idx}/{total}", flush=True)
                    plane = stitch_chunks(out_chunks, plane.shape)
                    print(f"[Bridge] Stellar {label} stitched: min={float(np.min(plane)):.4f} max={float(np.max(plane)):.4f}", flush=True)
                    del sess
                else:
                    print(f"[Bridge] WARNING: stellar model not found", flush=True)

            # --- Non-Stellar ---
            if sharpen_mode in ('Both', 'Non-Stellar Only') and ns_amt > 0:
                model_path = os.path.join(mdir, 'deep_nonstellar_sharp_conditional_psf_AI4.onnx')
                if os.path.exists(model_path):
                    r = float(np.clip(ns_psf, 1.0, 8.0))
                    psf01 = encode_psf(r)
                    print(f"[Bridge] Non-stellar sharpen ({label}) amount={ns_amt:.2f} psf={r:.1f} enc={psf01:.3f}", flush=True)
                    sess = ort.InferenceSession(model_path, sess_options=sess_opts, providers=providers)
                    out_chunks = []
                    total = sum(1 for _ in split_chunks(plane))
                    for idx, (ch, i, j) in enumerate(split_chunks(plane), start=1):
                        y = _infer_2d(sess, ch, psf01=psf01)
                        out_chunks.append((blend(ch, y, ns_amt), i, j))
                        if _every(idx, total, interval=10):
                            print(f"[Bridge]   NonStellar {label} chunk {idx}/{total}", flush=True)
                    plane = stitch_chunks(out_chunks, plane.shape)
                    print(f"[Bridge] NonStellar {label} stitched: min={float(np.min(plane)):.4f} max={float(np.max(plane)):.4f}", flush=True)
                    del sess
                else:
                    print(f"[Bridge] WARNING: non-stellar model not found", flush=True)

            sharpened_planes.append(plane)

        # Re-assemble
        if sep_channels and not was_mono:
            for ch_idx, sp in enumerate(sharpened_planes):
                result[..., ch_idx] = sp
        else:
            result = merge_luminance(sharpened_planes[0], cb, cr)
            print(f"[Bridge] After sharpen merge: min={float(np.min(result)):.4f} max={float(np.max(result)):.4f} med={float(np.median(result)):.4f}", flush=True)
        
        # CRITICAL FIX: If input was mono AND we're NOT doing denoise after, reduce back to mono now
        # OR if we are doing denoise, we need checks.
        # This prevents channel count mismatch when only sharpen is requested
        if was_mono and mode == 'sharpen':
             # Average channels back to mono before returning
            result = np.mean(result, axis=2, keepdims=True).astype(np.float32)
            print(f"[Bridge] Reduced sharpen output back to mono: shape={result.shape}", flush=True)

    # ---------------------------------------------------------------- DENOISE
    if mode in ('denoise', 'both'):
        dn_strength = float(params.get('denoise_luma', 0.5))
        dn_color    = float(params.get('denoise_color', 0.5))
        dn_mode     = params.get('denoise_mode', 'full')
        
        # CRITICAL FIX: Re-detect if input is actually mono (matches saspro denoise_engine)
        # This catches cases where sharpen tripled it but it's still logically mono
        detected_mono = was_mono if mode == 'denoise' else _is_triplicated_mono(result)
        
        if detected_mono:
            print(f"[Bridge] Denoise detected mono input (channels are identical)", flush=True)
            # If we are effectively mono, force use of Mono Model only, avoiding Color Model color shifts
            # This handles the case where user passed Mono image but App treated it as RGB
            dn_mode = 'luminance' 
            
        # Verify mono model availability
        model_name_mono = 'deep_denoise_mono_AI4.onnx'
        model_path_mono = os.path.join(mdir, model_name_mono)
        if not os.path.exists(model_path_mono):
            model_path_mono = os.path.join(mdir, 'deep_denoise_mono_AI4_lite.onnx')
            
        # Verify color model availability
        model_name_color = 'deep_denoise_color_AI4.onnx'
        model_path_color = os.path.join(mdir, model_name_color)
        if not os.path.exists(model_path_color):
            model_path_color = os.path.join(mdir, 'deep_denoise_color_AI4_lite.onnx')

        if detected_mono:
            if os.path.exists(model_path_mono):
                print(f"[Bridge] Denoise (mono) strength={dn_strength:.2f}", flush=True)
                sess = ort.InferenceSession(model_path_mono, sess_options=sess_opts, providers=providers)
                plane = result[..., 0]
                out_chunks = []
                total = sum(1 for _ in split_chunks(plane))
                for idx, (ch, i, j) in enumerate(split_chunks(plane), start=1):
                    y = _infer_2d(sess, ch)
                    out_chunks.append((blend(ch, y, dn_strength), i, j))
                    if _every(idx, total, interval=10):
                        print(f"[Bridge]   Denoise mono: {idx}/{total}", flush=True)
                denoised_plane = stitch_chunks(out_chunks, plane.shape)
                result[..., 0] = denoised_plane
                del sess
            else:
                print(f"[Bridge] WARNING: denoise mono model not found", flush=True)
        else:
            # RGB Denoise Logic (matches saspro orchestration)
            # 1. Always denoise Luma with MONO model
            y_in, cb_in, cr_in = extract_luminance(result)
            y_denoised = y_in 
            
            if os.path.exists(model_path_mono):
                print(f"[Bridge] Denoise (luma) strength={dn_strength:.2f}", flush=True)
                sess = ort.InferenceSession(model_path_mono, sess_options=sess_opts, providers=providers)
                out_chunks = []
                total = sum(1 for _ in split_chunks(y_in))
                for idx, (ch, i, j) in enumerate(split_chunks(y_in), start=1):
                    y = _infer_2d(sess, ch)
                    out_chunks.append((blend(ch, y, dn_strength), i, j))
                    if _every(idx, total, interval=10):
                        print(f"[Bridge]   Denoise luma: {idx}/{total}", flush=True)
                y_denoised = stitch_chunks(out_chunks, y_in.shape)
                del sess
            else:
                print(f"[Bridge] WARNING: denoise mono model not found (skipping luma denoise)", flush=True)

            # 2. If 'full' mode, denoise RGB with COLOR model
            # *** FIXED: Use conservative chroma blending to avoid color shift artifacts ***
            cb_out, cr_out = cb_in, cr_in  # default: keep original chroma
            
            if dn_mode == 'full':
                if os.path.exists(model_path_color):
                    print(f"[Bridge] Denoise (color) blend strength={dn_color:.2f}", flush=True)
                    sess_color = ort.InferenceSession(model_path_color, sess_options=sess_opts, providers=providers)
                    out_chunks = []
                    total = sum(1 for _ in split_chunks_rgb(result))
                    for idx, (ch, i, j) in enumerate(split_chunks_rgb(result), start=1):
                        y = _infer_rgb(sess_color, ch)
                        # Saspro Logic: Do NOT blend RGB here. Get pure model output.
                        # We will extract chroma from this pure output and blend ONLY chroma later.
                        out_chunks.append((y, i, j))
                        if _every(idx, total, interval=10):
                            print(f"[Bridge]   Color RGB: {idx}/{total}", flush=True)
                    
                    # This is the "Pure Denoised RGB" image
                    rgb_denoised = stitch_chunks_rgb(out_chunks, result.shape)
                    del sess_color
                    
                    # Extract Chroma from the Denoised RGB
                    _, cb_den, cr_den = extract_luminance(rgb_denoised)
                    
                    # Saspro Logic: Blend Chroma Channels Only
                    # cb_final = (1-color_strength)*cb + color_strength*cbd
                    print(f"[Bridge] Blending Chroma channels with strength={dn_color:.2f}", flush=True)
                    cb_out = blend(cb_in, cb_den, dn_color)
                    cr_out = blend(cr_in, cr_den, dn_color)

                else:
                    print(f"[Bridge] WARNING: denoise color model not found (keeping original chroma)", flush=True)
            
            # Merge clean Luma + clean Chroma
            result = merge_luminance(y_denoised, cb_out, cr_out)

    # ---------------------------------------------------------------- SUPERRES
    if mode == 'superres':
        scale = int(params.get('scale', 2))
        model_path = os.path.join(mdir, f'superres_{scale}x.onnx')
        if os.path.exists(model_path):
            print(f"[Bridge] SuperRes {scale}x", flush=True)
            sess = ort.InferenceSession(model_path, sess_options=sess_opts, providers=providers)
            # Process each channel separately (same as saspro)
            out_channels = []
            for ch_idx in range(3):
                plane = result[..., ch_idx]
                out_h, out_w = h * scale, w * scale
                out_plane = np.zeros((out_h, out_w), dtype=np.float32)
                # Process in 256x256 patches
                patch_sz = 256
                step = patch_sz  # no overlap for superres
                total_patches = ((h + patch_sz - 1) // patch_sz) * ((w + patch_sz - 1) // patch_sz)
                done = 0
                for py in range(0, h, step):
                    for px in range(0, w, step):
                        pe_y = min(py + patch_sz, h)
                        pe_x = min(px + patch_sz, w)
                        patch = plane[py:pe_y, px:pe_x]
                        # Zero-pad to 256x256, tile to 3ch
                        patch_in = np.zeros((patch_sz, patch_sz, 3), dtype=np.float32)
                        patch_in[:patch.shape[0], :patch.shape[1], 0] = patch
                        patch_in[:patch.shape[0], :patch.shape[1], 1] = patch
                        patch_in[:patch.shape[0], :patch.shape[1], 2] = patch
                        inp = np.expand_dims(patch_in.transpose(2, 0, 1), 0).astype(np.float32)
                        img_name, _, out_name = _pick_io(sess)
                        out = sess.run([out_name], {img_name: inp})[0]
                        if out.ndim == 4:
                            out = out[0]
                        if out.ndim == 3 and out.shape[0] == 3:
                            out = out[0]  # take channel 0
                        # Place in output
                        oh = (pe_y - py) * scale
                        ow = (pe_x - px) * scale
                        out_plane[py*scale:py*scale+oh, px*scale:px*scale+ow] = out[:oh, :ow]
                        done += 1
                        if _every(done, total_patches, interval=5):
                            print(f"[Bridge]   SuperRes ch{ch_idx}: {done}/{total_patches}", flush=True)
                out_channels.append(out_plane)
            result = np.stack(out_channels, axis=-1)
            w *= scale
            h *= scale
            del sess
        else:
            print(f"[Bridge] WARNING: superres model not found: {model_path}", flush=True)

    print(f"[Bridge] Before unstretch: min={float(np.min(result)):.4f} max={float(np.max(result)):.4f} med={float(np.median(result)):.4f}", flush=True)
    
    # UNSTRETCH (inverse of earlier stretch)
    if stretch_needed and orig_min is not None:
        print("[Bridge] Unstretching", flush=True)
        result = unstretch_image_unlinked_rgb(result, orig_meds, orig_min, False)  # was_mono=False during processing
    
    # REMOVE BORDER (we added 16px at start)
    if was_mono:
        if result.ndim == 3:
            result = result[16:-16, 16:-16, :]
        else:
            result = result[16:-16, 16:-16]
    else:
        result = result[16:-16, 16:-16, :]
    
    # Convert back to original channel count
    if was_mono:
        # Reduce to mono if it's still HxWx3
        if result.ndim == 3 and result.shape[2] == 3:
            result = np.mean(result, axis=2, keepdims=True).astype(np.float32)
            print(f"[Bridge] Final: converted HxWx3 back to mono HxWx1", flush=True)
        elif result.ndim == 3 and result.shape[2] == 1:
            pass  # Already mono
        else:
            # Ensure HxWx1 shape for mono output
            if result.ndim == 2:
                result = result[..., np.newaxis]
    else:
        result = result.astype(np.float32)

    final = np.clip(result, 0.0, 1.0).astype(np.float32)
    
    # CRITICAL VERIFICATION: Output shape must match input
    expected_shape = (h, w, c) if c > 1 else (h, w, 1)
    actual_shape = final.shape
    if actual_shape != expected_shape:
        print(f"[Bridge] ERROR: Output shape mismatch! Expected {expected_shape}, got {actual_shape}", flush=True)
        print(f"[Bridge] ERROR: This will cause memory corruption in C++!", flush=True)
        sys.exit(7)
    
    final_flat = final.astype(np.float32)
    if c == 1:
        # For mono output, flatten to HxW (remove keepdims from HxWx1)
        final_flat = final_flat[..., 0]
    
    final_flat.tofile(raw_out)
    print(f"RESULT: {w} {h} {c} (output shape verified)", flush=True)
    print("[Bridge] Done", flush=True)


# ============================================================================
#  Legacy TIFF conversion  (kept for backward compatibility)
# ============================================================================
def save(tiff_out_path, w, h, c, raw_in_path):
    try:
        data = np.fromfile(raw_in_path, dtype=np.float32)
        expected_size = w * h * c
        if data.size != expected_size:
            print(f"Error: Raw file size mismatch. Expected {expected_size} floats, got {data.size}.", flush=True)
            sys.exit(2)
        if c == 1:
            img = data.reshape((h, w))
        else:
            img = data.reshape((h, w, c))
        img = np.clip(img, 0.0, 1.0)
        img = (img * 65535.0).astype(np.uint16)
        photometric = 'minisblack' if c == 1 else 'rgb'
        tifffile.imwrite(tiff_out_path, img, photometric=photometric)
        print(f"Saved TIFF: {tiff_out_path}", flush=True)
    except Exception as e:
        print(f"Error in save: {e}", flush=True)
        sys.exit(3)


def load(tiff_in_path, raw_out_path):
    try:
        if not os.path.exists(tiff_in_path):
            print(f"Error: Output Tiff not found: {tiff_in_path}", flush=True)
            sys.exit(4)
        img = tifffile.imread(tiff_in_path)
        if img.dtype == np.uint16:
            img = img.astype(np.float32) / 65535.0
        elif img.dtype == np.uint8:
            img = img.astype(np.float32) / 255.0
        else:
            img = img.astype(np.float32)
        if img.ndim == 2:
            h, w = img.shape
            c = 1
        elif img.ndim == 3:
            d0, d1, d2 = img.shape
            if d0 <= 4 and d1 > 4 and d2 > 4:
                img = np.transpose(img, (1, 2, 0))
                h, w, c = img.shape
            else:
                h, w, c = img.shape
        else:
            print(f"Error: Unexpected Image Dimensions: {img.shape}", flush=True)
            sys.exit(5)
        img.tofile(raw_out_path)
        print(f"RESULT: {w} {h} {c}", flush=True)
    except Exception as e:
        print(f"Error in load: {e}", flush=True)
        sys.exit(6)


# ============================================================================
#  CLI entry point
# ============================================================================
if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: cosmic_bridge.py <process|save|load> ...", flush=True)
        sys.exit(1)

    cmd = sys.argv[1]

    if cmd == "process":
        if len(sys.argv) < 5:
            print("Usage: process <params.json> <input.raw> <output.raw>", flush=True)
            sys.exit(1)
        process(sys.argv[2], sys.argv[3], sys.argv[4])

    elif cmd == "save":
        if len(sys.argv) < 7:
            print("Usage: save <tiff_out> <w> <h> <c> <raw_in>", flush=True)
            sys.exit(1)
        save(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), sys.argv[6])

    elif cmd == "load":
        if len(sys.argv) < 4:
            print("Usage: load <tiff_in> <raw_out>", flush=True)
            sys.exit(1)
        load(sys.argv[2], sys.argv[3])

    else:
        print(f"Unknown command: {cmd}", flush=True)
        sys.exit(1)