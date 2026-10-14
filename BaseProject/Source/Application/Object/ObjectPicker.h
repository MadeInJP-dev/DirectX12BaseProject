#pragma once

#include "../../Engine/GameObject/GameObjectManager/GameObjectManager.h"
#include "Engine/EditorField/EditorField.h"

//==========================================================================================
// シーンに置いたオブジェクトを GUID で指すための小物
//
// 進行役(HomeSequence / MissionSelect)は、相手のポインタではなく GUID を保存して
// 実行時に引き直す。ポインタを持つと読み込み順に縛られるうえ、保存もできないため。
//
// 引く側の書き方が毎回同じなので、ここへまとめてある。
//==========================================================================================
namespace App::Object::Picker
{
	/// <summary>
	/// GUID から T を引く
	/// </summary>
	/// <returns>見つからない・型が違うなら nullptr(設定ミスでも落ちないように)</returns>
	template<typename T>
	inline T* Find(Engine::GameObject::GameObjectManager* a_pObjectManager, const Core::GUID& a_guid)
	{
		if (a_pObjectManager == nullptr) return nullptr;
		if (!a_guid.IsValid()) return nullptr;

		return Core::TypeInfo::Cast<T>(a_pObjectManager->FindByGUID(a_guid));
	}

	/// <summary>
	/// 設定されているのに引けなければ警告を出す
	/// </summary>
	/// <remarks>
	/// 進行役がボタンへ差し込むときに、指している先を確かめるのに使う。
	///
	/// 以前は「全部そろうまで差し込まない」形で、引けないものを読み込みの途中とみなして待っていた。
	/// ところがシーンはファイルから一度に組み立てられ、GameObject の更新は読み込みが
	/// 済むまで回らないので、更新の時点で引けないものは「消したオブジェクトを指したまま」の設定漏れ。
	/// 待ち続けると、ボタンを1つ消しただけで残りのボタンまで全部反応しなくなっていた。
	/// 今は引けたものにだけ差し込み、引けないものはここで知らせる
	/// </remarks>
	/// <param name="a_owner">知らせる文に出す持ち主の名前</param>
	/// <param name="a_label">知らせる文に出す設定の名前</param>
	/// <returns>未設定か、引けたら true</returns>
	template<typename T>
	inline bool CheckFound(
		Engine::GameObject::GameObjectManager* a_pObjectManager,
		const Core::GUID& a_guid,
		const char* a_owner,
		const char* a_label)
	{
		if (!a_guid.IsValid()) return true;	// 設定されていないものは問題にしない
		if (Find<T>(a_pObjectManager, a_guid) != nullptr) return true;

		ENGINE_WARNING("[%s] %s が指しているオブジェクトがシーンにありません(消したものを指したままの可能性) : %s",
			a_owner, a_label, a_guid.String().c_str());
		return false;
	}

	//======================================================================================
	// エディター用
	//======================================================================================

	/// 一覧に出す見出し : 型名 + GUIDの頭だけ(生のGUIDは長すぎて見分けられない)
	inline std::string MakeLabel(const Engine::GameObject::BaseObject* a_pObject)
	{
		if (a_pObject == nullptr) return "None";

		const std::string _guid = a_pObject->GetGUID().String();
		return std::string(a_pObject->GetEditorName()) + " [" + _guid.substr(0, 8) + "]";
	}

	/// <summary>
	/// シーンに置いた T のなかから1つ選ぶコンボ
	/// </summary>
	/// <returns>選び直したら true</returns>
	template<typename T>
	inline bool DrawCombo(
		const char* a_label,
		Engine::GameObject::GameObjectManager* a_pObjectManager,
		Core::GUID& a_inoutGUID)
	{
		bool _isChanged = false;

		const auto* _pCurrent = Find<T>(a_pObjectManager, a_inoutGUID);

		// 指しているものが見つからないときは、設定ミスと読み込み前を見分けられるようにしておく
		std::string _currentLabel = "None";
		if (a_inoutGUID.IsValid())
		{
			_currentLabel = _pCurrent ? MakeLabel(_pCurrent) : ("(missing) " + a_inoutGUID.String().substr(0, 8));
		}

		Engine::EditorField::ComboScope _combo(a_label, _currentLabel.c_str());
		if (!_combo) return false;

		if (Engine::EditorField::Selectable("None", !a_inoutGUID.IsValid()))
		{
			a_inoutGUID = {};
			_isChanged = true;
		}

		if (a_pObjectManager)
		{
			const auto& _objectVec = a_pObjectManager->GetObjects();
			for (size_t _i = 0; _i < _objectVec.size(); ++_i)
			{
				auto* _pObject = Core::TypeInfo::Cast<T>(_objectVec[_i].get());
				if (!_pObject) continue;

				// 同名でもIDがぶつからないようにする
				Engine::EditorField::IDScope _id(static_cast<int>(_i));

				const bool _isSelected = (a_inoutGUID == _pObject->GetGUID());
				if (Engine::EditorField::Selectable(MakeLabel(_pObject).c_str(), _isSelected))
				{
					a_inoutGUID = _pObject->GetGUID();
					_isChanged = true;
				}
				if (_isSelected) Engine::EditorField::SetItemDefaultFocus();
			}
		}

		return _isChanged;
	}

	/// <summary>
	/// GUIDの配列を編集する(差し替え・削除・追加)
	/// </summary>
	/// <returns>中身が変わったら true</returns>
	template<typename T>
	inline bool DrawList(
		const char* a_label,
		Engine::GameObject::GameObjectManager* a_pObjectManager,
		std::vector<Core::GUID>& a_inoutGUIDVec)
	{
		bool _isChanged = false;

		Engine::EditorField::TreeScope _tree(a_label);
		if (!_tree) return false;

		// 回している間に配列を触ると足元が崩れるので、削除は覚えておいて後で行う
		int _removeIndex = -1;

		for (size_t _i = 0; _i < a_inoutGUIDVec.size(); ++_i)
		{
			Engine::EditorField::IDScope _id(static_cast<int>(_i));

			// ボタンを先に置く : コンボが残り幅を全部使うため
			if (Engine::EditorField::DeleteSmallButton("X"))
			{
				_removeIndex = static_cast<int>(_i);
			}

			Engine::EditorField::SameLine();
			if (DrawCombo<T>("##Object", a_pObjectManager, a_inoutGUIDVec[_i])) _isChanged = true;
		}

		if (_removeIndex >= 0)
		{
			a_inoutGUIDVec.erase(a_inoutGUIDVec.begin() + _removeIndex);
			_isChanged = true;
		}

		if (Engine::EditorField::CreateButton("Add"))
		{
			a_inoutGUIDVec.push_back({});
			_isChanged = true;
		}

		return _isChanged;
	}
}
