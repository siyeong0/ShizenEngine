import os
import glob
import numpy as np
from PIL import Image

# -----------------------------
# Settings
# -----------------------------
BLACK_RGB_THRESHOLD = 32    # 0~255: 검정 판정(각 채널이 이 값 이하)
ALPHA_TO_ZERO_ON_BLACK = True

IN_PLACE = False
OUT_DIR = "out_png_fixed_avg"

def process_png(path: str) -> Image.Image:
    img = Image.open(path).convert("RGBA")
    rgba = np.array(img, dtype=np.uint8)

    rgb = rgba[..., :3]
    a   = rgba[..., 3]

    # (거의) 검정 판정
    is_black = (rgb[..., 0] > 255 - BLACK_RGB_THRESHOLD) & \
               (rgb[..., 1] > 255 - BLACK_RGB_THRESHOLD) & \
               (rgb[..., 2] > 255 - BLACK_RGB_THRESHOLD)

    if ALPHA_TO_ZERO_ON_BLACK:
        rgba[is_black, 3] = 0
        a = rgba[..., 3]  # 갱신

    # "검정이 아닌" 픽셀들로 평균색 계산 (alpha>0 조건도 같이 걸면 더 안전)
    non_black = ~is_black
    valid_for_avg = non_black & (a > 0)

    if np.any(valid_for_avg):
        mean_rgb = np.round(rgb[valid_for_avg].mean(axis=0)).astype(np.uint8)
    else:
        # 전부 검정/투명인 이미지면 fallback
        mean_rgb = np.array([128, 128, 128], dtype=np.uint8)

    # alpha==0 영역 RGB를 평균색으로 통일 (fringing 방지)
    alpha_zero = (a == 0)
    rgba[alpha_zero, 0] = mean_rgb[0]
    rgba[alpha_zero, 1] = mean_rgb[1]
    rgba[alpha_zero, 2] = mean_rgb[2]

    print(mean_rgb)

    return Image.fromarray(rgba, mode="RGBA")

def main():
    pngs = sorted(glob.glob("*.png"))
    if not pngs:
        print("[Info] No PNG files found in current directory.")
        return

    if not IN_PLACE:
        os.makedirs(OUT_DIR, exist_ok=True)

    for p in pngs:
        try:
            out_img = process_png(p)
            out_path = p if IN_PLACE else os.path.join(OUT_DIR, p)
            out_img.save(out_path)
            print(f"[OK] {p} -> {out_path}")
        except Exception as e:
            print(f"[Fail] {p}: {e}")

if __name__ == "__main__":
    main()