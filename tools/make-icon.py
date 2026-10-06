"""Draws the title's icon, sce_sys/icon0.png (512x512): "RA" over a red glow, with no artwork of
the game's own. Uses DejaVu Sans Bold (fonts-dejavu-core in the build image).

    python3 tools/make-icon.py sce_sys/icon0.png
"""
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

SIZE = 512
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def main():
    out = sys.argv[1]
    icon = Image.new("RGB", (SIZE, SIZE), (14, 10, 10))

    # A red glow behind the letters, darkest at the corners.
    glow = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(glow).ellipse((40, 30, SIZE - 40, SIZE - 70), fill=255)
    glow = glow.filter(ImageFilter.GaussianBlur(70))
    icon.paste(Image.new("RGB", (SIZE, SIZE), (150, 18, 18)), (0, 0), glow)

    draw = ImageDraw.Draw(icon)
    letters = ImageFont.truetype(FONT, 250)
    box = draw.textbbox((0, 0), "RA", font=letters)
    x = (SIZE - (box[2] - box[0])) // 2 - box[0]
    y = 210 - (box[3] - box[1]) // 2 - box[1]
    draw.text((x + 6, y + 8), "RA", font=letters, fill=(0, 0, 0))
    draw.text((x, y), "RA", font=letters, fill=(244, 238, 232))

    # A bar under the letters and the port's name.
    draw.rectangle((96, 352, SIZE - 96, 362), fill=(214, 36, 30))
    small = ImageFont.truetype(FONT, 46)
    label = "PS5 NATIVE"
    box = draw.textbbox((0, 0), label, font=small)
    draw.text(((SIZE - (box[2] - box[0])) // 2 - box[0], 392), label, font=small, fill=(232, 220, 214))

    icon.save(out, optimize=True)


if __name__ == "__main__":
    main()
