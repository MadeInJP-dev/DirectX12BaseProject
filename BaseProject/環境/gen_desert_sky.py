# -*- coding: utf-8 -*-
"""
砂漠の空(正距円筒)を大気の散乱から作るスクリプト。numpy が要るので Blender 付属の Python で動かす。

    "C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe" 環境/gen_desert_sky.py
    ... gen_desert_sky.py --only Desert02      # 1枚だけ作り直す
    ... gen_desert_sky.py --preview            # 縮小版の PNG も書く(scratch 用、Asset には置かない)

出力 : Asset/Texture/Sky/<名前>.dds(R11G11B10_FLOAT、4096x2048、ミップなし = 読み込み時に作られる)
       .assetmeta は GUID 固定で、無ければ作る。

作り方
  1. 空 : 地上 100m から見た単一散乱(Nishita)。レイリー・ミー・オゾンに、砂ぼこりの青の吸収を少し足して
     地平線を黄みがかった霞にする。太陽への透過率は高さ × 太陽の天頂角の表を先に作って引く。
  2. 地平線より下 : 遠ざかる砂漠の地面。地面の明るさ(太陽 + 空)に、見下ろす距離ぶんの霞を重ねる。
     砂丘のうねりを低い周波数のノイズで少しだけ付ける。地形のモデルが無いところ(遠く・高い所から)に見える。
  3. 太陽の芯 : 1 を超える値(ブルームに乗る)。周りの光輪はミー散乱が作る。
  4. 薄い巻雲 : 高さ 8km の面に流れる筋雲を、太陽の色で薄く重ねる。
  5. 画面用の値へ : 今のパイプラインは出力に sRGB 変換が無く、空の画像は写真と同じ「画面用の値」として
     使われてきた。そこで物理の明るさに露出を掛けて sRGB へ直す(1 を超えた分はなだらかに残す)。

あわせて、シーンへ入れる値を出す(太陽の色・環境光の色・地平線の霞の色)。
  太陽の色   : 地上に届く太陽の色(大気の透過率)。明るさはシーンの値(輝度 5)に合わせる
  環境光の色 : 空(上半球)の照度の色と、砂の照り返しの色を半分ずつ。明るさは今の 0.4 に合わせる
  霞の色     : 太陽から 90° 横を向いた地平線の少し上の色(画面用の値)
"""

import argparse
import importlib.util
import json
import math
import os
import struct

import numpy as np

_HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("gen_explosion_textures", os.path.join(_HERE, "gen_explosion_textures.py"))
gx = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gx)

OUT_DIR = "Asset/Texture/Sky"

# シーンの平行光の向き(DLDir = 光の進む向き)から太陽の位置を決める。GUID は固定
SKIES = {
    "DesertSky_Desert02": {"guid": "dfdf901c-10ba-4836-ba6c-249f3c13d012", "dl_dir": (0.0, -0.4, -0.49)},
    "DesertSky_Desert00": {"guid": "1b691b21-a4d5-4441-9e76-1000559ebbb7", "dl_dir": (0.0, -1.0, 1.0)},
}

W, H = 4096, 2048           # 出力
SW, SH = 1024, 512          # 散乱を計算する解像度(なめらかなので後で引き伸ばす)

# ---- 大気(m) ----
R_GROUND = 6360e3
R_TOP = 6420e3
H0 = 100.0                                          # 見ている高さ
HR, HM = 8000.0, 1200.0
BETA_R = np.array([5.802e-6, 13.558e-6, 33.1e-6])
BETA_M_SCA = 14.0e-6                                 # 砂漠の霞(晴れた日より濃い)
BETA_M_EXT = BETA_M_SCA * 1.11
BETA_DUST_ABS = np.array([0.25e-6, 0.45e-6, 0.95e-6])   # 砂ぼこりは青を少し吸う(地平線が黄みに寄る)
BETA_O = np.array([0.650e-6, 1.881e-6, 0.085e-6])
MIE_G = 0.78

SAND_ALBEDO = np.array([0.52, 0.42, 0.30])
MULTI_SCATTER = 5.0
SUN_LUMINANCE = 5.0          # シーンの太陽の明るさ(DLColor の輝度)
AMBIENT_LUMINANCE = 0.4      # シーンの環境光の明るさ(今の値)


def lum(c):
    return 0.2126 * c[..., 0] + 0.7152 * c[..., 1] + 0.0722 * c[..., 2]


def densities(h):
    h = np.maximum(h, 0.0)
    r = np.exp(-h / HR)
    m = np.exp(-h / HM)
    o = np.maximum(0.0, 1.0 - np.abs(h - 25000.0) / 15000.0)
    return r, m, o


def extinction(r, m, o):
    return (BETA_R * r[..., None] + (BETA_M_EXT + BETA_DUST_ABS) * m[..., None] + BETA_O * o[..., None])


def ray_sphere(o, d, radius):
    """o, d : (..., 3)。大きい方の解と、小さい方の解(当たらなければ nan)"""
    b = np.sum(o * d, -1)
    c = np.sum(o * o, -1) - radius * radius
    disc = b * b - c
    sq = np.sqrt(np.maximum(disc, 0.0))
    t0 = np.where(disc >= 0, -b - sq, np.nan)
    t1 = np.where(disc >= 0, -b + sq, np.nan)
    return t0, t1


# =========================================================================================
# 太陽への透過率の表 : 高さ × 太陽の天頂角の cos
# =========================================================================================
def build_transmittance_lut(nh=128, nmu=256, steps=64):
    hs = np.linspace(0.0, R_TOP - R_GROUND, nh)
    mus = np.linspace(-0.3, 1.0, nmu)
    hh, mm = np.meshgrid(hs, mus, indexing="ij")
    o = np.stack([np.zeros_like(hh), R_GROUND + hh, np.zeros_like(hh)], -1)
    d = np.stack([np.sqrt(np.maximum(1 - mm * mm, 0)), mm, np.zeros_like(mm)], -1)
    g0, _ = ray_sphere(o, d, R_GROUND)
    hit_ground = np.isfinite(g0) & (g0 > 0)
    _, t_top = ray_sphere(o, d, R_TOP)
    tau = np.zeros(hh.shape + (3,))
    for i in range(steps):
        t = (i + 0.5) / steps * t_top
        p = o + d * t[..., None]
        h = np.linalg.norm(p, axis=-1) - R_GROUND
        r, m, oz = densities(h)
        tau += extinction(r, m, oz) * (t_top / steps)[..., None]
    T = np.exp(-tau)
    T[hit_ground] = 0.0
    return hs, mus, T


def lut_lookup(lut, h, mu):
    hs, mus, T = lut
    fi = np.clip(h / hs[-1] * (len(hs) - 1), 0, len(hs) - 1.001)
    fj = np.clip((mu - mus[0]) / (mus[-1] - mus[0]) * (len(mus) - 1), 0, len(mus) - 1.001)
    i0 = fi.astype(int)
    j0 = fj.astype(int)
    a = (fi - i0)[..., None]
    b = (fj - j0)[..., None]
    return (T[i0, j0] * (1 - a) * (1 - b) + T[i0 + 1, j0] * a * (1 - b)
            + T[i0, j0 + 1] * (1 - a) * b + T[i0 + 1, j0 + 1] * a * b)


def equirect_dirs(w, h):
    u = (np.arange(w) + 0.5) / w
    v = (np.arange(h) + 0.5) / h
    uu, vv = np.meshgrid(u, v)
    yaw = (uu - 0.5) * 2.0 * math.pi           # SkyShader : u = atan2(x, z) / 2π + 0.5
    th = vv * math.pi                           # v = acos(y) / π
    return np.stack([np.sin(yaw) * np.sin(th), np.cos(th), np.cos(yaw) * np.sin(th)], -1)


# =========================================================================================
# 単一散乱
# =========================================================================================
def scatter(dirs, sun, lut, steps=48):
    shp = dirs.shape[:-1]
    o = np.zeros(shp + (3,))
    o[..., 1] = R_GROUND + H0
    g0, _ = ray_sphere(o, dirs, R_GROUND)
    hit = np.isfinite(g0) & (g0 > 0)
    _, t_top = ray_sphere(o, dirs, R_TOP)
    t_max = np.where(hit, g0, t_top)

    mu = np.sum(dirs * sun, -1)
    pr = 3.0 / (16.0 * math.pi) * (1 + mu * mu)
    g = MIE_G
    pm = 3.0 / (8.0 * math.pi) * ((1 - g * g) * (1 + mu * mu)) / ((2 + g * g) * np.power(1 + g * g - 2 * g * mu, 1.5))

    L = np.zeros(shp + (3,))
    tau = np.zeros(shp + (3,))
    prev_t = np.zeros(shp)
    for i in range(steps):
        # 手前を細かく(地平線付近の長い道のりでも、近くの霞が粗くならないように)
        s = ((i + 1) / steps) ** 2
        t = s * t_max
        dt = t - prev_t
        tm = (t + prev_t) * 0.5
        prev_t = t
        p = o + dirs * tm[..., None]
        rr = np.linalg.norm(p, axis=-1)
        h = rr - R_GROUND
        r, m, oz = densities(h)
        tau_mid = tau + extinction(r, m, oz) * (dt * 0.5)[..., None]
        mu_s = np.sum(p * sun, -1) / rr
        Ts = lut_lookup(lut, h, mu_s)
        Tv = np.exp(-tau_mid)
        L += Tv * Ts * (BETA_R * (r * pr)[..., None] + BETA_M_SCA * (m * pm)[..., None]) * dt[..., None]
        tau += extinction(r, m, oz) * dt[..., None]
    return L, np.exp(-tau), hit, t_max


def srgb_encode(x):
    x = np.maximum(x, 0.0)
    return np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.power(x, 1 / 2.4) - 0.055)


def to_display(L, k):
    """物理の明るさ → 画面用の値。1 を超えた分はなだらかに残す(太陽の周りがブルームに乗る)"""
    x = L * k
    low = srgb_encode(np.minimum(x, 1.0))
    return np.where(x > 1.0, 1.0 + (x - 1.0) * 0.6, low)


def pack_r11g11b10(rgb):
    """float → R11G11B10_FLOAT(半精度の上位ビットをそのまま使う。負は 0)"""
    h = np.maximum(rgb, 0.0).astype(np.float16).view(np.uint16).astype(np.uint32)
    r = (h[..., 0] >> 4) & 0x7FF
    g = (h[..., 1] >> 4) & 0x7FF
    b = (h[..., 2] >> 5) & 0x3FF
    return (r | (g << 11) | (b << 22)).astype(np.uint32)


def write_dds_r11g11b10(path, rgb):
    hgt, wid = rgb.shape[:2]
    data = pack_r11g11b10(rgb)
    hdr = bytearray(b"DDS ")
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x8          # CAPS|HEIGHT|WIDTH|PIXELFORMAT|PITCH
    hdr += struct.pack("<7I", 124, flags, hgt, wid, wid * 4, 0, 1)
    hdr += b"\x00" * 44
    hdr += struct.pack("<2I4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)  # pixel format : FOURCC DX10
    hdr += struct.pack("<5I", 0x1000, 0, 0, 0, 0)      # caps
    hdr += struct.pack("<5I", 26, 3, 0, 1, 0)          # DXGI_FORMAT_R11G11B10_FLOAT, TEXTURE2D, misc, array 1, misc2
    with open(path, "wb") as f:
        f.write(bytes(hdr))
        f.write(data.tobytes())


def resize(img, w, h):
    return gx.resize_bilinear(img.astype(np.float32), h, w)


def make_sky(name, cfg, fbm, bil, preview):
    sun = -np.array(cfg["dl_dir"], float)
    sun /= np.linalg.norm(sun)
    print(f"{name}: sun elevation {math.degrees(math.asin(sun[1])):.1f} deg")

    lut = build_transmittance_lut()
    dirs_s = equirect_dirs(SW, SH)
    L, Tview, hit, t_max = scatter(dirs_s, sun, lut)
    # 多重散乱の分を足す(単一散乱だけだと空が地面に比べて暗すぎる。写真の空と地面の比に合わせた値)
    L = L * MULTI_SCATTER

    # ---- 地上に届く太陽の色と、空の照度 ----
    T_sun_ground = lut_lookup(lut, np.array([H0]), np.array([sun[1]]))[0]
    E_sun = T_sun_ground * max(sun[1], 0.0)                 # 水平面の照度(大気の外を 1 とする)
    th = (np.arange(SH) + 0.5) / SH * math.pi
    dOmega = (2 * math.pi / SW) * (math.pi / SH) * np.sin(th)[:, None]
    up = dirs_s[..., 1] > 0
    E_sky = np.sum((L * (dirs_s[..., 1] * up)[..., None]) * dOmega[..., None], axis=(0, 1))

    # ---- 地平線より下 : 霞んだ砂漠の地面 ----
    o = np.array([0.0, R_GROUND + H0, 0.0])
    p = o + dirs_s * np.nan_to_num(t_max)[..., None]
    n = p / np.linalg.norm(p, axis=-1, keepdims=True)
    mu_s = np.sum(n * sun, -1)
    Ts_g = lut_lookup(lut, np.zeros(mu_s.shape), mu_s)
    # 砂丘のうねり : 地面の位置で引く低い周波数のノイズ(遠いほどならされる)
    gx_, gz_ = p[..., 0] / 3000.0, p[..., 2] / 3000.0
    dune = gx.sample3(bil, gx_ * 0.4 + 0.1, gz_ * 0.15 + 0.3, np.full_like(gx_, 0.5)) - 0.5
    ground = SAND_ALBEDO / math.pi * (Ts_g * np.maximum(mu_s, 0.0)[..., None] * (1.0 + 0.5 * dune[..., None]) + E_sky)
    L = np.where(hit[..., None], L + ground * Tview, L)

    # ---- 引き伸ばし ----
    L = resize(L, W, H)
    dirs = equirect_dirs(W, H)
    mu = np.sum(dirs * sun, -1)

    # ---- 薄い巻雲(上を向いた方向だけ) ----
    upd = np.maximum(dirs[..., 1], 1e-3)
    tc = 8000.0 / upd
    cx, cz = dirs[..., 0] * tc / 40000.0, dirs[..., 2] * tc / 40000.0
    streak = gx.sample3(fbm, cx * 1.4 + 0.2, cz * 5.0 + 0.1, np.full_like(cx, 0.7))
    fine = gx.sample3(fbm, cx * 6.0, cz * 14.0, np.full_like(cx, 0.2))
    cov = np.clip((streak - 0.6) * 3.0 + (fine - 0.5) * 0.8, 0.0, 1.0) ** 1.5 * 0.22
    cov *= gx.smoothstep(0.02, 0.15, dirs[..., 1])
    cloud_light = T_sun_ground * (0.55 + 1.6 * np.maximum(mu, 0) ** 6)[..., None] + E_sky * 0.5
    L = L * (1 - cov[..., None] * 0.6) + cov[..., None] * cloud_light * 0.3

    # ---- 太陽の芯(見た目のために本物より少し大きく、周縁減光あり) ----
    ang = np.arccos(np.clip(mu, -1, 1))
    r_sun = math.radians(0.55)
    core = np.clip(1.0 - (ang / r_sun) ** 2, 0.0, 1.0)
    limb = np.sqrt(core)
    disk = gx.smoothstep(r_sun, r_sun * 0.8, ang) * (0.6 + 0.4 * limb)

    # ---- 露出 : 15° 見下ろした砂の地面を画面用の値 0.5 前後に(写真で地面が飛ばない露出) ----
    vg = int((90.0 + 15.0) / 180.0 * H)
    k = 0.21 / max(float(np.median(lum(L[vg - 4:vg + 4]))), 1e-9)
    disp = to_display(L, k)
    disp = disp + disk[..., None] * 30.0 * (T_sun_ground / T_sun_ground.max())

    os.makedirs(OUT_DIR, exist_ok=True)
    write_dds_r11g11b10(os.path.join(OUT_DIR, name + ".dds"), disp.astype(np.float32))
    meta = os.path.join(OUT_DIR, name + ".assetmeta")
    if not os.path.exists(meta):
        with open(meta, "w", encoding="utf-8", newline="\n") as f:
            f.write('{\n    "Files": [\n        ".dds"\n    ],\n'
                    f'    "GUID": "{cfg["guid"]}",\n    "Type": "Texture"\n}}')

    # ---- シーンへ入れる値 ----
    sun_col = T_sun_ground / lum(T_sun_ground) * SUN_LUMINANCE
    bounce = SAND_ALBEDO * (E_sun + E_sky)
    amb = E_sky / lum(E_sky) * 0.5 + bounce / lum(bounce) * 0.5
    amb = amb / lum(amb) * AMBIENT_LUMINANCE
    side = np.cross(sun, [0.0, 1.0, 0.0])
    side = side / np.linalg.norm(side) if np.linalg.norm(side) > 1e-6 else np.array([1.0, 0.0, 0.0])
    hz = np.array([side[0], 0.035, side[2]])
    hz /= np.linalg.norm(hz)
    yaw = math.atan2(hz[0], hz[2])
    hu = int(((yaw / (2 * math.pi) + 0.5) % 1.0) * W)
    hv = int(math.acos(hz[1]) / math.pi * H)
    fog = disp[hv - 2:hv + 3, :, :].mean(axis=(0, 1)) * 0.5 + disp[hv - 2:hv + 3, max(hu - 40, 0):hu + 40].mean(axis=(0, 1)) * 0.5
    result = {"DLColor": [round(float(c), 3) for c in sun_col],
              "AmbientColor": [round(float(c), 3) for c in amb],
              "FogColor": [round(float(c), 3) for c in fog]}
    print(" ", json.dumps(result))
    if preview:
        small = resize(disp, 1024, 512)
        gx.write_png(os.path.join(preview, name + "_preview.png"),
                     np.concatenate([np.clip(small, 0, 1), np.ones(small.shape[:2] + (1,), np.float32)], -1))
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=None)
    ap.add_argument("--preview", default=None, help="縮小版の PNG を書くフォルダ")
    args = ap.parse_args()
    rng = np.random.default_rng(3)
    fbm, bil = gx.make_noise_volumes(rng)
    for name, cfg in SKIES.items():
        if args.only and args.only.lower() not in name.lower():
            continue
        make_sky(name, cfg, fbm, bil, args.preview)


if __name__ == "__main__":
    main()
