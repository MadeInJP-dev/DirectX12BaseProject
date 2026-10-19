#include "ParticleShader.hlsli"

float4 PSMain(VSOutput a_input) : SV_TARGET
{
	float4 _outColor = float4(1,1,1,1);

	// 絵は板ごとに違う(半透明はいろいろなアセットの粒を1回で描く)ので、番号は NonUniformResourceIndex で引く
	Texture2D _mainTex = ResourceDescriptorHeap[NonUniformResourceIndex(a_input.texIndex)];
	float4 _texColor = _mainTex.Sample(g_samp, a_input.uv).rgba;
	
	_outColor = a_input.color * _texColor;

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
	
	return _outColor;
}
