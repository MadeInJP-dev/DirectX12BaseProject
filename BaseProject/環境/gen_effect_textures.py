# -*- coding: utf-8 -*-
"""
エフェクト全般(噴射・銃口・着弾・砂ぼこり・もや など)で使うパーティクルのテクスチャを作るスクリプト。

爆発用(gen_explosion_textures.py)のノイズと書き出しを借りるので、同じく Blender 付属の Python で動かす。

    "C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe" 環境/gen_effect_textures.py
    ... gen_effect_textures.py --only Muzzle        # 名前に Muzzle を含むものだけ作り直す

出力 : Asset/Texture/ParticleTexture/FX/ の下(.assetmeta は GUID 固定で、無ければ作る)
  FX_FlameTongue_4x1.png  噴射の炎の舌 4 種(下 = 噴射口、上 = 先。EmitterAxis で噴く向きへ向ける)   512x256
  FX_MuzzleFront_2x2.png  銃口の閃光を正面から見た形 4 種(花びらのような光芒)                      512x512
  FX_MuzzleSide_2x2.png   銃口の閃光を横から見た形 4 種(下 = 銃口、上 = 先。EmitterAxis)            512x512
  FX_WindStreak.png       風に流れる砂の筋(上が頭)                                                 32x512
  FX_Wisp_2x2.png         地面を這うもや・砂煙の帯 4 種(横長)                                       512x512
  FX_Rock_4x4.png         岩と土くれ 16 種(粒ごとに1つ選ぶ)                                         512x512

炎・閃光は爆発の火球と同じ「詰めたチャンネル」(パーティクルアセットの TexMode = FireSmoke で読む):
  R : 煙の明暗(炎・閃光は G と同じ。煙の色を 0 にして使う)
  G : 温度(芯ほど高い。熱いところだけ発光色で光る)
  A : 濃さ
もや・岩は R = 明暗(上から光を当てた陰)、G = 0、A = 濃さ。色はパーティクルアセット側で付ける。
"""

import argparse
import importlib.util
import math
import os

import numpy as np

_HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("gen_explosion_textures", os.path.join(_HERE, "gen_explosion_textures.py"))
gx = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gx)

OUT_DIR = "Asset/Texture/ParticleTexture/FX"

GUIDS = {
    "FX_FlameTongue_4x1": "da5893e5-4961-49ab-8ec6-3afbcb8d3723",
    "FX_MuzzleFront_2x2": "c2454c7d-87d7-44ad-9b95-26036697c120",
    "FX_MuzzleSide_2x2": "ad6846a0-ebc5-4e22-b650-86c1108d29fc",
    "FX_WindStreak": "a50da7f1-748d-46a2-b9b3-14c138448ba3",
    "FX_Wisp_2x2": "9c3cd7cc-9cda-41ab-a055-c83b1de5855b",
    "FX_Rock_4x4": "2c1b68b0-0c94-4623-bae1-ca413949f73d",
}

smoothstep = gx.smoothstep
sample3 = gx.sample3


def save(name, rgba):
    os.makedirs(OUT_DIR, exist_ok=True)
    gx.write_png(os.path.join(OUT_DIR, name + ".png"), np.clip(rgba, 0.0, 1.0))
    meta = os.path.join(OUT_DIR, name + ".assetmeta")
    if not os.path.exists(meta):
        with open(meta, "w", encoding="utf-8", newline="\n") as f:
            f.write('{\n    "Files": [\n        ".png"\n    ],\n'
                    f'    "GUID": "{GUIDS[name]}",\n    "Type": "Texture"\n}}')
    print(f"  wrote {name}.png {rgba.shape[1]}x{rgba.shape[0]}")


def grid(h, w):
    """x : -1〜1(左→右)、s : 0〜1(下→上)"""
    y, x = np.meshgrid(np.linspace(1.0, 0.0, h, dtype=np.float32),
                       np.linspace(-1.0, 1.0, w, dtype=np.float32), indexing="ij")
    return x, y


def pack_fire(temp, dens):
    """炎 : R = G = 温度、A = 濃さ"""
    out = np.zeros(temp.shape + (4,), np.float32)
    out[..., 0] = temp
    out[..., 1] = temp
    out[..., 3] = dens
    return out


def place(sheet, img, k, cols, ch, cw):
    gy, gx_ = divmod(k, cols)
    sheet[gy * ch:(gy + 1) * ch, gx_ * cw:(gx_ + 1) * cw] = img


# =========================================================================================
# 噴射の炎の舌 : 噴射口(下)は細く白熱し、先へ行くほど太って千切れる
# =========================================================================================
def make_flame_tongue(fbm, bil):
    cw, ch, cols = 128, 256, 4
    sheet = np.zeros((ch, cw * cols, 4), np.float32)
    for k in range(cols):
        x, s = grid(ch, cw)
        z = np.full_like(x, 0.17 * k + 0.05)
        n = sample3(bil, x * 0.35 + 0.1 * k, s * 0.9 - 0.13 * k, z)
        nf = sample3(fbm, x * 0.9 + 0.3, s * 2.2 + 0.07 * k, z + 0.4)
        width = 0.16 + 0.62 * s ** 0.75
        xx = x + 0.18 * (nf - 0.5) * s          # 先ほど横へ揺らぐ
        field = 1.0 - np.abs(xx) / width + 0.55 * (n - 0.5) * (0.3 + s)
        tip = smoothstep(0.95, 0.45 + 0.25 * nf, s)
        dens = smoothstep(0.0, 0.3, field) * tip * smoothstep(0.0, 0.03, s)
        core = np.clip(1.0 - np.abs(xx) / (width * 0.55), 0.0, 1.0)
        temp = np.clip(core * (1.0 - s * 0.85) + 0.35 * (nf - 0.5) + 0.15, 0.0, 1.0) * smoothstep(0.0, 0.1, dens)
        place(sheet, pack_fire(temp, dens), k, cols, ch, cw)
    save("FX_FlameTongue_4x1", sheet)


# =========================================================================================
# 銃口の閃光(正面) : 中心の白い芯から、太さと長さの違う花びらが 5〜7 枚
# =========================================================================================
def make_muzzle_front(fbm, rng):
    n_, cols = 256, 2
    sheet = np.zeros((n_ * 2, n_ * 2, 4), np.float32)
    x, y = np.meshgrid(np.linspace(-1, 1, n_, dtype=np.float32), np.linspace(-1, 1, n_, dtype=np.float32))
    r = np.sqrt(x * x + y * y)
    th = np.arctan2(y, x)
    for k in range(4):
        petals = int(rng.integers(5, 8))
        base = rng.random() * 2 * math.pi
        inten = np.zeros_like(r)
        for i in range(petals):
            a = base + 2 * math.pi * i / petals + (rng.random() - 0.5) * 0.5
            length = 0.45 + 0.45 * rng.random()
            wdt = 0.3 + 0.2 * rng.random()
            d = np.angle(np.exp(1j * (th - a)))
            taper = wdt * (1.0 - np.clip(r / length, 0, 1)) ** 0.8 + 0.02
            inten = np.maximum(inten, np.exp(-(d / taper) ** 2) * smoothstep(length, length * 0.3, r))
        noise = sample3(fbm, x * 1.2 + k * 0.21, y * 1.2, np.full_like(x, 0.3 + k * 0.1))
        inten *= np.clip(0.35 + 1.2 * noise, 0, 1.3)
        core = np.exp(-(r / (0.24 + 0.08 * noise)) ** 2)
        dens = np.clip(np.maximum(inten, core) * smoothstep(1.0, 0.85, r), 0, 1)
        temp = np.clip(core * 1.0 + inten * (0.75 - 0.5 * r), 0, 1)
        place(sheet, pack_fire(temp, dens), k, cols, n_, n_)
    save("FX_MuzzleFront_2x2", sheet)


# =========================================================================================
# 銃口の閃光(横) : 銃口(下)からふくらむ炎の円錐。先は千切れた舌になる
# =========================================================================================
def make_muzzle_side(fbm, bil, rng):
    n_, cols = 256, 2
    sheet = np.zeros((n_ * 2, n_ * 2, 4), np.float32)
    for k in range(4):
        x, s = grid(n_, n_)
        z = np.full_like(x, 0.6 + 0.11 * k)
        n = sample3(bil, x * 0.5 + 0.2 * k, s * 0.7, z)
        nf = sample3(fbm, x * 1.4, s * 1.8 + 0.1 * k, z + 0.2)
        lobe = 0.5 + 0.2 * rng.random()
        width = 0.08 + 0.55 * np.sin(np.clip(s / 0.95, 0, 1) * math.pi * lobe) ** 0.9
        field = 1.0 - np.abs(x) / np.maximum(width, 1e-3) + 0.6 * (n - 0.5)
        tip = smoothstep(0.98, 0.55 + 0.3 * nf, s)
        dens = smoothstep(0.0, 0.25, field) * tip * smoothstep(0.0, 0.02, s)
        temp = np.clip((1.0 - np.abs(x) / np.maximum(width * 0.6, 1e-3)) * (1.0 - s * 0.7) + 0.3 * (nf - 0.5) + 0.2, 0, 1)
        temp *= smoothstep(0.0, 0.1, dens)
        place(sheet, pack_fire(temp, dens), k, cols, n_, n_)
    save("FX_MuzzleSide_2x2", sheet)


# =========================================================================================
# 風に流れる砂の筋 : 細い線に、粒の濃淡を並べる
# =========================================================================================
def make_wind_streak(fbm):
    h, w = 512, 32
    x, s = grid(h, w)
    grain = sample3(fbm, np.full_like(x, 0.37), s * 3.0, x * 0.05 + 0.5)
    head = smoothstep(1.0, 0.85, s)
    tail = smoothstep(0.0, 0.6, s)
    line = np.exp(-(x / 0.28) ** 2)
    a = line * head * tail * (0.45 + 0.8 * grain)
    out = np.ones((h, w, 4), np.float32)
    out[..., 3] = np.clip(a, 0, 1)
    save("FX_WindStreak", out)


# =========================================================================================
# もや・砂煙の帯 : 横長のやわらかい雲。上から光を当てて下を少し暗くする
# =========================================================================================
def make_wisp(fbm, bil):
    n_, cols = 256, 2
    sheet = np.zeros((n_ * 2, n_ * 2, 4), np.float32)
    x, y = np.meshgrid(np.linspace(-1, 1, n_, dtype=np.float32), np.linspace(-1, 1, n_, dtype=np.float32))
    for k in range(4):
        z = np.full_like(x, 0.21 * k + 0.33)
        n = sample3(bil, x * 0.45 + 0.13 * k, y * 0.8, z)
        nf = sample3(fbm, x * 1.2, y * 2.0 + 0.2 * k, z + 0.5)
        ell = (x / 0.92) ** 2 + (y / (0.42 + 0.12 * k / 3)) ** 2
        field = 1.0 - ell + 0.9 * (n - 0.5) + 0.3 * (nf - 0.5)
        dens = smoothstep(0.0, 0.7, field) * smoothstep(1.0, 0.85, np.maximum(np.abs(x), np.abs(y)))
        # 上(y が負)ほど明るい。濃いところの下側は少し陰る
        lit = np.clip(0.75 + 0.25 * (-y) + 0.25 * (nf - 0.5) - 0.15 * dens, 0, 1)
        img = np.zeros((n_, n_, 4), np.float32)
        img[..., 0] = lit
        img[..., 3] = dens
        place(sheet, img, k, cols, n_, n_)
    save("FX_Wisp_2x2", sheet)


# =========================================================================================
# 岩と土くれ : 丸みのあるでこぼこの塊。高さから面の向きを作って左上から光を当てる
# =========================================================================================
def make_rocks(fbm, rng):
    grid_n, cell, ss = 4, 128, 2
    n_ = cell * ss
    sheet = np.zeros((grid_n * cell, grid_n * cell, 4), np.float32)
    x, y = np.meshgrid(np.linspace(-1, 1, n_, dtype=np.float32), np.linspace(-1, 1, n_, dtype=np.float32))
    r = np.sqrt(x * x + y * y)
    th = np.arctan2(y, x)
    L = np.array([-0.5, -0.6, 0.62], np.float32)
    L /= np.linalg.norm(L)
    for k in range(grid_n * grid_n):
        u = (th / (2 * math.pi)) % 1.0
        edge = sample3(fbm, u * 1.0 + k * 0.07, np.full_like(u, 0.3 + k * 0.05), np.full_like(u, 0.6))
        stretch = 0.6 + 0.4 * rng.random()
        rot = rng.random() * math.pi
        xr = x * math.cos(rot) - y * math.sin(rot)
        yr = (x * math.sin(rot) + y * math.cos(rot)) / stretch
        rr = np.sqrt(xr * xr + yr * yr)
        rad = 0.55 + 0.3 * (edge - 0.5) * 2.0
        inside = rr < rad
        # 高さ : ドーム + 表面のでこぼこ
        h = np.sqrt(np.clip(1.0 - (rr / np.maximum(rad, 1e-3)) ** 2, 0, 1))
        bump = sample3(fbm, x * 1.5 + k * 0.13, y * 1.5, np.full_like(x, 0.2 + k * 0.03))
        h = h + 0.25 * bump
        gy_, gx_ = np.gradient(h)
        nx, ny, nz = -gx_ * 40.0, -gy_ * 40.0, np.ones_like(h)
        ln = np.sqrt(nx * nx + ny * ny + nz * nz)
        lam = np.clip((nx * L[0] + ny * L[1] + nz * L[2]) / ln, 0, 1)
        tone = 0.75 + 0.5 * (sample3(fbm, x * 3.0, y * 3.0, np.full_like(x, k * 0.09)) - 0.5)
        lit = (0.25 + 0.85 * lam) * tone
        img = np.zeros((n_, n_, 4), np.float32)
        img[..., 0] = lit
        img[..., 1] = lit * 0.96
        img[..., 2] = lit * 0.9
        img[..., 3] = inside.astype(np.float32)
        img = img.reshape(cell, ss, cell, ss, 4).mean(axis=(1, 3))
        a = img[..., 3:4]
        img[..., 0:3] = np.where(a > 1e-3, img[..., 0:3] / np.maximum(a, 1e-3), 0.0)
        place(sheet, img, k, grid_n, cell, cell)
    save("FX_Rock_4x4", np.clip(sheet, 0, 1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=None)
    ap.add_argument("--seed", type=int, default=11)
    args = ap.parse_args()

    rng = np.random.default_rng(args.seed)
    print("noise volumes ...")
    fbm, bil = gx.make_noise_volumes(rng)

    def want(n):
        return args.only is None or args.only.lower() in n.lower()

    if want("FlameTongue"):
        make_flame_tongue(fbm, bil)
    if want("MuzzleFront"):
        make_muzzle_front(fbm, rng)
    if want("MuzzleSide"):
        make_muzzle_side(fbm, bil, rng)
    if want("WindStreak"):
        make_wind_streak(fbm)
    if want("Wisp"):
        make_wisp(fbm, bil)
    if want("Rock"):
        make_rocks(fbm, rng)


if __name__ == "__main__":
    main()
