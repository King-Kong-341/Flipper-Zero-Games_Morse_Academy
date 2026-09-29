from PIL import Image

# 10x10 1-bit icon: a small radio mast with a dit-dah underneath.
# Flipper draws the BLACK pixels (the PNG is inverted on import),
# drawn as '#' rows so it is easy to tweak.
rows = [
    "#........#",
    ".#......#.",
    "#.#.##.#.#",
    ".#..##..#.",
    "....##....",
    "...#..#...",
    "...#..#...",
    "..#....#..",
    "..........",
    ".#..####..",
]
img = Image.new("1", (10, 10), 1)  # white background
px = img.load()
for y, row in enumerate(rows):
    for x, ch in enumerate(row):
        if ch == "#":
            px[x, y] = 0  # black = drawn on the Flipper
img.save("icon.png")
print("icon.png written", img.size, img.mode)
