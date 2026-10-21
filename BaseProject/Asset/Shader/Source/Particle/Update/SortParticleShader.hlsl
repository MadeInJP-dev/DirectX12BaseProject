#include "../../../Common/RootParameters/Particle.hlsli"

//==========================================================================================
// 粒を加算・半透明を問わず全アセットぶん1本の一覧へ集め、カメラから遠い順に並べる
// (加算も並べるのは、煙の奥の炎が煙の上に乗って見えないようにするため。
//  描くのはプリマルチプライドの1つの PSO なので、重ね方が違っても同じ一覧で描ける)
//
// 粒は深度を書かないので、後から描いた粒が上に乗る。
// アセットごとに描くと、違うアセットの粒どうしの前後は描く順で決まってしまい、
// 奥の煙が手前の煙の上に乗る。さらに生存リストは Update が InterlockedAdd で積んだ順
// (GPU のスレッドの都合で毎フレーム変わる)なので、同じアセットの中でも前後がちらつく。
// そこで全アセットの粒を集めて遠い順に並べ、ParticlePass が1回の描画で描く。
//
// 集める先の1件は (キー, 描く一覧の1件)。描く一覧の1件は「描画設定の表の番号 + 粒の番号」を詰めたもの
// (PARTICLE_DRAW_RECORD_SHIFT)。距離が同じなら表の番号の小さい方(SortOrder の小さいアセット)が先に来る。
//
// 並べ方はバイトニックソート。要素数は2のべき乗でないといけないので、集める先は2のべき乗で持ち、
// 集めた数より後ろは「いちばん後ろへ行く値」で埋めて並べる(集めた数は GPU から読み戻さない)。
//
//   mode 0 : 集める(1プールぶん)。プールの生存リストからキーを作り、引数の[1]を数えながら積む
//   mode 1 : 集めたものを SORT_BLOCK_SIZE ごとに共有メモリの中で並べ切る
//   mode 2 : ブロックをまたぐ比較1段ぶん(j >= SORT_BLOCK_SIZE)。集める先を直接読み書きする
//   mode 3 : ブロックの中の残りの段(j < SORT_BLOCK_SIZE)を共有メモリでまとめて行う
// 並べ終わる Dispatch(writeOut = 1)だけが、並んだ結果を描く一覧へ書き出す。
//
// ルートパラメーター
//   0 : CBV(b0)            設定
//   1 : ルート定数(b100)  番号(下の PassDescriptorIndex0)
//==========================================================================================
#define SORTPARTICLE_ROOT_SIG \
	"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED)," \
	"CBV(b0)," \
	"RootConstants(num32BitConstants=7, b100)"

// ※ C++ 側(ParticleData.h)の PARTICLE_SORT_BLOCK_SIZE / PARTICLE_SORT_THREAD_GROUP_SIZE と合わせること
#define SORT_BLOCK_SIZE			1024
#define SORT_THREAD_GROUP_SIZE	512		// 1スレッドが2要素(比較する1組)を受け持つ

#define SORT_MODE_GATHER		0
#define SORT_MODE_LOCAL_SORT	1
#define SORT_MODE_GLOBAL_STEP	2
#define SORT_MODE_LOCAL_MERGE	3

// 集めた数より後ろを埋める値。キーも一覧の1件も最大なので、必ずいちばん後ろへ行く
#define SORT_SENTINEL uint2(0xFFFFFFFFu, 0xFFFFFFFFu)

// ※ C++ 側 SortCB(ParticleSimulation.cpp)と並びを合わせること
cbuffer CBParticleSort : register(b0)
{
	float3	g_cameraPos;	// 距離を測るカメラ(半透明のメッシュと同じ、描くカメラの位置)
	uint	g_sortSize;		// 集める先の要素数(2のべき乗、SORT_BLOCK_SIZE 以上)

	uint	g_mode;			// 上の SORT_MODE_*
	uint	g_k;			// バイトニックの段の大きさ(mode 2 / 3)
	uint	g_j;			// 比較する相手までの距離(mode 2)
	uint	g_writeOut;		// 1 なら結果を描く一覧へ書き出す(mode 1 / 3)

	uint	g_recordIndex;	// 集めるプールの描画設定の表の番号(mode 0)
	uint3	g_pad;
}

// ResourceDescriptorHeap の添字。ルート定数で届く。
// 粒・発生源の席・生存リスト・プールの引数は集める(mode 0)ときだけ読む
cbuffer PassDescriptorIndex0 : register(b100)
{
	uint g_particleBufferIndex;		// 粒(UAV。Update と同じく読むだけ)
	uint g_emitterSlotIndex;		// 発生源の席(SRV)
	uint g_aliveListIndex;			// プールの生存リスト(UAV)
	uint g_poolDrawArgsIndex;		// プールの間接描画の引数(UAV)。[1] がそのプールの生きている数
	uint g_sortBufferIndex;			// 集める先(UAV。x = キー, y = 描く一覧の1件)
	uint g_drawArgsIndex;			// 半透明をまとめて描く引数(UAV)。[1] が集めた数
	uint g_drawListIndex;			// 描く一覧(UAV)
}

RWStructuredBuffer<ParticleData> Get_particleBuffer() { RWStructuredBuffer<ParticleData> _r = ResourceDescriptorHeap[g_particleBufferIndex]; return _r; }
#define g_particleBuffer Get_particleBuffer()
StructuredBuffer<EmitterTransform> Get_emitterSlots() { StructuredBuffer<EmitterTransform> _r = ResourceDescriptorHeap[g_emitterSlotIndex]; return _r; }
#define g_emitterSlots Get_emitterSlots()
RWStructuredBuffer<uint> Get_aliveList() { RWStructuredBuffer<uint> _r = ResourceDescriptorHeap[g_aliveListIndex]; return _r; }
#define g_aliveList Get_aliveList()
RWStructuredBuffer<uint> Get_poolDrawArgs() { RWStructuredBuffer<uint> _r = ResourceDescriptorHeap[g_poolDrawArgsIndex]; return _r; }
#define g_poolDrawArgs Get_poolDrawArgs()
RWStructuredBuffer<uint2> Get_sortBuffer() { RWStructuredBuffer<uint2> _r = ResourceDescriptorHeap[g_sortBufferIndex]; return _r; }
#define g_sortBuffer Get_sortBuffer()
RWStructuredBuffer<uint> Get_drawArgs() { RWStructuredBuffer<uint> _r = ResourceDescriptorHeap[g_drawArgsIndex]; return _r; }
#define g_drawArgs Get_drawArgs()
RWStructuredBuffer<uint> Get_drawList() { RWStructuredBuffer<uint> _r = ResourceDescriptorHeap[g_drawListIndex]; return _r; }
#define g_drawList Get_drawList()

groupshared uint2 gs_entries[SORT_BLOCK_SIZE];

// 集めた数(集める先の大きさで丸める。足りないことはふつう起きない)
uint GetGatheredCount()
{
	return min(g_drawArgs[1], g_sortSize);
}

//------------------------------------------------------------------------------------------
// 集める : プールの生存リストの i 番目から(キー, 描く一覧の1件)を作って積む
//
// 距離の2乗を、昇順に並べると遠いものが先に来る整数へ直す。
// 0 以上の float はビット列を符号なし整数として見ても大小が変わらないので、
// +Inf(0x7F800000)から引けば「遠いほど小さい」になる(DrawLists::ResolveTransparentSortKeys と同じ考え)。
// 本物のキーは 0x7F800000 以下に収まるので、埋め草(0xFFFFFFFF)とは混ざらない
//------------------------------------------------------------------------------------------
void Gather(uint a_index)
{
	uint _listCount, _listStride;
	g_aliveList.GetDimensions(_listCount, _listStride);
	const uint _aliveCount = min(g_poolDrawArgs[1], _listCount);
	if (a_index >= _aliveCount) return;

	const uint _particleIndex = g_aliveList[a_index];
	const ParticleData _p = g_particleBuffer[_particleIndex];

	// 粒は発生源の座標系で持っていることがあるので、描くときと同じく席の行列でワールドへ戻す
	uint _slotCount, _slotStride;
	g_emitterSlots.GetDimensions(_slotCount, _slotStride);
	const uint _slotIndex = min(_p.emitterIndex, _slotCount - 1);
	const float3 _worldPos = mul(float4(_p.pos, 1.0f), g_emitterSlots[_slotIndex].worldMat).xyz;

	const float3 _toParticle = _worldPos - g_cameraPos;
	const uint _distBits = min(asuint(max(dot(_toParticle, _toParticle), 0.0f)), 0x7F800000u);	// NaN も丸める
	const uint _key = 0x7F800000u - _distBits;
	const uint _drawEntry = (g_recordIndex << PARTICLE_DRAW_RECORD_SHIFT) | (_particleIndex & PARTICLE_DRAW_PARTICLE_MASK);

	// 自分の枠を取る(戻り値は進める前の値)。集める先は容量の合計以上あるので、ふつうはあふれない
	uint _slot;
	InterlockedAdd(g_drawArgs[1], 1, _slot);
	if (_slot < g_sortSize)
	{
		g_sortBuffer[_slot] = uint2(_key, _drawEntry);
	}
}

// a が b より後ろへ行くべきか(キー、同じなら一覧の1件 = 表の番号 → 粒の番号で比べる)
bool IsAfter(uint2 a_a, uint2 a_b)
{
	return (a_a.x != a_b.x) ? (a_a.x > a_b.x) : (a_a.y > a_b.y);
}

//------------------------------------------------------------------------------------------
// 共有メモリの中で、比較1段ぶん(相手までの距離 a_j、段の大きさ a_k)
//
// 1スレッドが1組を受け持つ。組の左は「j の倍の区切りの中で、左半分の何番目か」。
// 並べる向きは、全体での位置が k の区切りの偶数番目なら昇順、奇数番目なら降順
//------------------------------------------------------------------------------------------
void CompareSwapShared(uint a_thread, uint a_blockBase, uint a_k, uint a_j)
{
	const uint _l = (2 * a_j) * (a_thread / a_j) + (a_thread % a_j);
	const uint _r = _l + a_j;
	const bool _isAscending = (((a_blockBase + _l) & a_k) == 0);

	const uint2 _a = gs_entries[_l];
	const uint2 _b = gs_entries[_r];
	if (IsAfter(_a, _b) == _isAscending)
	{
		gs_entries[_l] = _b;
		gs_entries[_r] = _a;
	}
}

// 並べた結果を書き出す。並べ終わる Dispatch だけは描く一覧へ(集めた数ぶん。埋め草は後ろに集まっている)
void StoreBlock(uint a_thread, uint a_blockBase)
{
	const uint _count = GetGatheredCount();

	[unroll]
	for (uint _h = 0; _h < 2; ++_h)
	{
		const uint _local = a_thread + (_h * SORT_THREAD_GROUP_SIZE);
		const uint _index = a_blockBase + _local;
		if (g_writeOut != 0)
		{
			if (_index < _count) g_drawList[_index] = gs_entries[_local].y;
		}
		else
		{
			g_sortBuffer[_index] = gs_entries[_local];
		}
	}
}

[RootSignature(SORTPARTICLE_ROOT_SIG)]
[numthreads(SORT_THREAD_GROUP_SIZE, 1, 1)]
void CSMain(uint3 a_groupID : SV_GroupID, uint a_groupIndex : SV_GroupIndex, uint3 a_dispatchID : SV_DispatchThreadID)
{
	//----------------------------------------------------------------
	// mode 0 : 集める(1スレッド = 生存リストの1件)
	//----------------------------------------------------------------
	if (g_mode == SORT_MODE_GATHER)
	{
		Gather(a_dispatchID.x);
		return;
	}

	//----------------------------------------------------------------
	// mode 2 : ブロックをまたぐ比較1段ぶん。共有メモリは使わない
	//----------------------------------------------------------------
	if (g_mode == SORT_MODE_GLOBAL_STEP)
	{
		const uint _t = a_dispatchID.x;
		if (_t >= (g_sortSize / 2)) return;

		const uint _l = (2 * g_j) * (_t / g_j) + (_t % g_j);
		const uint _r = _l + g_j;
		const bool _isAscending = ((_l & g_k) == 0);

		const uint2 _a = g_sortBuffer[_l];
		const uint2 _b = g_sortBuffer[_r];
		if (IsAfter(_a, _b) == _isAscending)
		{
			g_sortBuffer[_l] = _b;
			g_sortBuffer[_r] = _a;
		}
		return;
	}

	//----------------------------------------------------------------
	// mode 1 / 3 : 1グループ = 1ブロック(SORT_BLOCK_SIZE 要素)を共有メモリへ読む
	//----------------------------------------------------------------
	const uint _blockBase = a_groupID.x * SORT_BLOCK_SIZE;

	if (g_mode == SORT_MODE_LOCAL_SORT)
	{
		// 集めた数より後ろは前のフレームの中身が残っているので、埋め草に置き換えて読む
		const uint _count = GetGatheredCount();

		[unroll]
		for (uint _h = 0; _h < 2; ++_h)
		{
			const uint _local = a_groupIndex + (_h * SORT_THREAD_GROUP_SIZE);
			const uint _index = _blockBase + _local;
			gs_entries[_local] = (_index < _count) ? g_sortBuffer[_index] : SORT_SENTINEL;
		}
		GroupMemoryBarrierWithGroupSync();

		// ブロックの中を並べ切る(段の大きさ 2 〜 SORT_BLOCK_SIZE)
		for (uint _k = 2; _k <= SORT_BLOCK_SIZE; _k <<= 1)
		{
			for (uint _j = _k >> 1; _j > 0; _j >>= 1)
			{
				CompareSwapShared(a_groupIndex, _blockBase, _k, _j);
				GroupMemoryBarrierWithGroupSync();
			}
		}
	}
	else
	{
		[unroll]
		for (uint _h = 0; _h < 2; ++_h)
		{
			const uint _local = a_groupIndex + (_h * SORT_THREAD_GROUP_SIZE);
			gs_entries[_local] = g_sortBuffer[_blockBase + _local];
		}
		GroupMemoryBarrierWithGroupSync();

		// 段 g_k の残り(相手までの距離がブロックより短いもの)
		for (uint _j = SORT_BLOCK_SIZE >> 1; _j > 0; _j >>= 1)
		{
			CompareSwapShared(a_groupIndex, _blockBase, g_k, _j);
			GroupMemoryBarrierWithGroupSync();
		}
	}

	StoreBlock(a_groupIndex, _blockBase);
}
