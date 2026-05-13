"""Convert reference PNG images to C sprite arrays for ST7789V2 4-bit display.
Works with Pillow 5.x (Python 3.6+).
"""
import os
from PIL import Image

W, H = 240, 240

IMAGES = {
    "cat_open":     "reference/3.png",
    "cat_half":     "reference/2.png",
    "cat_closed":   "reference/1.png",
    "cat_bite":     "reference/4.png",
    "cat_belly":    "reference/6.png",
    "cat_bad":      "reference/7.png",
    "cat_good":     "reference/8.png",
    "cat_hidden":   "reference/10.png",
    "cat_intro":    "reference/9.png",
    "cg_12":        "reference/12.png",
    "cg_13":        "reference/13.png",
    "cg_14":        "reference/14.png",
    "cg_15":        "reference/15.png",
    "cg_16":        "reference/16.png",
    "cg_17":        "reference/17.png",
}

def rgb565_swapped(r, g, b):
    """Convert 8-bit RGB to byte-swapped RGB565."""
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    rgb565 = (r5 << 11) | (g6 << 5) | b5
    return ((rgb565 & 0xFF) << 8) | (rgb565 >> 8)

def build_unified_palette():
    """Quantize each image to 16 colors, collect all palette colors, build unified palette."""
    all_rgb = set()
    for name, path in IMAGES.items():
        if not os.path.exists(path):
            continue
        img = Image.open(path).convert("RGB")
        img = img.resize((W, H), Image.LANCZOS)
        q = img.quantize(colors=16, method=0)  # 0=MEDIANCUT
        pal = q.getpalette()[:48]
        for i in range(16):
            r, g, b = pal[i*3], pal[i*3+1], pal[i*3+2]
            all_rgb.add((r, g, b))

    # Reduce to 16 colors: build a palette image
    all_rgb = list(all_rgb)[:256]  # limit sample size
    sample = Image.new("RGB", (len(all_rgb), 1))
    sample.putdata(all_rgb)
    q = sample.quantize(colors=16, method=0)
    pal = q.getpalette()[:48]

    palette_hex = []
    for i in range(16):
        r, g, b = pal[i*3], pal[i*3+1], pal[i*3+2]
        palette_hex.append(f"0x{rgb565_swapped(r, g, b):04X}")

    return palette_hex, q

def quantize_to_unified(img, unified_pal_img):
    """Quantize RGB image to the unified palette using Pillow 5.x API."""
    # First convert to P mode with adaptive palette
    q = img.quantize(colors=16, method=0)
    # Get the per-image palette and build a mapping
    pal_a = q.getpalette()[:48]
    pal_b = unified_pal_img.getpalette()[:48]

    # Build nearest-color mapping from per-image palette to unified palette
    mapping = {}
    for i in range(16):
        r1, g1, b1 = pal_a[i*3], pal_a[i*3+1], pal_a[i*3+2]
        best_idx = 0
        best_dist = 999999
        for j in range(16):
            r2, g2, b2 = pal_b[j*3], pal_b[j*3+1], pal_b[j*3+2]
            dr, dg, db = r1 - r2, g1 - g2, b1 - b2
            dist = dr*dr + dg*dg + db*db
            if dist < best_dist:
                best_dist = dist
                best_idx = j
        mapping[i] = best_idx

    # Remap pixels
    pixels = list(q.getdata())
    remapped = [mapping[p] for p in pixels]
    result = Image.new("P", (W, H))
    result.putpalette(unified_pal_img.getpalette())
    result.putdata(remapped)
    return result

def image_to_c_array(img_q, name, w, h):
    """Convert quantized image to C uint8_t array."""
    pixels = list(img_q.getdata())
    lines = [f"const uint8_t sprite_{name}[{w * h}] = {{"]
    for y in range(h):
        row = pixels[y * w:(y + 1) * w]
        lines.append("    " + ",".join(str(p) for p in row) + ("," if y < h - 1 else ""))
    lines.append("};")
    return "\n".join(lines)

def main():
    print("Building unified 16-color palette...")
    palette_hex, unified_pal = build_unified_palette()

    print("Palette (RGB565 byte-swapped):")
    for i, c in enumerate(palette_hex):
        print(f"  {i}: {c}")

    os.makedirs("CatSprites", exist_ok=True)

    # Header
    header = [
        "#ifndef CATSPRITES_H",
        "#define CATSPRITES_H",
        "",
        "#include <stdint.h>",
        "",
        "// Sprite dimensions",
        f"#define CAT_SPRITE_W {W}",
        f"#define CAT_SPRITE_H {H}",
        "",
        "// Unified 16-color palette (RGB565, byte-swapped for ST7789V2)",
        "static const uint16_t cat_palette[16] = {",
        "    " + ", ".join(palette_hex),
        "};",
        "",
    ]
    for name in IMAGES:
        header.append(f"extern const uint8_t sprite_{name}[{W * H}];")
    header.append("")
    header.append("#endif")

    with open("CatSprites/CatSprites.h", "w") as f:
        f.write("\n".join(header))
    print("Wrote CatSprites/CatSprites.h")

    # Source
    c_parts = ['#include "CatSprites.h"', ""]
    count = 0
    for name, path in IMAGES.items():
        if not os.path.exists(path):
            print(f"  SKIP {name}: missing {path}")
            continue
        print(f"  Processing {name}...")
        img = Image.open(path).convert("RGB")
        img = img.resize((W, H), Image.LANCZOS)
        q = quantize_to_unified(img, unified_pal)
        c_parts.append(f"// {name}")
        c_parts.append(image_to_c_array(q, name, W, H))
        c_parts.append("")
        count += 1

    with open("CatSprites/CatSprites.c", "w") as f:
        f.write("\n".join(c_parts))
    print(f"Wrote CatSprites/CatSprites.c ({count} sprites)")

    total_kb = (W * H * count) / 1024.0
    print(f"Total sprite data: {W*H*count} bytes ({total_kb:.1f} KB)")
    print("Done!")

if __name__ == "__main__":
    main()
