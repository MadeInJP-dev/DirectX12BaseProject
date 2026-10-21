#pragma once

#include "../../../../Engine/GameObject/BaseObject/BaseObject.h"

#include "../../SequenceBgm.h"

namespace Engine::Scene
{
	class SceneManager;
}

namespace App::Object
{
	/// <summary>
	/// タイトル画面の進行役 : シーンに一つ置く
	/// </summary>
	/// <remarks>
	/// UIButton は「押された」ことを知らせるだけで、押されて何をするかは持たない。
	/// その差し込み先をここが引き受ける。
	///
	///   ・シーン上の UIButton を GUID で探し、押されたらシーンを切り替える処理を差し込む
	///   ・タイトルの間はカーソルの中央固定を切る(固定したままだとカーソルを動かせない)
	///
	/// ボタンの見た目・位置は UIButton 側、背景は UIElement 側が持つ。
	/// ここが持つのは「どのボタンを押したらどのシーンへ行くか」だけ。
	/// </remarks>
	class TitleSequence : public Engine::GameObject::BaseObject
	{
	public:

		// 更新処理 : ボタンへの差し込みと、カーソル固定の解除
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// 解放処理 : カーソル固定を設定値へ戻す
		void Release(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "TitleSequence"; }

		// インスペクター : 対象ボタンと遷移先の設定
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		// ボタンへ押下時の処理を差し込む(まだ見つからなければ何もしない)
		void TryBindButton(Engine::GameObject::ObjectContext& a_context);

		// 遷移先のシーンを読み込む
		void ReserveChangeScene(Engine::Scene::SceneManager& a_sceneManager);

	private:

		//-------------------------------------------------------------------
		// 設定(保存される)
		//-------------------------------------------------------------------
		// 押したらシーンを切り替えるボタン(同じシーンに置いた UIButton のGUID)
		Core::GUID m_playButtonGUID = {};

		// 遷移先のシーン
		Core::GUID m_nextSceneGUID = {};

		// タイトルの間はカーソルの中央固定を切るか。
		// 固定したままだとカーソルが毎フレーム中央へ戻され、ボタンを狙えない
		bool m_isReleaseCursorLock = true;

		//-------------------------------------------------------------------
		// 状態(保存しない)
		//-------------------------------------------------------------------
		// 差し込み済みか。ボタンは同じシーンに居るので普通は初回で見つかるが、
		// 読み込み順に依存しないよう、見つかるまで毎フレーム試す
		bool m_isBound = false;

		// 二重に遷移要求を出さないための印
		bool m_isSceneRequested = false;

		//-------------------------------------------------------------------
		// BGM
		//-------------------------------------------------------------------
		SequenceBgm m_bgm = {};
	};
}
