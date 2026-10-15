#pragma once
namespace Engine::Graphics::Particle
{
	/// <summary>
	/// CPUが毎フレーム計算して Uploadヒープ経由で
	/// いまフレーム、どこから何個出すかの命令
	/// </summary>
	/// <remarks>
	/// HLSL 側 EmitData(Common/RootParameters/Particle.hlsli)と並びを合わせること。
	/// StructuredBuffer なので float4 の境界に揃える必要はない(並びと大きさが一致していればよい)
	/// </remarks>
	struct EmitterData
	{
		//------------------------------------------------------------------
		// 発生行列 : 形状のローカル空間 → 粒を保存する空間
		//
		// 形状(Cone / Sphere / Hemisphere とばらつき半径)はシェーダーが
		// 常にローカル空間(+Z が噴き出す向き、+Y が上)で作り、最後にこれを掛ける。
		//   ワールド空間の粒 : 発生源のワールドの位置と回転
		//   ローカル空間の粒 : 席(発生源)の座標系から見た位置と回転
		// 拡縮は入れないこと(大きさは baseScale / positionRadius が持つ)。
		// 作るときは MakeEmitMatrix を通す
		//------------------------------------------------------------------
		Math::Matrix emitMatrix;
		UINT emitCount;					// 発生させる数
		float baseScale;				// エミッター専用のスケール

		// ---- ランダム要素 ----
		float positionRadius;		// 発生位置の半径
		float directionAngle;		// 方向のばらつき角度 (ラジアン。Cone のときだけ使う)

		// 拡縮区間
		float minScale;
		float maxScale;

		// スピード区間
		float minSpeed;
		float maxSpeed;

		// 生存時間区間
		float minLifeTime;
		float maxLifeTime;

		// 発生方向の決め方(EParticleEmitShape)。
		// Cone のときだけ directionAngle が効く
		UINT emitShape;

		// 出した粒に持たせる発生源の番号。
		// ローカル空間で回すときだけ 1 以上になる(0 は単位行列 = ワールド空間)
		UINT emitterIndex;

		// emitMatrix の回転部分(クォータニオン xyzw)。粒の orientation にそのまま入る。
		// シェーダーで行列から作り直さずに済むよう、CPU 側で作って渡す(SetEmitTransform)
		Math::Quaternion emitRotation;

		// 板を面の中で回す角度と速さの区間(ラジアン、ラジアン/秒)
		float minRotation;
		float maxRotation;
		float minAngularVelocity;
		float maxAngularVelocity;

		// このプールの命令の中で、この命令の粒が何番目から始まるか(前の命令までの emitCount の合計)。
		// 発生シェーダーは 1スレッド = 1粒 で回り、「このスレッドはどの命令の何個目か」を
		// これで二分探索して引く。入れるのは ParticleBufferManager::UploadEmitData(呼ぶ側は触らない)
		UINT emitStart;

		// 発生位置を散らす箱の半分の大きさ(発生行列のローカル : X = 横、Y = 上、Z = 噴き出す向き)。
		// 0 なら箱は使わず positionRadius だけ。両方あるときは箱の中の点からさらに半径ぶん散らす
		Math::Vector3 positionExtents;
	};
	static_assert(sizeof(EmitterData) == 160, "HLSL 側 EmitData と大きさを合わせること");

	/// <summary>
	/// 発生行列を作る : +Z が噴き出す向き、+Y が上、第4行が位置
	/// </summary>
	/// <param name="a_pos">発生位置</param>
	/// <param name="a_forward">噴き出す向き(正規化していなくてよい。0 なら +Z)</param>
	/// <param name="a_upHint">
	/// 上の手がかり。噴き出す向きを軸にした回転(ロール)がこれで決まる。
	/// ふつうは持ち主の +Y を渡す(機体が傾けば粒の向きも一緒に傾く)
	/// </param>
	/// <remarks>
	/// 上の手がかりが噴き出す向きとほぼ平行だと横の軸が決まらないので、
	/// そのときはワールドの +Y(真上・真下へ噴くなら +X)で代用する
	/// </remarks>
	inline Math::Matrix MakeEmitMatrix(
		const Math::Vector3& a_pos,
		const Math::Vector3& a_forward,
		const Math::Vector3& a_upHint)
	{
		Math::Vector3 _forward = a_forward;
		if (_forward.LengthSquared() <= 1e-8f)
		{
			_forward = Math::Vector3(0.0f, 0.0f, 1.0f);
		}
		_forward.Normalize();

		Math::Vector3 _up = a_upHint;
		const bool _isUsable =
			(_up.LengthSquared() > 1e-8f) &&
			(std::abs(_up.Normalized().Dot(_forward)) < 0.999f);
		if (!_isUsable)
		{
			_up = (std::abs(_forward.y) < 0.999f)
				? Math::Vector3(0.0f, 1.0f, 0.0f)
				: Math::Vector3(1.0f, 0.0f, 0.0f);
		}

		return Math::Matrix::CreateWorld(a_pos, _forward, _up);
	}

	/// <summary>
	/// 発生行列とその回転(クォータニオン)を一緒に入れる
	/// </summary>
	/// <remarks>
	/// 2つは必ず同じものから作ること(食い違うと、粒の位置と板の向きがずれる)。
	/// 引数は MakeEmitMatrix と同じ
	/// </remarks>
	inline void SetEmitTransform(
		EmitterData& a_out,
		const Math::Vector3& a_pos,
		const Math::Vector3& a_forward,
		const Math::Vector3& a_upHint)
	{
		a_out.emitMatrix = MakeEmitMatrix(a_pos, a_forward, a_upHint);
		a_out.emitRotation = Math::Quaternion::CreateFromRotationMatrix(a_out.emitMatrix);
	}
}
