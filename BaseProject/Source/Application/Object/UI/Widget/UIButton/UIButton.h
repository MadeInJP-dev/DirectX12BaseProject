#pragma once

#include "Application/Object/UI/UIBase.h"

namespace App::Object
{
	/// <summary>
	/// 押せるUI : タイトル・ホーム・リザルトなど、ボタンが要る場所ならどこでも使える
	/// </summary>
	/// <remarks>
	/// 役割は「押されたことを知らせる」ところまでで、押されて何をするかは持たない。
	/// 実際の処理は SetOnClick で外から差し込む。こうしておけば、シーンを増やすたびに
	/// ボタンを継承して作り直す必要が無くなる。
	///
	///     _pButton->SetOnClick([]() { シーンを切り替える等 });
	///
	/// ・カーソルの判定・押下の進行・音・状態(Normal/Hovered/Pressed/Disabled)は
	///   すべて UIBase が持っている。ここに残っているのは差し込み口だけ。
	///   見た目の変化は飾り側(Decoration の Reaction)で作る。
	///
	/// ・押した/乗っている といった状態は UIBase の IsHovered / IsPressed / IsClicked
	///   でも取れるので、コールバックを使わずに呼び出し側で毎フレーム見に行く形でもよい。
	///
	/// ※ カーソルを画面中央へ固定する設定(InputOption)が入っていると、カーソルは毎フレーム
	///    中央へ戻されるため中央のボタンしか押せない。メニューを出す場面では固定を切ること。
	/// </remarks>
	class UIButton final : public UIBase
	{
	public:

		//=======================================================================
		// ふるまいの差し替え
		//=======================================================================

		/// <summary>
		/// 押されたときに呼ぶ処理を設定する
		/// </summary>
		/// <param name="a_callback">押し切られた瞬間に1回だけ呼ばれる</param>
		void SetOnClick(std::function<void()> a_callback)
		{
			m_onClick = std::move(a_callback);
		}

		/// <summary>
		/// 設定した処理を外す
		/// </summary>
		void ClearOnClick() { m_onClick = nullptr; }

		//=======================================================================
		// オブジェクト
		//=======================================================================

		// 更新処理 : 押し切られていたら差し込まれた処理を呼ぶ
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "UIButton"; }

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		// 押されたときに呼ぶ処理(設定されていなければ何もしない)
		std::function<void()> m_onClick = nullptr;
	};
}
