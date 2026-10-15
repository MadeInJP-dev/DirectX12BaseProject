// GPUパーティクルのバッファ要素とディスパッチ設定。
// ※ CPU 側 Engine::Particle 以下の同名構造体と並びを合わせること
#ifndef ROOTPARAM_PARTICLE_HLSLI
#define ROOTPARAM_PARTICLE_HLSLI

// 板ポリの向きの決め方
// ※ CPU 側 Engine::Particle::EParticleOrientation と数値を合わせること
#define PARTICLE_ORIENT_BILLBOARD			0	// 常にカメラ正面
#define PARTICLE_ORIENT_VELOCITY_BILLBOARD	1	// カメラ正面のまま、画面上で進行方向へ回す
#define PARTICLE_ORIENT_VELOCITY_AXIS		2	// 進行方向をワールドの縦軸にする(手前へ向かうと縮む)
#define PARTICLE_ORIENT_EMITTER_AXIS		3	// 画像の上を発生源の噴き出す向き(+Z)へ。板は発生源の X-Z 面
#define PARTICLE_ORIENT_EMITTER_FACING		4	// 板を発生源の噴き出す向きへ向けて立てる。画像の上は発生源の +Y

// 発生方向の決め方
// ※ CPU 側 Engine::Particle::EParticleEmitShape と数値を合わせること
#define PARTICLE_EMIT_SHAPE_CONE		0	// 指定方向を軸にした円錐(噴射)
#define PARTICLE_EMIT_SHAPE_SPHERE		1	// 中心から全方向へ均等(爆発)
#define PARTICLE_EMIT_SHAPE_HEMISPHERE	2	// 指定方向側の半球だけ(地面での爆発)

// 粒1つぶんの状態。更新も描画も同じバッファを読む
struct ParticleData
{
	float3 pos;
	float life;				// 残り寿命
	float3 velocity;
	float size;

	// 発生時点の寿命。寿命は粒ごとにランダムなので、
	// 進行度(0〜1)を出すには割る相手を粒自身が覚えているしかない
	float startLife;

	// 発生源の席番号(EmitterTransform の StructuredBuffer の添字)。
	// 0 は単位行列で予約してあるので、ワールド空間で回す粒は 0 のまま
	uint emitterIndex;

	// 板を面の中で回す角度(ラジアン)と速さ(ラジアン/秒)
	float rotation;
	float angularVelocity;

	// 発生したときの発生源の向き(クォータニオン xyzw)。
	// ワールド空間の粒ならワールド、ローカル空間の粒なら席から見た向き
	float4 orientation;
};

// アセット単位の描画設定。
// 定数バッファなので float4 単位で行が変わる。並べ替えるときはCPU側も必ず揃えること
struct ParticleDrawData
{
	uint	orientation;	// 上の PARTICLE_ORIENT_*
	float	stretch;		// 進行方向への伸ばし倍率
	float	endSizeScale;	// 寿命の終わりでのサイズ倍率(1で変化なし)
	float	fadeInRatio;	// 寿命のうち頭で透明→不透明にする割合(0でなし)

	float	fadeOutRatio;	// 寿命のうち末尾で不透明→透明にする割合(0でなし)
	float	softDepth;		// 奥の物との境目をぼかす幅(m。0でぼかさない)
	float2	pad;

	float4	startColor;		// RGBは1を超えてよい(超えたぶんにブルームが乗る)
	float4	endColor;
};

// 発生源の席1つぶん。全アセット共通の StructuredBuffer に並ぶ。
// ローカル空間で回している粒は、描画時にここの行列でワールドへ戻す。
// 添字0を単位行列で予約してあるので、ワールド空間の粒も分岐なしで同じ経路を通る
// ※ CPU 側 Engine::Particle::EmitterTransform と並びを合わせること
struct EmitterTransform
{
	row_major float4x4 worldMat;	// 拡縮を落とした位置と回転
};

// 発生命令1件ぶん
struct EmitData
{
	// 発生行列 : 形状のローカル空間(+Z が噴き出す向き) → 粒を保存する空間。
	// ワールド空間の粒ならワールド、ローカル空間の粒なら席の座標系。拡縮は入っていない。
	// ※ CPU 側は行ベクトル(v * M)で組んでいるので row_major にすること
	row_major float4x4 emitMatrix;
	uint	emitCount;
	float	baseScale;

	// ---- ばらつき ----
	float positionRadius;	// 発生位置の半径
	float directionAngle;	// 方向のばらつき角度(ラジアン。Cone のみ)

	float minScale;
	float maxScale;

	float minSpeed;
	float maxSpeed;

	float minLifeTime;
	float maxLifeTime;

	uint	emitShape;		// 上の PARTICLE_EMIT_SHAPE_*
	uint	emitterIndex;	// 出した粒に持たせる発生源の番号(ワールド空間なら 0)

	float4	emitRotation;	// emitMatrix の回転部分(クォータニオン xyzw)。粒の orientation に入る

	// 板を面の中で回す角度と速さの区間(ラジアン、ラジアン/秒)
	float	minRotation;
	float	maxRotation;
	float	minAngularVelocity;
	float	maxAngularVelocity;

	// このプールの命令の中で、この命令の粒が何番目から始まるか(前の命令までの emitCount の合計)。
	// 1スレッド = 1粒 で回すときに、どの命令の何個目かを二分探索で引くのに使う
	uint	emitStart;

	// 発生位置を散らす箱の半分の大きさ(ローカル : X = 横、Y = 上、Z = 噴き出す向き)。
	// 0 なら箱は使わず positionRadius だけ
	float3	positionExtents;
};

// クォータニオン(xyzw)でベクトルを回す。
// CPU 側の Math::Quaternion(DirectXMath)と同じ向きに回る
// (行ベクトルの v * M と、M から作ったクォータニオンで回した結果が一致する)
float3 ParticleQuatRotate(float4 a_q, float3 a_v)
{
	float3 _t = 2.0f * cross(a_q.xyz, a_v);
	return a_v + a_q.w * _t + cross(a_q.xyz, _t);
}

// 発生ディスパッチ1回ぶんの設定
struct ParticleEmitSetting
{
	uint requestOffset;		// 発生命令のバッファ(全プール共通の1本)の中で、このプールの命令が始まる位置
	uint requestCount;		// 今回処理する発生命令の数
	uint emitTotal;			// 今回出す粒の合計(= スレッド数。プールの容量で頭打ちにしてある)
	uint frameSeed;			// フレームごとに変わる乱数の種
};

// 更新ディスパッチ1回ぶんの設定
struct ParticleUpdateSetting
{
	float deltaTime;
	float3 gravity;

	float drag;				// 速度の減衰(1秒あたりの割合)。0 で減衰しない
	float3 pad;
};

#endif
