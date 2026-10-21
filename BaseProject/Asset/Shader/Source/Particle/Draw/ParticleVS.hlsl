#include "ParticleShader.hlsli"

[RootSignature(PARTICLE_ROOT_SIG)]
VSOutput VSMain(VSInput a_input)
{
	VSOutput _out;

	//----------------------------------------------------------------
	// 描く一覧から粒を引く
	//
	// SV_InstanceID は描く一覧の何番目か(インスタンス数は数えた数。範囲外は読まないよう表の長さで丸める)。
	//   ふつう       : 一覧は全アセットの粒(加算・半透明とも)を遠い順に並べたもの。
	//                  1件の上位ビットが表の番号、下位ビットが粒の番号
	//   並べられない : 一覧はそのプールの生存リスト(粒の番号)。表の番号はルート定数
	// 1回の描画でアセット(= 粒バッファ)が粒ごとに変わるので、番号は NonUniformResourceIndex で引く
	//----------------------------------------------------------------
	uint _listCount, _listStride;
	g_drawList.GetDimensions(_listCount, _listStride);
	const uint _entry = g_drawList[min(a_input.instID, _listCount - 1)];

	const bool _isFromList = (g_recordIndex == PARTICLE_DRAW_RECORD_FROM_LIST);
	uint _recordCount, _recordStride;
	g_drawRecords.GetDimensions(_recordCount, _recordStride);
	const uint _recordIndex = min(_isFromList ? (_entry >> PARTICLE_DRAW_RECORD_SHIFT) : g_recordIndex, _recordCount - 1);
	const uint _particleIndex = _isFromList ? (_entry & PARTICLE_DRAW_PARTICLE_MASK) : _entry;

	const ParticleDrawRecord _draw = g_drawRecords[_recordIndex];
	StructuredBuffer<ParticleData> _particles = ResourceDescriptorHeap[NonUniformResourceIndex(_draw.particleBufferIndex)];
	ParticleData _particleData = _particles[_particleIndex];

	// PS が使うもの(絵とぼかす幅と読み方)は板ごとに渡す
	_out.texIndex = _draw.texIndex;
	_out.softDepth = _draw.softDepth;
	_out.texMode = _draw.texMode;
	_out.blendMode = _draw.blendMode;
	_out.frameBlend = 0.0f;
	_out.emissive = float4(0.0f, 0.0f, 0.0f, 2.0f);

	// パーティクルの寿命判定
	if (_particleData.life <= 0.0f)
	{
		// 寿命が切れている場合は処理をスキップ

		_out.pos = float4(0, 0, 0, 0);
		_out.uv = float4(0, 0, 0, 0);
		_out.color = float4(1, 0, 0, 1);
		_out.viewDepth = 0.0f;
		return _out;
	}

	//----------------------------------------------------------------
	// 発生源の座標系からワールドへ戻す
	//
	// ローカル空間で回している粒は、発生源(ブースターなど)にくっついて動いてほしい。
	// シミュレーションはローカルのまま進めて、描くときにここで戻す。
	// 添字 0 は単位行列で予約してあるので、ワールド空間の粒は素通りする(分岐が要らない)。
	//----------------------------------------------------------------
	// 範囲外を読まないよう、表の長さで丸める(はみ出したら最後の席。ふつうは起きない)
	uint _slotCount, _slotStride;
	g_emitterSlots.GetDimensions(_slotCount, _slotStride);
	uint _emitterIndex = min(_particleData.emitterIndex, _slotCount - 1);
	float4x4 _emitterMat = g_emitterSlots[_emitterIndex].worldMat;

	float3 _simPos = mul(float4(_particleData.pos, 1.0f), _emitterMat).xyz;
	float3 _simVelocity = mul(float4(_particleData.velocity, 0.0f), _emitterMat).xyz;

	float _debugSize = (_particleData.size <= 0.0f) ? 1.0f : _particleData.size;
	float3 _debugPos = g_camera.invView[3].xyz + (g_camera.invView[2].xyz * 5.0f);
	
	// カメラの右、上ベクトルを取得
	float3 _camRight = g_camera.invView[0].xyz;
	float3 _camUp = g_camera.invView[1].xyz;

	// ---------------------------------------------------------------
	// 板ポリの軸を決める
	// 既定はカメラ正面のビルボード。進行方向に合わせる指定なら、
	// 板の縦軸(+Y = テクスチャの上)を速度ベクトルへ向ける。
	// 発生源に合わせる指定なら、発生したときの発生源の向きから軸を取る。
	// 板ポリの頂点は y=+1 が v=0 なので、+Y = 画像の上端になる。
	// ---------------------------------------------------------------
	float3 _axisX = _camRight;
	float3 _axisY = _camUp;
	float _stretch = 1.0f;
	bool _isRotate = false;		// 面の中で回してよい向きか(進行方向に合わせる向きでは回さない)

	if (_draw.orientation == PARTICLE_ORIENT_BILLBOARD)
	{
		_isRotate = true;
	}
	else if (_draw.orientation == PARTICLE_ORIENT_EMITTER_AXIS ||
		_draw.orientation == PARTICLE_ORIENT_EMITTER_FACING)
	{
		//------------------------------------------------------------
		// 発生源に合わせる
		//
		// orientation は発生したときの発生源の向き。ローカル空間の粒なら席から見た向きなので、
		// 席の回転(w = 0 で位置は無視)も掛けて今のワールドの向きにする。
		// ワールド空間の粒は席 0(単位行列)なので、発生したときの向きのまま残る。
		//
		//   EmitterAxis   : 画像の上 = 噴き出す向き(+Z)、横 = 発生源の +X(炎の舌・噴射の芯)
		//   EmitterFacing : 板の面が噴き出す向きを向く。画像の上 = 発生源の +Y(衝撃波の輪)
		//------------------------------------------------------------
		const float4 _q = _particleData.orientation;
		const float3 _emitX = normalize(mul(float4(ParticleQuatRotate(_q, float3(1.0f, 0.0f, 0.0f)), 0.0f), _emitterMat).xyz);
		const float3 _emitY = normalize(mul(float4(ParticleQuatRotate(_q, float3(0.0f, 1.0f, 0.0f)), 0.0f), _emitterMat).xyz);
		const float3 _emitZ = normalize(mul(float4(ParticleQuatRotate(_q, float3(0.0f, 0.0f, 1.0f)), 0.0f), _emitterMat).xyz);

		_axisX = _emitX;
		if (_draw.orientation == PARTICLE_ORIENT_EMITTER_AXIS)
		{
			_axisY = _emitZ;
			_stretch = _draw.stretch;
		}
		else
		{
			_axisY = _emitY;
		}
		_isRotate = true;
	}
	else
	{
		float3 _velocity = _simVelocity;
		float _speedSq = dot(_velocity, _velocity);

		// 止まっているものは向きが決まらないので、そのままビルボードで出す
		if (_speedSq > 1e-8f)
		{
			float3 _dir = _velocity * rsqrt(_speedSq);

			if (_draw.orientation == PARTICLE_ORIENT_VELOCITY_BILLBOARD)
			{
				// 進行方向を画面へ射影して、その向きへ板を回す。
				// 常にカメラ正面を向いたままなので、真横から見ても板が消えない
				float2 _screenDir = float2(dot(_dir, _camRight), dot(_dir, _camUp));
				if (dot(_screenDir, _screenDir) > 1e-8f)
				{
					_screenDir = normalize(_screenDir);
					_axisY = (_camRight * _screenDir.x) + (_camUp * _screenDir.y);
					_axisX = (_camRight * _screenDir.y) - (_camUp * _screenDir.x);
					_stretch = _draw.stretch;
				}
			}
			else
			{
				// ワールドの進行方向をそのまま縦軸にする。
				// カメラへ真っ直ぐ向かってくるものは短く(点に)見えるので、
				// 弾道や火花のように「線として飛んでいる」表現向き
				float3 _toCamera = g_camera.cameraPos.xyz - _simPos;
				float3 _side = cross(_toCamera, _dir);
				if (dot(_side, _side) > 1e-8f)
				{
					_axisY = _dir;
					_axisX = normalize(_side);
					_stretch = _draw.stretch;
				}
			}
		}
	}

	// ---------------------------------------------------------------
	// 板を面の中で回す
	// 初期角と回転の速さは粒ごと(アセットの区間の乱数)。既定は 0 で回らない
	// ---------------------------------------------------------------
	if (_isRotate)
	{
		float _sin, _cos;
		sincos(_particleData.rotation, _sin, _cos);

		const float3 _rotX = (_axisX * _cos) + (_axisY * _sin);
		const float3 _rotY = (_axisY * _cos) - (_axisX * _sin);
		_axisX = _rotX;
		_axisY = _rotY;
	}

	// ---------------------------------------------------------------
	// 寿命のどこまで進んだか (0 = 生まれた瞬間 / 1 = 消える直前)
	// サイズ・色・フェードはすべてこれを基準にする
	// ---------------------------------------------------------------
	float _startLife = max(_particleData.startLife, 1e-4f);
	float _lifeT = saturate(1.0f - (_particleData.life / _startLife));

	// 寿命に沿ったサイズ変化。
	// 煙は膨らみ(>1)、火花は縮む(<1)。1 なら変化しない。
	// 曲がり具合が 1 より小さいと頭で一気に膨らんで、あとはゆっくりになる(爆風・煙)
	float _sizeScale = lerp(1.0f, _draw.endSizeScale, pow(_lifeT, max(_draw.sizeCurvePow, 0.01f)));

	// ビルボード計算
	float3 _localPos = a_input.pos.xyz * (_particleData.size * _sizeScale);
	//float3 _localPos = a_input.pos.xyz * _debugSize;

	// 進行方向へ伸ばす(縦軸のみ)
	_localPos.y *= _stretch;

	// ワールド座標の算出。
	// 板ポリの軸はカメラ基準(ワールド)なので、中心だけを戻せばよい
	float3 _worldPos = _simPos + (_axisX * _localPos.x) + (_axisY * _localPos.y);
	//float3 _worldPos = _debugPos + (_camRight * _localPos.x) + (_camUp * _localPos.y);

	// ワールド座標から射影空間へ変換
	_out.pos = mul(float4(_worldPos, 1.0f), g_camera.viewProj);

	// 奥の物との境目をぼかすための奥行き(PS で深度と比べる)
	_out.viewDepth = mul(float4(_worldPos, 1.0f), g_camera.view).z;

	// ---------------------------------------------------------------
	// フリップブック : 絵を cols x rows のコマに割って、どのコマを出すか決める
	//
	// コマは左上から右へ、行が終わったら次の行。
	//   OverLife : 寿命に沿って最初のコマから最後のコマまで。混ぜる指定なら隣のコマと混ぜる
	//   Random   : 粒ごとにどれか1コマ(発生時の寿命のビットと粒の番号から引くので、粒の間は変わらない)
	// ---------------------------------------------------------------
	const uint _cols = max(_draw.flipbookCols, 1u);
	const uint _rows = max(_draw.flipbookRows, 1u);
	const uint _frameCount = _cols * _rows;
	uint _frameA = 0;
	uint _frameB = 0;
	if (_frameCount > 1)
	{
		if (_draw.flipbookMode == PARTICLE_FLIPBOOK_OVER_LIFE)
		{
			const float _frame = _lifeT * (float)(_frameCount - 1);
			_frameA = min((uint)_frame, _frameCount - 1);
			_frameB = min(_frameA + 1, _frameCount - 1);
			_out.frameBlend = (_draw.isFlipbookBlend != 0) ? frac(_frame) : 0.0f;
		}
		else if (_draw.flipbookMode == PARTICLE_FLIPBOOK_RANDOM)
		{
			uint _hash = asuint(_particleData.startLife) ^ (_particleIndex * 0x9E3779B9u);
			_hash ^= _hash >> 16; _hash *= 0x7FEB352Du; _hash ^= _hash >> 15; _hash *= 0x846CA68Bu; _hash ^= _hash >> 16;
			_frameA = _hash % _frameCount;
			_frameB = _frameA;
		}
	}
	const float2 _cellSize = float2(1.0f / (float)_cols, 1.0f / (float)_rows);
	const float2 _cellA = float2((float)(_frameA % _cols), (float)(_frameA / _cols));
	const float2 _cellB = float2((float)(_frameB % _cols), (float)(_frameB / _cols));
	_out.uv = float4((_cellA + a_input.uv) * _cellSize, (_cellB + a_input.uv) * _cellSize);

	// ---------------------------------------------------------------
	// 寿命に沿った色
	//
	// 爆発は「白く光る → オレンジ → くすんだ煙」のように色が変わる。
	// 途中の色があれば 開始 → 途中 → 終わり の3色で変える(火の色から煙の色へ、のような変化)。
	// RGB は 1 を超えてよく、超えたぶんがブルームのしきい値を抜けて光って見える
	// ---------------------------------------------------------------
	float4 _color = lerp(_draw.startColor, _draw.endColor, _lifeT);
	if (_draw.midColorTime > 0.0f && _draw.midColorTime < 1.0f)
	{
		_color = (_lifeT < _draw.midColorTime)
			? lerp(_draw.startColor, _draw.midColor, _lifeT / _draw.midColorTime)
			: lerp(_draw.midColor, _draw.endColor, (_lifeT - _draw.midColorTime) / (1.0f - _draw.midColorTime));
	}

	// ---------------------------------------------------------------
	// FireSmoke の火 : 冷えたしきい値は寿命の頭で 0、emissiveTime で 1 を超えて光らなくなる。
	// 爆発の火は出だしは熱いまま膨らみ、後から一気に冷えるので、しきい値は2乗に近い曲線で上げる
	// ---------------------------------------------------------------
	if (_draw.texMode == PARTICLE_TEX_MODE_FIRE_SMOKE && _draw.emissiveTime > 0.0f)
	{
		const float _coolT = saturate(_lifeT / _draw.emissiveTime);
		_out.emissive = float4(_draw.emissiveColor.rgb, pow(_coolT, 1.6f) * (1.0f + PARTICLE_HEAT_SOFTNESS) + step(1.0f, _coolT));
	}

	// 出だしと終わりのフェード。
	// 割合(0〜1)で指定するので、粒ごとに寿命が違っても同じ見え方になる
	float _alpha = _color.a;
	if (_draw.fadeInRatio > 0.0f)
	{
		_alpha *= saturate(_lifeT / _draw.fadeInRatio);
	}
	if (_draw.fadeOutRatio > 0.0f)
	{
		_alpha *= saturate((1.0f - _lifeT) / _draw.fadeOutRatio);
	}

	_out.color = float4(_color.rgb, _alpha);

	return _out;
}
