#include "ParticleShader.hlsli"

float4 PSMain(VSOutput a_input) : SV_TARGET
{
	float4 _outColor = float4(1,1,1,1);

	// 絵は板ごとに違う(いろいろなアセットの粒を1回で描く)ので、番号は NonUniformResourceIndex で引く
	Texture2D _mainTex = ResourceDescriptorHeap[NonUniformResourceIndex(a_input.texIndex)];
	float4 _texColor = _mainTex.Sample(g_samp, a_input.uv.xy).rgba;

	// フリップブックで隣のコマと混ぜる(コマ送りがカクつかないように)
	if (a_input.frameBlend > 0.0f)
	{
		_texColor = lerp(_texColor, _mainTex.Sample(g_samp, a_input.uv.zw).rgba, a_input.frameBlend);
	}

	if (a_input.texMode == PARTICLE_TEX_MODE_FIRE_SMOKE)
	{
		//------------------------------------------------------------------
		// 火と煙 : R = 煙の明暗、G = 温度、A = 濃さ
		//
		// 煙の色(寿命の色)に明暗を掛け、熱いところだけ発光色を足す。
		// 濃さは寿命の色のアルファに掛ける(フェードもここに入っている)
		//------------------------------------------------------------------
		const float3 _smoke = a_input.color.rgb * _texColor.r;
		const float3 _fire = a_input.emissive.rgb * ParticleHeatGlow(_texColor.g, a_input.emissive.a);
		_outColor = float4(_smoke + _fire, a_input.color.a * _texColor.a);
	}
	else
	{
		_outColor = a_input.color * _texColor;
	}

	//------------------------------------------------------------------
	// 奥の物との境目をぼかす(ソフトパーティクル)
	//
	// 板が地面へ刺さると、交わったところで絵がまっすぐ切れて見える。
	// 板と奥の物の距離が softDepth より近いところほど薄くして、その線を消す。
	// 何も描かれていない所(深度が far のまま)は比べる相手が無いのでそのまま
	//------------------------------------------------------------------
	if (a_input.softDepth > 0.0f && g_depthTexIndex != PARTICLE_INVALID_DESCRIPTOR_INDEX)
	{
		const float _sceneDepth = g_depthTex.Load(int3(int2(a_input.pos.xy), 0)).r;
		if (_sceneDepth < 1.0f)
		{
			// 深度からビュー空間の奥行きへ戻す(奥行きは xy に依らないので画面の中心で求めてよい)
			const float4 _sceneView = mul(float4(0.0f, 0.0f, _sceneDepth, 1.0f), g_camera.invProj);
			const float _sceneZ = _sceneView.z / _sceneView.w;
			_outColor.a *= saturate((_sceneZ - a_input.viewDepth) / a_input.softDepth);
		}
	}

	// アルファ値がほぼ0ならピクセルを破棄して描画負荷を下げる
	if (_outColor.a < 0.001f)
	{
		discard;
	}

	// 定数バッファで発光強度を持たせておく
	//float intensity = 5.0f;
	//_outColor.rgb *= intensity;

	//------------------------------------------------------------------
	// プリマルチプライドで出す(合成は 色 × 1 + 下地 × (1 - α))
	//
	//   半透明 (色 × α, α) : 下地を α のぶん隠して色を置く
	//   加算   (色 × α, 0) : 下地を隠さず色を足す
	// どちらも以前の2つの PSO(SrcAlpha/InvSrcAlpha と SrcAlpha/One)と同じ色になる
	//------------------------------------------------------------------
	const float3 _premultiplied = _outColor.rgb * _outColor.a;
	const float _coverAlpha = (a_input.blendMode == PARTICLE_BLEND_ADDITIVE) ? 0.0f : _outColor.a;
	return float4(_premultiplied, _coverAlpha);
}
