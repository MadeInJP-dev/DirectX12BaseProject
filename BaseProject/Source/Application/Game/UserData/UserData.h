#pragma once
#include "../Core/InputSettings.h"

namespace Engine
{
	class MainEngine;
}

namespace Engine::Audio
{
	class AudioManager;
}

namespace App::Game
{
	class UserData
	{
	public:

		// 各ステージごとの記録
		struct StageRecord
		{
			bool cleard = false;
			float bestTime = FLT_MAX;
			uint32_t bestScore = 0;
		};

		// ---- 音量設定 ----
		// 0〜SOUND_VOLUME_MAX。作り手の音量(エンジンの AudioOption)へさらに掛かる
		static constexpr uint32_t SOUND_VOLUME_MAX = 100;

		struct SoundSettings
		{
			// マスター音量
			uint32_t mainVol = 50;

			// 各種音量
			uint32_t bgmVol = 100;
			uint32_t uiVol = 100;
			uint32_t seVol = 100;
		};

		// ---- 画面設定 ----
		// 明るさ(%)の範囲。作り手の露出へ掛ける倍率になる(100 で作り手の値のまま)
		static constexpr uint32_t BRIGHTNESS_MIN = 50;
		static constexpr uint32_t BRIGHTNESS_MAX = 150;

		// 画面の揺れの強さ(%)の上限
		static constexpr uint32_t SCREEN_SHAKE_MAX = 100;

		struct GraphicsSettings
		{
			uint32_t brightness = 100;		// 明るさ(%)
			bool isBloomEnable = true;		// 切ると作り手が入れていてもブルームを掛けない
			uint32_t screenShake = 100;		// 画面の揺れの強さ(%)。揺らす側がこれを掛ける
			bool isHUDVisible = true;		// HUD(IsHUD の付いた UI)を出すか

			// ウィンドウモード。未選択(nullopt)なら作り手の値(エンジンの WindowOption)のまま
			std::optional<Engine::EWindowMode> opWindowMode = std::nullopt;
		};

		// ---- ゲーム設定 ----
		struct GameSettings
		{
			// FPS の上限。未選択(nullopt)なら作り手の値(エンジンの WindowOption)のまま
			std::optional<Engine::EFPSRate> opFPSRate = std::nullopt;
		};

		//------------------------------------------------------------------------------
		// 保存先
		//------------------------------------------------------------------------------
		// 読み書きする場所はこの3つだけが持つ。設定画面からも保存するため、
		// 呼ぶ側にパスを配らずに Load/Save を通させる。
		static constexpr const char* FILE_DIR  = "Asset/Data/User";
		static constexpr const char* FILE_NAME = "UserData";
		static constexpr const char* FILE_EXT  = "data";

		// ファイルからの復元 / ファイルへの保存
		void Load();
		void Save();

		// ユーザーデータすべてのアーカイブ処理
		void Archive(Engine::Persistence::Archive& a_ar);

		/// <summary>
		/// 音量設定を鳴らす側へ流し込む
		/// </summary>
		/// <remarks>
		/// プレイヤーの音量として入れる(作り手の音量へ掛け合わされる)。
		/// 起動時と、設定画面で変えた瞬間に呼ぶ
		/// </remarks>
		void ApplySoundSettings(Engine::Audio::AudioManager& a_audioManager) const;

		// 画面設定を流し込む(明るさ・ブルーム・ウィンドウモード)。HUD と揺れは使う側が読む
		void ApplyGraphicsSettings(Engine::MainEngine& a_engine) const;

		// ゲーム設定を流し込む(FPS の上限)
		void ApplyGameSettings(Engine::MainEngine& a_engine) const;

		// 視点の設定を流し込む(感度・上下反転)。キーの割り当ては InputActionManager が流す
		void ApplyLookSettings(Engine::MainEngine& a_engine) const;

		// 全部を流し込む : 起動時に一度
		void ApplyAll(Engine::MainEngine& a_engine) const;

		// アクセサ
		SoundSettings& RefDoundSettings() { return m_soundSettings; }
		const SoundSettings& GetSoundSettings() const { return m_soundSettings; }
		GraphicsSettings& RefGraphicsSettings() { return m_graphicsSettings; }
		const GraphicsSettings& GetGraphicsSettings() const { return m_graphicsSettings; }
		GameSettings& RefGameSettings() { return m_gameSettings; }
		const GameSettings& GetGameSettings() const { return m_gameSettings; }
		InputSettings& RefInputSettings() { return m_inputSettings; }
		const InputSettings& GetInputSettings() const { return m_inputSettings; }

	private:

		// ゲームデータ
		void GameDataArchive(Engine::Persistence::Archive& a_ar);

		// サウンド設定
		void SoundArchive(Engine::Persistence::Archive& a_ar);

		// 入力設定データ
		void InputArchive(Engine::Persistence::Archive& a_ar);

		// 画面設定
		void GraphicsArchive(Engine::Persistence::Archive& a_ar);

		// ゲーム設定
		void GameSettingsArchive(Engine::Persistence::Archive& a_ar);

	private:

		SoundSettings m_soundSettings;
		InputSettings m_inputSettings;
		GraphicsSettings m_graphicsSettings;
		GameSettings m_gameSettings;

	};
}