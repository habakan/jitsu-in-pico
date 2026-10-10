from pathlib import Path
import struct


OUT = Path(__file__).resolve().parent
CASE_X, CASE_Y = 100.0, 90.0
BASE_X, BASE_Y, BASE_H = 98.0, 88.0, 19.4
BASE_FLOOR, WALL = 2.4, 2.4
FRONT_H, FACE_T = 15.4, 2.4
BOARD_X, BOARD_Y = 92.0, 82.0
BOARD_X0, BOARD_Y0 = (CASE_X - BOARD_X) / 2, (CASE_Y - BOARD_Y) / 2
SCREWS = [(50.0, 2.5), (50.0, 87.5), (2.5, 45.0), (97.5, 45.0)]
LCD = (BOARD_X0 + 25.86, BOARD_Y0 + 20.0)
CAMERA = (BOARD_X0 + 70.75, BOARD_Y0 + 19.75)
BUTTONS = [(BOARD_X0 + 16.0, BOARD_Y0 + 44.0), (BOARD_X0 + 34.0, BOARD_Y0 + 44.0)]
USB_Y = BOARD_Y0 + 64.5


def cuboid(x0, x1, y0, y1, z0, z1):
    return (x0, x1, y0, y1, z0, z1)


def base_operations():
    operations = [
        ("add", cuboid(1, 99, 1, 89, 0, BASE_H)),
        ("cut", cuboid(BOARD_X0 - 0.6, BOARD_X0 + BOARD_X + 0.6,
                       BOARD_Y0 - 0.6, BOARD_Y0 + BOARD_Y + 0.6, BASE_FLOOR, BASE_H + 2.4)),
    ]
    for x, y in [(5.5, 5.5), (94.5, 5.5), (5.5, 84.5), (94.5, 84.5)]:
        operations.append(("add", cuboid(x - 2.25, x + 2.25, y - 2.25, y + 2.25,
                                           BASE_FLOOR - 0.2, 15.4)))
    operations.append(("cut", cuboid(-2, 4, USB_Y - 6.5, USB_Y + 6.5, 4.25, 11.75)))
    for x, y in SCREWS:
        operations.append(("cut", cuboid(x - 1.4, x + 1.4, y - 1.4, y + 1.4, -0.5, BASE_H + 0.5)))
        operations.append(("cut", cuboid(x - 2.4, x + 2.4, y - 2.4, y + 2.4, -0.1, 1.8)))
    return operations


def front_operations():
    operations = [
        ("add", cuboid(0, CASE_X, 0, CASE_Y, 0, FRONT_H)),
        ("cut", cuboid(WALL, CASE_X - WALL, WALL, CASE_Y - WALL, FACE_T, FRONT_H - 2.4)),
        ("cut", cuboid(0.75, CASE_X - 0.75, 0.75, CASE_Y - 0.75, FRONT_H - 2.4, FRONT_H + 0.6)),
    ]
    for x, y in SCREWS:
        operations.append(("add", cuboid(x - 2.4, x + 2.4, y - 2.4, y + 2.4, FACE_T, FRONT_H)))
    operations.append(("cut", cuboid(LCD[0] - 14.2, LCD[0] + 14.2,
                                      LCD[1] - 14.2, LCD[1] + 14.2, -0.5, FACE_T + 0.5)))
    operations.append(("cut", cuboid(CAMERA[0] - 7.5, CAMERA[0] + 7.5,
                                      CAMERA[1] - 7.5, CAMERA[1] + 7.5, -0.5, FACE_T + 0.5)))
    for x, y in BUTTONS:
        operations.append(("cut", cuboid(x - 3.4, x + 3.4, y - 3.4, y + 3.4, -0.5, FACE_T + 0.5)))
    for x, y in SCREWS:
        operations.append(("cut", cuboid(x - 1.05, x + 1.05, y - 1.05, y + 1.05,
                                          FRONT_H - 10, FRONT_H + 0.5)))
    return operations


def occupy(operations):
    axes = [sorted({box[i + offset] for _, box in operations for offset in (0, 1)})
            for i in (0, 2, 4)]
    indices = [{value: i for i, value in enumerate(axis)} for axis in axes]
    solid = set()
    for operation, box in operations:
        x0, x1 = indices[0][box[0]], indices[0][box[1]]
        y0, y1 = indices[1][box[2]], indices[1][box[3]]
        z0, z1 = indices[2][box[4]], indices[2][box[5]]
        for i in range(x0, x1):
            for j in range(y0, y1):
                for k in range(z0, z1):
                    cell = (i, j, k)
                    if operation == "add":
                        solid.add(cell)
                    else:
                        solid.discard(cell)
    return axes, solid


def triangles(axes, solid):
    xs, ys, zs = axes
    nx, ny, nz = len(xs) - 1, len(ys) - 1, len(zs) - 1
    for i, j, k in solid:
        x0, x1 = xs[i], xs[i + 1]
        y0, y1 = ys[j], ys[j + 1]
        z0, z1 = zs[k], zs[k + 1]
        if (i - 1, j, k) not in solid:
            yield ( -1, 0, 0), ((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0))
        if (i + 1, j, k) not in solid:
            yield (1, 0, 0), ((x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1))
        if (i, j - 1, k) not in solid:
            yield (0, -1, 0), ((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1))
        if (i, j + 1, k) not in solid:
            yield (0, 1, 0), ((x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0))
        if (i, j, k - 1) not in solid:
            yield (0, 0, -1), ((x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0))
        if (i, j, k + 1) not in solid:
            yield (0, 0, 1), ((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1))


def write_stl(operations, filename):
    axes, solid = occupy(operations)
    path = OUT / filename
    count = 0
    with path.open("wb") as stream:
        stream.write(b"Pico 2 H carrier case, dimensions in mm".ljust(80, b"\0"))
        stream.write(struct.pack("<I", 0))
        for normal, quad in triangles(axes, solid):
            for a, b, c in ((quad[0], quad[1], quad[2]), (quad[0], quad[2], quad[3])):
                stream.write(struct.pack("<12fH", *normal, *a, *b, *c, 0))
                count += 1
        stream.seek(80)
        stream.write(struct.pack("<I", count))
    print(f"{filename}: {count} triangles, {path.stat().st_size} bytes")


write_stl(base_operations(), "pico2h_case_base.stl")
write_stl(front_operations(), "pico2h_case_front.stl")
