# -*- coding: utf-8 -*-
"""
ボイドワーム(SwarmBossController)の音を作るスクリプト。

金属がきしむような響きを持った、クジラの鳴き声のような声と、飛んでいるときの音。
外部ライブラリは使わない(標準の Python だけで動く)。

    python 環境/gen_worm_sounds.py                 # BaseProject/ で実行。全部作る
    python 環境/gen_worm_sounds.py --only Roar     # 1つだけ作り直す
    python 環境/gen_worm_sounds.py --seed 3        # 雑音の出方を変える

出力 : Asset/Sound/Boss/ の下
  VoidWorm_Roar.wav       咆哮(出現)       44.1kHz / 16bit / ステレオ
  VoidWorm_AttackCry.wav  攻撃の鳴き声     44.1kHz / 16bit / ステレオ
  VoidWorm_DeathCry.wav   断末魔(死亡)     44.1kHz / 16bit / ステレオ
  VoidWorm_Flight.wav     飛行音(ループ)   44.1kHz / 16bit / モノラル(3D で鳴らすため)
  .assetmeta が無ければ一緒に作る(GUID はコントローラーの既定値と同じもの)。
  作り直しても GUID は変わらないので、シーンの設定はそのまま使える。

鳴き声の組み立て(3つとも同じ作りで、音程の動きと各成分の量だけ変える)
  1. 鳴き声の芯   : 音程の折れ線をなだらかにつないだ低い声(クジラのうなり)。
                    倍音を重ね、母音のあたり(既定 550Hz)を持ち上げて「ウォー」という感じを出す。
  2. 金属の響き   : 周波数比が整数にならない FM と、鐘のような倍音の束。少しずらして唸らせる。
  3. 管の共鳴     : 短い遅延のコムフィルターで、金属の管の中で鳴っているような色を付ける。
  4. 唸り         : 鳴き始めは振幅を細かく揺らして、喉の奥で唸る感じにする。
  5. 地鳴り       : 低く濁した雑音で重さを足す。
  6. 残響         : 大きな空間の響き(Schroeder)。左右で遅延を変えて広がりを出す。

飛行音の組み立て
  ・風の唸り(帯域を絞った雑音)と地鳴り
  ・金属のこすれ : 短く鳴って消える高い金属音の粒をばらまく + 細かく刻んだ高い雑音(きしみ)
  ・終わりの 0.6 秒を頭へ重ねて、継ぎ目なくループさせる
"""

import argparse
import math
import os
import random
import struct
import wave

SAMPLE_RATE = 44100
OUT_DIR     = os.path.join("Asset", "Sound", "Boss")
TWO_PI      = math.pi * 2.0


#------------------------------------------------------------------------------
# 形を作る道具
#------------------------------------------------------------------------------
def smoothstep(a, b, x):
    if b <= a:
        return 1.0 if x >= b else 0.0
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def lerp(a, b, t):
    return a + (b - a) * t


def window(t, on, off):
    """on の区間で 0→1、off の区間で 1→0 になる窓"""
    return smoothstep(on[0], on[1], t) * (1.0 - smoothstep(off[0], off[1], t))


def polyline(points, t):
    """(時刻, 値) の折れ線を、区間ごとに smoothstep でなだらかにつなぐ"""
    if t <= points[0][0]:
        return points[0][1]
    for (t0, v0), (t1, v1) in zip(points, points[1:]):
        if t < t1:
            return lerp(v0, v1, smoothstep(t0, t1, t))
    return points[-1][1]


#------------------------------------------------------------------------------
# フィルター
#------------------------------------------------------------------------------
class OnePoleLowPass:
    def __init__(self, cutoff):
        self.a = math.exp(-TWO_PI * cutoff / SAMPLE_RATE)
        self.z = 0.0

    def process(self, x):
        self.z = (1.0 - self.a) * x + self.a * self.z
        return self.z


class Comb:
    def __init__(self, delay_sec, feedback, damp=0.0):
        self.buf = [0.0] * max(1, int(delay_sec * SAMPLE_RATE))
        self.i = 0
        self.fb = feedback
        self.damp = damp
        self.lp = 0.0

    def process(self, x):
        y = self.buf[self.i]
        self.lp = y * (1.0 - self.damp) + self.lp * self.damp
        self.buf[self.i] = x + self.lp * self.fb
        self.i = (self.i + 1) % len(self.buf)
        return y


class AllPass:
    def __init__(self, delay_sec, gain):
        self.buf = [0.0] * max(1, int(delay_sec * SAMPLE_RATE))
        self.i = 0
        self.g = gain

    def process(self, x):
        b = self.buf[self.i]
        y = -x + b
        self.buf[self.i] = x + b * self.g
        self.i = (self.i + 1) % len(self.buf)
        return y


class Reverb:
    """Schroeder の残響。a_spread で遅延をずらして左右を作り分ける"""
    def __init__(self, spread):
        k = 1.0 + spread
        self.combs = [Comb(d * k, 0.86, 0.35) for d in (0.0297, 0.0371, 0.0411, 0.0437, 0.0533)]
        self.alls  = [AllPass(0.0050 * k, 0.7), AllPass(0.0017 * k, 0.7)]

    def process(self, x):
        y = sum(c.process(x) for c in self.combs) * 0.2
        for a in self.alls:
            y = a.process(y)
        return y


#------------------------------------------------------------------------------
# 鳴き声の設計図
#------------------------------------------------------------------------------
CRIES = {
    # 咆哮(出現) : 持ち上がって伸び、最後に低く落ちる。約 5 秒
    "Roar": dict(
        guid="ce80f545-bef9-4dde-a491-18edc25cea0d",
        duration=5.6, end=5.0,
        pitch=[(0.0, 85.0), (0.7, 150.0), (2.9, 215.0), (4.8, 62.0)],
        vib_rate=4.6, vib_depth=0.035, vib_on=(0.8, 1.8), vib_off=(3.6, 4.6),
        late_vib_depth=0.0, late_vib=(0.0, 0.0),
        tremolo_rate=0.0, tremolo_depth=0.0, tremolo=(0.0, 0.0),
        attack=0.3, release=1.2, swell=0.15,
        growl_depth=0.45, growl_fade=(0.2, 1.4), growl_rate=28.0,
        metal_base=0.35, metal_on=(0.4, 1.5), metal_off=(3.4, 4.8),
        fm_index=2.2, formant=550.0, rumble=1.0,
    ),
    # 攻撃の鳴き声 : 短く鋭く跳ね上がって落ちる。約 2 秒
    "AttackCry": dict(
        guid="35fddedf-a55b-47d0-b905-3857f5d57037",
        duration=2.6, end=2.1,
        pitch=[(0.0, 110.0), (0.18, 250.0), (0.8, 290.0), (2.0, 95.0)],
        vib_rate=7.0, vib_depth=0.05, vib_on=(0.25, 0.6), vib_off=(1.4, 1.9),
        late_vib_depth=0.0, late_vib=(0.0, 0.0),
        tremolo_rate=0.0, tremolo_depth=0.0, tremolo=(0.0, 0.0),
        attack=0.05, release=0.7, swell=0.1,
        growl_depth=0.55, growl_fade=(0.05, 0.6), growl_rate=34.0,
        metal_base=0.6, metal_on=(0.05, 0.4), metal_off=(1.5, 2.0),
        fm_index=3.0, formant=650.0, rumble=0.5,
    ),
    # 断末魔 : 高く伸びてから、震えを強めながら長く落ちていく。約 7 秒
    "DeathCry": dict(
        guid="9e91f135-56fb-4d6a-9404-71e4e02a6ec2",
        duration=8.2, end=7.4,
        pitch=[(0.0, 150.0), (0.6, 250.0), (2.2, 275.0), (4.2, 190.0), (6.8, 48.0)],
        vib_rate=5.5, vib_depth=0.03, vib_on=(0.5, 1.5), vib_off=(7.0, 7.4),
        late_vib_depth=0.07, late_vib=(3.5, 6.5),
        tremolo_rate=9.0, tremolo_depth=0.5, tremolo=(4.0, 7.0),
        attack=0.2, release=2.2, swell=0.15,
        growl_depth=0.4, growl_fade=(0.2, 1.5), growl_rate=26.0,
        metal_base=0.5, metal_on=(0.3, 1.5), metal_off=(5.5, 7.2),
        fm_index=2.6, formant=520.0, rumble=1.3,
    ),
}


def synthesize_cry(spec, seed):
    rng = random.Random(seed)
    duration = spec["duration"]
    end = spec["end"]
    n = int(duration * SAMPLE_RATE)

    harmonics = list(range(1, 11))
    voice_phase = [rng.random() * TWO_PI for _ in harmonics]

    # 金属の響き : 整数にならない倍音の束(鐘・金属棒の固有振動の比)。2本ずつ少しずらして唸らせる
    metal_ratios = [2.76, 5.40, 8.93, 13.34]
    metal_detune = [1.0, 1.0035]
    metal_phase = [[rng.random() * TWO_PI for _ in metal_detune] for _ in metal_ratios]

    fm_carrier_phase = 0.0
    fm_mod_phase = 0.0

    pipe_a = Comb(0.0043, 0.62)
    pipe_b = Comb(0.0061, 0.55)

    rumble_lp1 = OnePoleLowPass(70.0)
    rumble_lp2 = OnePoleLowPass(70.0)

    rev_l = Reverb(0.0)
    rev_r = Reverb(0.023)

    formant = spec["formant"]
    def formant_gain(freq):
        peak = math.exp(-((freq - formant) / 260.0) ** 2)
        return 0.35 + 1.2 * peak

    left = [0.0] * n
    right = [0.0] * n
    dt = 1.0 / SAMPLE_RATE

    for i in range(n):
        t = i * dt

        # 音程 : 折れ線 + ビブラート(断末魔は後半で震えを強める)
        depth = spec["vib_depth"] * window(t, spec["vib_on"], spec["vib_off"])
        if spec["late_vib_depth"] > 0.0:
            depth += spec["late_vib_depth"] * smoothstep(spec["late_vib"][0], spec["late_vib"][1], t) * (1.0 - smoothstep(end - 0.4, end, t))
        f0 = polyline(spec["pitch"], t) * (1.0 + depth * math.sin(TWO_PI * spec["vib_rate"] * t))

        # 音量の包絡
        env = smoothstep(0.0, spec["attack"], t) * (1.0 - smoothstep(end - spec["release"], end, t))
        env *= (1.0 - spec["swell"]) + spec["swell"] * math.sin(TWO_PI * 0.35 * t)
        if spec["tremolo_depth"] > 0.0:
            trem = spec["tremolo_depth"] * smoothstep(spec["tremolo"][0], spec["tremolo"][1], t)
            env *= 1.0 - trem * (0.5 + 0.5 * math.sin(TWO_PI * spec["tremolo_rate"] * t))

        # 1. 鳴き声の芯
        voice = 0.0
        for k, h in enumerate(harmonics):
            fh = f0 * h
            if fh > 5000.0:
                continue
            voice_phase[k] += TWO_PI * fh * dt
            voice += math.sin(voice_phase[k]) * formant_gain(fh) / (h ** 1.1)
        voice *= 0.55

        # 4. 唸り
        growl = 1.0 - spec["growl_depth"] * (1.0 - smoothstep(spec["growl_fade"][0], spec["growl_fade"][1], t)) \
            * (0.5 + 0.5 * math.sin(TWO_PI * spec["growl_rate"] * t))
        voice *= growl

        # 2. 金属の響き
        fm_index = spec["fm_index"] + 1.6 * math.sin(TWO_PI * 0.6 * t)
        fm_mod_phase += TWO_PI * (f0 * 1.414) * dt
        fm_carrier_phase += TWO_PI * (f0 * 3.5) * dt
        fm = math.sin(fm_carrier_phase + fm_index * math.sin(fm_mod_phase))

        bell = 0.0
        for r_i, ratio in enumerate(metal_ratios):
            for d_i, det in enumerate(metal_detune):
                metal_phase[r_i][d_i] += TWO_PI * (f0 * 2.0 * ratio * det) * dt
                bell += math.sin(metal_phase[r_i][d_i]) / (1.0 + r_i * 1.3)
        bell *= 0.18

        metal_amount = spec["metal_base"] + (1.0 - spec["metal_base"]) * window(t, spec["metal_on"], spec["metal_off"])
        metal = (fm * 0.22 + bell) * metal_amount

        # 5. 地鳴り
        noise = rng.uniform(-1.0, 1.0)
        rumble = rumble_lp2.process(rumble_lp1.process(noise)) * 9.0 * spec["rumble"]
        rumble *= 0.6 + 0.4 * (1.0 - smoothstep(0.0, 2.0, t))

        dry = (voice + metal) * env

        # 3. 管の共鳴 + 軽い歪み
        piped = dry + 0.45 * (pipe_a.process(dry) + pipe_b.process(dry))
        sig = math.tanh(piped * 1.6) * 0.8 + rumble * env

        # 6. 残響
        left[i] = sig * 0.75 + rev_l.process(sig) * 0.55
        right[i] = sig * 0.75 + rev_r.process(sig) * 0.55

    # 尾の最後はなだらかに 0 へ(切れ目でプツッと鳴らさない)
    fade = int(0.15 * SAMPLE_RATE)
    for i in range(fade):
        g = i / fade
        left[n - 1 - i] *= g
        right[n - 1 - i] *= g

    return normalize([left, right], -1.0)


#------------------------------------------------------------------------------
# 飛行音(ループ)
#------------------------------------------------------------------------------
FLIGHT_GUID     = "1aa55060-e330-47c4-9861-6087e2d11f64"
FLIGHT_LENGTH   = 4.0	# ループ1周の長さ(秒)
FLIGHT_XFADE    = 0.6	# 頭へ重ねる長さ(秒)


def synthesize_flight(seed):
    rng = random.Random(seed + 100)
    loop_n = int(FLIGHT_LENGTH * SAMPLE_RATE)
    xfade_n = int(FLIGHT_XFADE * SAMPLE_RATE)
    n = loop_n + xfade_n
    dt = 1.0 / SAMPLE_RATE

    wind_hi = OnePoleLowPass(900.0)
    wind_lo = OnePoleLowPass(120.0)
    rumble_1 = OnePoleLowPass(60.0)
    rumble_2 = OnePoleLowPass(60.0)
    grind_lo = OnePoleLowPass(2500.0)

    # 金属の粒 : (始まりのサンプル, 周波数, 減衰の時定数, 強さ)
    grains = []
    t = 0.0
    while t < n * dt:
        t += rng.expovariate(35.0)
        grains.append((int(t * SAMPLE_RATE), rng.uniform(1800.0, 6500.0), rng.uniform(0.008, 0.03), rng.uniform(0.05, 0.25)))
    active = []
    grain_i = 0

    out = [0.0] * n
    for i in range(n):
        t = i * dt
        noise = rng.uniform(-1.0, 1.0)

        # 風の唸り : 帯域を絞った雑音。うねりの周期はループの長さで割り切れるものにする
        wind = (wind_hi.process(noise) - wind_lo.process(noise)) * 2.2
        wind *= 0.65 + 0.2 * math.sin(TWO_PI * 0.25 * t) + 0.15 * math.sin(TWO_PI * 0.75 * t + 1.3)

        # 地鳴り
        rumble = rumble_2.process(rumble_1.process(noise)) * 10.0

        # きしみ : 高い雑音を 47Hz で刻む
        hi = noise - grind_lo.process(noise)
        grind = hi * (0.5 + 0.5 * math.copysign(1.0, math.sin(TWO_PI * 47.0 * t))) * 0.12
        grind *= 0.6 + 0.4 * math.sin(TWO_PI * 0.5 * t + 0.7)

        # 金属の粒
        while grain_i < len(grains) and grains[grain_i][0] <= i:
            active.append(grains[grain_i])
            grain_i += 1
        metal = 0.0
        alive = []
        for (start, freq, tau, amp) in active:
            age = (i - start) * dt
            if age > tau * 6.0:
                continue
            alive.append((start, freq, tau, amp))
            e = math.exp(-age / tau) * amp
            metal += (math.sin(TWO_PI * freq * age) + 0.6 * math.sin(TWO_PI * freq * 1.47 * age)) * e
        active = alive

        out[i] = wind * 0.9 + rumble * 0.8 + grind + metal * 0.5

    # 終わりの xfade_n を頭へ重ねて、継ぎ目を無くす
    loop = out[:loop_n]
    for i in range(xfade_n):
        w = i / xfade_n
        loop[i] = out[i] * w + out[loop_n + i] * (1.0 - w)

    return normalize([loop], -3.0)


#------------------------------------------------------------------------------
# 書き出し
#------------------------------------------------------------------------------
def normalize(channels, db):
    peak = max(max(abs(v) for v in ch) for ch in channels)
    gain = (10.0 ** (db / 20.0)) / max(peak, 1e-9)
    return [[v * gain for v in ch] for ch in channels]


def write_wav(path, channels):
    with wave.open(path, "wb") as w:
        w.setnchannels(len(channels))
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        frames = bytearray()
        for samples in zip(*channels):
            for v in samples:
                frames += struct.pack("<h", int(max(-1.0, min(1.0, v)) * 32767))
        w.writeframes(bytes(frames))


def write_meta(path, guid):
    # 既にあれば触らない(GUID を変えない)
    if os.path.exists(path):
        return
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write('{\n    "Files": [\n        ".wav"\n    ],\n    "GUID": "%s",\n    "Type": "Sound"\n}' % guid)


def output(name, guid, channels):
    wav_path = os.path.join(OUT_DIR, "VoidWorm_" + name + ".wav")
    write_wav(wav_path, channels)
    write_meta(os.path.join(OUT_DIR, "VoidWorm_" + name + ".assetmeta"), guid)
    print("wrote", wav_path, "(%.2f sec, %dch)" % (len(channels[0]) / SAMPLE_RATE, len(channels)))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--only", choices=list(CRIES.keys()) + ["Flight"], default=None)
    args = parser.parse_args()

    os.makedirs(OUT_DIR, exist_ok=True)

    for name, spec in CRIES.items():
        if args.only and args.only != name:
            continue
        output(name, spec["guid"], synthesize_cry(spec, args.seed))

    if not args.only or args.only == "Flight":
        output("Flight", FLIGHT_GUID, synthesize_flight(args.seed))


if __name__ == "__main__":
    main()
