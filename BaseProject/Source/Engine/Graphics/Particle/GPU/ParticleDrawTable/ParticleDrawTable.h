#pragma once

#include "Engine/Graphics/D3D12/GPUBuffer/RWStructuredBuffer/RWStructuredBuffer.h"
#include "Engine/Graphics/D3D12/GPUBuffer/StructuredBuffer/StaticStructuredBuffer.h"
#include "Engine/Graphics/Particle/Core/ParticleData.h"
#include "Engine/Resource/Data/Particles/ParticlesAsset.h"

namespace Engine::Graphics::Particle
{
	//======================================================================================
	// 描画設定の表と、半透明の粒をまとめて描く一覧
	//
	// 半透明は深度を書かないので、奥から順に描かないと正しく重ならない。
	// アセットごとに描くと、違うアセットの粒どうしの前後は描く順(SortOrder)で決まってしまう。
	// そこで半透明の粒は全アセットぶんを1本の一覧へ集め、カメラから遠い順に並べて1回で描く。
	//
	// ・描画設定の表 : アセット1つぶんの設定(粒バッファ・絵・色など)を1件にして並べたもの。
	//   毎フレーム、シミュレーションが更新するプールのぶんを組み直して送る(BeginBuild → AddRecord → Commit)。
	//   粒は「表の番号 + 粒の番号」を1つの uint に詰めて一覧へ積み、描くときに表を引く。
	//   加算の粒は今まで通りアセットごとに描き、表の番号はルート定数で渡す。
	// ・半透明の一覧 : 集める先(キー付き、2のべき乗)と、並んだ結果(描く一覧)と、間接描画の引数。
	//   シミュレーションが集めて並べ、ParticlePass が読む。
	//
	// 状態の約束 : フレームの頭は COMMON。シミュレーションで UAV にして集めて並べ、
	// 描く一覧は NON_PIXEL_SHADER_RESOURCE、引数は INDIRECT_ARGUMENT へ。
	// フレームの終わり(FinishFrame)で COMMON へ戻す。並べ替え用は並べ終わったら COMMON へ戻す
	//
	// ※ メインスレッドからだけ呼ぶこと
	//======================================================================================
	class ParticleDrawTable
	{
	public:

		ParticleDrawTable() = default;
		~ParticleDrawTable() = default;
		NON_COPYABLE_NON_MOVABLE(ParticleDrawTable);

		// 並べ替え用の1件 : キー(遠いほど小さい)と描く一覧の1件
		// ※ シェーダーの uint2(SortParticleShader.hlsl)と並びを合わせること
		struct SortEntry
		{
			uint32_t key = 0;
			uint32_t drawEntry = 0;
		};

		//------------------------------------------------------------------
		// 描画設定の表を組む(シミュレーションが毎フレーム)
		//------------------------------------------------------------------
		void BeginBuild();

		// 1アセットぶんを積む。番号は Commit で決まる。
		// a_capacity : そのプールの容量(半透明なら集める先の大きさに足す)
		// a_sortOrder / a_name : 半透明で距離が同じ粒の描く順(SortOrder の小さい順、同じなら名前順)
		void AddRecord(
			const Handle<Resource::ParticlesAsset>& a_handle,
			const ParticleDrawRecord& a_record,
			bool a_isAlphaBlend,
			UINT a_capacity,
			int a_sortOrder,
			std::string_view a_name);

		// 表を並べて番号を決め、GPU へ送る。半透明の粒を集める先も要る大きさにそろえる
		void Commit(
			Graphics::D3D12::Device* a_pDevice,
			Graphics::D3D12::DescriptorHeapManager* a_pHeapManager,
			Graphics::D3D12::GraphicsCommandList* a_pCmdList,
			UINT a_frameIndex);

		// このフレームの表を送ったか。送っていなければ何も描けない
		bool IsRecordReady() const { return m_isRecordReady; }
		UINT GetRecordSRVIndex() const;

		// そのアセットの表の番号。このフレームに積んでいなければ空
		std::optional<uint32_t> FindRecordIndex(const Handle<Resource::ParticlesAsset>& a_handle) const;
		bool IsAlphaBlendRecord(uint32_t a_recordIndex) const;

		//------------------------------------------------------------------
		// 半透明の粒を集めて並べる先
		//------------------------------------------------------------------
		// このフレームに半透明の表があり、集める先が用意できているか
		bool HasAlphaBlend() const { return m_alphaCapacity > 0 && m_sortBuffer.GetResource() && m_drawList.GetResource() && m_drawArgs.GetResource(); }

		Graphics::D3D12::RWStructuredBuffer<SortEntry>& RefSortBuffer() { return m_sortBuffer; }
		UINT GetSortSize() const { return m_sortSize; }		// 集める先の要素数(2のべき乗)

		Graphics::D3D12::RWStructuredBuffer<uint32_t>& RefDrawList() { return m_drawList; }
		const Handle<Graphics::D3D12::SRV>& GetDrawListSRV() const { return m_drawList.GetSRV(); }

		// 間接描画の引数(D3D12_DRAW_INDEXED_ARGUMENTS)。インスタンス数が集めた粒の数
		Graphics::D3D12::RWStructuredBuffer<uint32_t>& RefDrawArgs() { return m_drawArgs; }
		ID3D12Resource* GetDrawArgsResource() const { return m_drawArgs.GetResource(); }

		// このフレームの半透明の一覧を並べ終えたか。描画はこれが立っているときだけ
		bool IsArgsReady() const { return m_isArgsReady; }
		void SetArgsReady(bool a_isReady) { m_isArgsReady = a_isReady; }

		//------------------------------------------------------------------
		// フレームの終わり・解放
		//------------------------------------------------------------------
		// 描く一覧と引数を COMMON へ戻して、印を下ろす(カメラごとの描画がすべて読み終わった後)
		void FinishFrame(Graphics::D3D12::GraphicsCommandList* a_pCmdList);

		// GPU のバッファを返す(DescriptorHeapManager より先に呼ぶ)
		void Release();

	private:

		// 集める先と描く一覧を要る大きさにそろえる(足りなければ伸ばし、大きすぎれば縮める)
		void ResizeAlphaBuffers(
			Graphics::D3D12::Device* a_pDevice,
			Graphics::D3D12::DescriptorHeapManager* a_pHeapManager,
			UINT a_need);

		// 描画設定の表の GPU 側を要る大きさにそろえる(足りなければ2のべき乗で伸ばす)
		bool ReserveRecordBuffer(
			Graphics::D3D12::Device* a_pDevice,
			Graphics::D3D12::DescriptorHeapManager* a_pHeapManager,
			Graphics::D3D12::GraphicsCommandList* a_pCmdList,
			UINT a_need);

	private:

		// 積んだもの(Commit で並べて番号を決める)
		struct PendingRecord
		{
			Handle<Resource::ParticlesAsset> handle = {};
			ParticleDrawRecord record = {};
			bool isAlphaBlend = false;
			UINT capacity = 0;
			int sortOrder = 0;
			std::string_view name = {};		// アセットの名前(フレームの中だけ借りる)
		};
		std::vector<PendingRecord> m_pending;

		// このフレームの表(CPU 側の写し)と、アセットから番号を引く表
		std::vector<ParticleDrawRecord> m_records;
		std::vector<bool> m_isAlphaBlendRecords;
		std::unordered_map<Handle<Resource::ParticlesAsset>, uint32_t> m_recordIndices;
		bool m_isRecordReady = false;
		bool m_isOverflowWarned = false;		// 表の上限を超えたことを知らせたか(1回だけ)

		// 表の GPU 側 : 毎フレーム全部送り直すので、足りなくなったら作り直すだけ
		std::unique_ptr<Graphics::D3D12::StaticStructuredBuffer<ParticleDrawRecord>> m_upRecordBuffer;
		UINT m_recordCapacity = 0;

		// 半透明の粒を集めて並べる先
		UINT m_alphaCapacity = 0;		// このフレームの半透明のプールの容量の合計(集まる粒の上限)
		Graphics::D3D12::RWStructuredBuffer<SortEntry> m_sortBuffer;
		Graphics::D3D12::RWStructuredBuffer<uint32_t> m_drawList;
		UINT m_sortSize = 0;

		static constexpr UINT DRAW_ARGS_ELEMENT_NUM = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) / sizeof(uint32_t);
		Graphics::D3D12::RWStructuredBuffer<uint32_t> m_drawArgs;
		bool m_isArgsReady = false;
	};
}
