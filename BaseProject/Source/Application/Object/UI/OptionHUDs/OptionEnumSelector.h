#pragma once

#include "OptionDropdown.h"

namespace App::Object
{
	//===============================================================================================
	// enum の値を1つ選ぶ部品 : プルダウン(選択欄を押すと下に候補が出る)
	//
	// 選択肢の表(値と表示名)と、プルダウンの操作(OptionDropdown)を持つ。
	// パネルが持ち、毎フレーム Update に今の値を渡すだけで、開閉・選択・名前の書き込みまで済む。
	//
	//     OptionEnumSelector<Engine::EWindowMode> m_windowModeSelector = {
	//         { Engine::EWindowMode::Windowed, "Windowed" },
	//         { Engine::EWindowMode::FullScreen, "Fullscreen" } };
	//
	//     if (m_windowModeSelector.Update(a_context, _current))
	//     {
	//         _settings.windowMode = m_windowModeSelector.GetValue();
	//     }
	//
	// ・候補は表の並びで上から出る。候補のボタンは選択肢の数だけシーンに置いておく。
	// ・並べ方・保存・インスペクターは、RefUI / GetUI を ScrollPanelBase の
	//   PlaceDropdown / ArchiveDropdown / DrawDropdownInspector へ渡す
	//   (行の置き場所はパネルの並べ方で決まるので、部品は持たない)。
	// ・開いている間はパネルの IsCapturingInput で IsOpen を返すこと(戻るキーで画面ごと閉じないように)
	//===============================================================================================
	template<typename EnumType>
	class OptionEnumSelector
	{
		static_assert(std::is_enum_v<EnumType>, "OptionEnumSelector は enum 専用");

	public:

		// 選択肢1つ
		struct Choice
		{
			EnumType value;
			const char* name;	// 表示名(選択欄と候補の文字の飾りへ書き込む)
		};

		// 選択肢の表を渡して作る。並びが候補の上からの並びになる
		OptionEnumSelector(std::initializer_list<Choice> a_choices) : m_choiceVec(a_choices)
		{
			m_nameVec.reserve(m_choiceVec.size());
			for (const Choice& _choice : m_choiceVec) m_nameVec.push_back(_choice.name);
		}

		/// <summary>
		/// 更新 : プルダウンの開閉と選択、選択欄への名前の書き込み
		/// </summary>
		/// <param name="a_current">今の値(設定の実体から渡す)</param>
		/// <returns>選び直されたら true。新しい値は GetValue で取る</returns>
		bool Update(Engine::GameObject::ObjectContext& a_context, EnumType a_current)
		{
			m_value = a_current;

			bool _isChanged = false;
			const int _chosen = m_dropdown.Update(a_context, m_nameVec);
			if (_chosen >= 0 && static_cast<size_t>(_chosen) < m_choiceVec.size())
			{
				m_value = m_choiceVec[static_cast<size_t>(_chosen)].value;
				_isChanged = (m_value != a_current);
			}

			m_dropdown.SetFieldText(a_context, GetName(m_value));
			return _isChanged;
		}

		// 選んでいる値(最後に Update へ渡した値か、そこから選び直した値)
		EnumType GetValue() const { return m_value; }

		// 値の表示名 : 表に無ければ enum の名前そのもの
		std::string GetName(EnumType a_value) const
		{
			for (const Choice& _choice : m_choiceVec)
			{
				if (_choice.value == a_value) return _choice.name;
			}
			return std::string(magic_enum::enum_name(a_value));
		}

		// 候補が開いているか / 閉じる
		bool IsOpen() const { return m_dropdown.IsOpen(); }
		void Close() { m_dropdown.Close(); }

		// 行の UI(見出し・選択欄・候補)
		const OptionDropdownUI& GetUI() const { return m_dropdown.GetUI(); }
		OptionDropdownUI& RefUI() { return m_dropdown.RefUI(); }

	private:

		std::vector<Choice> m_choiceVec = {};		// 選択肢の表(候補の上からの並び)
		std::vector<const char*> m_nameVec = {};	// 表示名だけを並べたもの(候補へ書き込む用)
		OptionDropdown m_dropdown = {};				// プルダウンの操作と行の UI
		EnumType m_value = {};						// 選んでいる値
	};
}
