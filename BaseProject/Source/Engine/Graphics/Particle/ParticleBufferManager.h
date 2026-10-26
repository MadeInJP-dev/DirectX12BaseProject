#pragma once

#include <deque>

#include "Engine/Graphics/Particle/Core/EmitterData.h"
#include "Engine/Graphics/Particle/Core/ParticleData.h"

#include "Engine/Graphics/Particle/GPU/GPUParticlePool/GPUParticlePool.h"

#include "Engine/Resource/Data/Particles/ParticlesAsset.h"

namespace Engine::Graphics
{
	class GraphicsEngine;
}

namespace Engine::Graphics::Particle
{
	class EmitterSlotPool;
	class ParticleDrawTable;

	class ParticleBufferManager
	{
	public:

		ParticleBufferManager();
		~ParticleBufferManager();
		NON_COPYABLE_NON_MOVABLE(ParticleBufferManager);

		// このフレームの発生命令のうち、あるプールのぶんが共通の1本のどこからどこまでか。
		// 発生の Dispatch は offset から count 件だけを読む(count = 0 なら出す命令が無い)。
		// emitTotal はその命令で出す粒の合計(= 発生のスレッド数)。プールの容量(伸ばす先を含む)で頭打ちにしてある
		struct EmitRange
		{
			uint32_t offset = 0;
			uint32_t count = 0;
			uint32_t emitTotal = 0;
		};

		/// <summary>
		/// 初期化
		/// </summary>
		/// <param name="a_pGraphicsEngine">
		/// デバイスと非同期転送の依頼先(借り物)。プールは非同期に作られるので、そこまで持ち回る
		/// </param>
		void Init(
			Graphics::GraphicsEngine* a_pGraphicsEngine,
			Graphics::D3D12::DescriptorHeapManager* a_pHeapManager,
			Graphics::D3D12::GraphicsCommandList* a_pCmdList
		);

		/// <summary>
		/// 解放。GPUプール・エミットバッファが持つGPUリソースを破棄する。
		/// バッファはディスクリプタヒープにハンドルを持つため、
		/// DescriptorHeapManager の解放より前に呼ぶこと。
		/// </summary>
		void Release();

		/// <summary>
		/// フレームの開始に呼ぶ
		/// リクエストのクリアなど
		/// </summary>
		void BeginFrame(float a_dt);

		/// <summary>
		/// 粒子の時間を止めるか
		/// </summary>
		/// <remarks>
		/// 粒子はワールドとは別に毎フレーム進む(更新シェーダーの経過時間と、眠ったプールを見分ける時計)。
		/// ポーズやリザルトを重ねて下のシーンを止めても、粒子だけ動き続け、やがてプールごと眠って消える。
		/// 止めている間はどちらも進めない。SceneManager が毎フレーム決める
		/// </remarks>
		void SetTimeFrozen(bool a_isFrozen) { m_isTimeFrozen = a_isFrozen; }
		bool IsTimeFrozen() const { return m_isTimeFrozen; }

		/// <summary>
		/// フレームの描画が全部終わった後、コマンドリストを流す前に呼ぶ
		/// </summary>
		/// <remarks>
		/// 間接描画の引数を COMMON へ戻して、「このフレームの引数を用意した」印を下ろす。
		/// バッファは ExecuteCommandLists が終わると COMMON に戻る(decay)が、
		/// GPUResource は CPU 側で状態を覚えているだけなので、明示して戻しておかないと
		/// 次のフレームの遷移で「遷移前の状態」が食い違う。
		/// カメラごとに何度描いても引数は同じなので、戻すのはフレームに1回でよい
		/// </remarks>
		void FinishFrame(Graphics::D3D12::GraphicsCommandList* a_pCmdList);

		/// <summary>
		/// パーティクルを指定して、個数やデータを代入
		/// </summary>
		/// <param name="a_handle">パーティクルハンドル</param>
		/// <param name="a_emitterData">個数やデータ</param>
		void ReserveEmit(const Handle<Resource::ParticlesAsset>& a_handle,const EmitterData& a_emitterData);

		//----------------------------------------------------------------------------------
		// 発生源の席(ローカル空間で回すパーティクル用)
		//
		// GPUプールはアセット単位なので、同じ噴射アセットを左右のブースターが使うと
		// 粒が1つのプールに混ざる。「どの発生源にくっついているか」を粒ごとに
		// 持たせないと、描くときに戻す行列を選べない。
		// そこで発生源ごとに席(番号)を配り、粒にはその番号だけを持たせている。
		// 席は全アセット共通の1つの表(EmitterSlotPool)。持ち主はエフェクト側が握る
		//----------------------------------------------------------------------------------
		EmitterSlotPool* RefEmitterSlotPool() { return m_upEmitterSlotPool.get(); }
		const EmitterSlotPool* GetEmitterSlotPool() const { return m_upEmitterSlotPool.get(); }

		//----------------------------------------------------------------------------------
		// 描画設定の表と、半透明の粒をまとめて描く一覧(全アセット共通)
		//
		// シミュレーションが毎フレーム組み直して半透明の粒を遠い順に並べ、ParticlePass が読む
		//----------------------------------------------------------------------------------
		ParticleDrawTable* RefDrawTable() { return m_upDrawTable.get(); }
		const ParticleDrawTable* GetDrawTable() const { return m_upDrawTable.get(); }

		/// <summary>
		/// パーティクルのバッファを取得
		/// </summary>
		/// <returns></returns>
		const std::unordered_map<Handle<Resource::ParticlesAsset>, std::unique_ptr<GPUParticlePool>>& GetPoolMap() const;

		/// <summary>
		/// ため込んだエミットデータを構造体バッファにマップする
		/// エミットデータ送信後、パス実行前の間に入れる必要あり
		/// a_frameIndex : 今のCPUフレーム番号(アップロード区画の選択に使う)
		/// </summary>
		void UploadEmitData(Graphics::D3D12::GraphicsCommandList* a_pCmdList, UINT a_frameIndex);

		/// <summary>
		/// 現在たまっている生成命令をパーティクルを指定して取得
		/// </summary>
		/// <param name="a_assetHandle"></param>
		std::span <const EmitterData> GetRequests(const Handle<Resource::ParticlesAsset>& a_assetHandle) const;

		/// <summary>
		/// ランタイム時に非同期で読み込む関数
		/// </summary>
		void CreateParticleDataAsync(const Handle<Resource::ParticlesAsset>& a_handle);

		// 準備完了かどうか : BeginFrameで確定させる、メインスレッドでのみ触る
		bool IsReady(const Handle<Resource::ParticlesAsset>& a_handle) const { return m_readyHandles.contains(a_handle); }

		// 命令バッファがあふれたことがあるか(デバッグ表示用。警告を出したアセット)
		// (共通の命令バッファの上限 EMIT_BUFFER_MAX_CAPACITY を超えて、命令を捨てたことがあるか)
		bool HasOverflowed(const Handle<Resource::ParticlesAsset>& a_handle) const { return m_overflowWarned.contains(a_handle); }

		//----------------------------------------------------------------------------------
		// 容量を伸ばす
		//
		// 要る数は CPU 側で数える :「生きている粒の数 ≦ 直近の最大寿命の間に出した粒の合計」。
		// UploadEmitData が足りないと見たら伸ばす先を決めておき、
		// シミュレーションが発生の前に BeginGrowPool → 増えた範囲を埋める CS → EndGrow と進める
		//----------------------------------------------------------------------------------
		bool HasGrowTarget(const Handle<Resource::ParticlesAsset>& a_handle) const { return m_growTargets.contains(a_handle); }

		// 伸ばす先の容量で作り直し、古い中身を写す(GPUParticlePool::BeginGrow)。
		// 伸ばしたら true。そのあと呼ぶ側が増えた範囲を埋めて、GPUParticlePool::EndGrow を呼ぶ
		bool BeginGrowPool(const Handle<Resource::ParticlesAsset>& a_handle, Graphics::D3D12::GraphicsCommandList* a_pCmdList);

		// デバッグ表示用
		uint64_t GetEstimatedLive(const Handle<Resource::ParticlesAsset>& a_handle) const;		// 直近の最大寿命の間に出した数(生きている数の上限)
		uint32_t GetGrowCount(const Handle<Resource::ParticlesAsset>& a_handle) const;			// 伸ばした回数
		bool HasHitHardLimit(const Handle<Resource::ParticlesAsset>& a_handle) const { return m_limitWarned.contains(a_handle); }

		//----------------------------------------------------------------------------------
		// 起きているか(更新と描画を回す必要があるか)
		//
		// 最後に粒を出してから、そのアセットの最大寿命(+ 少しの余裕)が経ったプールには
		// 生きている粒が1つも残っていない。そういうプールは更新も描画も丸ごと飛ばす。
		// 更新と描画は粒の数ではなく容量ぶん走るので、出していないプールほど無駄が大きい。
		// 判定は CPU 側だけで済む(GPU から生存数を読み戻さない)。
		//
		// 一度も出していないプールは眠っている。アセットが引けないときは安全側(起きている)
		//----------------------------------------------------------------------------------
		bool IsAwake(const Handle<Resource::ParticlesAsset>& a_handle) const;

		// 最後に粒を出してからの秒数(デバッグ表示用)。一度も出していなければ負
		double GetSecondsSinceLastEmit(const Handle<Resource::ParticlesAsset>& a_handle) const;

		//----------------------------------------------------------------------------------
		// 発生命令のバッファ(全プール共通の1本)
		//
		// UploadEmitData が全プールの命令をつなげて送る。プールごとの範囲は GetEmitRange で引く。
		// まだ一度も送っていなければ GetEmitterBuffer は nullptr
		//----------------------------------------------------------------------------------
		const Graphics::D3D12::StaticStructuredBuffer<EmitterData>* GetEmitterBuffer() const;		// 共通の一本を返す
		EmitRange GetEmitRange(const Handle<Resource::ParticlesAsset>& a_handle) const;	// なければ count = 0

		// デバッグ表示用 : 命令バッファの容量と、このフレームに送った命令の数
		uint32_t GetEmitBufferCapacity() const { return m_emitBufferCapacity; }
		uint32_t GetFrameEmitCount() const { return static_cast<uint32_t>(m_frameEmitData.size()); }

	private:

		// 使われなくなったプールを捨てる(BeginFrame から呼ぶ)。
		// アセットが破棄されて取り残されたプールと、眠ったまま大きく伸びているプール(後者は小さく作り直す)。
		// アセットの Capacity(最初に用意する数)が今の容量より大きくなっていたら伸ばす
		void ReleaseUnusedPools();

		// 粒の寿命の最大値 + 余裕(秒)。この間に出した粒の合計が、生きている粒の数の上限になる。
		// アセットが引けなければ負
		double GetLifeWindow(const Handle<Resource::ParticlesAsset>& a_handle) const;

		// 直近の窓より古い発生の記録を捨てて、窓の中の合計を返す
		uint64_t CountRecentEmits(const Handle<Resource::ParticlesAsset>& a_handle);

		// プールを登録から外し、GPU が使い終わってからバッファを返す。
		// 外した後は誰も参照しないので、次に必要になれば ReserveEmit / Warmup が作り直す。
		// ロード中のプールには呼ばないこと(コピーキューがまだ書いているかもしれない)
		void DestroyPool(const Handle<Resource::ParticlesAsset>& a_handle, const char* a_reason);

	private:
		// ビューの置き場(借り物)。実体は GraphicsEngine が持っている。
		// プールは非同期に作られるので、Init で受け取ったものを持ち続ける
		Graphics::D3D12::DescriptorHeapManager* m_pHeapManager = nullptr;

		// デバイスと非同期転送の依頼先(借り物)。持ち主は MainEngine
		Graphics::GraphicsEngine* m_pGraphicsEngine = nullptr;

		// アセットと 1対1 で紐づくバッファ群のマップ
		std::unordered_map<Handle<Resource::ParticlesAsset>, std::unique_ptr<GPUParticlePool>> m_pools;

		// 種類ごとの、今フレームの発生リクエスト（毎フレームクリアされる）
		std::unordered_map<Handle<Resource::ParticlesAsset>, std::vector<EmitterData>> m_emitRequests;


		std::mutex m_mutex;
		std::unordered_set<Handle<Resource::ParticlesAsset>> m_loadingHandles;

		// メインスレッドだけが触る確定済みの集合
		std::unordered_set<Handle<Resource::ParticlesAsset>> m_readyHandles;	// 準備中パーティクルアセット達

		// 命令バッファのあふれを警告済みのアセット(毎フレーム出すとログが埋まるので1回だけ)
		std::unordered_set<Handle<Resource::ParticlesAsset>> m_overflowWarned;

		// エミット用の座席プール : すべてのパーティクルアセットのワールド座標と生存時間を管理
		std::unique_ptr<EmitterSlotPool> m_upEmitterSlotPool = nullptr;

		// 描画設定の表と半透明の一覧 : 全アセット共通で1つ
		std::unique_ptr<ParticleDrawTable> m_upDrawTable = nullptr;

		// 眠っているプールを見分けるための時刻(メインスレッドのみ)
		double m_elapsedTime = 0.0;

		// 粒子の時間を止めているか(SetTimeFrozen)
		bool m_isTimeFrozen = false;
		std::unordered_map<Handle<Resource::ParticlesAsset>, double> m_lastEmitTime;

		//----------------------------------------------------------------------------------
		// 容量(メインスレッドのみ)
		//----------------------------------------------------------------------------------
		// プールごとの発生の記録 : いつ何粒出したか(容量で頭打ちにした数。実際に出たのはこれ以下)
		struct EmitRecord
		{
			double time = 0.0;
			uint64_t count = 0;
		};
		std::unordered_map<Handle<Resource::ParticlesAsset>, std::deque<EmitRecord>> m_emitHistory;

		// 伸ばす先の容量(シミュレーションが伸ばしたら消える)
		std::unordered_map<Handle<Resource::ParticlesAsset>, UINT> m_growTargets;

		// 伸ばした回数(デバッグ表示用)
		std::unordered_map<Handle<Resource::ParticlesAsset>, uint32_t> m_growCounts;

		// 上限(PARTICLE_POOL_HARD_LIMIT)に届いたことを警告済みのアセット(1回だけ)
		std::unordered_set<Handle<Resource::ParticlesAsset>> m_limitWarned;

		// フレームで一本の発生命令バッファ。全プールの命令をつなげて送る
		std::vector<EmitterData> m_frameEmitData;											//CPU側の写し : 毎フレーム作り直す
		std::unique_ptr<Graphics::D3D12::StaticStructuredBuffer<EmitterData>> m_upEmitterBuffer;		// GPU側 : 足りなければ作り直す
		uint32_t m_emitBufferCapacity = 0;

		// プールごとの、このフレームの命令の範囲
		std::unordered_map<Handle<Resource::ParticlesAsset>, EmitRange> m_emitRanges;

	};
}