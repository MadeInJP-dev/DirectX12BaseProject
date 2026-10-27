#pragma once

#include "Application/Object/UI/UIBase.h"

namespace App::Object
{
	/// <summary>
	/// ラジオボタン : 並んだ選択肢のうち1つだけが「選ばれている」押せる UI
	/// </summary>
	/// <remarks>
	/// 押されたことを知らせるのは UIButton と同じ(IsClicked)。違うのは「選ばれている」を持ち、
	/// 選ばれている間だけ出す飾り・選ばれていない間だけ出す飾りを切り替えるところ。
	///
	///   SelectedDecorations   … 選ばれている間だけ出す(塗りの帯・明るい文字など)
	///   UnselectedDecorations … 選ばれていない間だけ出す(暗い文字など)
	///   どちらにも入れない飾りは常に出す(背景・乗ったときの枠など)
	///
	/// ・どれを選ばれているにするかは持ち主が決める(SetSelected)。押されても自分では切り替えない。
	///   並んだ選択肢をまとめて扱うときは UIRadioGroup を使う。
	/// ・選ばれている状態は保存しない(値の実体は持ち主の設定にあるので、毎フレーム入れ直す)
	/// </remarks>
	class UIRadioButton final : public UIBase
	{
	public:

		//=======================================================================
		// 選ばれているか
		//=======================================================================

		bool IsSelected() const { return m_isSelected; }
		void SetSelected(bool a_isSelected) { m_isSelected = a_isSelected; }

		//=======================================================================
		// オブジェクト
		//=======================================================================

		// 更新処理 : 選ばれているかで飾りを出し分ける
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ : 出し分ける飾りの番号
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "UIRadioButton"; }

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		// 選ばれているかで飾りを出し分ける(両方の組へ)
		void ApplySelectedVisible();

		// 飾りの番号の並びのインスペクター
		void DrawDecorationIdList(const char* a_label, std::vector<uint32_t>& a_inoutIdVec);

	private:

		//------------------------------------------------------------------------------
		// 設定(保存される)
		//------------------------------------------------------------------------------
		std::vector<uint32_t> m_selectedDecorationIdVec = {};		// 選ばれている間だけ出す飾り
		std::vector<uint32_t> m_unselectedDecorationIdVec = {};		// 選ばれていない間だけ出す飾り

		//------------------------------------------------------------------------------
		// 状態(保存しない)
		//------------------------------------------------------------------------------
		bool m_isSelected = false;
	};
}
