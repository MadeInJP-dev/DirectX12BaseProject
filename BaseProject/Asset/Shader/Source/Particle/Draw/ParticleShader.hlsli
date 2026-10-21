// ルートパラメーターの構造体
#include "../../../Common/RootParameters/CameraData.hlsli"
#include "../../../Common/RootParameters/Particle.hlsli"

#include "../../../Common/RootSignatureLayout.hlsli"

//==========================================================================================
// ルートパラメーター
//
//   0 : CBV(b0)         カメラ
//   1 : SRVの番号     描画設定の表 + 発生源の席 + 描く一覧、と表の番号
//   2 : SRVの番号     深度(グラフが張る。奥の物との境目をぼかすのに使う)
//
// 粒バッファと絵はアセットごとに違うので、描画設定の表(ParticleDrawRecord)から番号を引く。
//   ふつう     : 加算・半透明を問わず全アセットの粒を遠い順に並べた一覧を1回で描く。
//                一覧の1件の上位ビットが表の番号(ルート定数は PARTICLE_DRAW_RECORD_FROM_LIST)
//   並べられない : アセットごとに描く。描く一覧 = そのプールの生存リスト(粒の番号)、表の番号はルート定数
// 合成はプリマルチプライド(色 × 1 + 下地 × (1 - α))。PS が加算なら α = 0、半透明なら α を出す
//==========================================================================================
#define PARTICLE_ROOT_SIG \
RS_FLAGS","\
"CBV(b0, visibility = SHADER_VISIBILITY_ALL),"\
"RootConstants(num32BitConstants=4, b100, visibility = SHADER_VISIBILITY_VERTEX),"\
"RootConstants(num32BitConstants=1, b102, visibility = SHADER_VISIBILITY_PIXEL),"\
RS_STATIC_SAMPLER

cbuffer CBCamera : register(b0)
{
	CameraData g_camera;
}

// SRVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex0 : register(b100)
{
	uint g_drawRecordIndex;			// 描画設定の表(全アセット共通の1本)
	uint g_emitterSlotIndex;		// 発生源の席(全アセット共通の1本)
	uint g_drawListIndex;			// 描く一覧(SV_InstanceID で引く)
	uint g_recordIndex;				// 表の番号。PARTICLE_DRAW_RECORD_FROM_LIST なら一覧の上位ビットから引く
}

StructuredBuffer<ParticleDrawRecord> Get_drawRecords() { StructuredBuffer<ParticleDrawRecord> _r = ResourceDescriptorHeap[g_drawRecordIndex]; return _r; }
#define g_drawRecords Get_drawRecords()
StructuredBuffer<EmitterTransform> Get_emitterSlots() { StructuredBuffer<EmitterTransform> _r = ResourceDescriptorHeap[g_emitterSlotIndex]; return _r; }
#define g_emitterSlots Get_emitterSlots()
StructuredBuffer<uint> Get_drawList() { StructuredBuffer<uint> _r = ResourceDescriptorHeap[g_drawListIndex]; return _r; }
#define g_drawList Get_drawList()

// 深度の SRV の番号(グラフが張る)。深度が繋がっていないパイプラインでは無効値が来る
cbuffer PassDescriptorIndex2 : register(b102)
{
	uint g_depthTexIndex;
}

#define PARTICLE_INVALID_DESCRIPTOR_INDEX 0xFFFFFFFFu

Texture2D<float4> Get_depthTex() { Texture2D<float4> _r = ResourceDescriptorHeap[g_depthTexIndex]; return _r; }
#define g_depthTex Get_depthTex()

SamplerState g_samp : register(s0);

// ヘルパー関数 : 上で宣言した g_camera を使うので、必ずこの位置で読むこと
#include "../../../Common/Math/Transform.hlsli"
#include "../../../Common/Math/Normal.hlsli"

// 頂点入力
struct VSInput
{
	float4 pos	: POSITION;			// 頂点座標
	float2 uv	: TEXCOORD0;		// UV座標
	uint instID : SV_InstanceID;	// インスタンス番号
};

// 頂点出力
struct VSOutput
{
	float4 pos : SV_Position;		// 射影行列

	// 絵の UV。xy = 今のコマ、zw = 次のコマ(フリップブックで隣のコマと混ぜるとき。それ以外は xy と同じ)
	float4 uv : TEXCOORD0;
	float4 color : TEXCOORD1;

	// 板のその点のビュー空間の奥行き(m)。奥の物との境目をぼかすのに使う
	float viewDepth : TEXCOORD2;

	// 描画設定の表から引いた、PS が使うもの。
	// 1回の描画にいろいろなアセットの粒が混ざるので、板ごとに持たせる
	nointerpolation uint texIndex : TEXCOORD3;		// 絵の SRV の番号
	nointerpolation float softDepth : TEXCOORD4;	// 奥の物との境目をぼかす幅(m。0でぼかさない)

	nointerpolation uint texMode : TEXCOORD5;		// PARTICLE_TEX_MODE_*
	nointerpolation uint blendMode : TEXCOORD8;		// PARTICLE_BLEND_*(加算なら α = 0 を出す)
	nointerpolation float frameBlend : TEXCOORD6;	// 次のコマをどれだけ混ぜるか(0〜1)

	// FireSmoke : rgb = 発光色、a = 冷えたしきい値(温度がこれより上のところだけ光る。1 以上で光らない)
	nointerpolation float4 emissive : TEXCOORD7;
};

//------------------------------------------------------------------------------
// FireSmoke の熱いところの光り方
//
// 温度(G)が冷えたしきい値を超えたぶんだけ光る。しきい値は寿命に沿って上がるので、
// 火は外側(温度の低いところ)から冷えて芯だけになり、消える。
// 芯ほど強く光らせて、同じ火の中にも明暗を付ける
//------------------------------------------------------------------------------
#define PARTICLE_HEAT_SOFTNESS 0.25f

float ParticleHeatGlow(float a_temp, float a_cool)
{
	const float _glow = saturate((a_temp - a_cool) / PARTICLE_HEAT_SOFTNESS);
	return _glow * _glow * (0.35f + 0.65f * a_temp);
}
