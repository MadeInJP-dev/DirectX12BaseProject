#pragma once

//==========================================================================================
//
// プレハブを座標指定で生成するための小さなヘルパー。
//
// 「爆発プレハブを当たった場所に出す」処理が、当たって消える側(ExplodeOnHitSystem)と
// 体力が尽きて消える側(HealthSystem)の2箇所に必要になったのでまとめている。
// ウェーブでの敵出現(SceneSequence)も同じ経路を通る。
//
// 生成はシステム反復中に即時に行えない(アーキタイプが壊れる)ため、
// World の遅延生成コマンドに積む。実体化は次の BeginFrame。
//
// ・反復の外(GameObject の初期化や Update)から出して、出したエンティティIDをすぐ
//   握りたいときは即時生成の経路(BuildSpawnInstanceData → CreateInstanceNow)を使う。
//   群れのボス(SwarmBossController)が「一つ前の小隊長」を覚えさせるのに使っている。
//
// ・即時生成は、親子(HierarchyComponent)とアタッチメントのスロットをエンティティIDで付ける。
//   GUIDComponent を持たないプレハブ(プレイヤー)でもつながる。
//   遅延生成はIDが返らないので GUID でしかつながらない。GUID を持たない子のあるプレハブは即時生成で出すこと。
//
// ・出したものには子も含めて TransientTag を付ける(シーンの保存に入れない)。
//   プレイヤーのスポナー(PlayerSpawner)が出したプレイヤーを保存すると、
//   次に開いたときに二重になるため。
//
//==========================================================================================

namespace Engine
{
	namespace ECS { class APPWorld; }
	namespace Resource { class ResourceManager; class Prefab; struct PrefabInstanceData; }
}

namespace App::Utility
{
	/// <summary>
	/// 生成時に上書きする初期値。
	/// LocalTransform を持たないプレハブでも、位置を入れるために足してから生成する。
	/// </summary>
	struct SpawnParams
	{
		Math::Vector3 pos  = { 0.0f, 0.0f, 0.0f };			// 生成位置(ワールド)

		// 生成時の向き。isOverrideRotation が false ならプレハブの保存値をそのまま使う
		// (爆発エフェクトのように、プレハブ側で付けた傾きを潰したくない用途があるため)
		Math::Quaternion quat = { 0.0f, 0.0f, 0.0f, 1.0f };
		bool isOverrideRotation = false;

		// 生成元の印(SpawnerComponent)。付けると出した側が生存数を数えられる。
		// spawnerGUID が無効なら印は付けない。
		Core::GUID spawnerGUID = Core::DEFAULT_GUID;
		int          waveIndex   = -1;

		// 追従先(FollowTargetComponent)。ボイドのリーダーなど、出した側を追わせるときに使う。
		// followTarget が無効なら触らない
		Engine::ECS::Entity followTarget = Engine::ECS::Limits::INVALID_ENTITY;
	};

	/// <summary>
	/// プレハブを生成する(遅延コマンドを積むだけ)
	/// </summary>
	/// <param name="a_prefabGUID">生成するプレハブ。未設定なら何もしない</param>
	/// <param name="a_refHandle">解決済みハンドルの置き場。未解決ならここへ解決結果を書き戻す</param>
	/// <param name="a_params">位置・向き・生成元の印</param>
	/// <returns>生成コマンドを積めたら true(プレハブが引けなければ false)</returns>
	bool SpawnPrefab(
		Engine::ECS::World& a_world,
		Engine::Resource::ResourceManager& a_resourceManager,
		const Core::GUID& a_prefabGUID,
		Engine::Handle<Engine::Resource::Prefab>& a_refHandle,
		const SpawnParams& a_params);

	/// <summary>
	/// プレハブを指定座標に生成する(向きと印は既定のまま)
	/// </summary>
	bool SpawnPrefabAt(
		Engine::ECS::World& a_world,
		Engine::Resource::ResourceManager& a_resourceManager,
		const Core::GUID& a_prefabGUID,
		Engine::Handle<Engine::Resource::Prefab>& a_refHandle,
		const Math::Vector3& a_pos);

	//======================================================================================
	// 即時生成
	//
	// 材料作り → (呼び出し側で足したいコンポーネントを足す) → 生成、の3段に分けてある。
	// 生成する側だけが知っている必須コンポーネント(移動に要るもの・役割の印など)を
	// プレハブに入れ忘れていても、ここで足してから出せるようにするため。
	//
	// CreateInstanceNow はその場で World::CreateEntity を呼ぶので、
	// システムの反復中からは呼ばないこと(遅延生成の SpawnPrefab を使う)。
	//======================================================================================

	/// <summary>
	/// 生成の材料を作り、位置・向き・印・追従先をルートへ書き込む(まだ生成はしない)。
	/// 保存しない印(TransientTag)は全ノードへ付ける
	/// </summary>
	/// <param name="a_prefab">生成するプレハブ</param>
	/// <param name="a_params">位置・向き・生成元の印・追従先</param>
	/// <param name="a_outInstanceVec">材料の書き込み先。先頭がルートで、親が子より前に並ぶ</param>
	/// <returns>材料を作れたら true(プレハブが空なら false)</returns>
	bool BuildSpawnInstanceData(
		Engine::ECS::World& a_world,
		const Engine::Resource::Prefab& a_prefab,
		const SpawnParams& a_params,
		std::vector<Engine::Resource::PrefabInstanceData>& a_outInstanceVec);

	/// <summary>
	/// 材料に指定コンポーネントの領域を用意する。持っていなければ足して既定構築する
	/// </summary>
	/// <returns>そのコンポーネントの書き込み先(バイト列の先頭)。用意できなければ nullptr</returns>
	uint8_t* EnsureInstanceComponent(
		Engine::ECS::World& a_world,
		Engine::Resource::PrefabInstanceData& a_data,
		Engine::ECS::ComponentTypeID a_typeID);

	/// <summary>
	/// 材料からエンティティをその場で生成する(子も一緒に作る)。
	/// 親子とアタッチメントのスロットはエンティティIDで付ける
	/// </summary>
	/// <param name="a_pOutEntityVec">作ったエンティティ(材料と同じ並び。作れなかった所は INVALID_ENTITY)の書き込み先。要らなければ nullptr</param>
	/// <returns>ルートのエンティティ。作れなければ INVALID_ENTITY</returns>
	Engine::ECS::Entity CreateInstanceNow(
		Engine::ECS::World& a_world,
		std::vector<Engine::Resource::PrefabInstanceData>& a_instanceVec,
		std::vector<Engine::ECS::Entity>* a_pOutEntityVec = nullptr);
}
