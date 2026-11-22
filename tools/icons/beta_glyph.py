# Copyright © Charlie Howard 2026 All rights reserved
import sys
from PIL import Image, ImageDraw, ImageFont

SIZE = 19

def main():
    font = ImageFont.truetype(sys.argv[1], SIZE)
    canvas = Image.new("L", (SIZE * 2, SIZE * 2), 0)
    ImageDraw.Draw(canvas).text((SIZE // 2, SIZE // 2), "β", font=font, fill=255)
    glyph = canvas.crop(canvas.getbbox())
    w, h = glyph.size
    px = glyph.tobytes()
    rows = ",\n".join("  " + ", ".join(f"0x{v:02x}" for v in px[y * w:(y + 1) * w]) for y in range(h))
    with open(sys.argv[2], "w") as f:
        f.write(f"""// Copyright © Charlie Howard 2026 All rights reserved
#include "lvgl.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

static const LV_ATTRIBUTE_MEM_ALIGN uint8_t iconBetaMap[] = {{
{rows}
}};

const lv_img_dsc_t iconBeta = {{
  .header.cf = LV_IMG_CF_ALPHA_8BIT,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = {w},
  .header.h = {h},
  .data_size = {w * h},
  .data = iconBetaMap,
}};
""")
    print(f"Saved {sys.argv[2]} ({w} x {h})")

if __name__ == "__main__":
    main()
