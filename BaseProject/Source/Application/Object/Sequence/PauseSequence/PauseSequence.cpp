#include "PauseSequence.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/GameObject/GameObjectManager/GameObjectManager.h"
#include "Engine/Input/InputManager/InputManager.h"
#include "Engine/Option/OptionManager.h"
#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/EditorField/EditorField.h"

#include "Application/Object/UI/Widget/UIButton/UIButton.h"

//==========================================================================================
// PauseSequence
//
// ポーズ画面で「閉じる」「やめて別のシーンへ移る」だけを持つ。
//
// ポーズ画面はゲームのシーンへ重ねて出している(Push)ので、閉じる(Pop)と後ろの
// ゲームがそのまま続きから動き出す。敵の配置もウェーブの進行も残ったままになる。
//
// ・重ねるのはゲーム側(SceneSequence)、閉じるのはここ
//     どちらも同じ入力(Pause)を見るが、更新されるのは一番上のシーンだけなので、
//     開いている間はここ、閉じている間はあちら、と自然に分かれる。
//
// ・やめるときは先に閉じる
//     重ねたままシーンを差し替えると、入れ替わるのは後ろのゲームの方で、
//     ポーズ画面が乗りっぱなしになる。Pop してから Replace を積む。
//     切り替え命令は積んだ順に処理されるので、この2つは同じフレームで片付く。
//==========================================================================================
namespace App::Object
{
	void PauseSequence::Update(Engine::GameObject::ObjectContext& a_context)
	{
		//==============================================================
		// カーソルの固定を切る
		//--------------------------------------------------------------
		// プレイ中は視点操作のためにカーソルを毎フレーム画面中央へ戻している。
		// そのままではカーソルを動かせず、中央のボタン以外を狙えない
		//==============================================================
		if (m_isReleaseCursorLock && a_context.pServices && a_context.pServices->pInputManager)
		{
			if (a_context.pServices->pInputManager->IsCursorLockActive())
			{
				a_context.pServices->pInputManager->SetCursorCentered(false);
			}
		}

		// ボタンへの差し込み(済んでいれば何もしない)
		TryBindButtons(a_context);

		//==============================================================
		// BGM
		//--------------------------------------------------------------
		// 自分のBGMは絞りの対象から外す。対象のままだと自分で自分を小さくする。
		// 下のゲームBGMは重ねている間ずっと絞っておく : 毎フレーム入れているのは、
		// 別の誰かが戻してしまっても押し切れるようにするため
		//==============================================================
		m_bgm.SetDuckTarget(false);
		SequenceBgm::SetGlobalDuck(m_gameBgmDuck);

		m_bgm.Update(a_context);

		//==============================================================
		// 開いたのと同じキーで閉じる
		//--------------------------------------------------------------
		// 開いたフレームの押下はゲーム側が受け取っており、こちらが動き出す
		// 次のフレームには押しっぱなし(Hold)になっている。
		// 押した瞬間(Press)だけを見ているので、開いた勢いで閉じることはない
		//==============================================================
		if (!m_isClosing && a_context.pServices && a_context.pServices->pInputManager && a_context.pServices->pSceneManager)
		{
			if (a_context.pServices->pInputManager->IsPress(m_pauseAction))
			{
				ReserveResume(*a_context.pServices->pSceneManager);
			}
		}
	}

	//======================================================================================
	// ボタンへ押下時の処理を差し込む
	//======================================================================================
	void PauseSequence::TryBindButtons(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_isBound) return;
		if (!a_context.pObjectManager) return;

		// 押されたときにシーンを積み替えるので、ここで受け取って掴ませる
		auto* _pSceneManager = a_context.pServices ? a_context.pServices->pSceneManager : nullptr;
		if (!_pSceneManager) return;

		auto* _pResume = Core::TypeInfo::Cast<UIButton>(
			m_resumeButtonGUID.IsValid() ? a_context.pObjectManager->FindByGUID(m_resumeButtonGUID) : nullptr);
		auto* _pExit = Core::TypeInfo::Cast<UIButton>(
			m_exitButtonGUID.IsValid() ? a_context.pObjectManager->FindByGUID(m_exitButtonGUID) : nullptr);
		auto* _pOption = Core::TypeInfo::Cast<UIButton>(
			m_optionButtonGUID.IsValid() ? a_context.pObjectManager->FindByGUID(m_optionButtonGUID) : nullptr);

		// 設定されているのに見つからないものがあれば、まだ読み込みの途中とみなして次のフレームへ回す
		if (m_resumeButtonGUID.IsValid() && !_pResume) return;
		if (m_exitButtonGUID.IsValid() && !_pExit) return;
		if (m_optionButtonGUID.IsValid() && !_pOption) return;

		// ボタンは同じシーンに居るので、this を掴んでも寿命は一緒に尽きる
		if (_pResume) _pResume->SetOnClick([this, _pSceneManager]() { ReserveResume(*_pSceneManager); });
		if (_pExit)   _pExit->SetOnClick([this, _pSceneManager]() { ReserveExitScene(*_pSceneManager); });
		if (_pOption) _pOption->SetOnClick([this, _pSceneManager]() { ReserveOpenOption(*_pSceneManager); });

		m_isBound = true;
	}

	//======================================================================================
	// ポーズを閉じてゲームへ戻る
	//======================================================================================
	void PauseSequence::ReserveResume(Engine::Scene::SceneManager& a_sceneManager)
	{
		// 連打で何度も積まないようにする
		if (m_isClosing) return;
		m_isClosing = true;

		// 自分を外すだけ。後ろのゲームは残っているので続きから動き出す
		a_sceneManager.ReserveChangeScene(
			Core::DEFAULT_GUID, Engine::Scene::ESceneChangeType::Pop);
	}

	//======================================================================================
	// ポーズを閉じてから、ゲームのシーンを行き先へ差し替える
	//======================================================================================
	void PauseSequence::ReserveExitScene(Engine::Scene::SceneManager& a_sceneManager)
	{
		if (m_isClosing) return;

		if (!m_exitSceneGUID.IsValid())
		{
			ENGINE_WARNING("[PauseSequence] やめたときの行き先が設定されていません");
			return;
		}

		m_isClosing = true;

		auto& _sceneManager = a_sceneManager;

		// 先に自分を外す。重ねたまま差し替えると、入れ替わるのは後ろのゲームの方で
		// ポーズ画面が乗りっぱなしになる
		_sceneManager.ReserveChangeScene(Core::DEFAULT_GUID, Engine::Scene::ESceneChangeType::Pop);

		// 続けて後ろのゲームを行き先へ差し替える(命令は積んだ順に処理される)
		_sceneManager.ReserveChangeScene(m_exitSceneGUID, Engine::Scene::ESceneChangeType::Replace);
	}

	//======================================================================================
	// 設定画面を重ねる
	//--------------------------------------------------------------------------------------
	// ポーズは閉じずに、その上へ重ねる。更新されるのは一番上のシーンだけなので、
	// 開いている間はここも止まり、設定画面が自分を外せばポーズの続きへ戻る。
	// カーソルの固定はポーズが切ったままなので、設定画面でもそのまま狙える
	//======================================================================================
	void PauseSequence::ReserveOpenOption(Engine::Scene::SceneManager& a_sceneManager)
	{
		// 閉じる途中に重ねると、外す順番が崩れる
		if (m_isClosing) return;

		if (!m_optionSceneGUID.IsValid())
		{
			ENGINE_WARNING("[PauseSequence] 設定画面のシーンが設定されていません");
			return;
		}

		a_sceneManager.ReserveChangeScene(m_optionSceneGUID, Engine::Scene::ESceneChangeType::Push);
	}

	//======================================================================================
	// 解放 : カーソルの固定を設定値へ戻す
	//======================================================================================
	void PauseSequence::Release(Engine::GameObject::ObjectContext& a_context)
	{
		// 借りているBGMを返す
		m_bgm.Release(a_context);

		// 絞ったぶんを戻す。戻し忘れると、ゲームへ帰った後も小さいままになる
		SequenceBgm::SetGlobalDuck(1.0f);

		if (!m_isReleaseCursorLock) return;
		if (!a_context.pServices) return;
		if (!a_context.pServices->pInputManager || !a_context.pServices->pOptionManager) return;

		// ポーズで切ったぶんを戻す。切りっぱなしにすると、
		// 戻った先で視点操作のカーソル固定が効かなくなる
		const bool _isLocked =
			a_context.pServices->pOptionManager->GetInputOption().isCursorLockedToCenter;

		a_context.pServices->pInputManager->SetCursorCentered(_isLocked);
	}

	//======================================================================================
	// シリアライズ
	//======================================================================================
	void PauseSequence::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& /*a_context*/)
	{
		a_ar.GUIDField("ResumeButtonGUID", m_resumeButtonGUID);
		a_ar.GUIDField("ExitButtonGUID", m_exitButtonGUID);
		a_ar.GUIDField("ExitSceneGUID", m_exitSceneGUID);
		Game::ActionField(a_ar, "PauseActionName", m_pauseAction);
		a_ar.Field("IsReleaseCursorLock", m_isReleaseCursorLock);
		a_ar.Field("GameBgmDuck", m_gameBgmDuck);

		m_bgm.Archive(a_ar);

		// ---- ここから下は後から足したもの : 追加は必ず末尾へ ----
		// 区切りを持たない古い .ob* には無いので読まない
		if (!a_ar.IsLegacyLayout())
		{
			a_ar.GUIDField("OptionButtonGUID", m_optionButtonGUID);
			a_ar.GUIDField("OptionSceneGUID", m_optionSceneGUID);
		}
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void PauseSequence::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		Engine::EditorField::Header("Buttons");

		// 同じシーンに置いた UIButton から選ぶ
		auto _drawButtonCombo = [&](const char* a_label, Core::GUID& a_inoutGUID)
			{
				std::string _current = a_inoutGUID.IsValid() ? a_inoutGUID.String() : "None";

				Engine::EditorField::ComboScope _combo(a_label, _current.c_str());
				if (!_combo) return;

				if (Engine::EditorField::Selectable("None", !a_inoutGUID.IsValid()))
				{
					a_inoutGUID = {};
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

						const bool _isSelected = (a_inoutGUID == _pButton->GetGUID());
						if (Engine::EditorField::Selectable(_pButton->GetGUID().String().c_str(), _isSelected))
						{
							a_inoutGUID = _pButton->GetGUID();

							// 差し込み直させる
							m_isBound = false;
						}
						if (_isSelected) Engine::EditorField::SetItemDefaultFocus();
					}
				}
			};

		_drawButtonCombo("Resume", m_resumeButtonGUID);
		_drawButtonCombo("Exit", m_exitButtonGUID);
		_drawButtonCombo("Option", m_optionButtonGUID);

		Engine::EditorField::Header("Option Scene");

		Engine::EditorField::AssetField(*a_context.pServices, "Scene", "OptionScene", m_optionSceneGUID);
		Engine::EditorField::Tooltip("Option を押したときに重ねる設定画面のシーン");
		if (!m_optionSceneGUID.IsValid())
		{
			Engine::EditorField::HelpText("(未設定 : Option を押しても開きません)");
		}

		Engine::EditorField::Header("Exit Scene");

		Engine::EditorField::AssetField(*a_context.pServices, "Scene", "Scene", m_exitSceneGUID);
		Engine::EditorField::Tooltip("やめたときの行き先(ホームなど)");
		if (!m_exitSceneGUID.IsValid())
		{
			Engine::EditorField::HelpText("(未設定 : Exit を押しても移りません)");
		}

		Engine::EditorField::Header("Input");

		Engine::EditorField::Field("Pause Action", m_pauseAction);
		Engine::EditorField::Tooltip("これを押しても閉じる。開くのと同じ名前にしておく");

		m_bgm.DrawInspector(a_context);

		Engine::EditorField::Field("GameBgmDuck", m_gameBgmDuck, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("ポーズ中、下のゲームBGMへ掛ける倍率(1で絞らない)");

		Engine::EditorField::Header("Cursor");

		Engine::EditorField::Field("ReleaseCursorLock", m_isReleaseCursorLock);
		Engine::EditorField::Tooltip("ポーズの間はカーソルの中央固定を切る");

		// 実行中の状態は表示のみ
		Engine::EditorField::Header("Runtime");
		Engine::EditorField::Value("Bound", "%s", m_isBound ? "yes" : "no");
		Engine::EditorField::Value("Closing", "%s", m_isClosing ? "yes" : "no");
	}
}
