#pragma once

#include "../Material/Material.h"
#include "../Animation/Animation.h"
#include "../Mesh/Mesh.h"
#include "../Node/Node.h"

namespace Engine::Resource
{
	class ResourceManager;

	struct ModelData
	{
		// モデル名
		std::string name;

		// マテリアル
		std::vector<Material>		MaterialVec;		// マテリアル
		std::vector <Mesh> 			MeshVec;			// メッシュ
		std::vector <AnimationData>	AnimationVec;		// アニメーション

		// ノード
		std::vector<Node>							originalNodes;			// 全ノード配列
		std::vector<int>							rootNodeIndices;			// Rootノード
		std::vector<int>							boneNodeIndices;			// ボーンノード
		std::vector<int>							meshNodeIndices;			// メッシュが存在するノード
		std::vector<int>							collisionMeshNodeIndices;	// 子リジョンメッシュが存在するノード
		std::vector<int>							drawMeshNodeIndices;		// 描画するノード
	};

	// モデル描画時用キャッシュデータ
	//
	// メッシュ・マテリアルの実体はリソースマネージャーに置いたままにして、
	// ここではハンドルだけを持つ。
	// 実体のポインタを持たせると、スイープでスロットが解放・再利用されたときに
	// 行き先を失ったポインタがモデル側に残る。
	// 添え字だけを持つのも同様に危険で、
	// スロットが再利用されていた場合に別のリソースを黙って掴んでしまう。
	// ハンドルなら世代が食い違った時点で取得が nullptr になる。
	// 描画時に ResourceManager から引き直すこと
	struct ModelDrawCommand
	{
		uint16_t nodeIndex;					// モデル内のローカルノード番号
		Handle<Mesh> meshHandle;			// 描画するメッシュ
		Handle<Material> materialHandle;	// 使用するマテリアル
		uint8_t  subIdx;
		Engine::Resource::EAlpha alphaMode;
	};

	// ボーンレイヤー1つ分の、ノード1つへの効き
	struct BoneWeight
	{
		uint16_t nodeIndex = 0;		// モデル内のノード番号(Node::boneIndex ではない)
		float weight = 1.0f;		// 0〜1
	};

	// ボーンレイヤー : アニメーションレイヤリングで、上に重ねるアニメーターを
	// どのノードへどれだけ効かせるかのマスク(上半身だけ別のアニメーション、など)。
	// 参照する側は名前のハッシュで引く(並びが変わっても壊れないように)
	struct BoneMask
	{
		std::string name;
		UINT nameHash = 0;
		std::vector<BoneWeight> bones;	// ノード番号の昇順。載っていないノードは重み0
	};

	/// ボーンレイヤーの保存・読込(.mdl の末尾)
	/// 読込時は名前のハッシュも作り直す
	void ArchiveBoneMasks(Persistence::Archive& a_ar, std::vector<BoneMask>& a_masks);

	//==========================================================================================
	// LOD の1段
	//
	// LOD0(このモデル自身)より粗いモデルと、そこへ切り替える大きさ。
	// どの段で描くかは増幅シェーダーがパスごとのカメラで決める(影のカスケードは光源のカメラ)。
	//
	// ・LOD モデルの描画コマンド(サブセット)は、LOD0 と同じ並びにそろえること。
	//   i 番目の描画コマンドは LOD0 の i 番目の代わりに描かれる。LOD 側に無い番号は、その段では描かない
	// ・ノードの変換は LOD0 のものを使う(LOD モデルは同じノード構成で書き出すこと)
	// ・LOD を持てるのは LOD0 だけ。LOD モデル自身の LOD は読まない
	// ・アニメーションするモデルには効かない(スキニングは LOD0 で回すため)
	//==========================================================================================
	struct ModelLodLevel
	{
		GUID modelGUID = {};		// この段で描くモデル
		float screenSize = 0.1f;	// 画面の高さに対する、モデルの直径の割合。これより小さく映るとこの段で描く
	};

	// LOD の段数の上限(LOD0 を含まない)
	constexpr size_t MAX_MODEL_LOD_LEVELS = 4;

	/// LOD の保存・読込(.mdl の末尾。ボーンレイヤーの後ろ)
	void ArchiveLodLevels(Persistence::Archive& a_ar, std::vector<ModelLodLevel>& a_levels);

	struct ModelAssetData
	{
		std::string name;

		std::vector<GUID> materialGUIDs;
		std::vector<GUID> meshGUIDs;
		std::vector<GUID> animationGUIDs;

		std::vector<Node> originalNodes;

		std::vector<int> rootNodeIndices;
		std::vector<int> boneNodeIndices;
		std::vector<int> meshNodeIndices;
		std::vector<int> collisionMeshNodeIndices;
		std::vector<int> drawMeshNodeIndices;

		// アニメーション用のボーンレイヤー
		std::vector<BoneMask> boneMasks;

		// LODモデル : LODが0のモデルにのみぶら下げれるようにする。
		// 細かい順(LOD1, LOD2, ...)に並べる
		std::vector<ModelLodLevel> lodLevels;
	};

	class Model;

	struct ModelRuntimeData
	{
		std::vector<ResourceRef<Material>> materials;
		std::vector<ResourceRef<Mesh>> meshes;
		std::vector<ResourceRef<AnimationData>> animations;

		std::vector<ModelDrawCommand> drawCommands;

		// LODモデル(lodLevels と同じ並び)。読めなかった段は無効なハンドルのまま
		std::vector<ResourceRef<Model>> lodModels;

		// モデル全体を囲む球(モデルのローカル空間。描画ノードの変換を掛けたもの)。
		// LOD を選ぶときに、画面にどれだけの大きさで映るかを測るのに使う
		DirectX::BoundingSphere localBounds = {};
	};
	// モデルデータ
	class Model
	{
	public:

		Model() = default;
		Model(ModelAssetData&& a_asset, ModelRuntimeData&& a_runtimeData) 
			: m_AssetData(std::move(a_asset)), m_runtimeData(std::move(a_runtimeData))
		{}
		~Model() = default;
		NON_COPYABLE_MOVABLE(Model);

		// サブアセットの実体を引き、メタファイルを先に作ってGUIDを埋める
		void Save(ResourceManager& a_resourceManager, const std::string& a_filePath);

		// 解放
		void Release();


		// アニメーションのハンドルからGUIDを逆引き
		Core::GUID GetAnimationGUIDFromHandle(const Handle<AnimationData>& a_handle) const;
		Handle<AnimationData> GetAnimationHandleFromGUID(const Core::GUID& a_guid) const;

		// ---- アクセサ ----
		// データのハンドル
		const std::vector<ResourceRef<Material>>&		GetMaterialHandles()	const { return m_runtimeData.materials; }
		const std::vector<ResourceRef<Mesh>>&			GetMeshHandles()		const { return m_runtimeData.meshes; }
		const std::vector<ResourceRef<AnimationData>>&	GetAnimationHandles()	const { return m_runtimeData.animations; }

		// モデルが管理する実データ
		const std::string& GetName() const { return m_AssetData.name; }
		const std::vector<Node>& GetOriginalNodeVec() const { return m_AssetData.originalNodes; }

		// 各種インデックス配列
		const std::vector<int>& GetRootNodeVec() const { return m_AssetData.rootNodeIndices; }
		const std::vector<int>& GetBoneNodeVec() const { return m_AssetData.boneNodeIndices; }
		const std::vector<int>& GetMeshNodeVec() const { return m_AssetData.meshNodeIndices; }
		const std::vector<int>& GetCollisionMeshNodeVec() const { return m_AssetData.collisionMeshNodeIndices; }
		const std::vector<int>& GetDrawNodeVec() const { return m_AssetData.drawMeshNodeIndices; }

		// アニメーション用のボーンレイヤー
		const std::vector<BoneMask>& GetBoneMaskVec() const { return m_AssetData.boneMasks; }
		std::vector<BoneMask>& RefBoneMaskVec() { return m_AssetData.boneMasks; }		// エディターでの編集用

		// 名前のハッシュからボーンレイヤーを引く。無ければ nullptr
		const BoneMask* FindBoneMask(UINT a_nameHash) const;

		// 描画時用コマンド取得
		const std::vector<ModelDrawCommand>& GetDrawCommandVec() const { return m_runtimeData.drawCommands; }

		// LOD
		const std::vector<ModelLodLevel>& GetLodLevelVec() const { return m_AssetData.lodLevels; }
		std::vector<ModelLodLevel>& RefLodLevelVec() { return m_AssetData.lodLevels; }		// エディターでの編集用
		const std::vector<ResourceRef<Model>>& GetLodModels() const { return m_runtimeData.lodModels; }
		void SetLodModels(std::vector<ResourceRef<Model>>&& a_lodModels) { m_runtimeData.lodModels = std::move(a_lodModels); }		// エディターでの編集用

		// モデル全体を囲む球(ローカル空間)
		const DirectX::BoundingSphere& GetLocalBounds() const { return m_runtimeData.localBounds; }

		// モデルの全データ取得
		const ModelAssetData& GetAssestData() const { return m_AssetData; }
		const ModelRuntimeData& GetRuntimeData() const { return m_runtimeData; }

	private:

		// アセットデータ
		ModelAssetData m_AssetData = {};

		// ランタイムデータ
		ModelRuntimeData m_runtimeData = {};
	};
}
