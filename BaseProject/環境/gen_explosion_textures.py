# -*- coding: utf-8 -*-
"""
敵の死亡時の爆発・落下の煙で使うパーティクルのテクスチャを作るスクリプト。

numpy が要るので、Blender に付いてくる Python で動かす(BaseProject/ で実行)。

    "C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe" 環境/gen_explosion_textures.py
    ... gen_explosion_textures.py --only Fireball      # 1つだけ作り直す
    ... gen_explosion_textures.py --frames 16          # 試し用に枚数を減らす(並びは 8x8 のまま、残りは空)

出力 : Asset/Texture/ParticleTexture/DeathExplosion/ の下
  DX_Fireball_8x8.png  火球(64コマ。寿命に沿って膨らみ、ちぎれて消える)    2048x2048
  DX_Smoke_8x8.png     煙のかたまり(64コマ。ゆっくり膨らんで渦を巻き、薄れる) 2048x2048
  DX_Debris_4x4.png    金属の破片 16 種(コマごとに別の形。粒ごとに1つ選ぶ)   512x512
  DX_Spark.png         火花の筋(上が頭。進行方向へ伸ばして使う)            64x256
  DX_Flash.png         閃光(芯と光芒)                                     256x256
  DX_Ember.png         火の粉                                             64x64
  DX_DustRing.png      地面を這う土煙の輪(上から見た輪)                   512x512
  .assetmeta が無ければ一緒に作る(GUID は固定。作り直してもパーティクルの参照はそのまま)。

火球と煙は「詰めたチャンネル」で持つ(パーティクルアセットの TexMode = FireSmoke で読む):
  R : 煙の明暗(上と右から光を当て、体積の中を通る光の減り方で陰を付けたもの)
  G : 温度(芯ほど高い。シェーダーが寿命に沿って冷えるしきい値を動かし、熱いところだけ光らせる)
  B : 未使用(0)
  A : 濃さ
色は持たせない。煙の色と火の色はパーティクルアセット側で決める。

作り方(火球と煙)
  1. 周期的な 3D の fBm ノイズの塊(128^3)を1度だけ作る(滑らかなものと、もこもこした billow の2種)。
  2. コマごとに、球の中を「中心からの距離 + ノイズ」で濃さにする。
     ノイズの座標は球の膨らみに合わせて広げ、少しずつずらして沸き立たせる。
     ノイズで座標をゆがめて(ドメインワープ)、丸い玉ではなく、もくもくした形にする。
  3. 寿命の後半はしきい値を上げて、外側からちぎれて筋になって消えていくようにする。
  4. 上と右から入る光は、その方向へ濃さを積み上げて Beer-Lambert で減らす(自己影)。
  5. 手前から奥へ重ねて、1枚の絵(明暗・温度・濃さ)にする。
  コマの端は必ず透明にする(隣のコマへにじまないように)。
"""

import argparse
import math
import os
import struct
import zlib

import numpy as np

OUT_DIR = "Asset/Texture/ParticleTexture/DeathExplosion"

# 作り直しても参照が切れないよう、GUID は固定
GUIDS = {
    "DX_Fireball_8x8": "4168666b-ff36-47dc-a228-31bca9b98170",
    "DX_Smoke_8x8": "dcb5a495-41ef-467f-bce0-85dedaccbe87",
    "DX_Debris_4x4": "3e12d4bf-8daf-463b-ac82-aafa4c97e77e",
    "DX_Spark": "c13d2ac0-98e7-4fc9-a387-7cc2fb882254",
    "DX_Flash": "10bb6e33-f465-41ae-aff8-3ef4cd824083",
    "DX_Ember": "0a2bd15d-c16d-4f68-8f0e-1b26ae24d6d4",
    "DX_DustRing": "66f147b3-7850-47e3-8782-880fc414dcf7",
}


# =========================================================================================
# 書き出し
# =========================================================================================
def write_png(path, rgba):
    """rgba : (H, W, 4) の 0〜1 の float。8bit の RGBA で書く"""
    data = (np.clip(rgba, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    h, w, _ = data.shape
    raw = b"".join(b"\x00" + data[y].tobytes() for y in range(h))

    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def write_meta(name):
    path = os.path.join(OUT_DIR, name + ".assetmeta")
    if os.path.exists(path):
        return
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write('{\n    "Files": [\n        ".png"\n    ],\n'
                f'    "GUID": "{GUIDS[name]}",\n    "Type": "Texture"\n}}')


def save(name, rgba):
    os.makedirs(OUT_DIR, exist_ok=True)
    write_png(os.path.join(OUT_DIR, name + ".png"), rgba)
    write_meta(name)
    print(f"  wrote {name}.png {rgba.shape[1]}x{rgba.shape[0]}")


# =========================================================================================
# ノイズ
# =========================================================================================
def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _upsample_axis(a, axis, size):
    """格子の値を、周期を保ったまま size へなめらかに伸ばす(5次のフェードで値ノイズ補間)"""
    n = a.shape[axis]
    pos = np.arange(size, dtype=np.float32) * (n / size)
    i0 = np.floor(pos).astype(np.int64)
    f = pos - i0
    f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0)
    i0 %= n
    i1 = (i0 + 1) % n
    a0 = np.take(a, i0, axis=axis)
    a1 = np.take(a, i1, axis=axis)
    shape = [1, 1, 1]
    shape[axis] = size
    f = f.reshape(shape)
    return a0 + (a1 - a0) * f


def make_noise_volumes(rng, size=128, base=4, octaves=5):
    """周期的な 3D ノイズを2種作る : fbm(0〜1)と billow(0〜1。もこもこした雲)"""
    fbm = np.zeros((size, size, size), np.float32)
    bil = np.zeros((size, size, size), np.float32)
    amp, total = 1.0, 0.0
    for o in range(octaves):
        n = base * (2 ** o)
        g = rng.random((n, n, n), dtype=np.float32)
        for ax in range(3):
            g = _upsample_axis(g, ax, size)
        fbm += g * amp
        bil += np.abs(g * 2.0 - 1.0) * amp
        total += amp
        amp *= 0.5
    fbm /= total
    bil /= total
    # 0〜1 へ広げる(重ねると真ん中へ寄るので)
    for v in (fbm, bil):
        lo, hi = np.percentile(v, 1), np.percentile(v, 99)
        v -= lo
        v /= (hi - lo)
        np.clip(v, 0.0, 1.0, out=v)
    return fbm, bil


def sample3(vol, x, y, z):
    """周期的な体積を三線形で引く。座標は体積の1周 = 1.0"""
    n = vol.shape[0]
    x = x * n
    y = y * n
    z = z * n
    x0 = np.floor(x)
    y0 = np.floor(y)
    z0 = np.floor(z)
    fx = (x - x0).astype(np.float32)
    fy = (y - y0).astype(np.float32)
    fz = (z - z0).astype(np.float32)
    x0 = x0.astype(np.int64) % n
    y0 = y0.astype(np.int64) % n
    z0 = z0.astype(np.int64) % n
    x1 = (x0 + 1) % n
    y1 = (y0 + 1) % n
    z1 = (z0 + 1) % n
    flat = vol.ravel()

    def g(a, b, c):
        return flat[(a * n + b) * n + c]

    c00 = g(x0, y0, z0) * (1 - fz) + g(x0, y0, z1) * fz
    c01 = g(x0, y1, z0) * (1 - fz) + g(x0, y1, z1) * fz
    c10 = g(x1, y0, z0) * (1 - fz) + g(x1, y0, z1) * fz
    c11 = g(x1, y1, z0) * (1 - fz) + g(x1, y1, z1) * fz
    c0 = c00 * (1 - fy) + c01 * fy
    c1 = c10 * (1 - fy) + c11 * fy
    return c0 * (1 - fx) + c1 * fx


def resize_bilinear(img, out_h, out_w):
    """(H, W, C) を双線形で拡縮する"""
    h, w = img.shape[:2]
    ys = (np.arange(out_h) + 0.5) * (h / out_h) - 0.5
    xs = (np.arange(out_w) + 0.5) * (w / out_w) - 0.5
    y0 = np.clip(np.floor(ys).astype(int), 0, h - 1)
    x0 = np.clip(np.floor(xs).astype(int), 0, w - 1)
    y1 = np.clip(y0 + 1, 0, h - 1)
    x1 = np.clip(x0 + 1, 0, w - 1)
    fy = np.clip(ys - y0, 0, 1)[:, None, None]
    fx = np.clip(xs - x0, 0, 1)[None, :, None]
    a = img[y0][:, x0]
    b = img[y0][:, x1]
    c = img[y1][:, x0]
    d = img[y1][:, x1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


# =========================================================================================
# 体積の玉(火球・煙)
# =========================================================================================
class VolumeStyle:
    """1枚の flipbook の作り方"""

    def __init__(self, **kw):
        # 球の半径(コマの半分 = 1)。寿命の頭と終わり
        self.r0 = 0.40
        self.r1 = 0.82
        self.grow_pow = 2.2          # 膨らみ方(大きいほど頭で一気に膨らむ)
        self.noise_scale = 0.55      # 大きな塊のノイズの細かさ
        self.detail_scale = 1.35     # 細かいもこもこの細かさ
        self.warp = 0.32             # 座標のゆがみ
        self.lump = 0.55             # 表面の凸凹の強さ
        self.detail = 0.22           # 細かいもこもこの強さ
        self.boil = 0.45             # 寿命のあいだに沸き立つ量(ノイズのずらし)
        self.rise = 0.20             # 上へ流れるずらし(煙が立ち上る)
        self.erode_start = 0.45      # しきい値を上げ始める進み具合
        self.erode = 0.65            # 終わりでのしきい値
        self.edge = 0.16             # 濃さの立ち上がり幅(小さいほど輪郭がくっきり)
        self.sigma = 9.0             # 見る向きの濃さ(大きいほど不透明)
        self.light_sigma = 5.0       # 光の減り方
        self.ambient = 0.28          # 陰の明るさ
        self.temp_core = 1.25        # 温度 : 芯の高さ
        self.temp_noise = 0.9        # 温度のむら
        self.end_fade = 0.25         # 最後のこの割合で薄くする
        for k, v in kw.items():
            setattr(self, k, v)


def render_volume_frame(t, style, fbm, bil, res, depth, seed_off):
    """1コマぶん : (res, res, 4) を返す。R = 明暗、G = 温度、A = 濃さ"""
    s = style
    lin = np.linspace(-1.0, 1.0, res, dtype=np.float32)
    zl = np.linspace(-1.0, 1.0, depth, dtype=np.float32)
    # [z, y, x]。y は上が -1(絵の上)、z は手前が -1
    z, y, x = np.meshgrid(zl, lin, lin, indexing="ij")

    grow = 1.0 - (1.0 - t) ** s.grow_pow
    R = s.r0 + (s.r1 - s.r0) * grow

    # ノイズの座標 : 玉の膨らみに合わせて広げる(模様ごと膨らんで見える)。
    # 寿命に沿ってずらして沸き立たせ、上へ流す
    q_scale = s.noise_scale / R
    ox, oy, oz = seed_off
    boil = s.boil * t
    qx = x * q_scale + ox + boil * 0.37
    qy = y * q_scale + oy + s.rise * t      # 模様が上へ流れる(絵の上 = -y なので + で上へ)
    qz = z * q_scale + oz + boil

    # ドメインワープ : 3つの軸をそれぞれ別の場所のノイズでゆがめる
    wx = sample3(fbm, qx * 0.7 + 0.13, qy * 0.7, qz * 0.7) - 0.5
    wy = sample3(fbm, qx * 0.7, qy * 0.7 + 0.41, qz * 0.7) - 0.5
    wz = sample3(fbm, qx * 0.7, qy * 0.7, qz * 0.7 + 0.77) - 0.5
    qx += wx * s.warp * 2.0
    qy += wy * s.warp * 2.0
    qz += wz * s.warp * 2.0

    big = sample3(bil, qx, qy, qz)
    fine = sample3(bil, qx * s.detail_scale + 0.31, qy * s.detail_scale + 0.17, qz * s.detail_scale + 0.53)

    r = np.sqrt(x * x + y * y + z * z) / R
    field = (1.0 - r) + s.lump * (big - 0.5) + s.detail * (fine - 0.5)

    # 後半はしきい値を上げて、外側からちぎれて消えていく
    erode_t = max(0.0, (t - s.erode_start) / max(1e-4, 1.0 - s.erode_start))
    thr = s.erode * (erode_t ** 1.4)
    dens = smoothstep(thr, thr + s.edge, field)

    # 球の殻の外とコマの端は必ず 0(隣のコマへにじませない)
    rxy = np.sqrt(x * x + y * y)
    dens *= smoothstep(0.98, 0.86, rxy)
    dens *= smoothstep(1.0, 0.85, np.abs(z))

    # 最後は全体を薄くする(しきい値だけだと筋が残る)
    dens *= 1.0 - smoothstep(1.0 - s.end_fade, 1.0, t) * 0.85

    # ---- 光 : 右上手前から入る光を、光の来る向きへ濃さを積んで減らす(自己影) ----
    # 光の向きへ体積をずらしながら足していく(ずらす量はボクセル単位に丸める。端は濃さ 0 なので回り込みは無害)
    dxy = 2.0 / res
    dzv = 2.0 / depth
    L = np.array([0.45, -0.80, -0.40], np.float32)       # x 右 / y 上(-) / z 手前(-)
    L /= np.linalg.norm(L)
    step = 0.035
    acc = np.zeros_like(dens)
    for k in range(1, 28):
        sx = int(round(-L[0] * step * k / dxy))
        sy = int(round(-L[1] * step * k / dxy))
        sz = int(round(-L[2] * step * k / dzv))
        acc += np.roll(dens, (sz, sy, sx), axis=(0, 1, 2))
    acc *= step
    t_sun = np.exp(-s.light_sigma * acc)
    # まわりからの光 : 近くの濃さが濃いほど届きにくい(芯の奥ほど暗い)
    acc_amb = np.zeros_like(dens)
    for (sz, sy, sx) in ((0, -3, 0), (0, 3, 0), (0, 0, 3), (0, 0, -3), (-2, 0, 0), (0, -7, 0), (0, 0, 7), (0, 0, -7)):
        acc_amb += np.roll(dens, (sz, sy, sx), axis=(0, 1, 2))
    t_amb = np.exp(-0.35 * acc_amb)
    # 下側は地面の照り返しで少しだけ明るい(真っ黒につぶさない)
    bounce = 0.05 * smoothstep(-0.2, 1.0, y)
    lit = s.ambient * t_amb + 0.95 * t_sun + bounce

    # ---- 温度 : 芯ほど高く、むらを付ける ----
    tn = sample3(fbm, qx * 1.6 + 0.6, qy * 1.6 + 0.2, qz * 1.6 + 0.9)
    temp = np.clip(s.temp_core * (1.0 - r * 1.1) + s.temp_noise * (tn - 0.5) + 0.25 * (big - 0.5), 0.0, 1.0)

    # ---- 手前から奥へ重ねる ----
    dz = 2.0 / depth
    alpha_i = 1.0 - np.exp(-s.sigma * dens * dz)
    trans = np.cumprod(np.concatenate([np.ones_like(alpha_i[:1]), 1.0 - alpha_i[:-1]], axis=0), axis=0)
    w = trans * alpha_i
    A = w.sum(axis=0)
    safe = np.maximum(A, 1e-5)
    Rch = (w * lit).sum(axis=0) / safe
    Gch = (w * temp).sum(axis=0) / safe

    out = np.zeros((res, res, 4), np.float32)
    out[..., 0] = np.clip(Rch, 0.0, 1.0)
    out[..., 1] = np.clip(Gch, 0.0, 1.0)
    out[..., 3] = np.clip(A, 0.0, 1.0)
    # 濃さがほぼ無いところは色を 0 にしておく(縮小したときに黒や緑がにじまない)
    return out


def make_flipbook(name, style, fbm, bil, rng, frames=64, grid=8, cell=256, res=176, depth=72):
    sheet = np.zeros((grid * cell, grid * cell, 4), np.float32)
    seed_off = tuple(rng.random(3).astype(np.float32))
    for f in range(min(frames, grid * grid)):
        t = f / (grid * grid - 1)
        img = render_volume_frame(t, style, fbm, bil, res, depth, seed_off)
        if res != cell:
            img = resize_bilinear(img, cell, cell)
        gy, gx = divmod(f, grid)
        sheet[gy * cell:(gy + 1) * cell, gx * cell:(gx + 1) * cell] = img
        print(f"    {name} frame {f + 1}/{grid * grid}", end="\r")
    print()
    # 透明なところの色は、縮小(ミップ)で黒くにじまないよう近くの色で埋めておく
    a = sheet[..., 3:4]
    sheet[..., 0:3] = np.where(a > 0.002, sheet[..., 0:3], 0.0)
    save(name, sheet)


# =========================================================================================
# 板もの
# =========================================================================================
def grid_uv(h, w):
    y, x = np.meshgrid(np.linspace(-1, 1, h, dtype=np.float32),
                       np.linspace(-1, 1, w, dtype=np.float32), indexing="ij")
    return x, y


def make_spark():
    """火花の筋 : 上(V=0)が頭。頭が白く光り、尾は細く消える"""
    h, w = 256, 64
    x, y = grid_uv(h, w)
    v = (y + 1.0) * 0.5                      # 0 = 頭、1 = 尾
    head = smoothstep(0.0, 0.06, v)          # 頭の先は丸く
    tail = (1.0 - v) ** 1.6
    width = 0.10 + 0.22 * (1.0 - v) ** 0.7   # 頭ほど太い
    core = np.exp(-(x / (width * 0.45)) ** 2)
    glow = np.exp(-(x / width) ** 2) * 0.45
    a = (core + glow) * head * tail
    a *= smoothstep(1.0, 0.85, np.abs(x))
    rgba = np.ones((h, w, 4), np.float32)
    rgba[..., 3] = np.clip(a, 0, 1)
    save("DX_Spark", rgba)


def make_flash():
    """閃光 : 芯は強く、外は長く尾を引いてぼける。光芒を薄く足す"""
    n = 256
    x, y = grid_uv(n, n)
    r = np.sqrt(x * x + y * y)
    th = np.arctan2(y, x)
    core = np.exp(-(r / 0.16) ** 2)
    halo = 0.35 / (1.0 + (r / 0.18) ** 2)
    rays = (np.abs(np.cos(th * 3.0)) ** 24 + 0.6 * np.abs(np.cos(th * 3.0 + 0.5)) ** 40) * np.exp(-r * 3.0) * 0.35
    a = (core + halo + rays) * smoothstep(1.0, 0.6, r)
    rgba = np.ones((n, n, 4), np.float32)
    rgba[..., 3] = np.clip(a, 0, 1)
    save("DX_Flash", rgba)


def make_ember():
    n = 64
    x, y = grid_uv(n, n)
    r = np.sqrt(x * x + y * y)
    a = np.exp(-(r / 0.22) ** 2) + 0.25 * np.exp(-(r / 0.55) ** 2)
    a *= smoothstep(1.0, 0.75, r)
    rgba = np.ones((n, n, 4), np.float32)
    rgba[..., 3] = np.clip(a, 0, 1)
    save("DX_Ember", rgba)


def make_dust_ring(fbm, bil):
    """地面を這う土煙の輪(上から見た形)。外へ広がる縁が濃く、内側は薄い"""
    n = 512
    x, y = grid_uv(n, n)
    r = np.sqrt(x * x + y * y)
    th = np.arctan2(y, x)
    # 角度方向に周期を持つノイズで縁をちぎる
    u = (th / (2 * math.pi)) % 1.0
    nb = sample3(bil, u * 1.0, r * 0.35 + 0.2, np.full_like(r, 0.37))
    nf = sample3(fbm, u * 3.0 + 0.5, r * 0.9 + 0.1, np.full_like(r, 0.71))
    ring_r = 0.66 + 0.08 * (nb - 0.5)
    inner = np.exp(-((r - ring_r) / 0.10) ** 2)
    outer = np.where(r > ring_r, np.exp(-((r - ring_r) / 0.07) ** 2), inner)
    a = outer * (0.55 + 0.75 * nf) + 0.18 * smoothstep(0.75, 0.1, r) * nf
    a *= smoothstep(0.98, 0.85, r)
    lit = 0.75 + 0.25 * (nf - 0.5) * 2.0
    rgba = np.zeros((n, n, 4), np.float32)
    rgba[..., 0] = np.clip(lit, 0, 1)
    rgba[..., 1] = np.clip(lit, 0, 1)
    rgba[..., 2] = np.clip(lit, 0, 1)
    rgba[..., 3] = np.clip(a, 0, 1)
    save("DX_DustRing", rgba)


def _convex_hull(pts):
    pts = sorted(map(tuple, pts))
    if len(pts) <= 2:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def make_debris(rng, fbm):
    """金属の破片 16 種。凸の多角形を2面に割って明暗を付け、欠けとこすれ傷を足す"""
    grid, cell, ss = 4, 128, 4
    n = cell * ss
    sheet = np.zeros((grid * cell, grid * cell, 4), np.float32)
    x, y = grid_uv(n, n)
    for k in range(grid * grid):
        # 細長いものから塊まで
        cnt = rng.integers(5, 9)
        ang = np.sort(rng.random(cnt) * 2 * math.pi)
        rad = 0.45 + 0.4 * rng.random(cnt)
        stretch = 0.45 + 0.55 * rng.random()
        pts = np.stack([np.cos(ang) * rad, np.sin(ang) * rad * stretch], 1)
        rot = rng.random() * 2 * math.pi
        c, s = math.cos(rot), math.sin(rot)
        pts = pts @ np.array([[c, -s], [s, c]])
        hull = _convex_hull(pts * 0.88)
        inside = np.ones_like(x, bool)
        for i in range(len(hull)):
            ax, ay = hull[i]
            bx, by = hull[(i + 1) % len(hull)]
            inside &= ((bx - ax) * (y - ay) - (by - ay) * (x - ax)) >= 0
        # 欠け : 縁に円を1〜2個かじる
        for _ in range(rng.integers(1, 3)):
            a0 = rng.random() * 2 * math.pi
            cx, cy = math.cos(a0) * 0.7, math.sin(a0) * 0.7 * stretch
            inside &= ((x - cx) ** 2 + (y - cy) ** 2) > (0.18 + 0.2 * rng.random()) ** 2
        # 2面に割る : 割り線のどちら側かで明るさを変える
        la = rng.random() * 2 * math.pi
        side = (x * math.cos(la) + y * math.sin(la)) > (rng.random() - 0.5) * 0.3
        facet = np.where(side, 0.95, 0.55)
        # なだらかな光の傾き(左上から光)と、こすれ傷
        grad = 0.85 + 0.25 * (-x - y) * 0.5
        grime = sample3(fbm, x * 0.6 + k * 0.071, y * 0.6, np.full_like(x, k * 0.13))
        scratch = sample3(fbm, x * 2.5 + 0.3, y * 0.15 + k * 0.05, np.full_like(x, 0.5))
        lit = facet * grad * (0.8 + 0.3 * grime) + 0.08 * smoothstep(0.66, 0.78, scratch)

        # 縁の明るい線(面取りの照り)
        edge = np.zeros_like(lit)
        for i in range(len(hull)):
            ax, ay = hull[i]
            bx, by = hull[(i + 1) % len(hull)]
            ex, ey = bx - ax, by - ay
            l = math.hypot(ex, ey) + 1e-6
            d = ((ex) * (y - ay) - (ey) * (x - ax)) / l
            nx_, ny_ = -ey / l, ex / l
            facing = max(0.0, -(nx_ + ny_) * 0.7071)
            edge = np.maximum(edge, np.exp(-(d / 0.035) ** 2) * facing)
        lit = lit + 0.5 * edge

        img = np.zeros((n, n, 4), np.float32)
        img[..., 0] = lit
        img[..., 1] = lit * 0.97
        img[..., 2] = lit * 0.93
        img[..., 3] = inside.astype(np.float32)
        # 縮めて縁をなめらかに
        img = img.reshape(cell, ss, cell, ss, 4).mean(axis=(1, 3))
        a = img[..., 3:4]
        img[..., 0:3] = np.where(a > 1e-3, img[..., 0:3] / np.maximum(a, 1e-3), 0.0)
        gy, gx = divmod(k, grid)
        sheet[gy * cell:(gy + 1) * cell, gx * cell:(gx + 1) * cell] = img
    save("DX_Debris_4x4", np.clip(sheet, 0, 1))


# =========================================================================================
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=None)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--frames", type=int, default=64)
    args = ap.parse_args()

    rng = np.random.default_rng(args.seed)
    print("noise volumes ...")
    fbm, bil = make_noise_volumes(rng)

    def want(n):
        return args.only is None or args.only.lower() in n.lower()

    if want("Fireball"):
        # 火球 : 頭で一気に膨らみ、芯が熱い。後半は外側からちぎれて筋になる
        style = VolumeStyle(r0=0.34, r1=0.84, grow_pow=2.6, noise_scale=0.55, warp=0.26,
                            lump=0.66, detail=0.18, boil=0.5, rise=0.25,
                            erode_start=0.40, erode=0.70, edge=0.14, sigma=10.0,
                            light_sigma=4.5, temp_core=1.35, temp_noise=0.95)
        make_flipbook("DX_Fireball_8x8", style, fbm, bil, rng, frames=args.frames)

    if want("Smoke"):
        # 煙 : ゆっくり膨らみ、模様が大きく、上へ流れて薄れる
        style = VolumeStyle(r0=0.48, r1=0.86, grow_pow=1.6, noise_scale=0.36, warp=0.28,
                            lump=0.66, detail=0.15, boil=0.35, rise=0.35,
                            erode_start=0.35, erode=0.55, edge=0.26, sigma=7.0,
                            light_sigma=4.0, ambient=0.32, temp_core=1.0, temp_noise=0.8,
                            end_fade=0.35)
        make_flipbook("DX_Smoke_8x8", style, fbm, bil, rng, frames=args.frames)

    if want("Debris"):
        make_debris(rng, fbm)
    if want("Spark"):
        make_spark()
    if want("Flash"):
        make_flash()
    if want("Ember"):
        make_ember()
    if want("DustRing"):
        make_dust_ring(fbm, bil)


if __name__ == "__main__":
    main()
