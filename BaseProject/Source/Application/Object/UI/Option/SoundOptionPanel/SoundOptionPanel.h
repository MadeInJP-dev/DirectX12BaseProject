#pragma once

#include "Application/Object/UI/Option/ScrollPanelBase.h"

#include "../../../../Game/UserData/UserData.h"

namespace App::Object
{
	//===============================================================================================
	// 設定画面のサウンド設定
	//
	// 配置と記憶のみ
	// スクロール処理はベースクラスに任せる
	// 描画範囲は親に任せる
	//
	// ・マスター・BGM・SE・UI の音量を、ゲージと上げ下げのボタンの行で並べる。
	// ・値の実体は UserData(SoundSettings, 0〜100)。ここが持つのは表示用の写しで、開いたときに読み直す。
	// ・変えた瞬間に UserData へ書き、鳴らす側(AudioManager)へも流し込むので、その場で音が変わる。
	//   プレイヤーの音量は作り手の音量(エンジンの AudioOption)へ掛け合わされる。
	//===============================================================================================
	class SoundOptionPanel final : public ScrollPanelBase
	{
	public:

		// パネルの名前(保存の区切りにも使うので変えないこと)
		const char* GetPanelName() const override { return "Sound"; }

		// 開かれたとき : 音量の写しを UserData から読み直す
		void OnOpen(Engine::GameObject::ObjectContext& a_context) override;

	protected:

		// 並べる : 見出し → 音量の行(マスター・BGM・SE・UI)
		void LayoutItems(Engine::GameObject::ObjectContext& a_context) override;

		// 項目の操作
		void UpdateItems(Engine::GameObject::ObjectContext& a_context) override;

		// 保存
		void ArchiveItems(Engine::Persistence::Archive& a_ar) override;

		// インスペクター
		void DrawItemsInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		// 音量の種類 : 行の並びと保存のキーの順
		enum class EVolumeKind : uint32_t
		{
			Master,		// 全部の音
			Bgm,		// 音楽
			Se,			// 効果音
			Ui,			// UI の操作音
			Count
		};

		static constexpr size_t VOLUME_KIND_COUNT = static_cast<size_t>(EVolumeKind::Count);

		// 1回押したときに動く量(0〜100 のうち)
		static constexpr uint32_t VOLUME_STEP = 5;

		// 種類ごとの値の置き場所(SoundSettings のどのメンバか)
		static uint32_t& RefVolume(Game::UserData::SoundSettings& a_settings, EVolumeKind a_kind);

		// 音量を書き込む : 範囲に詰めて UserData へ入れ、鳴らす側へ流し込む
		void ApplyVolume(Engine::GameObject::ObjectContext& a_context, EVolumeKind a_kind, int a_volume);

	private:

		// 表示用の写し(実体は UserData)
		Game::UserData::SoundSettings m_settings = {};

		OptionItemUI m_header = {};		// 見出し(文字だけの UI)。未設定なら行を取らない

		// 音量の行(EVolumeKind の並び)
		std::array<OptionStepperUI, VOLUME_KIND_COUNT> m_volumeStepperArray = {};
	};
}
