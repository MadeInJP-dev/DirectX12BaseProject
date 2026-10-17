# -*- coding: utf-8 -*-
"""
プレイヤーのチャージダッシュの溜めの音を作るスクリプト(ChargeDashSoundComponent の既定の音)。
リアル寄りの SF : ジェットエンジンの回転が上がる音と、機械の金属音。
外部ライブラリは使わない(標準の Python だけで動く)。

    python 環境/gen_charge_dash_sounds.py      # BaseProject/ で実行

出力 : Asset/Sound/Player/ の下(44.1kHz / 16bit / ステレオ)
  ChargeDash_Charging.wav  溜めている間(単発 0.55 秒)
                           ジェットエンジンが回り始めるように、タービンの甲高い音が駆け上がり、
                           噴射のゴーという音が大きくなっていく。溜め時間 0.5 秒に合わせてある
  ChargeDash_Charged.wav   溜まりきった瞬間(単発)
                           「チャキン」: 機械が噛み合う「チャ」(カチッとした打撃)の直後に、
                           金属が鳴る「キン」(整数比でない高い響き)
  ChargeDash_Ready.wav     溜まりきって押している間(2 秒でつながるループ)
                           回りきったジェットが噴かし続けている音。終わりを頭へ重ねて継ぎ目なくつなぐ
  .assetmeta が無ければ一緒に作る(GUID は ChargeDashSoundComponent の既定値と同じもの)。
  音量はコンポーネントの音量で下げる前提で、ここではピークをそろえるだけにしている。
"""

import math
import os
import random
import struct
import wave

SAMPLE_RATE = 44100
OUT_DIR = os.path.join("Asset", "Sound", "Player")
TWO_PI = math.pi * 2.0

GUIDS = {
    "Charging": "9bab18fc-261d-453c-9562-844e28ee9286",
    "Charged":  "3e61a0c5-8eec-49e0-b787-13e5c462adf7",
    "Ready":    "55a7e151-4bf7-44c1-8fd6-9d7938ac9541",
}


def smoothstep(a, b, x):
    if b <= a:
        return 1.0 if x >= b else 0.0
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


class OnePole:
    """1次のローパス。カットオフはサンプルごとに変えてよい"""
    def __init__(self, cutoff):
        self.z = 0.0
        self.set(cutoff)

    def set(self, cutoff):
        self.a = math.exp(-TWO_PI * cutoff / SAMPLE_RATE)

    def process(self, x):
        self.z = (1.0 - self.a) * x + self.a * self.z
        return self.z


#------------------------------------------------------------------------------
# ジェットの部品
#------------------------------------------------------------------------------
class Jet:
    """
    ジェットエンジン1基ぶんの音。rpm01(0〜1)で回転の高さを渡す
      ・噴射のゴー  : 雑音を2段のローパスで丸めたもの。回転が上がるほど明るく・大きく
      ・タービンの鳴き : 羽根の回る高い音(倍音を少し)。わずかに揺らして機械らしい粗さを出す
      ・吸気のシュー : 高い雑音。回転が上がるほど強く
    """
    def __init__(self, rng, detune):
        self.rng = rng
        self.detune = detune
        self.roar1 = OnePole(400.0)
        self.roar2 = OnePole(400.0)
        self.hiss_lp = OnePole(5000.0)
        self.whine_phase = 0.0
        self.wobble_phase = rng.random() * TWO_PI

    def process(self, rpm01, dt):
        noise = self.rng.uniform(-1.0, 1.0)

        cutoff = 250.0 + 2200.0 * rpm01
        self.roar1.set(cutoff)
        self.roar2.set(cutoff)
        roar = self.roar2.process(self.roar1.process(noise)) * (2.0 + 3.0 * rpm01)

        hiss = (noise - self.hiss_lp.process(noise)) * (0.05 + 0.25 * rpm01 * rpm01)

        self.wobble_phase += TWO_PI * 6.0 * dt
        f = (700.0 + 2600.0 * rpm01) * self.detune * (1.0 + 0.004 * math.sin(self.wobble_phase))
        self.whine_phase += TWO_PI * f * dt
        p = self.whine_phase
        whine = (math.sin(p) + 0.3 * math.sin(2.0 * p) + 0.12 * math.sin(3.02 * p)) * (0.04 + 0.14 * rpm01)

        return roar * 0.5 + hiss + whine


#------------------------------------------------------------------------------
# 溜めている間 : 回転が上がって大きくなっていくジェット
#------------------------------------------------------------------------------
def synth_charging(rng):
    duration = 0.55
    n = int(duration * SAMPLE_RATE)
    dt = 1.0 / SAMPLE_RATE
    jets = [Jet(rng, 1.0), Jet(rng, 1.004)]
    out = [[0.0] * n, [0.0] * n]
    for i in range(n):
        t = i * dt
        u = min(t / 0.5, 1.0)						# 0.5 秒で回りきる
        rpm = 0.15 + 0.85 * (1.0 - (1.0 - u) ** 2)	# 立ち上がりは速く、上の方でなだらかに
        gain = (0.25 + 0.75 * u ** 1.5) * smoothstep(0.0, 0.03, t) * (1.0 - smoothstep(duration - 0.03, duration, t))
        for ch in range(2):
            out[ch][i] = jets[ch].process(rpm, dt) * gain
    return out


#------------------------------------------------------------------------------
# 溜まりきった瞬間 : チャキン
#------------------------------------------------------------------------------
def synth_charged(rng):
    duration = 0.5
    n = int(duration * SAMPLE_RATE)
    dt = 1.0 / SAMPLE_RATE

    # 「チャ」: 帯域を絞った雑音のごく短い打撃 + 低い「カ」
    hp = OnePole(1800.0)
    lp = OnePole(7000.0)

    # 「キン」: 少し遅れて鳴る金属の響き(周波数比が整数にならない)。(周波数, 強さ, 減衰)
    KIN_DELAY = 0.045
    partials = [(3170.0, 0.55, 0.13), (4710.0, 0.4, 0.10), (6390.0, 0.25, 0.07), (8130.0, 0.15, 0.05), (3182.0, 0.3, 0.16)]

    out_l = [0.0] * n
    for i in range(n):
        t = i * dt
        noise = rng.uniform(-1.0, 1.0)
        band = lp.process(noise - hp.process(noise))

        # チャ : 2つの細かな打撃(噛み合う機械の音)
        cha = band * (math.exp(-t / 0.006) + 0.6 * math.exp(-max(t - 0.018, 0.0) / 0.005) * (t >= 0.018)) * 1.6
        ka = math.sin(TWO_PI * 210.0 * t) * math.exp(-t / 0.015) * 0.5

        # キン
        kin = 0.0
        tk = t - KIN_DELAY
        if tk >= 0.0:
            attack = smoothstep(0.0, 0.002, tk)
            for k, (f, a, tau) in enumerate(partials):
                kin += a * math.exp(-tk / tau) * math.sin(TWO_PI * f * tk + k)
            kin *= attack
            # 鳴り始めの擦れ
            kin += band * 0.5 * math.exp(-tk / 0.004)

        out_l[i] = cha + ka + kin * 0.9

    # 右は 25 サンプル(約 0.6 ミリ秒)遅らせて広がりを出す
    out_r = [0.0] * 25 + out_l[:-25]
    return [out_l, out_r]


#------------------------------------------------------------------------------
# 溜まりきって押している間 : 回りきったジェットの噴かし(2 秒ループ)
#------------------------------------------------------------------------------
def synth_ready(rng):
    loop_sec = 2.0
    xfade_sec = 0.4
    loop_n = int(loop_sec * SAMPLE_RATE)
    xfade_n = int(xfade_sec * SAMPLE_RATE)
    n = loop_n + xfade_n
    dt = 1.0 / SAMPLE_RATE
    jets = [Jet(rng, 1.0), Jet(rng, 1.004)]
    out = [[0.0] * n, [0.0] * n]
    for i in range(n):
        t = i * dt
        # 回りきった少し下で、わずかに息づく(周期はループの長さで割り切れるもの)
        rpm = 0.93 + 0.03 * math.sin(TWO_PI * 0.5 * t)
        for ch in range(2):
            out[ch][i] = jets[ch].process(rpm, dt)

    # 終わりを頭へ重ねて継ぎ目を無くす
    loops = []
    for ch in range(2):
        loop = out[ch][:loop_n]
        for i in range(xfade_n):
            w = i / xfade_n
            loop[i] = out[ch][i] * w + out[ch][loop_n + i] * (1.0 - w)
        loops.append(loop)
    return loops


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
    if os.path.exists(path):
        return
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write('{\n    "Files": [\n        ".wav"\n    ],\n    "GUID": "%s",\n    "Type": "Sound"\n}' % guid)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    rng = random.Random(1)
    for name, synth in (("Charging", synth_charging), ("Charged", synth_charged), ("Ready", synth_ready)):
        channels = normalize(synth(rng), -3.0)
        path = os.path.join(OUT_DIR, "ChargeDash_" + name + ".wav")
        write_wav(path, channels)
        write_meta(os.path.join(OUT_DIR, "ChargeDash_" + name + ".assetmeta"), GUIDS[name])
        print("wrote", path, "(%.2f sec)" % (len(channels[0]) / SAMPLE_RATE))


if __name__ == "__main__":
    main()
