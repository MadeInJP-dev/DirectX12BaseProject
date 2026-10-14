#include "CombatReticleHUD.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"
#include "Engine/Common/Color.h"
#include "Engine/Option/OptionManager.h"	// ウィンドウ解像度(px)取得用

#include "Application/ECS/World/APPWorld.h"

#include "Application/InstanceResource/PlayerHUDResource.h"

namespace App::Object
{
	namespace
	{
		// テスト用レティクルテクスチャのパス
		constexpr const char* RETICLE_TEXTURE_PATH = "Asset/Texture/Test/uiTest.png";
	}

	void CombatReticleHUD::PostDeserialize(Engine::GameObject::ObjectContext& a_context)
	{
		// リソース周りはコンテキストが運んできたサービスを使う
		if (!a_context.pServices || !a_context.pServices->pAssetDatabase) return;

		// 既定のレティクルを1つ用意する。作るのは飾りを1つも持っていないときだけなので、
		// 保存された飾りを持つシーンでは何もしない
		if (m_decorationVec.empty())
		{
			Decoration::Decoration& _reticle = AddDecoration(Decoration::EDecorationType::Image);
			_reticle.name = "Reticle";
			_reticle.pixelSize = m_anchor.pixelSize;
			_reticle.RefImage()->texGUID = a_context.pServices->pAssetDatabase->GetGUIDFromFilePath(RETICLE_TEXTURE_PATH);
		}
	}

	//======================================================================================
	// リソースの要求
	//======================================================================================
	void CombatReticleHUD::Awake(Engine::GameObject::ObjectContext& a_context)
	{
		// 実体の到着は待たない。描画側が IsReady を見てスキップする
		RequestDecorationResources(a_context);
	}

	float CombatReticleHUD::CalcArtRadius() const
	{
		// 縦横で違う場合は小さい方。円としてはみ出さない側に合わせる
		const float _half = std::min(m_anchor.pixelSize.x, m_anchor.pixelSize.y) * 0.5f;
		return std::max(_half, 0.0f);
	}

	void CombatReticleHUD::Update(Engine::GameObject::ObjectContext& a_context)
	{
		// 飾りのアニメーションを進める
		UIBase::Update(a_context);

		m_hasReticle = false;

		auto* _pWorld = a_context.pWorld;
		if (!_pWorld) return;
		if (!_pWorld->HasResource<InstanceResource::PlayerHUDResource>()) return;

		// 見た目の円は MissileLockComponent のもの(HUDGatherSystem が集めてある)。
		// ここからは書き込まない : 判定はゲーム側が持ち、UI は見た目を合わせるだけ
		const auto& _hud = _pWorld->GetResource<InstanceResource::PlayerHUDResource>();
		if (!_hud.missileReticle.isValid) return;

		m_reticleCenter = _hud.missileReticle.center;
		m_reticleRadius = _hud.missileReticle.radius;
		m_hasReticle = true;
	}

	void CombatReticleHUD::Draw(Engine::GameObject::ObjectContext& a_context)
	{
		// 円が届いていない(プレイ中でない・プレイヤーが居ない)ときは置いたとおりに出す
		if (!m_hasReticle)
		{
			UIBase::Draw(a_context);
			return;
		}

		// アンカーに内接する円が、見た目の半径と同じ大きさになるよう拡大して描く
		Decoration::DrawOverride _override = {};
		_override.isUsePos = true;
		_override.pixelPos = m_reticleCenter;

		const float _artRadius = CalcArtRadius();
		if (_artRadius > 0.0f) _override.scale = m_reticleRadius / _artRadius;

		DrawDecorations(a_context, _override);
	}

	void CombatReticleHUD::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		UIBase::DrawInspector(a_context);

		Engine::EditorField::Header("Missile Lock");
		Engine::EditorField::HelpText("収集の円はプレイヤーの MissileLockComponent(ReticleRadius × ReticleScale)。ここは絵を合わせるだけ");

		Engine::EditorField::Value("ArtRadius", "%.1f px", CalcArtRadius());
		Engine::EditorField::Tooltip("置いたままの絵で、見た目の円に当たる半径(アンカーの PixelSize に内接)。\nプレイ中はこれが ReticleRadius と同じ大きさになるよう拡大して、画面中央へ出す");

		if (m_hasReticle)
		{
			Engine::EditorField::Value("ReticleRadius", "%.1f px", m_reticleRadius);
		}
	}
}
