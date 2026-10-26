#pragma once

namespace Engine::Audio
{
	class AudioManager;
}

namespace Engine::Scene
{
	//======================================================================================
	// シーンの音量
	//
	// 作り手が決める音量のつり合い(マスター + グループごと)を、
	// シーン(BaseScene)の持ち物として持つ。シーンと一緒に保存される。
	//
	// ・実際に鳴る音量
	//     鳴らす側が指定した音量 × シーンの音量 × プレイヤーの音量(設定画面)
	//   ホーム画面は BGM を大きめ、戦闘は効果音を抑えめ、といった具合にシーンごとに決められる。
	//
	// ・どのシーンの音か
	//     シーンは開くときに AudioManager の席を1つ取って音量を預ける。
	//     シーンの更新の間はその席を張っておき(BaseScene が VolumeScopeGuard で張る)、
	//     そこで発行された音にはその席の札が付く。
	//     重ねたシーン(ポーズ画面)の音は自分の音量で、後ろのゲームの音は後ろの音量で鳴る。
	//
	// ・値の実体は保存側(ここ)。読み込んだ後とエディターで動かした後に席へ流し込む
	//======================================================================================
	class SceneAudio
	{
	public:

		SceneAudio() = default;
		~SceneAudio() = default;
		NON_COPYABLE_NON_MOVABLE(SceneAudio);

		//----------------------------------------------------------------------------------
		// 席の出し入れ(BaseScene の Enter / Exit から呼ぶ)
		//----------------------------------------------------------------------------------
		// a_pAudioManager : 席を取る先(借り物)。nullptr なら何もしない
		void Register(Audio::AudioManager* a_pAudioManager);
		void Unregister();

		// 発行元と席 : シーンの更新の間に張る(VolumeScopeGuard)
		Audio::AudioManager* RefAudioManager() { return m_pAudioManager; }
		Audio::VolumeScopeID GetVolumeScopeID() const { return m_volumeScopeID; }

		//----------------------------------------------------------------------------------
		// 保存・読み込み : 読み込んだら席へ流し込む
		//----------------------------------------------------------------------------------
		void Archive(Persistence::Archive& a_ar);

		//----------------------------------------------------------------------------------
		// 編集UI(エディターの SceneSettingsPanel から呼ばれる)。動かした瞬間に効く
		//----------------------------------------------------------------------------------
		void DrawEdit();

		const Audio::SoundVolume& GetVolume() const { return m_volume; }

	private:

		// 音量を席へ流し込む(鳴っている音にもその場で効く)
		void Apply();

	private:

		//-------------------------------------------------------------------
		// 設定(保存される)
		//-------------------------------------------------------------------
		Audio::SoundVolume m_volume = {};

		//-------------------------------------------------------------------
		// 状態(保存しない)
		//-------------------------------------------------------------------
		// 席を取った先(借り物) : 持ち主は MainEngine。シーンより後に消える
		Audio::AudioManager* m_pAudioManager = nullptr;
		Audio::VolumeScopeID m_volumeScopeID = Audio::INVALID_VOLUME_SCOPE_ID;
	};
}
