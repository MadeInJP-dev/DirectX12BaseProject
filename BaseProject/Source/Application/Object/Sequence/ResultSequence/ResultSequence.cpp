#include "ResultSequence.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/GameObject/GameObjectManager/GameObjectManager.h"
#include "Engine/Input/InputManager/InputManager.h"
#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/EditorField/EditorField.h"

#include "../../ObjectPicker.h"
#include "Application/Object/UI/UIBase.h"
#include "Application/Object/UI/Widget/UIButton/UIButton.h"
#include "../../../InstanceResource/GameDataResource.h"

//==========================================================================================
// ResultSequence
//
// リザルト画面で「ホームのボタンが押されたらタイトルへ戻す」ところだけを持つ。
// タイトル(TitleSequence)とまったく同じ作りで、行き先が違うだけ。
//
// ・ボタンは GUID で引く
//     ポインタを持つとシーンの読み込み順に縛られるうえ、保存もできない。
// ・カーソルの中央固定を切る
//     ゲーム中は視点操作のためにカーソルを毎フレーム画面中央へ戻している。
//     そのままではボタンを狙えないので、リザルトの間だけ切って、抜けるときに戻す。
// ・記録(スコア・タイム)はここでは消さない
//     消すのはゲームシーンの入り口(SceneSequence)。ここで消すと、
//     リザルトを見ている間に数字が 0 になってしまう。
//==========================================================================================
namespace App::Object
{
	void ResultSequence::Update(Engine::GameObject::ObjectContext& a_context)
	{
		//==============================================================
		// カーソルの固定を切る
		//--------------------------------------------------------------
		// 毎フレーム呼んでいるのは、他所で固定を入れ直されても
		// リザルトの間は必ず解除された状態にしたいため
		//==============================================================
		if (m_isReleaseCursorLock && a_context.pServices && a_context.pServices->pInputManager)
		{
			if (a_context.pServices->pInputManager->IsCursorLockActive())
			{
				a_context.pServices->pInputManager->SetCursorCentered(false);
			}
		}

		// ボタンへの差し込み(済んでいれば何もしない)
		TryBindButton(a_context);

		// 勝ち負けで出し分ける UI(済んでいれば何もしない)
		ApplyResultVisible(a_context);

		// 下のゲーム(重ねているとき)の HUD と BGM
		if (m_isHideGameHUD) UIBase::SetHUDSuppressed(true);
		if (m_gameBgmDuck < 1.0f)
		{
			// 自分の曲は絞らない
			m_bgm.SetDuckTarget(false);
			SequenceBgm::SetGlobalDuck(m_gameBgmDuck);
		}

		// BGM : 初回で鳴らし始め、フェードを進める
		m_bgm.Update(a_context);
	}

	//======================================================================================
	// ボタンへ押下時の処理を差し込む
	//======================================================================================
	void ResultSequence::TryBindButton(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_isBound) return;
		if (!a_context.pObjectManager) return;
		if (!m_homeButtonGUID.IsValid()) return;

		// 押されたときにシーンを切り替えるので、ここで受け取って掴ませる
		auto* _pSceneManager = a_context.pServices ? a_context.pServices->pSceneManager : nullptr;
		if (!_pSceneManager) return;

		auto* _pObject = a_context.pObjectManager->FindByGUID(m_homeButtonGUID);
		if (!_pObject) return;

		// 指定されたGUIDがボタン以外だった場合は差し込まない
		auto* _pButton = Core::TypeInfo::Cast<UIButton>(_pObject);
		if (!_pButton)
		{
			// 設定ミスに気付けるよう一度だけ知らせて、以降は試さない
			ENGINE_WARNING("[ResultSequence] 指定されたGUIDは UIButton ではありません");
			m_isBound = true;
			return;
		}

		// 押されたらタイトルへ。
		// this を掴むが、ボタンは同じシーンに居るので寿命は一緒に尽きる
		_pButton->SetOnClick([this, _pSceneManager]() { ReserveBackToTitle(*_pSceneManager); });

		m_isBound = true;
	}

	//======================================================================================
	// 勝ち負けで出し分ける UI
	//--------------------------------------------------------------------------------------
	// 指定した UI が揃うまで毎フレーム試す(読み込み順に依存しないように)
	//======================================================================================
	void ResultSequence::ApplyResultVisible(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_isResultApplied) return;
		if (!a_context.pObjectManager) return;
		if (!m_clearUIGUID.IsValid() && !m_failedUIGUID.IsValid()) return;

		auto* _pClear = Picker::Find<UIBase>(a_context.pObjectManager, m_clearUIGUID);
		auto* _pFailed = Picker::Find<UIBase>(a_context.pObjectManager, m_failedUIGUID);
		if (m_clearUIGUID.IsValid() && !_pClear) return;
		if (m_failedUIGUID.IsValid() && !_pFailed) return;

		const auto* _pGameData = InstanceResource::GameDataResource::Find(a_context.pWorld);
		const bool _isFailed = _pGameData && (_pGameData->result == App::Game::EGameResult::GameOver);

		if (_pClear) _pClear->SetVisible(!_isFailed);
		if (_pFailed) _pFailed->SetVisible(_isFailed);

		m_isResultApplied = true;
	}

	//======================================================================================
	// タイトルへ戻る
	//======================================================================================
	void ResultSequence::ReserveBackToTitle(Engine::Scene::SceneManager& a_sceneManager)
	{
		// 連打で何度も積まないようにする
		if (m_isSceneRequested) return;
		if (!m_titleSceneGUID.IsValid())
		{
			ENGINE_WARNING("[ResultSequence] タイトルシーンが設定されていません");
			return;
		}

		m_isSceneRequested = true;

		// ステージの上へ重ねて出していることがあるので、下まで外して切り替える
		// (重ねていなければ Replace と同じ)
		a_sceneManager.ReserveChangeScene(
			m_titleSceneGUID, Engine::Scene::ESceneChangeType::ReplaceAll);
	}

	//======================================================================================
	// 解放 : カーソルの固定を設定値へ戻す
	//======================================================================================
	void ResultSequence::Release(Engine::GameObject::ObjectContext& a_context)
	{
		// 借りているBGMを返す
		m_bgm.Release(a_context);

		// 立てた全体の印を下ろす
		if (m_isHideGameHUD) UIBase::SetHUDSuppressed(false);
		if (m_gameBgmDuck < 1.0f) SequenceBgm::SetGlobalDuck(1.0f);

		if (!m_isReleaseCursorLock) return;
		if (!a_context.pServices) return;
		if (!a_context.pServices->pInputManager) return;

		// リザルトで切ったぶんを戻す。切りっぱなしにすると、
		// 遷移先で視点操作のカーソル固定が効かなくなる
		const bool _isLocked =
			a_context.pServices->pInputManager->GetConfig().isCursorLockedToCenter;

		a_context.pServices->pInputManager->SetCursorCentered(_isLocked);
	}

	//======================================================================================
	// シリアライズ
	//======================================================================================
	void ResultSequence::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& /*a_context*/)
	{
		a_ar.GUIDField("HomeButtonGUID", m_homeButtonGUID);
		a_ar.GUIDField("TitleSceneGUID", m_titleSceneGUID);
		a_ar.Field("IsReleaseCursorLock", m_isReleaseCursorLock);

		m_bgm.Archive(a_ar);

		// ---- ここから下は後から足したもの : 追加は必ず末尾へ ----
		// 区切りを持たない古い .ob* には無いので読まない
		if (!a_ar.IsLegacyLayout())
		{
			a_ar.Field("IsHideGameHUD", m_isHideGameHUD);
			a_ar.Field("GameBgmDuck", m_gameBgmDuck);
			a_ar.GUIDField("ClearUIGUID", m_clearUIGUID);
			a_ar.GUIDField("FailedUIGUID", m_failedUIGUID);
		}
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void ResultSequence::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		Engine::EditorField::Header("Home Button");

		// 同じシーンに置いた UIButton から選ぶ
		std::string _current = "None";
		if (m_homeButtonGUID.IsValid()) _current = m_homeButtonGUID.String();

		if (Engine::EditorField::ComboScope _combo{ "Button", _current.c_str() })
		{
			if (Engine::EditorField::Selectable("None", !m_homeButtonGUID.IsValid()))
			{
				m_homeButtonGUID = {};
				m_isBound = false;
			}

			if (a_context.pObjectManager)
			{
				const auto& _objectVec = a_context.pObjectManager->GetObjects();
				for (size_t _i = 0; _i < _objectVec.size(); ++_i)
				{
					auto* _pButton = Core::TypeInfo::Cast<UIButton>(_objectVec[_i].get());
					if (!_pButton) continue;

					// 同名でもIDがぶつからないようにする
					Engine::EditorField::IDScope _id(static_cast<int>(_i));

					const bool _isSelected = (m_homeButtonGUID == _pButton->GetGUID());
					if (Engine::EditorField::Selectable(_pButton->GetGUID().String().c_str(), _isSelected))
					{
						m_homeButtonGUID = _pButton->GetGUID();

						// 差し込み直させる
						m_isBound = false;
					}
					if (_isSelected) Engine::EditorField::SetItemDefaultFocus();
				}
			}
		}

		Engine::EditorField::Header("Title Scene");

		Engine::EditorField::AssetField(*a_context.pServices, "Scene", "Scene", m_titleSceneGUID);

		m_bgm.DrawInspector(a_context);

		Engine::EditorField::Header("Over The Stage");
		Engine::EditorField::Field("Hide Game HUD", m_isHideGameHUD);
		Engine::EditorField::Tooltip("ステージの上へ重ねたとき、下のゲームの HUD を隠す");
		Engine::EditorField::Field("Game Bgm Duck", m_gameBgmDuck, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("ステージの上へ重ねたとき、下のゲームの BGM に掛ける倍率(1 でそのまま)");

		Engine::EditorField::Header("Clear / Failed");
		if (Picker::DrawCombo<UIBase>("Clear UI", a_context.pObjectManager, m_clearUIGUID)) m_isResultApplied = false;
		Engine::EditorField::Tooltip("勝ったときだけ出す UI(記録が無いときもこちら)");
		if (Picker::DrawCombo<UIBase>("Failed UI", a_context.pObjectManager, m_failedUIGUID)) m_isResultApplied = false;
		Engine::EditorField::Tooltip("負けたときだけ出す UI");

		Engine::EditorField::Header("Cursor");

		Engine::EditorField::Field("ReleaseCursorLock", m_isReleaseCursorLock);
		Engine::EditorField::Tooltip("リザルトの間はカーソルの中央固定を切る");

		//----------------------------------------------------------------------
		// 持ち越されてきた記録(表示のみ)
		//----------------------------------------------------------------------
		Engine::EditorField::Header("Carried Data");

		if (const auto* _pGameData = InstanceResource::GameDataResource::Find(a_context.pWorld))
		{
			const auto& _gameData = *_pGameData;

			const char* _resultName = "None";
			switch (_gameData.result)
			{
			case App::Game::EGameResult::Clear:    _resultName = "Clear";    break;
			case App::Game::EGameResult::GameOver: _resultName = "GameOver"; break;
			default: break;
			}

			Engine::EditorField::Value("Result", "%s", _resultName);
			Engine::EditorField::Value("Score", "%d", _gameData.score);
			Engine::EditorField::Value("Kill", "%d", _gameData.killCount);
			Engine::EditorField::Value("Time", "%.2f", _gameData.time);
			Engine::EditorField::Value("Wave", "%d / %d", _gameData.clearedWaveCount, _gameData.totalWaveCount);
			Engine::EditorField::Tooltip("数字を画面に出すのは ScoreHUD の仕事");
		}
		else
		{
			Engine::EditorField::HelpText("このワールドには記録(GameDataResource)がありません");
		}

		// 実行中の状態は表示のみ
		Engine::EditorField::Header("Runtime");
		Engine::EditorField::Value("Bound", "%s", m_isBound ? "yes" : "no");
		Engine::EditorField::Value("Requested", "%s", m_isSceneRequested ? "yes" : "no");
	}
}
