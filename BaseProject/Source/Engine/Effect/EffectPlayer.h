#pragma once

#include "EffectInstance.h"
#include "Engine/Effect/Trail/TrailNode.h"

namespace Engine::Effect
{
	//==========================================================================================
	// EffectPlayer
	//
	// エフェクトの再生(実行)を受け持つ。データ(EffectAsset)と実行中の値(EffectInstance)を受け取って進める。
	//
	// ・使う側は「再生する・止める・時間を進める」だけを伝えればよく、
	//   何個のパーティクルとメッシュと音で出来ているかを知らなくてよい。
	// ・状態を持たない(関数はすべて static)。実行中の値は EffectInstance、設計図は EffectAsset が持つ。
	// ・以前は EffectAsset(Resource 層)が再生まで持っていて、Resource 層が AudioManager に依存していた。
	//   データと実行を分けるために、ここへ移した(中身は変えていない)
	//==========================================================================================
	class EffectPlayer
	{
	public:

		/// <summary>
		/// 声(サウンドインスタンス)を空の状態から始める。生成時に呼ぶ(EffectFixupSystem)
		/// </summary>
		/// <remarks>
		/// 声は鳴らす直前に借り、鳴り終わったら返す(ループ音は止めたときに返す)。
		/// ここでは持っているものを返して空にするだけで、借りはしない。
		/// (以前は生成時に借りてエンティティの寿命いっぱい握っていたので、
		///  一発ものを大量に出すと鳴る前から声が埋まっていた)
		/// 鳴らす瞬間に波形の読み込みは走らない(波形は EffectAsset::ResolveReferences が先に読む)
		/// </remarks>
		static void PrepareSounds(
			const Resource::EffectAsset& a_asset,
			Engine::Audio::AudioManager& a_audioManager,
			EffectInstance& a_inst);

		// 頭から再生する
		static void Play(const Resource::EffectAsset& a_asset, EffectInstance& a_inst);

		/// <summary>
		/// 止める。出ている途中のパーティクルはそのまま寿命で消える
		/// </summary>
		/// <remarks>
		/// 止めたあとに動くパーツ(OnStop : 消火の火花・終了音など)を持つエフェクトは、
		/// 「止めている最中」(EffectInstance::isStopping)になり、それらを出し終わってから止まりきる
		/// </remarks>
		/// <param name="a_pAudioManager">
		/// 渡すと鳴っている音も止める。null なら音はそのまま鳴り続ける
		/// (単発音を最後まで鳴らしたい場合)
		/// </param>
		static void Stop(
			const Resource::EffectAsset& a_asset,
			EffectInstance& a_inst,
			Engine::Audio::AudioManager* a_pAudioManager = nullptr);

		/// <summary>
		/// 時間を進めて、このフレームの発生数を決める。時間が来たサウンドもここで鳴らす
		/// </summary>
		/// <param name="a_pAudioManager">null ならサウンドパーツは鳴らさない</param>
		/// <param name="a_pParams">
		/// 個体ごとのパラメータ(EFFECT_PARAM_MAX 個。EffectOverrideComponent::params)。
		/// 音量の倍率(SoundVolume の結び付け)に使う。null なら全部 0 として扱う
		/// </param>
		/// <remarks>
		/// 実フレーム時間が要るので Update フェーズで呼ぶこと。
		/// 実際の発生要求は Draw フェーズ側が pendingEmit を見て行う
		/// (パーティクルとメッシュへのパラメータの倍率も Draw 側で掛ける)
		/// </remarks>
		static void Update(
			const Resource::EffectAsset& a_asset,
			EffectInstance& a_inst,
			float a_dt,
			Engine::Audio::AudioManager* a_pAudioManager = nullptr,
			const float* a_pParams = nullptr);

		/// <summary>
		/// 全パーツが出し終わったか
		/// </summary>
		/// <param name="a_pAudioManager">
		/// 渡すと isWaitFinish のサウンドが鳴り終わるまで false を返す。
		/// null ならサウンドは見ない
		/// </param>
		/// <remarks>
		/// 出しっぱなし(duration = 0)のパーツが1つでもあれば、いつまでも false。
		/// 単発エフェクトの後片付け(自分を消す)の判断に使う
		/// </remarks>
		static bool IsFinished(
			const Resource::EffectAsset& a_asset,
			const EffectInstance& a_inst,
			Engine::Audio::AudioManager* a_pAudioManager = nullptr);

		/// <summary>
		/// メッシュパーツの今フレームの描画情報を作る
		/// </summary>
		/// <param name="a_ownerWorld">エフェクトが付いている相手のワールド行列</param>
		/// <returns>今出していないパーツなら false(描画しない)</returns>
		static bool BuildMeshDraw(
			const Resource::EffectAsset& a_asset,
			size_t a_index,
			const EffectInstance& a_inst,
			const Math::Matrix& a_ownerWorld,
			Math::Matrix& a_outWorld,
			Math::Color& a_outColorScale,
			Math::Vector3& a_outEmissiveAdd);

		/// <summary>
		/// ライトパーツの今フレームの値を作る
		/// </summary>
		/// <param name=a_effectWorld>エフェクトの置き場の行列(上書きの位置・向き・大きさ込み)</param>
		/// <param name=a_rangeScale>届く距離に掛ける倍率(エフェクト全体の大きさ)</param>
		/// <returns>今出していないパーツなら false(ライトを返す)</returns>
		static bool BuildLightDraw(
			const Resource::EffectAsset& a_asset,
			size_t a_index,
			const EffectInstance& a_inst,
			const Math::Matrix& a_effectWorld,
			float a_rangeScale,
			Graphics::PointLight& a_outLight);

		/// <summary>
		/// トレイルパーツの今フレームの点と見た目を作る
		/// </summary>
		/// <param name=a_effectWorld>エフェクトの置き場の行列(上書きの位置・向き・大きさ込み)</param>
		/// <param name=a_widthScale>幅に掛ける倍率(エフェクト全体の大きさ)</param>
		/// <returns>今引いていないパーツなら false(トレイルを手放す)</returns>
		static bool BuildTrailPoint(
			const Resource::EffectAsset& a_asset,
			size_t a_index,
			const EffectInstance& a_inst,
			const Math::Matrix& a_effectWorld,
			float a_widthScale,
			Math::Vector3& a_outPos,
			TrailStyle& a_outStyle);

	private:

		// 止めたあとに動くパーツ(OnStop)が、全部出し終わったか(止めている最中を終えてよいか)
		static bool IsStopPartsDone(const Resource::EffectAsset& a_asset, const EffectInstance& a_inst);

		/// <summary>
		/// 借りている声を、今のアセットの指定に合わせ直す
		/// </summary>
		/// <remarks>
		/// 食い違っている声だけ返す(鳴らし直しは次に鳴らす番が来たときに、新しい指定で借りる)。
		/// 毎フレーム呼んでよい。エディターで音や 3D 指定を差し替えたときの受け口
		/// </remarks>
		static void SyncSoundInstances(
			const Resource::EffectAsset& a_asset,
			Engine::Audio::AudioManager& a_audioManager,
			EffectInstance& a_inst);
	};
}
