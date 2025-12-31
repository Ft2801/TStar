import sys
import numpy as np
import tifffile as tiff
import os

def save(tiff_path, w, h, c, raw_path):
    try:
        data = np.fromfile(raw_path, dtype=np.float32)
        if c == 1: data = data.reshape((h, w))
        else: data = data.reshape((h, w, c))
        # GraXpert prefers 'minisblack' for mono
        tiff.imwrite(tiff_path, data, photometric='minisblack' if c==1 else 'rgb')
        print('Saved TIFF')
    except Exception as e: print('Error:', e); sys.exit(1)

def load(path, raw_out_path):
    try:
        data = None
        try:
            data = tiff.imread(path)
        except Exception:
            from astropy.io import fits
            with fits.open(path) as hdul:
                data = hdul[0].data
        data = data.astype(np.float32)
        # FITS often has channels first or is 2D
        if data.ndim == 2: h, w = data.shape; c = 1
        elif data.ndim == 3:
            if data.shape[0] < 5: # Assuming CHW (FITS style)
                c, h, w = data.shape
                data = np.transpose(data, (1, 2, 0))
            else: # Assuming HWC (TIFF style)
                h, w, c = data.shape
        data.tofile(raw_out_path)
        print(f'RESULT: {w} {h} {c}')
    except Exception as e: print('Error:', e); sys.exit(1)

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'save':
        save(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), sys.argv[6])
    elif cmd == 'load':
        load(sys.argv[2], sys.argv[3])
