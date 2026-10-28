#include "MissionSelect.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/GameObject/GameObjectManager/GameObjectManager.h"
#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/EditorField/EditorField.h"

#include "../../ObjectPicker.h"
#include "Application/Object/UI/UIBase.h"
#include "Application/Object/UI/Widget/UIButton/UIButton.h"

//==========================================================================================
// MissionSelect
//
// 出撃先を選ぶ画面。ホーム(HomeSequence)から SetVisible で出し入れされる。
//
// ・一覧はシーンへ置いた UIButton を配列で覚える
//     並べ方を持たないので、縦一列でも横並びでも円形でも、置き方はエディター任せ。
//     判定も見た目も UIButton のものがそのまま効くので、ここで作り直すものが無い。
//
// ・押した瞬間に確認ボックスを出す
//     コールバックの中でも配下へ反映できるよう、マネージャーを Awake で覚えてある。
//     頼まれごとを覚えて次の更新で処理する形にすると、押してから1フレーム遅れて出る。
//
// ・確認中は一覧を押せなくする
//     裏のミッションが押せると、確認ボックスの中身と行き先が食い違う。
//==========================================================================================
namespace App::Object
{
	//======================================================================================
	// 初期化
	//======================================================================================
	void MissionSelect::Awake(Engine::GameObject::ObjectContext& a_context)
	{
		m_pObjectManager = a_context.pObjectManager;
	}

	//======================================================================================
	// 更新
	//======================================================================================
	void MissionSelect::Update(Engine::GameObject::ObjectContext& a_context)
	{
		// Awake より先に読み込みが走る経路でも取りこぼさないように、ここでも拾っておく
		if (m_pObjectManager == nullptr) m_pObjectManager = a_context.pObjectManager;

		// ボタンへの差し込み(済んでいれば何もしない)
		TryBind(a_context);

		// 隠れている間は何も追わない(裏で詳細が切り替わっても意味が無い)
		if (!m_isVisible) return;

		UpdateHover();
	}

	//======================================================================================
	// ボタンへ押下時の処理を差し込む
	//--------------------------------------------------------------------------------------
	// 見つかったボタンにだけ差し込む。見つからないものは知らせるだけで待たない
	// (理由は HomeSequence::TryBindButtons と同じ。待つと残りのボタンまで押せなくなる)。
	//======================================================================================
	void MissionSelect::TryBind(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_isBound) return;
		if (m_pObjectManager == nullptr) return;

		WarnMissingButtons();

		// 出撃はシーンの切り替え。押されたときに使うので、ここで受け取って掴ませる
		auto* _pSceneManager = a_context.pServices ? a_context.pServices->pSceneManager : nullptr;
		if (!_pSceneManager) return;

		// ミッション : 押したら確認ボックスを出す
		for (int _i = 0; _i < static_cast<int>(m_missionVec.size()); ++_i)
		{
			auto* _pButton = Picker::Find<UIButton>(m_pObjectManager, m_missionVec[_i].buttonGUID);
			if (!_pButton) continue;

			// ボタンは同じシーンに居るので、this を掴んでも寿命は一緒に尽きる
			_pButton->SetOnClick([this, _i]() { OpenConfirm(_i); });
		}

		// 確認ボックス
		if (auto* _pYes = Picker::Find<UIButton>(m_pObjectManager, m_yesButtonGUID))
		{
			_pYes->SetOnClick([this, _pSceneManager]() { ReserveSortie(*_pSceneManager); });
		}
		if (auto* _pNo = Picker::Find<UIButton>(m_pObjectManager, m_noButtonGUID))
		{
			_pNo->SetOnClick([this]() { CloseConfirm(); });
		}

		m_isBound = true;

		// そろったところで、今の状態の見た目にしておく
		ApplyVisible();
	}

	//======================================================================================
	// 指しているのに見つからないボタンを知らせる
	//======================================================================================
	void MissionSelect::WarnMissingButtons() const
	{
		constexpr const char* OWNER = "MissionSelect";
		for (const MissionEntry& _mission : m_missionVec)
		{
			Picker::CheckFound<UIButton>(m_pObjectManager, _mission.buttonGUID, OWNER, _mission.name.c_str());
		}

		Picker::CheckFound<UIButton>(m_pObjectManager, m_yesButtonGUID, OWNER, "YesButton");
		Picker::CheckFound<UIButton>(m_pObjectManager, m_noButtonGUID, OWNER, "NoButton");
	}

	//======================================================================================
	// カーソルが乗っているミッションを追う
	//--------------------------------------------------------------------------------------
	// 乗っている間だけそのミッションの詳細を出す。どこにも乗っていないフレームは
	// 最後に乗ったものを出しっぱなしにする(離すたびに消えると、目で追えないため)。
	//
	// 確認中はボタンを押せない状態にしてあるので、UIButton 側の hover も落ちる。
	// つまりここは自然と何もしなくなり、確認しているミッションの詳細が出たままになる。
	//======================================================================================
	void MissionSelect::UpdateHover()
	{
		if (m_pObjectManager == nullptr) return;

		for (int _i = 0; _i < static_cast<int>(m_missionVec.size()); ++_i)
		{
			const auto* _pButton = Picker::Find<UIButton>(m_pObjectManager, m_missionVec[_i].buttonGUID);
			if (!_pButton || !_pButton->IsHovered()) continue;

			if (m_showIndex != _i)
			{
				m_showIndex = _i;
				ApplyVisible();
			}
			return;
		}
	}

	//======================================================================================
	// 出し入れ
	//======================================================================================
	void MissionSelect::SetVisible(bool a_isVisible)
	{
		if (m_isVisible == a_isVisible) return;

		m_isVisible = a_isVisible;

		// 閉じるときは確認も畳む。
		// 出しっぱなしにすると、次に開いたときに前回の確認が残ってしまう
		if (!m_isVisible) m_confirmIndex = -1;

		ApplyVisible();
	}

	//======================================================================================
	// 今の状態に合わせて配下のUIを切り替える
	//--------------------------------------------------------------------------------------
	// 消して作り直すのではなく Visible で出し入れする。
	// 非表示のUIは描画も入力も止まるので、裏で押せてしまうことがない。
	//======================================================================================
	void MissionSelect::ApplyVisible()
	{
		if (m_pObjectManager == nullptr) return;

		const bool _isConfirm = IsValidIndex(m_confirmIndex);

		//--------------------------------------------------------------
		// ミッションの一覧と詳細
		//--------------------------------------------------------------
		for (int _i = 0; _i < static_cast<int>(m_missionVec.size()); ++_i)
		{
			const MissionEntry& _mission = m_missionVec[_i];

			if (auto* _pButton = Picker::Find<UIButton>(m_pObjectManager, _mission.buttonGUID))
			{
				_pButton->SetVisible(m_isVisible);

				// 確認中は裏の一覧を押させない。
				// 押せてしまうと、出ているボックスの中身と行き先が食い違う
				_pButton->SetInteractable(m_isVisible && !_isConfirm);
			}

			// 詳細は「見ている1つ」だけ
			const bool _isShowDetail = m_isVisible && (_i == m_showIndex);
			for (const Core::GUID& _guid : _mission.detailUIGUIDVec)
			{
				if (auto* _pUI = Picker::Find<Engine::GameObject::BaseObject>(m_pObjectManager, _guid))
				{
					_pUI->SetVisible(_isShowDetail);
				}
			}
		}

		//--------------------------------------------------------------
		// 確認ボックス
		//--------------------------------------------------------------
		const bool _isShowConfirm = m_isVisible && _isConfirm;

		for (const Core::GUID& _guid : m_confirmUIGUIDVec)
		{
			if (auto* _pUI = Picker::Find<Engine::GameObject::BaseObject>(m_pObjectManager, _guid))
			{
				_pUI->SetVisible(_isShowConfirm);
			}
		}

		if (auto* _pYes = Picker::Find<Engine::GameObject::BaseObject>(m_pObjectManager, m_yesButtonGUID))
		{
			_pYes->SetVisible(_isShowConfirm);
		}
		if (auto* _pNo = Picker::Find<Engine::GameObject::BaseObject>(m_pObjectManager, m_noButtonGUID))
		{
			_pNo->SetVisible(_isShowConfirm);
		}
		if (auto* _pName = Picker::Find<Engine::GameObject::BaseObject>(m_pObjectManager, m_nameUIGUID))
		{
			_pName->SetVisible(_isShowConfirm);
		}
	}

	//======================================================================================
	// 確認ボックスを出す
	//======================================================================================
	void MissionSelect::OpenConfirm(int a_index)
	{
		if (!IsValidIndex(a_index)) return;

		m_confirmIndex = a_index;

		// 押したものの詳細を出したままにする
		m_showIndex = a_index;

		ApplyMissionName(m_missionVec[a_index]);
		ApplyVisible();
	}

	//======================================================================================
	// 確認ボックスを閉じる
	//======================================================================================
	void MissionSelect::CloseConfirm()
	{
		m_confirmIndex = -1;
		ApplyVisible();
	}

	//======================================================================================
	// 確認ボックスへミッション名を流し込む
	//--------------------------------------------------------------------------------------
	// 名前のためだけに画像を1枚ずつ用意しなくて済むよう、Text の飾りへ直接書き込む。
	// 飾りは番号で引くので、飾りの名前を変えたり並べ替えたりしても指し直さずに済む。
	//======================================================================================
	void MissionSelect::ApplyMissionName(const MissionEntry& a_mission)
	{
		auto* _pUI = Picker::Find<UIBase>(m_pObjectManager, m_nameUIGUID);
		if (!_pUI) return;

		Decoration::UITextElement* _pText = ResolveNameDecoration(*_pUI);
		if (_pText == nullptr)
		{
			ENGINE_WARNING("[MissionSelect] 名前を出す Text の飾りが見つかりません : %u", m_nameDecorationId);
			return;
		}

		_pText->SetValue(FormatMissionName(a_mission));
	}

	//======================================================================================
	// 書き込む文字を作る
	//--------------------------------------------------------------------------------------
	// 形の中の {name} をすべてミッション名に置き換える。
	// {name} を含まない形なら、形の文字がそのまま出る(名前を出さない見出しにも使える)。
	//======================================================================================
	std::string MissionSelect::FormatMissionName(const MissionEntry& a_mission) const
	{
		constexpr std::string_view KEY = "{name}";

		std::string _result = m_nameFormat;
		size_t _pos = _result.find(KEY);
		while (_pos != std::string::npos)
		{
			_result.replace(_pos, KEY.size(), a_mission.name);
			_pos = _result.find(KEY, _pos + a_mission.name.size());
		}
		return _result;
	}

	Decoration::UITextElement* MissionSelect::ResolveNameDecoration(UIBase& a_nameUI)
	{
		return a_nameUI.FindDecorationAs<Decoration::UITextElement>(m_nameDecorationId);
	}

	//======================================================================================
	// 確認しているミッションへ出撃する
	//======================================================================================
	void MissionSelect::ReserveSortie(Engine::Scene::SceneManager& a_sceneManager)
	{
		// 連打で何度も積まないようにする
		if (m_isSceneRequested) return;

		if (!IsValidIndex(m_confirmIndex))
		{
			ENGINE_WARNING("[MissionSelect] 出撃先のミッションが選ばれていません");
			return;
		}

		const Core::GUID& _sceneGUID = m_missionVec[m_confirmIndex].sceneGUID;
		if (!_sceneGUID.IsValid())
		{
			ENGINE_WARNING("[MissionSelect] このミッションにはシーンが設定されていません");
			return;
		}

		m_isSceneRequested = true;

		// シーンの切り替えは SceneManager が持っている(ボタンを結んだときにサービスから受け取ったもの)
		a_sceneManager.ReserveChangeScene(
			_sceneGUID, Engine::Scene::ESceneChangeType::Replace);
	}

	//======================================================================================
	// シリアライズ
	//======================================================================================
	void MissionSelect::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& /*a_context*/)
	{
		//----------------------------------------------------------------------
		// ミッション
		//----------------------------------------------------------------------
		size_t _missionCount = m_missionVec.size();
		if (a_ar.BeginArray("Missions", _missionCount))
		{
			m_missionVec.resize(_missionCount);

			for (size_t _i = 0; _i < _missionCount; ++_i)
			{
				if (!a_ar.BeginObject(_i)) continue;

				MissionEntry& _mission = m_missionVec[_i];
				a_ar.StringField("name", _mission.name);
				a_ar.GUIDField("buttonGUID", _mission.buttonGUID);
				a_ar.GUIDField("sceneGUID", _mission.sceneGUID);
				a_ar.GUIDVectorField("detailUIGUIDs", _mission.detailUIGUIDVec);

				a_ar.EndObject();
			}
			a_ar.EndArray();
		}

		//----------------------------------------------------------------------
		// 確認ボックス
		//----------------------------------------------------------------------
		a_ar.GUIDVectorField("ConfirmUIGUIDs", m_confirmUIGUIDVec);
		a_ar.GUIDField("YesButtonGUID", m_yesButtonGUID);
		a_ar.GUIDField("NoButtonGUID", m_noButtonGUID);
		a_ar.GUIDField("NameUIGUID", m_nameUIGUID);

		//----------------------------------------------------------------------
		// 初期状態
		//----------------------------------------------------------------------
		// 出ているかどうかも保存する。ホームから開く作りなら既定は隠しておく
		a_ar.Field("IsVisible", m_isVisible);

		// ---- ここから下は区切りを入れた後に足したもの : 追加は必ず末尾へ ----
		// 区切りを持たない古い .ob* には無いので読まない
		if (!a_ar.IsLegacyLayout())
		{
			a_ar.Field("NameDecorationId", m_nameDecorationId);
			a_ar.StringField("NameFormat", m_nameFormat);
		}

		if (a_ar.IsLoading())
		{
			// 指し先が変わったので差し込み直す
			m_isBound = false;
			m_confirmIndex = -1;
			m_showIndex = 0;
		}
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void MissionSelect::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pObjectManager = a_context.pObjectManager;

		//----------------------------------------------------------------------
		// 出ているか
		//----------------------------------------------------------------------
		// エディターでは押して切り替えられないので、ここから出し入れして配置を見る
		//----------------------------------------------------------------------
		Engine::EditorField::Header("Visible");

		bool _isVisible = m_isVisible;
		if (Engine::EditorField::Field("Visible", _isVisible)) SetVisible(_isVisible);
		Engine::EditorField::Tooltip("普段は HomeSequence が出し入れする。配置を見るときはここで切り替える");

		//----------------------------------------------------------------------
		// ミッション
		//----------------------------------------------------------------------
		Engine::EditorField::Header("Missions");
		Engine::EditorField::HelpText("1ミッション = シーンへ置いた UIButton 1つ。並べ方はそのボタン側で決める");

		int _removeIndex = -1;

		for (size_t _i = 0; _i < m_missionVec.size(); ++_i)
		{
			MissionEntry& _mission = m_missionVec[_i];

			Engine::EditorField::IDScope _id(static_cast<int>(_i));

			const std::string _label = std::to_string(_i) + " : " + _mission.name;
			if (Engine::EditorField::TreeScope _tree{ _label.c_str() })
			{
				if (Engine::EditorField::Field("Name", _mission.name))
				{
					// 開いている確認ボックスへ即座に反映して、見ながら直せるようにする
					if (m_confirmIndex == static_cast<int>(_i)) ApplyMissionName(_mission);
				}
				Engine::EditorField::Tooltip("確認ボックスの Text 飾りへ流し込む名前");

				if (Picker::DrawCombo<UIButton>("Button", _pObjectManager, _mission.buttonGUID))
				{
					m_isBound = false;
				}
				Engine::EditorField::HelpText("押すと確認ボックスが出る");

				Engine::EditorField::AssetField(*a_context.pServices, "Scene", "Scene", _mission.sceneGUID);
				Engine::EditorField::Tooltip("Yes で飛ぶ先");

				Engine::EditorField::HelpText("カーソルが乗っている間だけ出すUI(画像・説明文)");
				if (Picker::DrawList<Engine::GameObject::BaseObject>("Detail UI", _pObjectManager, _mission.detailUIGUIDVec))
				{
					ApplyVisible();
				}

				if (Engine::EditorField::DeleteButton("Remove Mission"))
				{
					_removeIndex = static_cast<int>(_i);
				}
			}
		}

		if (_removeIndex >= 0)
		{
			m_missionVec.erase(m_missionVec.begin() + _removeIndex);

			// 消したぶんで指し先がずれるので直す
			m_showIndex = std::clamp(m_showIndex, 0, std::max(static_cast<int>(m_missionVec.size()) - 1, 0));
			m_confirmIndex = -1;
			m_isBound = false;

			ApplyVisible();
		}

		if (Engine::EditorField::CreateButton("Add Mission"))
		{
			m_missionVec.push_back({});
			m_isBound = false;
		}

		//----------------------------------------------------------------------
		// 確認ボックス
		//----------------------------------------------------------------------
		Engine::EditorField::Header("Confirm");
		Engine::EditorField::HelpText("ミッションを押したときに中央へ出すもの");

		if (Picker::DrawList<Engine::GameObject::BaseObject>("Confirm UI", _pObjectManager, m_confirmUIGUIDVec))
		{
			ApplyVisible();
		}

		if (Picker::DrawCombo<UIButton>("Yes", _pObjectManager, m_yesButtonGUID)) m_isBound = false;
		if (Picker::DrawCombo<UIButton>("No", _pObjectManager, m_noButtonGUID))   m_isBound = false;

		if (Picker::DrawCombo<UIBase>("Name UI", _pObjectManager, m_nameUIGUID))
		{
			// UI を選び直したら、前の UI の飾りの番号は意味を持たない
			m_nameDecorationId = 0;
			ApplyVisible();
		}

		// 指定した飾りが本当にあるか、その場で分かるようにしておく
		if (auto* _pNameUI = Picker::Find<UIBase>(_pObjectManager, m_nameUIGUID))
		{
			ResolveNameDecoration(*_pNameUI);

			_pNameUI->DrawDecorationPicker("Name Decoration", m_nameDecorationId, Decoration::EElementType::Text);
			Engine::EditorField::Tooltip("上のUIが持つ Text 飾り。ここへミッション名を書き込む(番号で指すので、名前を変えても外れない)");

			const auto* _pDecoration = _pNameUI->FindDecorationById(m_nameDecorationId);
			if (_pDecoration == nullptr)
			{
				Engine::EditorField::ErrorText("飾りが選ばれていません");
			}
			else if (_pDecoration->GetType() != Decoration::EElementType::Text)
			{
				Engine::EditorField::WarningText("飾りが Text ではありません");
			}
		}

		if (Engine::EditorField::Field("Name Format", m_nameFormat))
		{
			// 開いている確認ボックスへ即座に反映して、見ながら直せるようにする
			if (IsValidIndex(m_confirmIndex)) ApplyMissionName(m_missionVec[m_confirmIndex]);
		}
		Engine::EditorField::Tooltip("飾りへ書き込む文字の形。{name} がミッション名に置き換わる(例 : OPERATION : {name})");

		//----------------------------------------------------------------------
		// 実行中の状態は表示のみ
		//----------------------------------------------------------------------
		Engine::EditorField::Header("Runtime");
		Engine::EditorField::Value("Bound", "%s", m_isBound ? "yes" : "no");
		Engine::EditorField::Value("Show", "%d", m_showIndex);
		Engine::EditorField::Value("Confirm", "%d", m_confirmIndex);
		Engine::EditorField::Value("Requested", "%s", m_isSceneRequested ? "yes" : "no");
	}
}
