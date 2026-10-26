#pragma once
#include "Editor/EditorCommon.h"
#include "../../../Widget/PopUp/Popup.h"
#include "../../../Widget/PopUp/NameInputPopup/NameInputPopup.h"

namespace Engine::ECS
{
	struct EngineServices;
}

namespace Editor
{
	//======================================================================================
	//
	// シーンファイルの操作(新規・開く・上書き保存・名前を付けて保存)
	//
	// メインメニューの File と Ctrl+S から使う。
	// 以前はシーンビューのメニューにあったので、シーンビューが裏のタブに回っていると
	// Ctrl+S が効かなかった。どのパネルを見ていても同じように保存できるよう、ここへ分けた。
	//
	// 保存先は「いま積まれている一番上のシーン」から毎フレーム取り直す。
	// シーンはゲーム側からも切り替わる(タイトル→ホーム など)ので、
	// 一度覚えたきりだと Ctrl+S で別のシーンのファイルへ上書きしてしまうため。
	//
	//======================================================================================
	class SceneFileMenu
	{
	public:

		SceneFileMenu();
		~SceneFileMenu() = default;
		NON_COPYABLE_NON_MOVABLE(SceneFileMenu);

		// a_pServices : アプリ寿命のサービス一式(借り物)
		void Init(ECS::EngineServices* a_pServices);

		// 毎フレームの更新 : 保存先の取り直しと Ctrl+S。メニューを開いていなくても呼ぶ
		void Update();

		// File メニューの中身(BeginMenu の中から呼ぶ)
		void DrawMenuItems();

		// ポップアップ(メニューの外、毎フレーム呼ぶ)
		void DrawPopups();

		// いま開いているシーンの名前(メニューバーに出す)。無ければ空
		std::string GetCurrentSceneName() const;

	private:

		// 保存先のシーンを書き出す
		void SaveScene(const Core::GUID& a_guid);

		// 名前を付けて保存 / 新規作成の決定
		bool SaveSceneAs(const std::string& a_name);
		bool CreateScene(const std::string& a_name);

		// シーンを開く一覧の中身
		void DrawOpenSceneContent();

	private:

		// アプリ寿命のサービス一式(借り物)
		ECS::EngineServices* m_pServices = nullptr;

		// 保存先(いま開いているシーン)
		Core::GUID m_currentSceneGUID = Core::DEFAULT_GUID;
		bool m_canOverwrite = false;	// 保存先のファイルがあるか

		//----------------------------------------------------------------------------------
		// ポップアップ
		//----------------------------------------------------------------------------------
		Popup m_openPopup;				// 一覧から開く
		NameInputPopup m_saveAsPopup;	// 名前を付けて保存
		NameInputPopup m_createPopup;	// 空のシーンを作って開く
	};
}
