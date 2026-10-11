#pragma once
#include "Engine/Graphics/Raytracing/DynamicRaytracingData.h"

namespace Engine::ECS
{
	class World;
}

namespace Engine::Graphics
{
	// 64ビットのソートキー
	//
	// ここに詰まっている ID は「並べ替えと、同じ状態をまとめるため」だけのもの。
	// 世代を持たないので、ここからリソースを引かないこと。
	// 実体が必要なときは LightWeightDrawItem のハンドルから取得する
	union RenderSortKey
	{
		uint64_t value;
		struct {
			// 下位ビットから順に判断優先度が低くなるように配置する
			//
			// 幅の配り方について:
			//   psoID は溢れると「描くときに別のPSOを引く」か「アイテムごと捨てる」しかなく、
			//   絵が消える。一方 meshID / materialID は並べ替えのための値でしかなく
			//   (ここからリソースは引かない)、被っても並び順が少し甘くなるだけ。
			//   なので psoID にはハンドルのインデックスと同じ16bitを渡し切って、
			//   切り捨てが起こりえない形にしてある。
			//   足りないぶんは、まだ誰も書いていない depth から回している
			//
			// subIndex は最下位に置く : 同じメッシュの同じサブセットを隣り合わせ、
			// 描くときに1回のディスパッチへまとめられる(インスタンシング)ようにするため。
			// これが無いと、同じマテリアルを使う別サブセット同士がキーで区別できず
			// 並びが混ざって、まとまりが途中で切れる。溢れても並びが甘くなるだけ
			uint64_t subIndex : 12;			// サブセット
			uint64_t meshID : 14;			// メッシュ
			uint64_t materialID : 14;		// マテリアル
			uint64_t psoID : 16;			// PSOID : Handle::GetIndex() と同じ幅
			uint64_t passIndex : 8;			// パスインデックス
		} bits;

		//------------------------------------------------------------------------------------------
		// 半透明用の並び
		//
		// 半透明は奥から手前へ描かないと重なりが崩れるので、PSOでまとめるより深さを優先する。
		// パス番号は不透明と同じ最上位8bitに置く(パスごとの範囲検索 GetPassItems がそのまま使える)。
		// farFirstDepth は「遠いほど小さい」値なので、昇順に並べると奥から描かれる
		//------------------------------------------------------------------------------------------
		struct {
			uint64_t psoID : 16;			// 同じ深さならPSOでまとめる
			uint64_t reserved : 8;			// 未使用
			uint64_t farFirstDepth : 32;	// カメラからの距離(遠いほど小さい)。DrawLists::ResolveTransparentSortKeys が入れる
			uint64_t passIndex : 8;			// パスインデックス
		} transparentBits;
	};

	struct LightWeightDrawItem
	{
		// 描画順序と各種IDの情報すべてを持つ
		RenderSortKey sortKey;
		UINT subIndex = 0;

		// 描画に使うリソース。
		// 実体はリソースマネージャーに置いたままにして、ここではハンドルだけを持つ。
		// 描画する瞬間に引き直すこと
		Handle<Resource::Mesh>		meshHandle = {};
		Handle<Resource::Material>	materialHandle = {};

		// インスタンスデータ
		bool isAnimation = false;

		// メッシュシェーダー用インデックス
		UINT meshInstanceIndex = 0;
		UINT meshMaterialIndex = 0;

		// このサブセットを描画するためのメッシュレット数
		UINT subsetMeshletCount = 0;

		// 描くときに張るPSOの番号。
		// ソートキーの中の位置は不透明と半透明で違うので、キーから取り出さずにここへ持つ
		uint16_t psoID = 0;

		// 半透明か。半透明はソートの直前に、sortPos とカメラの距離でキーを決め直す
		bool isTransparent = false;
		Math::Vector3 sortPos = {};

		// ヘルパー関数 : ビット位置を直に書くと幅を変えたときに追従し損ねるので、
		// 取り出しはビットフィールド越しにする(パス番号はどちらの並びでも同じ位置)
		uint8_t GetPassIndex()		const { return static_cast<uint8_t>(sortKey.bits.passIndex); }
		uint16_t GetPSOID()			const { return psoID; }
	};



	/// <summary>
	/// GPUスキニングするエンティティの命令
	/// </summary>
	struct SkinningDispatchItem
	{
		RangeHandle<Resource::MeshVertexPacked> staticVertexHandle;		// アセット側の頂点データ
		RangeHandle<Resource::MeshSkinVertex> skinVertexHandle;			// アセット側のボーン割り当て(スキンメッシュでなければ無効)
		RangeHandle<uint32_t> staticIndexHandle;						// アセット側のインデックスデータ
		RangeHandle<Resource::NodePoseMatrix> nodePoseMat;				// CPUで更新されたボーンノード行列
		RangeHandle<Resource::AnimatedVertex> animatedHandle;
		RangeHandle<Resource::BoneMatrix> boneHandle;					// ボーン行列(ワールド内のプール添字)

		// ボーンパレット(GPU)での開始位置。
		// プールの添字はワールドごとに 0 から振り直されるので、
		// 複数のシーンを重ねて描くとそのままでは他シーンのボーンを踏む。
		// アップロード時に付く土台を足した「GPU上の」位置をここに持つ
		uint32_t boneBufferStart = 0;

		// 自身のBLASと変形後頂点を入れるメガバッファのハンドルを保持しているインスタンスのハンドル
		Handle<Raytracing::DynamicRaytracingData> animHandle;

		// この命令を出したワールド。
		// animHandle はワールドごとのプールの鍵なので、引くときは必ずこのワールドから引く。
		// シーンを重ねて描くと命令配列に複数のワールドのものが混ざるため、
		// 「今の一番上のシーン」から引くと他人のプールを鍵違いで探すことになる
		ECS::World* pWorld = nullptr;
	};
}