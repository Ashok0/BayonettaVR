"""Estimate coarse translation between two captured eye images."""

import sys
from PIL import Image, ImageChops, ImageStat


def crop_for_shift(image, dx, dy):
    width, height = image.size
    left = max(0, dx)
    top = max(0, dy)
    right = min(width, width + dx)
    bottom = min(height, height + dy)
    return image.crop((left, top, right, bottom))


def best_shift(left, right, limit_x=80, limit_y=16):
    best = None
    for dy in range(-limit_y, limit_y + 1):
        for dx in range(-limit_x, limit_x + 1):
            a = crop_for_shift(left, dx, dy)
            b = crop_for_shift(right, -dx, -dy)
            width = min(a.width, b.width)
            height = min(a.height, b.height)
            if width <= 0 or height <= 0:
                continue
            score = ImageStat.Stat(ImageChops.difference(
                a.crop((0, 0, width, height)), b.crop((0, 0, width, height))
            )).mean[0]
            candidate = (score, dx, dy)
            if best is None or candidate < best:
                best = candidate
    return best


def main():
    left = Image.open(sys.argv[1]).convert("L").resize((256, 144))
    right = Image.open(sys.argv[2]).convert("L").resize((256, 144))
    regions = {
        "whole": (0, 0, 256, 144),
        "world_top": (0, 0, 256, 92),
        "world_center": (32, 20, 224, 112),
        "hud_bottom": (0, 108, 256, 144),
    }
    for name, box in regions.items():
        score, dx, dy = best_shift(left.crop(box), right.crop(box))
        print(f"{name}: dx={dx * 8:+d}px dy={dy * 8:+d}px mean_abs_error={score:.2f}")


if __name__ == "__main__":
    main()
