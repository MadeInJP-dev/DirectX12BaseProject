// ルートパラメーターの構造体
#include "../../Common/RootParameters/UIData.hlsli"

#include "../../Common/RootSignatureLayout.hlsli"

//==========================================================================================
// ルートパラメーター
//
//   0 : SRV(t0) UIの一覧(CPU側でインスタンス順に並べてある)
//
// テクスチャはディスクリプタテーブルを持たず、UIData.texIndex から
// ResourceDescriptorHeap で直接引く。1ドローで違う絵を混ぜられるようにするため
//==========================================================================================
#define UI_RS \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED)," \
	"SRV(t0)," \
	RS_STATIC_SAMPLER

StructuredBuffer<UIData> g_uiDataBuffer : register(t0);

SamplerState g_samp : register(s0);

// 頂点入力
struct VSInput
{
	float4 pos : POSITION; // 頂点座標
	float2 uv : TEXCOORD0; // UV座標
	uint instID : SV_InstanceID; // インスタンス番号
};

// 頂点出力
struct VSOutput
{
	float4 pos : SV_Position; // 射影行列

	float2 uv : TEXCOORD0;
	float4 color : TEXCOORD1;

	// テクスチャのSRVインデックス(ディスクリプタヒープ番号)。
	// インスタンスごとに一定なので nointerpolation で渡す。
	nointerpolation uint texIndex : TEXCOORD2;

	// 切り抜き : このピクセルのNDC座標と、切り抜く矩形(NDC)。
	// 矩形はインスタンスごとに一定なので nointerpolation で渡す
	float2 ndc : TEXCOORD3;
	nointerpolation float4 clipRect : TEXCOORD4;

	// 掛けるポストプロセスの印(UI_POST_EFFECT_*)。インスタンスごとに一定
	nointerpolation uint postEffectFlags : TEXCOORD5;
};

// ピクセル出力
//
//   0 : 色(シーンの上へアルファ合成する)
//   1 : スキャンラインのマスク(r)。ScanLinePass がここだけに効果を掛ける
//
// ブレンドは全ターゲット共通(UIPass)。マスクも a で重ねるので、
// 印の無いUIが上に重なれば、その下のマスクはちゃんと消える
struct PSOutput
{
	float4 color : SV_Target0;
	float4 scanLineMask : SV_Target1;
};
