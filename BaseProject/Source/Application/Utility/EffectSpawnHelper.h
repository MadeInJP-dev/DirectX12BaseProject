#pragma once

//==========================================================================================
//
// EffectAsset を「その場に出す」ための小さなヘルパー。
//
// プレハブを介さずに、エフェクト再生に必要な最小限のエンティティを組んで出す。
//   LocalTransform / WorldMatrix / EffectAssetComponent / EffectComponent
//
// 出したエンティティは destroyOnFinish で自分から消えるので、
// 呼んだ側は後片付けを気にしなくてよい
// (出しっぱなしのパーツを含むエフェクトを渡すと消えないので注意)。
//
// 生成はシステム反復中に即時に行えない(アーキタイプが壊れる)ため、
// World の遅延生成コマンドに積む。実体化は次の BeginFrame。
//
//==========================================================================================

namespace Engine
{
	namespace ECS { class APPWorld; }
	namespace Graphics::Particle { struct EmitterTransform; }
	namespace Effect { struct EffectInstance; }
}

namespace App::Utility
{
	/// <summary>
	/// エフェクトアセットを指定座標で再生する(遅延コマンドを積むだけ)
	/// </summary>
	/// <param name="a_effectGUID">再生するエフェクト。未設定なら何もしない</param>
	/// <param name="a_pos">再生位置(ワールド)</param>
	/// <param name="a_isDestroyOnFinish">
	/// 出し切ったら自分ごと消えるか。
	/// 撃ちっぱなしにする通常の演出は true。
	/// エディターのプレビューのように「同じものを何度も再生し直したい」場合は false にして、
	/// 出したエンティティの寿命を呼んだ側が握る
	/// </param>
	/// <param name="a_emitDir">
	/// 吹き出す向き(ワールド)。0ベクトルならアセットのパーツが持っている向きのまま出す。
	/// 出す場所は同じでも向きだけ変えたいもの(ブースターのスパークなど)向け
	/// </param>
	/// <param name="a_scale">
	/// エフェクト全体の大きさ倍率。アセットは GUID 単位で共有されるので、
	/// 同じ絵を大小で使い分けたいときはアセットを増やさずここで付ける
	/// </param>
	/// <returns>生成コマンドを積めたら true</returns>
	bool ReserveSpawnEffectAt(
		Engine::ECS::World& a_world,
		const Core::GUID& a_effectGUID,
		const Math::Vector3& a_pos,
		bool a_isDestroyOnFinish = true,
		const Math::Vector3& a_emitDir = {},
		float a_scale = 1.0f);

	/// <summary>
	/// エフェクトアセットを指定の位置と向きで再生する(遅延コマンドを積むだけ)
	/// </summary>
	/// <remarks>
	/// ReserveSpawnEffectAt との違いは、向きも行列から付けるところだけ。
	/// 単発エフェクトのイベント(OneShotEffectEvent)を出すのに使う。
	/// 行列の拡縮は使わない(大きさは a_scale で付ける)
	/// </remarks>
	/// <param name="a_worldMat">出す位置と向き(ワールド)</param>
	/// <returns>生成コマンドを積めたら true</returns>
	bool ReserveSpawnEffect(
		Engine::ECS::World& a_world,
		const Core::GUID& a_effectGUID,
		const Math::Matrix& a_worldMat,
		bool a_isDestroyOnFinish = true,
		float a_scale = 1.0f);

	/// <summary>
	/// エフェクトアセットを、指定のエンティティの子として再生する(遅延コマンドを積むだけ)
	/// </summary>
	/// <remarks>
	/// 持ち主に付いて行かせたい出しっぱなしのもの(落下中の煙など)向け。
	/// ・親の原点に置き、位置だけ親に付いて行く(回転と拡縮は継がない)。
	/// ・自分からは消えない(destroyOnFinish なし)。親が消えるときに一緒に消える。
	///   止めたいときは、a_extraTagIDs で付けた印から自分を探して再生の要求を下ろす。
	/// ・保存しない印(TransientTag)を付ける。
	/// ・遅延生成なので出したエンティティの ID は返らない。
	/// </remarks>
	/// <param name="a_parent">付いて行く相手。生きていなければ何もしない</param>
	/// <param name="a_extraTagIDs">一緒に付ける印(データを持たない型)のタイプID</param>
	/// <returns>生成コマンドを積めたら true</returns>
	bool ReserveSpawnChildEffect(
		Engine::ECS::World& a_world,
		const Core::GUID& a_effectGUID,
		Engine::ECS::Entity a_parent,
		float a_scale = 1.0f,
		std::initializer_list<Engine::ECS::ComponentTypeID> a_extraTagIDs = {});

	/// <summary>
	/// 単発のエフェクトを、指定のエンティティに付けたまま再生する(遅延コマンドを積むだけ)
	/// </summary>
	/// <remarks>
	/// 速く動く持ち主の上で弾けさせたいもの(ブーストの炎など)向け。
	/// ReserveSpawnEffectAt で出すとその場に置き去りになり、持ち主だけが先へ行ってしまう。
	/// ・位置と回転は親に付いて行く。拡縮は継がない(大きさは a_scale で付ける)。
	///   a_localPos は親の拡縮を外した座標系での位置(親のスケールを掛けてから渡す)。
	/// ・a_localEmitDir は親の回転の中での向き。0 ベクトルならアセットの向きのまま。
	/// ・出し切ったら自分から消える(destroyOnFinish)。保存しない印(TransientTag)を付ける。
	/// ・粒を持ち主と一緒に動かしたいときは、パーツのシミュレーション空間を Local にしておくこと。
	/// </remarks>
	/// <param name="a_parent">付いて行く相手。生きていなければ何もしない</param>
	/// <returns>生成コマンドを積めたら true</returns>
	bool ReserveSpawnAttachedEffect(
		Engine::ECS::World& a_world,
		const Core::GUID& a_effectGUID,
		Engine::ECS::Entity a_parent,
		const Math::Vector3& a_localPos,
		const Math::Vector3& a_localEmitDir = {},
		float a_scale = 1.0f);

	/// <summary>
	/// エフェクトアセットを指定座標で再生し、作ったエンティティを返す(即時生成)
	/// </summary>
	/// <remarks>
	/// ReserveSpawnEffectAt との違いは「その場で作って ID を返す」ところだけ。
	/// 出したあとも位置を動かし続けたい・こちらの都合で消したい、という
	/// 相手を握っておきたい場面(環境のチリなど)向け。
	///
	/// ただし即時生成はチャンクの並びを変えるので、
	/// **ECS の反復中に呼んではいけない**。
	/// 呼んでよいのはシステムの外 : GameObject の Update / Draw など。
	/// システムの中から出すときは今までどおり ReserveSpawnEffectAt(遅延)を使うこと。
	///
	/// 返ったエンティティは PostDeserialize から始まるので、
	/// アセットの解決(EffectFixupSystem)が済むのは次の BeginFrame。
	/// それまでは何も出ないが、位置は入れておいてよい。
	/// </remarks>
	/// <returns>作ったエンティティ。作れなければ INVALID_ENTITY</returns>
	Engine::ECS::Entity SpawnEffectAtNow(
		Engine::ECS::World& a_world,
		const Core::GUID& a_effectGUID,
		const Math::Vector3& a_pos,
		bool a_isDestroyOnFinish = true,
		const Math::Vector3& a_emitDir = {},
		float a_scale = 1.0f);

	/// <summary>
	/// エフェクトが使うパーティクルのGPUプールを先に作らせる
	/// 出す瞬間に作り始めると数フレーム遅れるため
	/// </summary>
	/// <param name="a_services"></param>
	/// <param name="a_effectHandle"></param>
	void WarmupEffectParticles(
		const Engine::ECS::EngineServices& a_services,
		Engine::Handle<Engine::Resource::EffectAsset> a_effectHandle
	);

	/// <summary>
	/// エフェクトが持っている発生源の席を返す予約をして、手元のハンドルを空にする
	/// </summary>
	/// <remarks>
	/// 持ち主が消えても、出した粒は寿命まで残って席の行列を読み続ける。
	/// そこで「このエフェクトが使うローカル空間パーティクルの最大寿命」だけ待ってから空きへ戻す。
	/// 寿命はエフェクトアセットから引くので、エフェクトのハンドルを返す前に呼ぶこと。
	/// 席を持っていなければ何もしない
	/// </remarks>
	void ReserveReturnEffectEmitterSlot(
		const Engine::ECS::EngineServices& a_services,
		Engine::Handle<Engine::Resource::EffectAsset> a_effectHandle,
		Engine::Handle<Engine::Graphics::Particle::EmitterTransform>& a_emitterSlot
	);

	/// <summary>
	/// エフェクトが借りているポイントライト(ライトパーツ)を返す
	/// </summary>
	/// <remarks>
	/// 出している間だけ EffectDrawSystem が借りているので、エンティティが消えるときに返し損ねがあればここで返す
	/// (EffectRuntimeComponent の Release から呼ぶ)
	/// </remarks>
	void ReleaseEffectLights(
		const Engine::ECS::EngineServices& a_services,
		Engine::Effect::EffectInstance& a_instance
	);

	/// <summary>
	/// エフェクトが借りているトレイル(トレイルパーツの点の履歴)を手放す
	/// </summary>
	/// <remarks>
	/// 手放した帯は残りの点が消えきるまで描かれ、その後プールへ返る(エンティティが消えても軌跡は急に消えない)。
	/// EffectRuntimeComponent の Release から呼ぶ
	/// </remarks>
	void ReleaseEffectTrails(
		const Engine::ECS::EngineServices& a_services,
		Engine::Effect::EffectInstance& a_instance
	);
}
