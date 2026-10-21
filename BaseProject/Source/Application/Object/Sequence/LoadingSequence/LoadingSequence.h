#pragma once

#include "../../../../Engine/GameObject/BaseObject/BaseObject.h"

namespace App::Object
{
	/// <summary>
	/// ロード画面の進行役 : ロード画面のシーンに一つ置く
	/// </summary>
	/// <remarks>
	/// ロード画面のシーンは SceneManager がスタックの外で常駐させ、
	/// 読み込みが長引いたときだけ一番上に重ねて出す(SceneManager::SetLoadingScreen)。
	///
	/// ここが持つのは「読み込みの進み具合をどのゲージへ流すか」だけ。
	/// 背景の画像は UIElement、バーの見た目は UIGauge 側が持つ。
	/// ゲージは値の取り元を Manual にしておくこと(それ以外だと毎フレーム上書きされる)。
	/// </remarks>
	class LoadingSequence : public Engine::GameObject::BaseObject
	{
	public:

		// 更新処理 : 読み込みの進み具合をゲージへ流す
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "LoadingSequence"; }

		// インスペクター : 対象のゲージの設定
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		//-------------------------------------------------------------------
		// 設定(保存される)
		//-------------------------------------------------------------------
		// 進み具合を流すゲージ(同じシーンに置いた UIGauge のGUID)
		Core::GUID m_gaugeGUID = {};

		//-------------------------------------------------------------------
		// 状態(保存しない)
		//-------------------------------------------------------------------
		// 最後に流した値(インスペクターの表示用)
		float m_progress = 0.0f;

		// 指定されたGUIDがゲージ以外だったと知らせたか(毎フレーム出さないように)
		bool m_isWarned = false;
	};
}
