# Copyright © Charlie Howard 2026 All rights reserved
import math
import sys
from PIL import Image, ImageDraw

SIZE = 22
SCALE = 16


def canvas():
    img = Image.new("L", (SIZE * SCALE, SIZE * SCALE), 0)
    return img, ImageDraw.Draw(img)


def box(*values):
    return tuple(v * SCALE for v in values)


def thermometer():
    img, d = canvas()
    d.rounded_rectangle(box(7, 1, 15, 17), radius=4 * SCALE, fill=255)
    d.ellipse(box(5, 11, 17, 22), fill=255)
    d.rounded_rectangle(box(9, 3, 13, 16), radius=2 * SCALE, fill=0)
    d.ellipse(box(7, 13, 15, 20), fill=0)
    d.rectangle(box(10, 8, 12, 16), fill=255)
    d.ellipse(box(8.5, 14.2, 13.5, 18.8), fill=255)
    return img


def snowflake():
    img, d = canvas()
    c = SIZE / 2

    def line(x1, y1, x2, y2):
        d.line(box(x1, y1, x2, y2), fill=255, width=int(1.9 * SCALE))
        for x, y in ((x1, y1), (x2, y2)):
            d.ellipse(box(x - 0.95, y - 0.95, x + 0.95, y + 0.95), fill=255)

    for k in range(6):
        a = math.radians(60 * k + 30)
        line(c, c, c + 9.6 * math.cos(a), c + 9.6 * math.sin(a))
        bx, by = c + 5.6 * math.cos(a), c + 5.6 * math.sin(a)
        for turn in (-50, 50):
            b = a + math.radians(turn)
            line(bx, by, bx + 3.2 * math.cos(b), by + 3.2 * math.sin(b))
    return img


def battery():
    img, d = canvas()
    d.rounded_rectangle(box(1, 6, 21, 19), radius=2 * SCALE, fill=255)
    d.rectangle(box(4, 3, 8, 7), fill=255)
    d.rectangle(box(14, 3, 18, 7), fill=255)
    d.rounded_rectangle(box(3, 8, 19, 17), radius=SCALE, fill=0)
    d.rectangle(box(4.5, 11.6, 9.5, 13.4), fill=255)
    d.rectangle(box(12.5, 11.6, 17.5, 13.4), fill=255)
    d.rectangle(box(14.1, 10, 15.9, 15), fill=255)
    return img


def main():
    out = """// Copyright © Charlie Howard 2026 All rights reserved
#include "lvgl.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif
"""
    for name, draw in (("iconOutside", thermometer), ("iconFrost", snowflake), ("iconBattery", battery)):
        px = draw().resize((SIZE, SIZE), Image.LANCZOS).tobytes()
        rows = ",\n".join("  " + ", ".join(f"0x{v:02x}" for v in px[y * SIZE:(y + 1) * SIZE]) for y in range(SIZE))
        out += f"""
static const LV_ATTRIBUTE_MEM_ALIGN uint8_t {name}Map[] = {{
{rows}
}};

const lv_img_dsc_t {name} = {{
  .header.cf = LV_IMG_CF_ALPHA_8BIT,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = {SIZE},
  .header.h = {SIZE},
  .data_size = {SIZE * SIZE},
  .data = {name}Map,
}};
"""
    with open(sys.argv[1], "w", encoding="utf-8") as f:
        f.write(out)
    print(f"Saved {sys.argv[1]}")


if __name__ == "__main__":
    main()
