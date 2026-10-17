#pragma once

#include "Engine/EditorField/EditorField.h"
#include "Engine/Audio/AudioManager.h"

namespace App::Component
{
	//==========================================================================================
	// ChargeDashSoundComponent
	//
	// チャージダッシュの溜めの音。ChargeDashComponent と同じエンティティ(機体)に付ける。
	//
	//   溜めている間         … chargingSound(単発。ジェットの回転が上がって大きくなっていく音)
	//   溜まりきった瞬間     … chargedSound(単発。「チャキン」という機械の金属音)
	//   溜まりきって押している間 … readySound(ループ。回りきったジェットを噴かし続ける音)
	//
	// 溜まりきる前に離した・撃ち出した・溜められなくなったときは全部止める。
	// 鳴らすのは ChargeDashSoundSystem。自分の機体の音なので 2D で鳴らす。
	//
	// ・溜め時間(ChargeDashComponent.chargeTime)は手触りで変わるので、溜めの音は単発にして
	//   溜まりきった時点で止める(長さが合わなくても、溜まった音へ切り替わる)。
	//   既定の音は溜め時間 0.5 秒に合わせて作ってある。
	// ・同じ型は1つしか付けられないので、エフェクトのサウンドパーツではなく音を直接持つ
	//   (機体自身の EffectAssetComponent はブーストの音が使っている)。
	// ・借りたサウンドインスタンスは消えるときに返す(ComponentTraits::Release)。
	//==========================================================================================
	struct ChargeDashSoundComponent
	{
		// ---- 設定(保存される) ----
		// 既定は Asset/Sound/Player/ChargeDash_{Charging,Charged,Ready}(環境/gen_charge_dash_sounds.py で作ったもの)
		Core::GUID chargingSoundGUID = Core::GUID("9bab18fc-261d-453c-9562-844e28ee9286");	// 溜めている間(単発)
		Core::GUID chargedSoundGUID  = Core::GUID("3e61a0c5-8eec-49e0-b787-13e5c462adf7");	// 溜まりきった瞬間(単発)
		Core::GUID readySoundGUID    = Core::GUID("55a7e151-4bf7-44c1-8fd6-9d7938ac9541");	// 溜まりきって押している間(ループ)

		float chargingVolume = 0.35f;
		float chargedVolume  = 0.45f;
		float readyVolume    = 0.25f;

		// ---- ランタイム(保存しない) ----
		// 今鳴らしている段階。0 : 鳴らしていない / 1 : 溜めている / 2 : 溜まりきった
		uint32_t stage = 0;

		Engine::Handle<Engine::Resource::SoundInstance> chargingHandle = {};
		Engine::Handle<Engine::Resource::SoundInstance> chargedHandle  = {};
		Engine::Handle<Engine::Resource::SoundInstance> readyHandle    = {};
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::ChargeDashSoundComponent>
{
	//----------------------------------------------------------------------------------
	// 借りているサウンドインスタンスを返す
	//
	// プールはアプリ寿命なので、消えるとき・外すとき・初期化へ入り直すときに必ず返す
	//----------------------------------------------------------------------------------
	static void Release(void* a_pData, const Engine::ECS::EngineServices& a_services)
	{
		App::Component::ChargeDashSoundComponent& _comp = Engine::EditorField::RefValue<App::Component::ChargeDashSoundComponent>(a_pData);
		if (a_services.pAudioManager)
		{
			a_services.pAudioManager->ReleaseSoundInstance(_comp.chargingHandle);
			a_services.pAudioManager->ReleaseSoundInstance(_comp.chargedHandle);
			a_services.pAudioManager->ReleaseSoundInstance(_comp.readyHandle);
		}
		_comp.chargingHandle = {};
		_comp.chargedHandle  = {};
		_comp.readyHandle    = {};
		_comp.stage = 0;
	}

	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::ChargeDashSoundComponent& _comp = Engine::EditorField::RefValue<App::Component::ChargeDashSoundComponent>(a_pData);
		a_ar.GUIDField("chargingSoundGUID", _comp.chargingSoundGUID);
		a_ar.GUIDField("chargedSoundGUID", _comp.chargedSoundGUID);
		a_ar.GUIDField("readySoundGUID", _comp.readySoundGUID);
		a_ar.Field("chargingVolume", _comp.chargingVolume);
		a_ar.Field("chargedVolume", _comp.chargedVolume);
		a_ar.Field("readyVolume", _comp.readyVolume);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::ChargeDashSoundComponent& _comp = Engine::EditorField::RefValue<App::Component::ChargeDashSoundComponent>(a_context.pData);
		const auto& _services = *a_context.pWorld->RefEngineServices();

		// 差し替えたら次に鳴らすときに借り直す(Release と同じく返しておく)
		bool _isChanged = false;
		_isChanged |= Engine::EditorField::AssetField(_services, "Charging (単発)", "Sound", _comp.chargingSoundGUID);
		Engine::EditorField::Tooltip("溜めている間に鳴らす。溜まりきったら止まる");
		Engine::EditorField::Field("ChargingVolume", _comp.chargingVolume, 0.01f, 0.0f);
		_isChanged |= Engine::EditorField::AssetField(_services, "Charged (単発)", "Sound", _comp.chargedSoundGUID);
		Engine::EditorField::Tooltip("溜まりきった瞬間に鳴らす");
		Engine::EditorField::Field("ChargedVolume", _comp.chargedVolume, 0.01f, 0.0f);
		_isChanged |= Engine::EditorField::AssetField(_services, "Ready (ループ)", "Sound", _comp.readySoundGUID);
		Engine::EditorField::Tooltip("溜まりきってから離すまで鳴らし続ける");
		Engine::EditorField::Field("ReadyVolume", _comp.readyVolume, 0.01f, 0.0f);

		if (_isChanged)
		{
			Release(a_context.pData, _services);
		}

		// ランタイムは表示のみ
		static const char* STAGE_NAMES[] = { "Idle", "Charging", "Charged" };
		Engine::EditorField::Value("Stage", "%s", STAGE_NAMES[std::min<uint32_t>(_comp.stage, 2)]);
	}
};
