#pragma once

// 切り抜きの矩形を受け取る
#include "Engine/Graphics/Frame/DrawList/UIData.h"

namespace Engine::GameObject
{
	struct ObjectContext;
}

namespace App::Object
{
	//===============================================================================================
	// ラジオボタンの組 : 並んだ UIRadioButton のうち1つだけを選ばれているにする
	//
	// 値で持つ部品(NumericMutate と同じ形)。何の値かは知らず、選択肢の並びの添え字だけを扱う。
	// 持ち主は毎フレーム今の添え字を Update に渡し、押された添え字を受け取って自分の値を書き換える。
	//
	//     const int _chosen = m_radio.Update(a_context, _isOn ? 0 : 1);
	//     if (_chosen >= 0) _isOn = (_chosen == 0);
	//
	// ・見た目はシーンに置いた UIRadioButton で作り、ここは GUID で指す(並び = 選択肢の並び)。
	// ・置く場所は持ち主が行の左上と大きさで渡す。各ボタンの横の位置はそこからのずれ(OffsetX)
	//===============================================================================================
	class UIRadioGroup
	{
	public:

		/// <summary>
		/// 並べる : 行の左上と大きさから、各ボタンを置く
		/// </summary>
		/// <param name="a_rowTopLeft">行の左上(画面の座標, px)</param>
		/// <param name="a_rowSize">行の大きさ(px)。ピボットのずらしに使う</param>
		/// <param name="a_pClip">切り抜く矩形(無ければ切り抜かない)</param>
		void Place(Engine::GameObject::ObjectContext& a_context,
			const Math::Vector2& a_rowTopLeft, const Math::Vector2& a_rowSize,
			const Engine::Graphics::UIClipRect* a_pClip = nullptr) const;

		/// <summary>
		/// 操作 : 今の添え字のボタンを選ばれているにし、押されたボタンを返す
		/// </summary>
		/// <param name="a_selectedIndex">今選ばれている添え字(範囲外ならどれも選ばれていない)</param>
		/// <returns>このフレームに押されたボタンの添え字。押されていなければ -1(選ばれているボタンを押しても返す)</returns>
		int Update(Engine::GameObject::ObjectContext& a_context, int a_selectedIndex) const;

		// 項目移動(キー・パッド)でボタンのどれかが選ばれているか(選んだ行へスクロールを追わせる用)
		bool IsNavFocused(const Engine::GameObject::ObjectContext& a_context) const;

		// 選択肢の数
		size_t GetCount() const { return m_itemVec.size(); }

		// アーカイブ : a_name を頭に付けたキーで書く(<name>RadioButtons など)
		void Archive(Engine::Persistence::Archive& a_ar, const std::string& a_name);

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context, const char* a_label);

	private:

		// ボタン1つ : GUID と、行の左端からのずれ
		struct Item
		{
			Core::GUID guid = {};
			float offsetX = 0.0f;
		};

	private:

		//------------------------------------------------------------------------------
		// 設定(保存される)
		//------------------------------------------------------------------------------
		std::vector<Item> m_itemVec = {};		// ボタン(UIRadioButton)。並びが選択肢の並び
	};
}
