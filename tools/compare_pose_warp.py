"""Compare a fixed-pose eye capture with a tracked-pose capture using a rotation homography."""

import math
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageStat


def transpose(r):
    return [[r[j][i] for j in range(3)] for i in range(3)]


def solve(a, b):
    matrix = [list(row) + [value] for row, value in zip(a, b)]
    for column in range(len(b)):
        pivot = max(range(column, len(b)), key=lambda row: abs(matrix[row][column]))
        matrix[column], matrix[pivot] = matrix[pivot], matrix[column]
        divisor = matrix[column][column]
        for item in range(column, len(b) + 1):
            matrix[column][item] /= divisor
        for row in range(len(b)):
            if row == column:
                continue
            factor = matrix[row][column]
            for item in range(column, len(b) + 1):
                matrix[row][item] -= factor * matrix[column][item]
    return [row[-1] for row in matrix]


def perspective_coefficients(points):
    # PIL wants input coordinates as rational functions of output coordinates.
    equations, values = [], []
    for (x, y), (u, v) in points:
        equations.append([x, y, 1, 0, 0, 0, -u * x, -u * y])
        values.append(u)
        equations.append([0, 0, 0, x, y, 1, -v * x, -v * y])
        values.append(v)
    return solve(equations, values)


def map_point(x, y, width, height, raw, rotation, y_sign):
    l, right, top, bottom = raw
    tx = l + (x / width) * (right - l)
    ty = top + (y / height) * (bottom - top)
    direction = [tx, y_sign * ty, -1.0]
    transformed = [sum(rotation[row][column] * direction[column]
                       for column in range(3)) for row in range(3)]
    sx = transformed[0] / -transformed[2]
    sy = (transformed[1] / -transformed[2]) / y_sign
    return ((sx - l) / (right - l) * width,
            (sy - top) / (bottom - top) * height)


def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: compare_pose_warp.py smooth native eye output-directory")
    smooth = Image.open(sys.argv[1]).convert("RGB")
    native = Image.open(sys.argv[2]).convert("RGB")
    eye = int(sys.argv[3])
    output = Path(sys.argv[4])
    output.mkdir(parents=True, exist_ok=True)
    raw = (-1.3764, 0.8391, -1.4281, 0.9657) if eye == 0 else \
          (-0.8391, 1.3764, -1.4281, 0.9657)
    # Native capture 02 relative pose from build 20260914.19.
    rotation = [
        [0.9472558, 0.0353548, -0.3185223],
        [-0.0433845, 0.9988936, -0.0181480],
        [0.3175283, 0.0310098, 0.9477417],
    ]
    width, height = smooth.size
    corners = [(0.0, 0.0), (width, 0.0), (width, height), (0.0, height)]
    for name, candidate in (("pose", rotation), ("inverse", transpose(rotation))):
        for y_sign in (1.0, -1.0):
            mappings = [(point, map_point(point[0], point[1], width, height,
                                           raw, candidate, y_sign)) for point in corners]
            coefficients = perspective_coefficients(mappings)
            warped = smooth.transform(smooth.size, Image.Transform.PERSPECTIVE,
                                      coefficients, Image.Resampling.BICUBIC)
            crop = (width // 8, height // 8, width * 7 // 8, height * 7 // 8)
            score = ImageStat.Stat(ImageChops.difference(
                warped.crop(crop).convert("L"), native.crop(crop).convert("L"))).mean[0]
            label = f"eye{eye}_{name}_ysign{int(y_sign):+d}_{score:.3f}.png"
            warped.save(output / label)
            print(label)


if __name__ == "__main__":
    main()
