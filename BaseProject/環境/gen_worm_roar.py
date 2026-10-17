# -*- coding: utf-8 -*-
"""
ボイドワーム(SwarmBossController)の鳴き声を作るスクリプト。

金属がきしむような響きを持った、クジラの鳴き声のようなもの。
外部ライブラリは使わない(標準の Python だけで動く)。

    python 環境/gen_worm_roar.py            # BaseProject/ で実行
    python 環境/gen_worm_roar.py --seed 3   # 雑音の出方を変える

出力 : Asset/Sound/Boss/VoidWorm_Roar.wav(44.1kHz / 16bit / ステレオ)
       .assetmeta が無ければ一緒に作る(GUID はコントローラーの既定値と同じもの)。
       作り直しても GUID は変わらないので、シーンの設定はそのまま使える。

音の組み立て
  1. 鳴き声の芯   : 低い声が持ち上がって伸び、最後に低く落ちていく(クジラのうなり)。
                    倍音を重ね、550Hz あたりを持ち上げて「ウォー」という母音っぽさを出す。
  2. 金属の響き   : 周波数比が整数にならない FM と、鐘のような倍音の束。少しずらして唸らせる。
  3. 管の共鳴     : 短い遅延のコムフィルターで、金属の管の中で鳴っているような色を付ける。
  4. 唸り         : 鳴き始めだけ振幅を細かく揺らして、喉の奥で唸る感じにする。
  5. 地鳴り       : 低く濁した雑音で重さを足す。
  6. 残響         : 大きな空間の響き(Schroeder)。左右で遅延を変えて広がりを出す。
"""

import argparse
import math
import os
import random
import struct
import wave

SAMPLE_RATE = 44100
DURATION    = 5.6      # 秒(鳴き声 5 秒 + 残響の尾)
ROAR_END    = 5.0      # 鳴き声そのものが終わる時刻(秒)。コントローラーの既定の「鳴いている長さ」と合わせる

SOUND_GUID  = "ce80f545-bef9-4dde-a491-18edc25cea0d"
OUT_DIR     = os.path.join("Asset", "Sound", "Boss")
OUT_NAME    = "VoidWorm_Roar"

TWO_PI = math.pi * 2.0


#------------------------------------------------------------------------------
# 包絡・音程の形
#------------------------------------------------------------------------------
def smoothstep(a, b, x):
    if b <= a:
        return 1.0 if x >= b else 0.0
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def lerp(a, b, t):
    return a + (b - a) * t


def pitch_curve(t):
    """鳴き声の芯の基本周波数(Hz)。持ち上がって伸び、最後に低く落ちる"""
    if t < 0.7:
        f = lerp(85.0, 150.0, smoothstep(0.0, 0.7, t))
    elif t < 2.9:
        f = lerp(150.0, 215.0, smoothstep(0.7, 2.9, t))
    else:
        f = lerp(215.0, 62.0, smoothstep(2.9, 4.8, t))

    # ゆっくり揺れるビブラート(伸ばしている間だけ深くなる)
    depth = 0.035 * smoothstep(0.8, 1.8, t) * (1.0 - smoothstep(3.6, 4.6, t))
    f *= 1.0 + depth * math.sin(TWO_PI * 4.6 * t)
    return f


def amp_envelope(t):
    """全体の音量の包絡"""
    attack  = smoothstep(0.0, 0.3, t)
    release = 1.0 - smoothstep(ROAR_END - 1.2, ROAR_END, t)
    swell   = 0.85 + 0.15 * math.sin(TWO_PI * 0.35 * t)	# 息継ぎのような大きなうねり
    return attack * release * swell


def formant_gain(freq):
    """550Hz あたりを持ち上げる(母音っぽさ)。高いところはなだらかに落とす"""
    peak = math.exp(-((freq - 550.0) / 260.0) ** 2)
    return 0.35 + 1.2 * peak


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
# 合成
#------------------------------------------------------------------------------
def synthesize(seed):
    rng = random.Random(seed)
    n = int(DURATION * SAMPLE_RATE)

    # 鳴き声の芯の倍音(1〜10)。位相は倍音ごとに持つ
    harmonics = list(range(1, 11))
    voice_phase = [rng.random() * TWO_PI for _ in harmonics]

    # 金属の響き : 整数にならない倍音の束(鐘・金属棒の固有振動の比)。2本ずつ少しずらして唸らせる
    metal_ratios = [2.76, 5.40, 8.93, 13.34]
    metal_detune = [1.0, 1.0035]
    metal_phase = [[rng.random() * TWO_PI for _ in metal_detune] for _ in metal_ratios]

    fm_carrier_phase = 0.0
    fm_mod_phase = 0.0

    # 管の共鳴(短い遅延のコム)。2本を並べて金属の管らしい色にする
    pipe_a = Comb(0.0043, 0.62)
    pipe_b = Comb(0.0061, 0.55)

    rumble_lp1 = OnePoleLowPass(70.0)
    rumble_lp2 = OnePoleLowPass(70.0)

    rev_l = Reverb(0.0)
    rev_r = Reverb(0.023)

    left = [0.0] * n
    right = [0.0] * n
    dt = 1.0 / SAMPLE_RATE

    for i in range(n):
        t = i * dt
        f0 = pitch_curve(t)
        env = amp_envelope(t)

        #------------------------------------------------------
        # 1. 鳴き声の芯
        #------------------------------------------------------
        voice = 0.0
        for k, h in enumerate(harmonics):
            fh = f0 * h
            if fh > 5000.0:
                continue
            voice_phase[k] += TWO_PI * fh * dt
            voice += math.sin(voice_phase[k]) * formant_gain(fh) / (h ** 1.1)
        voice *= 0.55

        # 4. 唸り : 鳴き始めは振幅を細かく揺らす(28Hz)
        growl = 1.0 - 0.45 * (1.0 - smoothstep(0.2, 1.4, t)) * (0.5 + 0.5 * math.sin(TWO_PI * 28.0 * t))
        voice *= growl

        #------------------------------------------------------
        # 2. 金属の響き : FM(比が整数にならない) + 鐘の倍音の束
        #------------------------------------------------------
        fm_index = 2.2 + 1.6 * math.sin(TWO_PI * 0.6 * t)
        fm_mod_phase += TWO_PI * (f0 * 1.414) * dt
        fm_carrier_phase += TWO_PI * (f0 * 3.5) * dt
        fm = math.sin(fm_carrier_phase + fm_index * math.sin(fm_mod_phase))

        bell = 0.0
        for r_i, ratio in enumerate(metal_ratios):
            for d_i, det in enumerate(metal_detune):
                metal_phase[r_i][d_i] += TWO_PI * (f0 * 2.0 * ratio * det) * dt
                bell += math.sin(metal_phase[r_i][d_i]) / (1.0 + r_i * 1.3)
        bell *= 0.18

        # 金属の成分は伸ばしている所で強く、鳴き始めと終わりは控えめ
        metal_amount = 0.35 + 0.65 * smoothstep(0.4, 1.5, t) * (1.0 - smoothstep(3.4, 4.8, t))
        metal = (fm * 0.22 + bell) * metal_amount

        #------------------------------------------------------
        # 5. 地鳴り
        #------------------------------------------------------
        noise = rng.uniform(-1.0, 1.0)
        rumble = rumble_lp2.process(rumble_lp1.process(noise)) * 9.0
        rumble *= 0.6 + 0.4 * (1.0 - smoothstep(0.0, 2.0, t))

        dry = (voice + metal) * env

        #------------------------------------------------------
        # 3. 管の共鳴
        #------------------------------------------------------
        piped = dry + 0.45 * (pipe_a.process(dry) + pipe_b.process(dry))

        # 軽く歪ませて金属のざらつきを出す
        sig = math.tanh(piped * 1.6) * 0.8 + rumble * env

        #------------------------------------------------------
        # 6. 残響
        #------------------------------------------------------
        wet_l = rev_l.process(sig)
        wet_r = rev_r.process(sig)
        left[i] = sig * 0.75 + wet_l * 0.55
        right[i] = sig * 0.75 + wet_r * 0.55

    # 尾の最後はなだらかに 0 へ(切れ目でプツッと鳴らさない)
    fade = int(0.15 * SAMPLE_RATE)
    for i in range(fade):
        g = i / fade
        left[n - 1 - i] *= g
        right[n - 1 - i] *= g

    # -1dB に正規化
    peak = max(max(abs(v) for v in left), max(abs(v) for v in right), 1e-9)
    gain = (10.0 ** (-1.0 / 20.0)) / peak
    return [v * gain for v in left], [v * gain for v in right]


def write_wav(path, left, right):
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        frames = bytearray()
        for l, r in zip(left, right):
            frames += struct.pack("<hh",
                int(max(-1.0, min(1.0, l)) * 32767),
                int(max(-1.0, min(1.0, r)) * 32767))
        w.writeframes(bytes(frames))


def write_meta(path):
    # 既にあれば触らない(GUID を変えない)
    if os.path.exists(path):
        return
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write('{\n    "Files": [\n        ".wav"\n    ],\n    "GUID": "%s",\n    "Type": "Sound"\n}' % SOUND_GUID)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()

    os.makedirs(OUT_DIR, exist_ok=True)
    left, right = synthesize(args.seed)

    wav_path = os.path.join(OUT_DIR, OUT_NAME + ".wav")
    write_wav(wav_path, left, right)
    write_meta(os.path.join(OUT_DIR, OUT_NAME + ".assetmeta"))
    print("wrote", wav_path, "(%.2f sec)" % (len(left) / SAMPLE_RATE))


if __name__ == "__main__":
    main()
