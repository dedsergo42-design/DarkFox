"""Генератор иконок лоадеров DarkFox.

Рисует стилизованную голову лисы в палитре лоадера и складывает
многомасштабный .ico (16/24/32/48/64/128/256). Два варианта:
обычный -- фиолетовый акцент, dev -- янтарный, плюс метка в углу,
чтобы различать их в панели задач на глаз.

Голова рисуется ОДНИМ контуром: уши и морда -- части одной фигуры, а не
три отдельные. Раздельные полигоны давали зазоры и силуэт кролика.
"""

from PIL import Image, ImageDraw

# Палитра совпадает с loader.cpp, чтобы иконка и окно читались как одно.
BG = (10, 11, 14, 255)
ACCENT = (198, 128, 240, 255)
ACCENT_SOFT = (240, 130, 208, 255)
DEV_ACCENT = (255, 190, 96, 255)
DEV_SOFT = (232, 152, 70, 255)

SIZES = [256, 128, 64, 48, 32, 24, 16]

# Рисуем в 8x и уменьшаем: у Pillow нет антиалиасинга для полигонов,
# поэтому суперсэмплинг -- единственный способ получить чистые края.
SS = 8
CANVAS = 256


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(4))


# Контур головы лисы в сетке 0..256, по часовой стрелке от левого уха.
# Форма: два острых уха, височные впадины, скулы, сходящиеся к подбородку.
def fox_outline():
    return [
        (52, 40),     # левое ухо, кончик
        (96, 92),     # внутренний срез левого уха -> висок
        (110, 84),    # впадина между ухом и лбом
        (128, 88),    # лоб
        (146, 84),    # правая впадина
        (160, 92),    # внутренний срез правого уха
        (204, 40),    # правое ухо, кончик
        (196, 132),   # правая скула
        (168, 176),   # к правой щеке
        (128, 218),   # подбородок
        (88, 176),    # левая щека
        (60, 132),    # левая скула
    ]


def make_icon(accent, accent_soft, dev):
    size = CANVAS * SS
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))

    def s(v):
        """Координата из сетки 0..256 в пиксели суперсэмпла."""
        return v / 256.0 * size

    # --- подложка: скруглённый квадрат с градиентом ---
    card = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    cd = ImageDraw.Draw(card)

    for y in range(size):
        t = y / size
        cd.line([(0, y), (size, y)], fill=lerp((28, 30, 38, 255), BG, t))

    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, size - 1, size - 1], radius=s(58), fill=255)
    img.paste(card, (0, 0), mask)

    # --- голова: один контур, залитый градиентом ---
    outline = [(s(x), s(y)) for x, y in fox_outline()]

    # Градиент заливки делаем послойно: рисуем контур сплошным акцентом,
    # затем накладываем светлый тон через маску, гаснущую книзу.
    body = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(body).polygon(outline, fill=accent)

    light = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(light).polygon(outline, fill=accent_soft)

    fade = Image.new("L", (size, size), 0)
    fd = ImageDraw.Draw(fade)
    for y in range(size):
        t = y / size
        fd.line([(0, y), (size, y)], fill=int(max(0.0, 1.0 - t * 1.7) * 165))

    body.alpha_composite(Image.composite(light, Image.new("RGBA", (size, size), (0, 0, 0, 0)), fade))
    img.alpha_composite(body)

    d = ImageDraw.Draw(img)

    # --- морда: светлая маска клином от переносицы к носу ---
    # Узкая и короткая: широкая маска заливала половину головы и силуэт
    # терял читаемость на 16 пикселях.
    d.polygon([(s(128), s(128)), (s(141), s(148)), (s(128), s(178)), (s(115), s(148))],
              fill=lerp(accent_soft, (255, 255, 255, 255), 0.35))

    # --- глаза: узкие миндалевидные вырезы, чуть под углом ---
    # Левый
    d.polygon([(s(99), s(128)), (s(119), s(123)), (s(117), s(140)), (s(101), s(143))], fill=BG)
    # Правый
    d.polygon([(s(157), s(128)), (s(137), s(123)), (s(139), s(140)), (s(155), s(143))], fill=BG)

    # --- нос: треугольник вершиной вниз ---
    d.polygon([(s(122), s(183)), (s(134), s(183)), (s(128), s(194))], fill=BG)

    # --- метка dev-сборки: кольцо в правом нижнем углу ---
    if dev:
        dcx, dcy = s(203), s(203)
        outer, inner = s(30), s(20)
        d.ellipse([dcx - outer, dcy - outer, dcx + outer, dcy + outer], fill=BG)
        d.ellipse([dcx - outer + s(2), dcy - outer + s(2), dcx + outer - s(2), dcy + outer - s(2)],
                  fill=accent)
        d.ellipse([dcx - inner, dcy - inner, dcx + inner, dcy + inner], fill=BG)

    return img.resize((CANVAS, CANVAS), Image.LANCZOS)


def main():
    normal = make_icon(ACCENT, ACCENT_SOFT, dev=False)
    dev = make_icon(DEV_ACCENT, DEV_SOFT, dev=True)

    normal.save("D:/Phantom/loader/icon_normal.ico",
                sizes=[(s, s) for s in SIZES], format="ICO")
    dev.save("D:/Phantom/loader/icon_dev.ico",
             sizes=[(s, s) for s in SIZES], format="ICO")

    # PNG-превью рядом: удобно глянуть результат без открытия проводника.
    normal.resize((160, 160), Image.LANCZOS).save("D:/Phantom/loader/_preview_normal.png")
    dev.resize((160, 160), Image.LANCZOS).save("D:/Phantom/loader/_preview_dev.png")

    print("icons written")


if __name__ == "__main__":
    main()
