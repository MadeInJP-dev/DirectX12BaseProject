#include "../MeshShader/MeshCommon.hlsli"
#include "../../../Common/Math/CalcNormal.hlsli"
#include "../../../Common/Math/DitherFade.hlsli"


// GBuffer用出力
struct PSOutput
{
	float4 albedo : SV_Target0;
	float2 normal : SV_Target1;
	float4 material : SV_Target2;
	float4 emissiv : SV_Target3;
	float2 velocity : SV_Target4;
};
// ルートシグネチャ定義
[RootSignature(MESHGLOBAL_ROOT_SIG)]
PSOutput PSMain(VertexOutput a_input)
{
	PSOutput _out;
	float2 _uv = a_input.uv;

    // -----------------------------------------------------------
    // マテリアル情報のフェッチ
    // -----------------------------------------------------------
    // MSから渡されたIDを使ってインスタンスデータとマテリアルを取得
	uint _instanceIdx = a_input.instanceID;
	MeshInstanceData _inst = g_instanceData[_instanceIdx];
	MeshMaterial _mat = g_materialData[_inst.materialOffset];

    // -----------------------------------------------------------
    // フェード(アルファディザー)
    // -----------------------------------------------------------
    // fade : 1 = 見える / 0 = 見えない。カメラに近い障害物や自機を透かす。
    // ZPre(DepthDitherFadePS)と同じ関数で捨てるので、深度の EQUAL とずれない
	if (IsDitherFadeDiscard(_inst.fade, a_input.pos.xy))
	{
		discard;
	}

    // -----------------------------------------------------------
    // アルベド (BaseColor)
    // -----------------------------------------------------------
	float4 _baseTex = float4(1, 1, 1, 1);
	if (_mat.albedoIndex >= 0)
	{
		Texture2D albedoTex = ResourceDescriptorHeap[NonUniformResourceIndex(_mat.albedoIndex)];
		_baseTex = albedoTex.Sample(smp, _uv);
	}
	_out.albedo = _baseTex * _mat.baseColor;
	//_out.albedo = float4(1,0,0,1);

    // -----------------------------------------------------------
    // 法線 (Normal) & TBN行列
    // -----------------------------------------------------------
	float3 _nTex = float3(0, 0, 1); // デフォルト法線（テクスチャが無い場合）
	if (_mat.normalIndex >= 0)
	{
		Texture2D normalTex = ResourceDescriptorHeap[NonUniformResourceIndex(_mat.normalIndex)];
		_nTex = normalTex.Sample(smp, _uv).xyz * 2.0f - 1.0f;
	}

    // TとNから従法線(B)を計算してTBN行列を構築
	float3 _N = normalize(a_input.normal);
	float3 _T = normalize(a_input.tangent);
    
    // 直交化（グラム・シュミット）して精度を上げる
	_T = normalize(_T - dot(_T, _N) * _N);
	float3 _B = cross(_N, _T); // 従法線の計算

	float3x3 TBN = { _T, _B, _N };
	float3 _wNormal = mul(_nTex, TBN);
    
	_out.normal = EncodeNormalOct(normalize(_wNormal)); // 元のエンコード関数をそのまま使用

    // -----------------------------------------------------------
    // マテリアル (Metallic / Roughness / AO)
    // -----------------------------------------------------------
	float3 _mr = float3(1, 1, 1); // デフォルト値 (AO=1, Roughness=1, Metallic=1)
	if (_mat.metaRoughnessIndex >= 0)
	{
		Texture2D mrTex = ResourceDescriptorHeap[NonUniformResourceIndex(_mat.metaRoughnessIndex)];
		_mr = mrTex.Sample(smp, _uv).rgb;
	}
    
	_out.material = float4(
        _mr.r, // AO
        _mr.g * _mat.roughness, // 滑らかさ(Roughness)
        _mr.b * _mat.metallic, // 金属度(Metallic)
        1.0f // 予備
    );

    // -----------------------------------------------------------
    // エミッシブ (Emissive)
    // -----------------------------------------------------------
	float4 _eTex = float4(1, 1, 1, 1);
	if (_mat.emissiveIndex >= 0)
	{
		Texture2D emissiveTex = ResourceDescriptorHeap[NonUniformResourceIndex(_mat.emissiveIndex)];
		_eTex = emissiveTex.Sample(smp, _uv);
	}
	// エミッシブテクスチャ由来の発光 ＋ マテリアルに依らない自己発光。
	// 加算側は1.0を超えて構わない(出力先がR11G11B10_FLOATなのでクランプされない)。
	// ここが1.0を超えるとブルームの抽出しきい値に乗る
	_out.emissiv = _eTex * float4(_mat.emissive, 1.0f) + float4(_mat.emissiveAdd, 0.0f);

    // -----------------------------------------------------------
    // モーションベクター (Velocity)
    // -----------------------------------------------------------
    // クリップ空間からNDC座標へ変換
	float2 _curNDCPos = a_input.curClipPos.xy / a_input.curClipPos.w;
	float2 _prevNDCPos = a_input.prevClipPos.xy / a_input.prevClipPos.w;

    // NDC空間(幅2)の差分をUV空間(幅1)の差分へ変換（Y軸反転込み）
	float2 _velocity = (_curNDCPos - _prevNDCPos) * float2(0.5f, -0.5f);

	_out.velocity = _velocity;
    
	return _out;
}
