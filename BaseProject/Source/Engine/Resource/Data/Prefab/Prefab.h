#pragma once
//==========================================================================================
//
// Prefab
//
// 「エンティティのテンプレート」リソース。
// シグネチャ(どのコンポーネントを持つか)と、各コンポーネントの初期値バイト列を保持する。
// 他リソースと同じく GUID で参照し、エディタ上で ECS のエンティティインスペクタと
// 同じ感覚でコンポーネントを追加・削除・編集できる。
//
//==========================================================================================

namespace Engine
{
	namespace ECS
	{
		class World;
	}
}

namespace Engine::Resource
{
	class AssetDatabase;
	struct ResourceBuildContext;

	//======================================================================================
	// プレハブに含める子エンティティ1つぶんのテンプレート
	//
	// 親子関係は「保存時のGUID」で持つ。実体化のたびに全ノードへ新しいGUIDを振り直し、
	// 保存済みのバイト列に残っている旧GUIDを新GUIDへ張り替えることで、
	// 親子リンク(HierarchyComponent.parentGUID)やアタッチメントの参照が
	// そのインスタンスの中だけで閉じるようにする。
	//======================================================================================
	struct PrefabChild
	{
		ECS::Signature sig;
		std::unordered_map<ECS::ComponentTypeID, std::vector<uint8_t>> dataMap = {};

		// 保存したときのGUID。実体化時に「旧→新」の対応を作るための鍵として使う
		Core::GUID savedGUID = {};

		// 親の位置。-1 ならルート直下、それ以外は children の添え字
		int parentIndex = -1;
	};

	// 実体化するときの1エンティティぶんの材料(ルートを先頭に、親→子の順)
	struct PrefabInstanceData
	{
		ECS::Signature sig;
		std::unordered_map<ECS::ComponentTypeID, std::vector<uint8_t>> dataMap = {};

		// 親の位置(この材料の並びの添え字)。-1 ならルート
		int parentIndex = -1;

		// このノードに振った GUID。
		// GUIDComponent を持たないノードにも振る。プレハブ内の参照(アタッチメントなど)は
		// この GUID へ張り替わるので、即時生成の側がここから相手を引いてエンティティIDで付け直せる
		Core::GUID guid = {};
	};

	class Prefab
	{
	public:
		// ノードの位置でルートを表す値(PrefabChild::parentIndex と同じ数え方。0 以上は子の添え字)
		static constexpr int ROOT_NODE = -1;

		Prefab();
		~Prefab() = default;
		NON_COPYABLE_MOVABLE(Prefab);

		//----------------------------------------------------------------------------------
		// リソースローダー / 生成(他リソースと同じ入口)
		//----------------------------------------------------------------------------------
		// ファイルパスから読み込んで実体を返す(ResourceManager の Load から使用)
		// a_pContext : 型を引くワールドの持ち主(pSceneManager)をここから受け取る
		static Prefab LoadFromFile(const std::string& a_path, const ResourceBuildContext* a_pContext);
		// 空のプレハブアセットを新規作成(AssetDataBasePanel から使用)
		// a_pWorld : 保存に使うワールド(コンポーネントの型を引く)
		static void   Create(AssetDatabase& a_assetDB, ECS::World* a_pWorld, const std::string& a_path, const std::string& a_name);

		//----------------------------------------------------------------------------------
		// 保存 / 読み込み(コンポーネントのメタ情報が必要なので World を受け取る)
		//----------------------------------------------------------------------------------
		void Save(ECS::World* a_pWorld, const std::string& a_savePath);
		void Load(ECS::World* a_pWorld, const std::string& a_filePath);

		//----------------------------------------------------------------------------------
		// 実体化
		// このプレハブのシグネチャ・初期値からエンティティを生成してルートを返す。
		// 子を持つプレハブなら子も一緒に作り、親子リンクを繋ぎ直す。
		// (シーン読み込みと同じく CreateEntity → 各コンポーネントへ初期値を流し込む)
		//----------------------------------------------------------------------------------
		ECS::Entity Instantiate(ECS::World* a_pWorld);

		/// <summary>
		/// 実体化に使う材料を組み立てる(まだ生成はしない)。
		///
		/// ルートを先頭に、親が子より前に来る順で返す。各ノードには新しいGUIDが振られ、
		/// バイト列に残っている保存時GUIDへの参照(親リンクやアタッチメント)も
		/// 新しいGUIDへ張り替え済み。
		///
		/// 遅延生成(World::ReserveCreateEntityWithData)側からも同じ材料を使えるように分けてある。
		/// </summary>
		std::vector<PrefabInstanceData> BuildInstanceData(ECS::World* a_pWorld) const;

		//----------------------------------------------------------------------------------
		// 子エンティティ(エディタのプレハブ化・プレハブエディターから積む)
		//----------------------------------------------------------------------------------
		void AddChild(PrefabChild a_child) { m_children.push_back(std::move(a_child)); }
		void ClearChildren() { m_children.clear(); }
		const std::vector<PrefabChild>& GetChildren() const { return m_children; }

		/// <summary>
		/// 子を1つ、その下の子孫ごと取り除く(プレハブエディターから使用)。
		/// 残った子の parentIndex は詰めた後の添え字へ付け直す。
		/// </summary>
		/// <param name="a_childIndex">取り除く子の添え字(ルートは取り除けない)</param>
		/// <returns>取り除いた数(範囲外なら 0)</returns>
		size_t RemoveChildTree(int a_childIndex);

		// ルートを保存したときのGUID(参照の張り替えに使う)
		void SetSavedGUID(const Core::GUID& a_guid) { m_savedGUID = a_guid; }
		const Core::GUID& GetSavedGUID() const { return m_savedGUID; }

		// そのノードを指す鍵(保存時GUID)を取得する。無ければ振ってから返す。
		// 子を足すとき、子から親を指すのに使う(新規作成したプレハブのルートは鍵を持たないため)
		const Core::GUID& RequestNodeGUID(int a_nodeIndex);

		//----------------------------------------------------------------------------------
		// コンポーネント操作(エディタから使用)
		//
		// a_nodeIndex で触るノードを選ぶ。既定はルート。
		// 範囲外の子を指したときは何もしない(参照系は空 / nullptr を返す)
		//----------------------------------------------------------------------------------
		// 既定値でコンポーネントを追加する
		void AddComponentDefault(ECS::World* a_pWorld, ECS::ComponentTypeID a_compTypeID, int a_nodeIndex = ROOT_NODE);
		// 既存データ(エンティティのコンポーネント等)をコピーして追加する
		void AddComponentData(ECS::World* a_pWorld, ECS::ComponentTypeID a_compTypeID, const uint8_t* a_pSrc);
		// コンポーネントを削除する
		void RemoveComponent(ECS::ComponentTypeID a_compTypeID, int a_nodeIndex = ROOT_NODE);

		/// <summary>
		/// シグネチャと初期値をまとめて差し替える(エディタのコピー&ペースト用)。
		///
		/// 「構成をそのまま持ってくる」操作なので、元から持っていて
		/// コピー元に無いコンポーネントは落とす(マージではない)。
		/// バイト列ごと入れ替えるため、編集済みの初期値もそのまま引き継ぐ。
		/// </summary>
		/// <param name="a_sig">貼り付け元のシグネチャ</param>
		/// <param name="a_dataMap">貼り付け元のコンポーネント初期値</param>
		void PasteSignatureAndData(
			const ECS::Signature& a_sig,
			const std::unordered_map<ECS::ComponentTypeID, std::vector<uint8_t>>& a_dataMap);

		// 所持しているか
		bool Has(ECS::ComponentTypeID a_compTypeID, int a_nodeIndex = ROOT_NODE) const;
		// コンポーネント先頭バイトへのポインタ(エディタ編集用。無ければ nullptr)
		uint8_t* RefData(ECS::ComponentTypeID a_compTypeID, int a_nodeIndex = ROOT_NODE);
		// シグネチャ参照
		const ECS::Signature& GetSignature(int a_nodeIndex = ROOT_NODE) const;
		// コンポーネント初期値バイト列の参照(実体化時の元データ)
		const std::unordered_map<ECS::ComponentTypeID, std::vector<uint8_t>>& GetDataMap(int a_nodeIndex = ROOT_NODE) const;

		// ノードの数(ルート + 子)
		size_t GetNodeCount() const { return m_children.size() + 1; }
		// そのノードが在るか
		bool IsValidNode(int a_nodeIndex) const;

		//----------------------------------------------------------------------------------
		// シリアライズ処理
		//----------------------------------------------------------------------------------
		void Archive(Persistence::Archive& a_ar, ECS::World* a_pWorld);

		/// <summary>
		/// バイト列に含まれる GUID を対応表で張り替える。
		/// 参照を持つコンポーネントを型で列挙しなくて済むよう、バイト単位で走査する。
		/// (GUID は乱数なので、別の意味のバイト列が偶然一致することは実質ない)
		/// </summary>
		static void RemapGUIDs(
			uint8_t* a_pData, size_t a_size,
			const std::unordered_map<Core::GUID, Core::GUID>& a_guidMap);

	private:

		// ノードのシグネチャと初期値の置き場を引く(範囲外なら false)
		bool ResolveNode(
			int a_nodeIndex,
			ECS::Signature*& a_outSig,
			std::unordered_map<ECS::ComponentTypeID, std::vector<uint8_t>>*& a_outDataMap);

	private:

		// 生成エンティティシグネチャ
		ECS::Signature m_sigunature;

		// コンポーネントごとの初期値バイト列(型ID -> バイト列)
		std::unordered_map<ECS::ComponentTypeID, std::vector<uint8_t>> m_dataMap = {};

		// ルートを保存したときのGUID。
		// ルート自身の GUIDComponent は空のまま(従来どおり実体化時に振る)なので、
		// 子から親を指すための鍵としてここに別で覚えておく。
		Core::GUID m_savedGUID = {};

		// 子エンティティ(親が子より前に来る順で持つ)
		std::vector<PrefabChild> m_children = {};
	};
}
