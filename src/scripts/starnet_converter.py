import sys, numpy as np, tifffile

try:
    data = tifffile.imread(sys.argv[1]).astype(np.float32)
    # Ensure HWC or HW
    data.tofile(sys.argv[2])
    print(f'Done:{data.shape}')
except Exception as e:
    print('Error:', e)
    sys.exit(1)
