
#include "../../../Common/RootParameters/Particle.hlsli"
#include "../../../Common/Math/Hash.hlsli"

//==========================================================================================
// ルートパラメーター
//
//   0 : CBV(b0)            発生ディスパッチの設定
//   1 : SRVの番号(t0)    発生命令の一覧(全プール共通の1本。このプールのぶんは requestOffset から)
//   2 : UAVの番号(u0-u2) 粒 + デッドリスト + カウンター
//==========================================================================================
#define EMITPARTICLE_ROOT_SIG \
	"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED),"\
	"CBV(b0),"\
	"RootConstants(num32BitConstants=1, b100),"\
	"RootConstants(num32BitConstants=3, b101)"

cbuffer CBParticleEmit : register(b0)
{
	ParticleEmitSetting g_emit;
}

// 入力（UPLOADヒープから）
// SRVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex0 : register(b100)
{
	uint g_emitDataIndex;
}

StructuredBuffer<EmitData> Get_emitData() { StructuredBuffer<EmitData> _r = ResourceDescriptorHeap[g_emitDataIndex]; return _r; }
#define g_emitData Get_emitData()

// 入出力（DEFAULTヒープ UAV）
// UAVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex1 : register(b101)
{
	uint g_particleBufferIndex;
	uint g_deadListIndex;
	uint g_counterBufferIndex;
}

RWStructuredBuffer<ParticleData> Get_particleBuffer() { RWStructuredBuffer<ParticleData> _r = ResourceDescriptorHeap[g_particleBufferIndex]; return _r; }
#define g_particleBuffer Get_particleBuffer()
RWStructuredBuffer<uint> Get_deadList() { RWStructuredBuffer<uint> _r = ResourceDescriptorHeap[g_deadListIndex]; return _r; }
#define g_deadList Get_deadList()
RWStructuredBuffer<uint> Get_counterBuffer() { RWStructuredBuffer<uint> _r = ResourceDescriptorHeap[g_counterBufferIndex]; return _r; }
#define g_counterBuffer Get_counterBuffer()

// ルートシグネチャセット
[RootSignature(EMITPARTICLE_ROOT_SIG)]

//==========================================================================================
// 1スレッド = 1粒
//
// 以前は 1スレッド = 1命令で、命令の emitCount 個を順番に作っていた。
// 1命令で数百粒出す爆発だと1スレッドだけが長く回り、他のスレッドは待つだけになる。
// 今は出す粒の合計ぶんスレッドを立て、各スレッドが「どの命令の何個目か」を引いて1粒だけ作る。
//
//   どの命令か : 命令ごとの emitStart(このプールの中で何粒目から始まるか)を二分探索
//   何個目か   : スレッド番号 - その命令の emitStart
//
// ※ C++ 側の Dispatch(ParticleSimulation.cpp)のスレッド数 64 と合わせること
//==========================================================================================
[numthreads(64, 1, 1)]
void CSMain( uint3 DTid : SV_DispatchThreadID )
{
	// このプールで今回出す粒の通し番号。合計(容量で頭打ち)以上のスレッドは落とす
	const uint _particleNo = DTid.x;
	if (_particleNo >= g_emit.emitTotal)
		return;

	//------------------------------------------------------------------
	// どの命令の粒か : emitStart <= _particleNo を満たす最後の命令(二分探索)
	//
	// emitCount = 0 の命令は次の命令と同じ emitStart を持つが、
	// 「満たす最後の命令」を選べば正しく飛ばせる
	//------------------------------------------------------------------
	uint _lo = 0;
	uint _hi = g_emit.requestCount;		// [lo, hi)
	while (_hi - _lo > 1)
	{
		const uint _mid = (_lo + _hi) / 2;
		if (g_emitData[g_emit.requestOffset + _mid].emitStart <= _particleNo)
		{
			_lo = _mid;
		}
		else
		{
			_hi = _mid;
		}
	}

	// 命令のバッファは全プール共通の1本。このプールの命令は requestOffset から並んでいる。
	// _emitterIndex(プールの中での命令の番号)は乱数の種にも使うので、ずらさずにそのまま残す
	const uint _emitterIndex = _lo;
	const EmitData _emitInfo = g_emitData[g_emit.requestOffset + _emitterIndex];

	// その命令の何個目か(以前のループの _i と同じ)
	const uint _i = _particleNo - _emitInfo.emitStart;

	{
		uint _origCount;

		// カウンターから１引いて、引く前の数を origCount に取得する（アトミック演算）
		InterlockedAdd(g_counterBuffer[0], (uint) - 1, _origCount);

		// 空きがあった場合 : 引く前の数が１以上なら
		// ★カウンターは uint。空きが0の状態で複数スレッドが同時に引くと、
		//   2番目以降は 0xFFFFFFFF(=符号付きなら-1) へラップする。
		//   unsigned のまま > 0 判定すると、この巨大値を「空きあり」と誤認して
		//   デッドリストを範囲外参照し、しかもカウンターを復元しないため
		//   カウンターが永久破壊され、以降エミットが止まる。
		//   符号付きで判定すれば、引きすぎたスレッドは必ず else 側で復元され、
		//   カウンターは最終的に 0 へ収束する（自己修復する）。
		if ((int) _origCount > 0)
		{
			// デッドリストの末尾から、空いているパーティクルのインデックス番号を取得
			uint _newIndex = g_deadList[_origCount - 1];

			// 新しいパーティクルデータを初期化してプールに書き込む。
			// ★構造体の全メンバーを埋めること。1つでも未初期化のまま UAV へ書くと
			//   DXC の検証が "Assignment of undefined values to UAV" で落ちる
			ParticleData _p;

			// どの発生源の座標系で回るか。ワールド空間なら 0(単位行列)
			_p.emitterIndex = _emitInfo.emitterIndex;

			// シード値取得
			// ・frameSeed を混ぜないと毎フレーム完全に同じパーティクルが生成され、
			//   すべて同じ位置に重なって「1個しか出ていない」ように見える。
			// ・単純な足し算だと (命令0,_i=1) と (命令1,_i=0) が同じ種になるので
			//   ハッシュを噛ませてから組み合わせる。
			uint _seed = PCGHash(PCGHash(g_emit.frameSeed + _emitterIndex * 9781u) + _i);

			//------------------------------------------------------------------
			// 形状はローカル空間(+Z が噴き出す向き、+Y が上)で作り、最後に発生行列を掛ける。
			// 発生行列は「ローカル → 粒を保存する空間」(ワールド空間ならワールド、
			// ローカル空間なら席の座標系)。拡縮は入っていない
			//------------------------------------------------------------------
			const float4x4 _emitMat = _emitInfo.emitMatrix;

			// 発射位置計算
			// ※ 要素ごとに種を進めること。同じ種を使い回すと
			//    寿命・速度・スケールがすべて同じ乱数値になる。
			float _radius = Random(_seed++) * _emitInfo.positionRadius;
			float3 _localOffset = RandomDirection(_seed) * _radius;
			_seed += 2;		// RandomDirection は内部で種を2つ消費する

			// 箱の中へ散らす(地面を這う霧のように広くて薄い範囲)。
			// 箱を使わない(0)ときも種は同じだけ進めて、後ろの乱数の並びを変えない
			const float3 _boxRandom = float3(Random(_seed), Random(_seed + 1), Random(_seed + 2));
			_seed += 3;
			_localOffset += (_boxRandom * 2.0f - 1.0f) * _emitInfo.positionExtents;
			_p.pos = mul(float4(_localOffset, 1.0f), _emitMat).xyz;

			// 生存時間
			_p.life = ValueFloat(_emitInfo.minLifeTime, _emitInfo.maxLifeTime, _seed++);

			// ★寿命が 0 以下だと Update 側の「死亡済み」ガードに即座に弾かれ、
			//   デッドリストへ返却されずスロットが永久リークする。
			//   （ライフタイム未設定 min=max=0 などで起きる）
			//   必ず正の最低寿命を保証し、たとえ即死でも次フレームに返却されるようにする。
			_p.life = max(_p.life, 0.0001f);

			// ★寿命に沿った変化(サイズ・色・フェード)の分母。
			//   寿命は粒ごとにランダムなので、発生時の値を覚えておかないと
			//   「今どこまで進んだか」が出せない。
			_p.startLife = _p.life;

			// 発射方向計算
			//
			//   Cone       : 指定方向を軸にした円錐。噴射・排気のように向きがあるもの
			//   Sphere     : 中心から全方向。爆発のように四方八方へ飛び散るもの
			//   Hemisphere : 指定方向側の半球だけ。地面での爆発など、下へ潜らせたくないもの
			//
			// ※ Cone の角度を 360 度にしても全方向にはならない。
			//    円錐の「半頂角」なので全方向にしたければ 180 度で、
			//    しかもその極限は分布が偏る。全方向は専用の分岐で出すこと。
			const float3 _forward = float3(0.0f, 0.0f, 1.0f);	// ローカルでは噴き出す向きは常に +Z
			float3 _localDir;

			if (_emitInfo.emitShape == PARTICLE_EMIT_SHAPE_SPHERE)
			{
				_localDir = RandomDirection(_seed);
				_seed += 2;		// RandomDirection は内部で種を2つ消費する
			}
			else if (_emitInfo.emitShape == PARTICLE_EMIT_SHAPE_HEMISPHERE)
			{
				// 全方向のうち、基準方向の裏側へ出たものだけ折り返す。
				// コーンの角度を90度にするより分布が素直になる
				float3 _randomDir = RandomDirection(_seed);
				_seed += 2;
				_localDir = (dot(_randomDir, _forward) < 0.0f) ? -_randomDir : _randomDir;
			}
			else
			{
				_localDir = RandomConeDirection(_forward, _emitInfo.directionAngle, _seed);
				_seed += 2;		// RandomConeDirection も内部で種を2つ消費する
			}

			// 向きだけ回す(w = 0)。行列に拡縮は無いが、念のため長さを 1 に戻してから速さを掛ける
			const float3 _emitDir = normalize(mul(float4(_localDir, 0.0f), _emitMat).xyz);
			_p.velocity = _emitDir * ValueFloat(_emitInfo.minSpeed, _emitInfo.maxSpeed, _seed++);

			// スケール
			_p.size = _emitInfo.baseScale * ValueFloat(_emitInfo.minScale, _emitInfo.maxScale, _seed++);

			// 板の向き : 発生源の向きを覚えておき、初期角と回転の速さは区間の乱数
			_p.orientation = _emitInfo.emitRotation;
			_p.rotation = ValueFloat(_emitInfo.minRotation, _emitInfo.maxRotation, _seed++);
			_p.angularVelocity = ValueFloat(_emitInfo.minAngularVelocity, _emitInfo.maxAngularVelocity, _seed++);

			g_particleBuffer[_newIndex] = _p;
		}
		// 空きがなかった場合 : 最大容量に達している場合
		else
		{
			// カウンターのマイナスを戻す(この粒は出さない)。
			// 引きすぎたスレッドがそれぞれ戻すので、カウンターは最終的に 0 へ収束する
			InterlockedAdd(g_counterBuffer[0], 1, _origCount);
		}
	}
}
