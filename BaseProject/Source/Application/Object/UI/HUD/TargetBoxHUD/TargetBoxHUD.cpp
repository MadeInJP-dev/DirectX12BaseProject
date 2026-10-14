#include "TargetBoxHUD.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/InstanceResource/PlayerHUDResource.h"

namespace App::Object
{
	namespace
	{
		// 既定のターゲットボックステクスチャのパス
		constexpr const char* TARGET_BOX_TEXTURE_PATH = "Asset/Texture/Reticle/Robot01.png";

		// 新規追加時の既定表示サイズ(px)
		constexpr float DEFAULT_BOX_SIZE = 96.0f;
	}

	void TargetBoxHUD::PostDeserialize(Engine::GameObject::ObjectContext& a_context)
	{
		// リソース周りはコンテキストが運んできたサービスを使う
		if (!a_context.pServices || !a_context.pServices->pAssetDatabase) return;

		// サイズが0のままだと何も見えないので、既定サイズを入れておく。
		// 保存値を読み終えた後に見るので、シーンに入っている値は潰さない
		if (m_anchor.pixelSize.x <= 0.0f || m_anchor.pixelSize.y <= 0.0f)
		{
			m_anchor.pixelSize = { DEFAULT_BOX_SIZE, DEFAULT_BOX_SIZE };
			m_anchor.editSize = m_anchor.pixelSize;
		}

		//--------------------------------------------------------------
		// 既定の枠を1つ用意する
		//
		// 作るのは飾りを1つも持っていないときだけなので、
		// 保存された飾りを持つシーンでは何もしない
		//--------------------------------------------------------------
		if (m_decorationVec.empty())
		{
			Decoration::Decoration& _box = AddDecoration(Decoration::EDecorationType::Image);
			_box.name = "TargetBox";
			_box.group = GROUP_NORMAL;
			_box.pixelSize = m_anchor.pixelSize;
			_box.RefImage()->texGUID = a_context.pServices->pAssetDatabase->GetGUIDFromFilePath(TARGET_BOX_TEXTURE_PATH);
		}
	}

	//======================================================================================
	// リソースの要求
	//======================================================================================
	void TargetBoxHUD::Awake(Engine::GameObject::ObjectContext& a_context)
	{
		// 実体の到着は待たない。描画側が IsReady を見てスキップする
		RequestDecorationResources(a_context);
	}

	//======================================================================================
	// その群の飾りを持っているか
	//======================================================================================
	bool TargetBoxHUD::HasDecorationGroup(uint32_t a_group) const
	{
		for (const Decoration::Decoration& _decoration : m_decorationVec)
		{
			if (_decoration.group == a_group) return true;
		}
		return false;
	}

	void TargetBoxHUD::Update(Engine::GameObject::ObjectContext& a_context)
	{
		// 飾りのアニメーションを進める
		UIBase::Update(a_context);

		// このフレームぶんを作り直す
		m_targetScreenPosVec.clear();
		m_isLocked = false;

		auto* _pWorld = a_context.pWorld;
		if (!_pWorld) return;
		if (!_pWorld->HasResource<InstanceResource::PlayerHUDResource>()) return;

		//==================================================================
		// プレイヤーのロック結果を読む
		//------------------------------------------------------------------
		// 射影(ワールド→スクリーン)もレティクル内の判定も LockOnTargetSystem が済ませ、
		// HUDGatherSystem が PlayerHUDResource へまとめてある。ここは読むだけ
		//==================================================================
		const auto& _hud = _pWorld->GetResource<InstanceResource::PlayerHUDResource>();

		for (int _i = 0; _i < _hud.targetCount; ++_i)
		{
			// ロック中の相手は赤い枠で別に描くので、黄色の枠からは外す
			if (_hud.targets[_i].isLocked) continue;

			m_targetScreenPosVec.push_back(_hud.targets[_i].screenPos);
		}

		if (_hud.isLocked)
		{
			m_lockedScreenPos = _hud.lockedScreenPos;
			m_isLocked = true;
		}
	}

	void TargetBoxHUD::Draw(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_targetScreenPosVec.empty() && !m_isLocked) return;

		//--------------------------------------------------------------
		// 画面内の敵 : 群 0 の飾りを、位置だけ差し替えて敵の数ぶん出す
		//--------------------------------------------------------------
		Decoration::DrawOverride _normal = {};
		_normal.isUsePos = true;
		_normal.isUseGroup = true;
		_normal.group = GROUP_NORMAL;

		for (const Math::Vector2& _screenPos : m_targetScreenPosVec)
		{
			_normal.pixelPos = _screenPos;
			DrawDecorations(a_context, _normal);
		}

		if (!m_isLocked) return;

		//--------------------------------------------------------------
		// ロック中の相手
		//
		// 専用の飾り(群 1)があればそれを、無ければ通常枠を LockColor で染めて代用する。
		// 枠が消えてしまうより、色が変わったほうがロックされたことが分かるため
		//--------------------------------------------------------------
		const bool _hasLockDecoration = HasDecorationGroup(GROUP_LOCK);

		Decoration::DrawOverride _lock = {};
		_lock.isUsePos = true;
		_lock.pixelPos = m_lockedScreenPos;
		_lock.scale = m_lockSizeScale;
		_lock.tint = m_lockColor;
		_lock.isUseGroup = true;
		_lock.group = _hasLockDecoration ? GROUP_LOCK : GROUP_NORMAL;

		DrawDecorations(a_context, _lock);
	}

	void TargetBoxHUD::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context)
	{
		// テクスチャ・色・サイズなどの共通ぶん
		UIBase::Archive(a_ar, a_context);

		// ここから下は TargetBoxHUD のぶん。基底(UIBase)とは区切りを分けてあるので、
		// どちらに足しても互いの読み出しはずれない。足すときはこの区切りの末尾へ
		Engine::Persistence::ArchiveSection _section(a_ar, "TargetBoxHUD");

		// 旧形式(ロック枠テクスチャ1枚)の名残。並びを変えないため読み書きは続ける
		a_ar.GUIDField("LockTexGUID", m_legacyLockTexGUID);
		a_ar.Field("LockSizeScale", m_lockSizeScale);
		a_ar.Field("LockColor", m_lockColor);

		if (!a_ar.IsLoading()) return;

		//--------------------------------------------------------------
		// 旧形式からの引き継ぎ
		// ロック枠のテクスチャを持っていたシーンは、群 1 の飾りへ移し替える
		//--------------------------------------------------------------
		if (m_legacyLockTexGUID.IsValid() && !HasDecorationGroup(GROUP_LOCK))
		{
			Decoration::Decoration& _lockBox = AddDecoration(Decoration::EDecorationType::Image);
			_lockBox.name = "LockBox";
			_lockBox.group = GROUP_LOCK;
			_lockBox.pixelSize = m_anchor.pixelSize;
			_lockBox.RefImage()->texGUID = m_legacyLockTexGUID;

			RequestDecorationResources(a_context);
		}
	}

	void TargetBoxHUD::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		UIBase::DrawInspector(a_context);

		Engine::EditorField::Header("TargetBox");

		if (!a_context.pServices || !a_context.pServices->pResourceManager) return;

		// ロック枠へ掛ける色。飾りの色へ乗算で乗る
		Engine::EditorField::Field("LockColor", m_lockColor);
		Engine::EditorField::Field("LockSizeScale", m_lockSizeScale, 0.01f, 0.0f, 8.0f);

		Engine::EditorField::HelpText("飾りの Group : 0 = 通常枠 / 1 = ロック枠");
		Engine::EditorField::Value("Lock decoration", "%s", HasDecorationGroup(GROUP_LOCK) ? "yes" : "no (通常枠を LockColor で代用)");

		// 枠は画面内の敵すべてに出る。
		// ロック(赤枠)の判定半径と距離はプレイヤー側(LockOnTargetComponent)の設定
		Engine::EditorField::HelpText("Boxes : every enemy on screen (within MaxDistance)");
		Engine::EditorField::HelpText("Lock radius / range : Player's LockOnTargetComponent");
		Engine::EditorField::HelpText("PixelPos is unused (follows enemies)");
		Engine::EditorField::Value("Boxes", "%d%s", static_cast<int>(m_targetScreenPosVec.size()), m_isLocked ? " (+lock)" : "");
	}
}
