#pragma once

#include "Application/Object/UI/Decoration/Decoration.h"

namespace Engine
{
	class MainEngine;
}

namespace Engine::Graphics
{
	class GraphicsEngine;
}

namespace App::Game
{
	class UserData;

	/// <summary>
	/// 画面の右上に出す FPS の表示
	/// </summary>
	/// <remarks>
	/// 設定画面(ゲーム → FPS Display)で出し入れする、プレイヤー向けの確認用の表示。
	/// デバッグキー(F7)の情報の表示とは別物で、デバッグキーを切っていても作る。
	/// 出すかどうかは UserData::GameSettings::isFPSVisible を毎フレーム読む。
	/// HUD の表示を切っていても出す(HUD とは別に選べる項目のため)
	/// </remarks>
	class FPSDisplay
	{
	public:

		FPSDisplay();
		~FPSDisplay();
		NON_COPYABLE_NON_MOVABLE(FPSDisplay);

		// 初期化 : 板と文字を作り、フォントを借りる
		void Init(Engine::MainEngine& a_engine);

		// フォントの参照を返す。リソースの解放より前に呼ぶこと
		void Release();

		/// <summary>
		/// 出すかどうかを読み、出すなら数字を取り直す : 毎フレーム呼ぶ
		/// </summary>
		/// <param name="a_pUserData">出すかどうかの設定(無ければ出さない)</param>
		void Update(const UserData* a_pUserData);

		// 板と文字を積む : シーンの UI を積み終えた後に呼ぶ
		void SubmitUI(Engine::Graphics::GraphicsEngine* a_pGraphicsEngine) const;

	private:

		bool m_isVisible = false;
		float m_refreshTimer = 0.0f;		// 次に数字を取り直すまでの残り(倍率を掛けない秒)

		Object::Decoration::Decoration m_background = {};	// 下敷きの板
		Object::Decoration::Decoration m_text = {};			// 数字

		// エンジン(借り物)
		Engine::MainEngine* m_pEngine = nullptr;
	};
}
