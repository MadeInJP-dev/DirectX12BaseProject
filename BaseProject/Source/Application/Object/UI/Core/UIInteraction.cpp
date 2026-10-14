#include "UIInteraction.h"

#include "Engine/GameObject/BaseObject/BaseObject.h"	// ObjectContext
#include "Engine/ECS/System/SystemContext.h"			// ObjectContext が運ぶサービス群
#include "Engine/MainEngine.h"
#include "Engine/Option/OptionManager.h"				// ウィンドウ解像度(px)取得用
#include "Engine/Input/InputManager/InputManager.h"		// カーソル位置の取得用
#include "Engine/Window/NativeWindow.h"					// クライアント領域の実サイズ取得用
#include "Engine/Audio/AudioManager.h"					// 乗った音・押した音
#include "Engine/EditorField/EditorField.h"

#include "Application/ECS/World/APPWorld.h"
#include "Application/InstanceResource/UICursorResource.h"

namespace App::Object
{
	//======================================================================================
	// 今の状態
	//======================================================================================
	Decoration::EUIState UIInteraction::GetState() const
	{
		if (!isInteractable) return Decoration::EUIState::Disabled;
		if (isPressed)       return Decoration::EUIState::Pressed;
		if (isHovered)       return Decoration::EUIState::Hovered;

		return Decoration::EUIState::Normal;
	}

	//======================================================================================
	// カーソルを受け取れる状態か
	//--------------------------------------------------------------------------------------
	// ・出していない(見えていないものは触れない)
	// ・無効にされている
	// ・プレイモードでない / エディターで文字を打っている
	//======================================================================================
	bool UIInteraction::IsReceivable(const Engine::GameObject::ObjectContext& a_context, bool a_isVisible) const
	{
		if (!a_context.pServices || !a_context.pServices->pInputManager) return false;
		if (!a_isVisible || !isInteractable) return false;

		return a_context.pServices->pInputManager->IsGameInputEnable();
	}

	//======================================================================================
	// 名乗り
	//--------------------------------------------------------------------------------------
	// 重なっているUIのうち手前の1つだけを反応させるための前半分。
	//
	// 「自分の矩形にカーソルが居るか」までをここで出し、受け取り手を決めるのは
	// 名乗りが揃ってから(Advance)。
	// 各UIがその場で hover を決めてしまうと、重なりを知らないまま全員が光る。
	//
	// ※位置を Update で動かすUIは、名乗りが1フレーム前の位置になる
	//======================================================================================
	void UIInteraction::ClaimCursor(
		Engine::GameObject::ObjectContext& a_context,
		const void* a_pOwner,
		float a_layer,
		bool a_isReceivable,
		bool a_isInside)
	{
		// 反応しないUIは名乗らない。名乗ると、自分は光らないのに
		// 下のUIだけを塞ぐ「見えない蓋」になってしまう
		isCursorInside = a_isReceivable && a_isInside;
		if (!a_isReceivable) return;

		// 押している最中は、カーソルが矩形から外れても持ち続ける。
		// 押したまま手を滑らせただけで下のUIが光り始めるのを止めるため
		// (押し切りが成立するかどうかは Advance が矩形で見ている)
		const bool _isCapture = isPressStartedInside;

		if (!isCursorInside && !_isCapture) return;

		if (!a_context.pWorld || !a_context.pWorld->HasResource<InstanceResource::UICursorResource>()) return;

		a_context.pWorld->RefResource<InstanceResource::UICursorResource>().Claim(
			a_context.frameIndex, a_pOwner, a_layer, _isCapture);
	}

	bool UIInteraction::IsCursorOwner(const Engine::GameObject::ObjectContext& a_context, const void* a_pOwner) const
	{
		if (!a_context.pWorld || !a_context.pWorld->HasResource<InstanceResource::UICursorResource>()) return false;

		return a_context.pWorld->GetResource<InstanceResource::UICursorResource>().IsOwner(a_context.frameIndex, a_pOwner);
	}

	//======================================================================================
	// 押下の進行
	//--------------------------------------------------------------------------------------
	// ・押下は「押し始めも離しも矩形の内側」で成立させる。押したまま外へ逃がせば
	//   取り消せる、よくあるボタンの作法に合わせてある。
	//
	// ・乗っているかは名乗りの結果を使う。重なっているときは
	//   手前の1つだけが受け取り手になるので、下になったUIは矩形の中に
	//   カーソルが居ても乗っていない扱いになる。
	//
	// ・入力はプレイモード中しか受け取らない(InputManager 側で止まる)。
	//   エディター操作でUIが光ったり押されたりしない。
	//======================================================================================
	void UIInteraction::Advance(Engine::GameObject::ObjectContext& a_context, const void* a_pOwner, bool a_isReceivable)
	{
		// 鳴らし直しの間引きを進める。
		// 反応しない状態でも進めておかないと、無効にしている間に止まってしまう
		if (hoverSoundCoolTime > 0.0f) hoverSoundCoolTime = std::max(hoverSoundCoolTime - a_context.dt, 0.0f);
		if (pressSoundCoolTime > 0.0f) pressSoundCoolTime = std::max(pressSoundCoolTime - a_context.dt, 0.0f);

		// 「このフレームに押し切られたか」は毎フレーム作り直す
		isClicked = false;

		// 乗った瞬間を見るために、前のフレームの状態を控えておく
		const bool _wasHovered = isHovered;

		//==================================================================
		// 反応しない状態
		//------------------------------------------------------------------
		// 状態を全部落とす。押しっぱなしのまま無効にされて、
		// 有効へ戻した瞬間に押し切られたことにならないようにする。
		//==================================================================
		if (!a_isReceivable)
		{
			isHovered = false;
			isPressed = false;
			isPressStartedInside = false;
			isCursorInside = false;
			return;
		}

		auto& _input = *a_context.pServices->pInputManager;

		//==================================================================
		// カーソルが乗っているか
		//
		// 矩形の中に居るか(名乗り)と、重なりの取り合いに勝ったかの両方。
		// 手前に別のUIが重なっているフレームは、下のUIはここで落ちる
		//==================================================================
		isHovered = isCursorInside && IsCursorOwner(a_context, a_pOwner);

		// 乗った瞬間だけ鳴らす。乗っている間ずっとだと鳴り続けてしまう
		if (isHovered && !_wasHovered)
		{
			PlaySound(a_context, hoverSoundGUID, hoverSoundHandle, hoverSoundCoolTime);
		}

		//==================================================================
		// 押下の進行
		//==================================================================
		const bool _isPressMoment   = _input.IsPress(clickAction);		// 押した瞬間
		const bool _isHoldMoment    = _input.IsHold(clickAction);		// 押している間
		const bool _isReleaseMoment = _input.IsRelease(clickAction);	// 離した瞬間

		// 内側で押し始めたときだけ受け付ける
		if (_isPressMoment && isHovered)
		{
			isPressStartedInside = true;

			// 押し切るまで待つと手応えが遅れるので、押した瞬間に鳴らす
			PlaySound(a_context, pressSoundGUID, pressSoundHandle, pressSoundCoolTime);
		}

		isPressed = isPressStartedInside && _isHoldMoment;

		if (_isReleaseMoment)
		{
			// 押し始めと離しの両方が内側なら成立
			isClicked = (isPressStartedInside && isHovered);

			isPressStartedInside = false;
			isPressed = false;
		}

		// 押していないのに押し始めの記録が残っていたら落とす
		// (ボタンを離した瞬間を取りこぼした場合の保険)
		if (!_isHoldMoment && !_isReleaseMoment)
		{
			isPressStartedInside = false;
			isPressed = false;
		}
	}

	//======================================================================================
	// 音
	//======================================================================================
	void UIInteraction::PlaySound(
		Engine::GameObject::ObjectContext& a_context,
		const Core::GUID& a_guid,
		Engine::Handle<Engine::Resource::SoundInstance>& a_inoutHandle,
		float& a_inoutCoolTime)
	{
		if (!a_guid.IsValid()) return;
		if (!a_context.pServices || !a_context.pServices->pAudioManager) return;

		//--------------------------------------------------------------
		// 間引き
		//
		// 判定の縁でカーソルが揺れると、乗った/離れたが毎フレーム入れ替わる。
		// インスタンスは1つなので音自体は重ならないが、頭出しの鳴らし直しが
		// 連続すると残響が積み上がって、だんだん大きくなったように聞こえる
		//--------------------------------------------------------------
		if (a_inoutCoolTime > 0.0f) return;
		a_inoutCoolTime = soundMinInterval;

		auto* _pAudioManager = a_context.pServices->pAudioManager;

		// 初めて鳴らすときに借りる。画面に並ぶUI全部が先に確保すると席が尽きる
		if (!a_inoutHandle.IsValid())
		{
			// 画面に出す音なので 2D で発行する(定位を付けない)。
			// UI の札を付けておくと、設定画面の UI 音量がそのまま効く
			a_inoutHandle = _pAudioManager->CreateSoundInstance(
				a_guid, false, Engine::Audio::ESoundGroup::Ui);
		}

		if (auto* _pInstance = _pAudioManager->RefInstance(a_inoutHandle))
		{
			_pInstance->SetVolume(soundVolume);
			_pInstance->Play(false);
		}
	}

	void UIInteraction::ReleaseSounds(Engine::GameObject::ObjectContext& a_context)
	{
		// サウンドインスタンスのプールはアプリ寿命なので、借りた側が必ず返す
		if (!a_context.pServices || !a_context.pServices->pAudioManager) return;

		auto* _pAudioManager = a_context.pServices->pAudioManager;
		_pAudioManager->ReleaseSoundInstance(hoverSoundHandle);
		_pAudioManager->ReleaseSoundInstance(pressSoundHandle);

		hoverSoundHandle = {};
		pressSoundHandle = {};
	}

	//======================================================================================
	// シリアライズ(UIBase::Archive から、並びの途中で呼ばれる)
	//======================================================================================
	void UIInteraction::ArchiveSettings(Engine::Persistence::Archive& a_ar)
	{
		// 名前で書き出す。アクション名で持っていた頃の古いシーンは
		// 読めない名前になるので、そのときは既定(Select)のまま残る
		Game::ActionField(a_ar, "ClickActionName", clickAction);
		a_ar.Field("HitPadding", hitPadding);
		a_ar.Field("IsInteractable", isInteractable);
		a_ar.GUIDField("HoverSoundGUID", hoverSoundGUID);
		a_ar.GUIDField("PressSoundGUID", pressSoundGUID);
		a_ar.Field("SoundVolume", soundVolume);
		a_ar.Field("SoundMinInterval", soundMinInterval);
	}

	//======================================================================================
	// カーソル位置をUIのピクセル座標へ直す
	//--------------------------------------------------------------------------------------
	// クライアント領域(実際のウィンドウの大きさ) → 描画解像度(UIの座標系)。
	// バックバッファは描画解像度で作られ、クライアント領域へ引き伸ばして表示されるので、
	// 単純に比率を掛ければよい。
	// (ウィンドウサイズを変えても判定がずれないよう、毎フレーム実測する)
	//======================================================================================
	bool UIInteraction::CalcCursorUIPos(Engine::GameObject::ObjectContext& a_context, Math::Vector2& a_outPos)
	{
		if (!a_context.pServices) return false;
		if (!a_context.pServices->pInputManager || !a_context.pServices->pOptionManager) return false;
		if (!a_context.pServices->pMainEngine) return false;

		// カーソル(クライアント座標)
		Math::Vector2 _clientPos = {};
		if (!a_context.pServices->pInputManager->GetCursorClientPos(_clientPos)) return false;

		// 描画解像度
		const auto& _winOp = a_context.pServices->pOptionManager->GetWindowOption();
		const float _renderW = static_cast<float>(_winOp.windowWidth);
		const float _renderH = static_cast<float>(_winOp.windowHeight);
		if (_renderW <= 0.0f || _renderH <= 0.0f) return false;

		// クライアント領域の実サイズ
		const auto* _pWind = a_context.pServices->pMainEngine->GetNativeWindow();
		if (!_pWind) return false;

		const float _clientW = static_cast<float>(_pWind->GetClientWidth());
		const float _clientH = static_cast<float>(_pWind->GetClientHeight());

		// 最小化中は0になる
		if (_clientW <= 0.0f || _clientH <= 0.0f) return false;

		a_outPos.x = _clientPos.x * (_renderW / _clientW);
		a_outPos.y = _clientPos.y * (_renderH / _clientH);

		return true;
	}

	//======================================================================================
	// インスペクター
	//--------------------------------------------------------------------------------------
	// 見た目の変化は飾り側(Decoration の Reaction)。ここは判定と音だけ
	//======================================================================================
	void UIInteraction::DrawSettingsInspector()
	{
		Engine::EditorField::Field("Interactable", isInteractable);
		Engine::EditorField::Tooltip("切ると Disabled 扱いになる");

		Engine::EditorField::Field("ClickAction", clickAction);
		Engine::EditorField::Tooltip("InputManager へ登録したアクション名");

		Engine::EditorField::Field("HitPadding", hitPadding, 1.0f);
		Engine::EditorField::Tooltip("判定の矩形へ足す余白(px)");

		Engine::EditorField::Field("HitFollowAnim", isHitFollowAnim);
		Engine::EditorField::Tooltip("飾りのアニメ・反応で大きくなったぶんも判定に入れる(PixelSize より優先)");
	}

	void UIInteraction::DrawSoundInspector(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pServices) return;

		// 音を差し替えたら、借りているインスタンスを返して取り直させる
		if (Engine::EditorField::AssetField(*a_context.pServices, "HoverSound", "Sound", hoverSoundGUID))
		{
			ReleaseSounds(a_context);
		}
		if (Engine::EditorField::AssetField(*a_context.pServices, "PressSound", "Sound", pressSoundGUID))
		{
			ReleaseSounds(a_context);
		}
		Engine::EditorField::Field("SoundVolume", soundVolume, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("SoundMinInterval", soundMinInterval, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("鳴らし直す最短間隔(秒)。縁で揺れて鳴り続けるのを止める");

		// 実行中の状態は表示のみ
		static const char* STATE_NAME[] = { "Normal", "Hovered", "Pressed", "Disabled" };
		Engine::EditorField::Value("State", "%s", STATE_NAME[static_cast<int>(GetState())]);
	}
}
