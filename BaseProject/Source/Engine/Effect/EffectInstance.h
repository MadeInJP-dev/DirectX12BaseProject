#pragma once

#include "Engine/Resource/Data/EffectAsset/EffectAsset.h"	// パーツ数の上限(EFFECT_*_MAX)

namespace Engine::Audio
{
	class AudioManager;
}

namespace Engine::Graphics
{
	struct PointLight;
	class LightManager;
}

namespace Engine::Effect
{
	class TrailNode;
	class TrailNodePool;

	//==========================================================================================
	// EffectInstance
	//
	// エフェクトを再生するものが1つずつ持つ、実行中の値(コンポーネントに持たせる)。
	// アセット(EffectAsset)は全員で共有する設計図なので、進み具合や借りている声はこちらが持つ。
	// 進めるのは EffectPlayer。
	//
	// ※ 以前は EffectAsset.h(Resource 層)にあった。データと実行を分けるために移した
	//==========================================================================================
	struct EffectInstance
	{
		bool  isPlaying = false;	// 再生中か
		float elapsed = 0.0f;		// 再生開始からの経過時間(秒)

		//------------------------------------------------------------------
		// 止めている最中か
		//
		// 止めたあとに動くパーツ(OnStop : 消火の火花・終了音など)を持つエフェクトは、
		// 止めてからそれらが出し終わるまでのあいだ、ここが立つ。
		// このあいだは再生中ではない(isPlaying = false)が、時間は stopElapsed で進む
		//------------------------------------------------------------------
		bool  isStopping = false;
		float stopElapsed = 0.0f;	// 止めてからの経過時間(秒)

		// 時間を進める必要があるか(再生中か、止めている最中か)
		bool IsActive() const { return isPlaying || isStopping; }

		// パーティクルパーツごとの進行状態
		float rateAccum[Resource::EFFECT_PARTICLE_MAX] = {};	// 連続発生の端数繰り越し
		int   pendingEmit[Resource::EFFECT_PARTICLE_MAX] = {};	// このフレームの発生数(Update が積み、Draw が消費)
		bool  wasEmitting[Resource::EFFECT_PARTICLE_MAX] = {};	// バーストの立ち上がり検出用

		//------------------------------------------------------------------
		// サウンドパーツごとの進行状態
		//
		// ハンドルは「借りている声」なので Reset() では消さないこと。
		// 消すと返却先が分からなくなって、鳴りっぱなしの声がプールに残る。
		// 発行は EffectPlayer::CreateSoundInstances / 返却は ReleaseSounds の担当
		//------------------------------------------------------------------
		Handle<Resource::SoundInstance> soundHandles[Resource::EFFECT_SOUND_MAX] = {};
		bool  soundTriggered[Resource::EFFECT_SOUND_MAX] = {};	// もう鳴らしたか(単発を1回だけにする)
		Math::Vector3 soundPos = { 0.0f, 0.0f, 0.0f };			// 3D 指定のパーツを鳴らす位置

		// その声を何から発行したか。
		// アセット側の指定と食い違っていたら作り直す(EffectPlayer の SyncSoundInstances)。
		// エディターで音や 3D 指定を差し替えたとき、
		// すでに出ているエフェクトにも次のフレームから効かせるためのもの
		Core::GUID soundSourceGUID[Resource::EFFECT_SOUND_MAX] = {};
		bool         soundSource3D[Resource::EFFECT_SOUND_MAX] = {};

		//------------------------------------------------------------------
		// ライトパーツごとの、借りているポイントライト
		//
		// 出している間だけ借りる(EffectDrawSystem)。出す時間帯から外れたら返す。
		// エンティティが消えるときは EffectRuntimeComponent の Release が返す
		//------------------------------------------------------------------
		Handle<Graphics::PointLight> lightHandles[Resource::EFFECT_POINTLIGHT_MAX] = {};

		//------------------------------------------------------------------
		// トレイルパーツごとの、借りている点の履歴(TrailNode)
		//
		// 出している間だけ借りる(EffectDrawSystem)。出す時間帯から外れたら手放す。
		// 手放しても点はプールに残り、寿命で消えきってから返る(帯が急に消えない)。
		// エンティティが消えるときは EffectRuntimeComponent の Release が手放す
		//------------------------------------------------------------------
		Handle<TrailNode> trailHandles[Resource::EFFECT_TRAIL_MAX] = {};

		// 頭から再生し直す
		void Reset()
		{
			elapsed = 0.0f;
			isStopping = false;
			stopElapsed = 0.0f;
			for (size_t _i = 0; _i < Resource::EFFECT_PARTICLE_MAX; ++_i)
			{
				rateAccum[_i] = 0.0f;
				pendingEmit[_i] = 0;
				wasEmitting[_i] = false;
			}
			for (size_t _i = 0; _i < Resource::EFFECT_SOUND_MAX; ++_i)
			{
				soundTriggered[_i] = false;
			}
		}

		//------------------------------------------------------------------
		// 発行済みインスタンスに対しての操作
		//
		// どれもアセットの中身を見ないので、
		// アセットが読めていない・破棄された後でも呼べる(AudioBehaviorInstance と同じ)
		//------------------------------------------------------------------

		// 3D再生の位置を更新する。鳴っている音にも即時反映される
		void SetSoundPos(Engine::Audio::AudioManager& a_audioManager, const Math::Vector3& a_pos);

		// 発行済みインスタンスをすべて返却して空にする
		void ReleaseSounds(Engine::Audio::AudioManager& a_audioManager);

		// 借りているポイントライトをすべて返す
		void ReleaseLights(Engine::Graphics::LightManager& a_lightManager);

		// 借りているトレイルをすべて手放す(残りの点は消えきるまで描かれる)
		void ReleaseTrails(TrailNodePool& a_trailNodePool);
	};
}
