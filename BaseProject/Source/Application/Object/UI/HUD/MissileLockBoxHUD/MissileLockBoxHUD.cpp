#include "MissileLockBoxHUD.h"

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
		// 既定の枠テクスチャのパス
		constexpr const char* LOCK_BOX_TEXTURE_PATH = "Asset/Texture/Reticle/Robot01.png";

		// 新規追加時の既定表示サイズ(px)
		constexpr float DEFAULT_BOX_SIZE = 80.0f;
	}

	void MissileLockBoxHUD::PostDeserialize(Engine::GameObject::ObjectContext& a_context)
	{
		// リソース周りはコンテキストが運んできたサービスを使う
		if (!a_context.pServices || !a_context.pServices->pAssetDatabase) return;

		// サイズが0のままだと何も見えないので、既定サイズと色を入れておく。
		// 保存値を読み終えた後に見るので、シーンに入っている値は潰さない
		if (m_anchor.pixelSize.x <= 0.0f || m_anchor.pixelSize.y <= 0.0f)
		{
			m_anchor.pixelSize = { DEFAULT_BOX_SIZE, DEFAULT_BOX_SIZE };
			m_anchor.editSize  = m_anchor.pixelSize;

			// ミサイルの溜めは黄色の枠
			m_anchor.color = Math::Color(1.0f, 1.0f, 0.0f, 1.0f);
		}

		// 既定の枠を1つ用意する。作るのは飾りを1つも持っていないときだけなので、
		// 保存された飾りを持つシーンでは何もしない
		if (m_decorationVec.empty())
		{
			Decoration::Decoration& _box = AddDecoration(Decoration::EDecorationType::Image);
			_box.name = "LockBox";
			_box.pixelSize = m_anchor.pixelSize;
			_box.RefImage()->texGUID = a_context.pServices->pAssetDatabase->GetGUIDFromFilePath(LOCK_BOX_TEXTURE_PATH);
		}
	}

	//======================================================================================
	// リソースの要求
	//======================================================================================
	void MissileLockBoxHUD::Awake(Engine::GameObject::ObjectContext& a_context)
	{
		// 実体の到着は待たない。描画側が IsReady を見てスキップする
		RequestDecorationResources(a_context);
	}

	void MissileLockBoxHUD::Update(Engine::GameObject::ObjectContext& a_context)
	{
		// 飾りのアニメーションを進める
		UIBase::Update(a_context);

		// このフレームぶんを作り直す
		m_lockScreenPosVec.clear();

		auto* _pWorld = a_context.pWorld;
		if (!_pWorld) return;
		if (!_pWorld->HasResource<InstanceResource::PlayerHUDResource>()) return;

		//==================================================================
		// プレイヤーの溜め結果を読む
		//------------------------------------------------------------------
		// 射影も円の内外判定も MissileSalvoSystem が済ませ、
		// HUDGatherSystem が PlayerHUDResource へまとめてある。ここは読むだけ
		//==================================================================
		const auto& _hud = _pWorld->GetResource<InstanceResource::PlayerHUDResource>();

		// 押している間だけ出す。撃った瞬間に溜めは捨てられるので枠も消える
		if (!_hud.isMissileCharging) return;

		for (int _i = 0; _i < _hud.missileLockCount; ++_i)
		{
			m_lockScreenPosVec.push_back(_hud.missileLockScreenPos[_i]);
		}
	}

	void MissileLockBoxHUD::Draw(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_lockScreenPosVec.empty()) return;

		// 見た目は飾りそのまま。位置だけ敵ごとに差し替える
		Decoration::DrawOverride _override = {};
		_override.isUsePos = true;

		for (const Math::Vector2& _screenPos : m_lockScreenPosVec)
		{
			_override.pixelPos = _screenPos;
			DrawDecorations(a_context, _override);
		}
	}

	void MissileLockBoxHUD::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		UIBase::DrawInspector(a_context);

		Engine::EditorField::Header("MissileLockBox");
		Engine::EditorField::HelpText("ミサイルキーを押している間、溜めた敵を囲みます");
		Engine::EditorField::HelpText("収集範囲・弾数はプレイヤーの MissileLockComponent");
		Engine::EditorField::HelpText("PixelPos is unused (follows enemies)");
		Engine::EditorField::Value("Boxes", "%d", static_cast<int>(m_lockScreenPosVec.size()));
	}
}
