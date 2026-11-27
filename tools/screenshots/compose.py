# Copyright © Charlie Howard 2026 All rights reserved
import sys
from PIL import Image, ImageDraw, ImageFont

PAGES = [
    ("boot", "Boot"),
]
SCREENS = [
    "Home", "Boost", "Oil and coolant", "IAT", "RPM",
    "Speed", "Voltage", "Load and throttle", "0-60", "Faults",
]
PAGES += [(f"page{i:02d}", name) for i, name in enumerate(SCREENS)]
PAGES += [("menu", "Menu")]
COLUMNS = 6
TILE = 363
GAP = 14
LABEL_HEIGHT = 48
BACKGROUND = (25, 25, 25)
TEXT = (230, 230, 230)
SCALE = 4

def main():
    src, out = sys.argv[1], sys.argv[2]
    font_path = sys.argv[3] if len(sys.argv) > 3 else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    font = ImageFont.truetype(font_path, 19)

    rows = (len(PAGES) + COLUMNS - 1) // COLUMNS
    width = GAP + COLUMNS * (TILE + GAP)
    height = rows * (LABEL_HEIGHT + TILE) + GAP
    sheet = Image.new("RGB", (width, height), BACKGROUND)
    draw = ImageDraw.Draw(sheet)

    mask = Image.new("L", (TILE * SCALE, TILE * SCALE), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, TILE * SCALE - 1, TILE * SCALE - 1), fill=255)
    mask = mask.resize((TILE, TILE), Image.LANCZOS)

    for i, (file, name) in enumerate(PAGES):
        col, row = i % COLUMNS, i // COLUMNS
        x = GAP + col * (TILE + GAP)
        y = row * (LABEL_HEIGHT + TILE)

        page = Image.open(f"{src}/{file}.ppm").convert("RGB").resize((TILE, TILE), Image.LANCZOS)
        sheet.paste(page, (x, y + LABEL_HEIGHT), mask)

        draw.text((x + TILE / 2, y + LABEL_HEIGHT / 2), f"{i}. {name}" if file.startswith("page") else name, font=font, fill=TEXT, anchor="mm")

    sheet.save(out, optimize=True)
    print(f"Saved {out} ({width} x {height})")

if __name__ == "__main__":
    main()
