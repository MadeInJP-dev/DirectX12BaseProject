#include "AimReticleHUD.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Option/OptionManager.h"			// ウィンドウ解像度(px)取得用

#include "Application/ECS/World/APPWorld.h"

#include "Application/InstanceResource/PlayerHUDResource.h"

namespace App::Object
{
	namespace
	{
		// 新規追加時の既定表示サイズ(px)
		constexpr float DEFAULT_RETICLE_SIZE = 160.0f;
	}

	void AimReticleHUD::PostDeserialize(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pServices || !a_context.pServices->pOptionManager) return;

		// サイズが0のままだと何も見えないので、既定サイズと画面中央を入れておく。
		// 保存値を読み終えた後に見るので、シーンに入っている値は潰さない
		if (m_anchor.pixelSize.x <= 0.0f || m_anchor.pixelSize.y <= 0.0f)
		{
			m_anchor.pixelSize = { DEFAULT_RETICLE_SIZE, DEFAULT_RETICLE_SIZE };
			m_anchor.editSize  = m_anchor.pixelSize;

			const auto& _winOp = a_context.pServices->pOptionManager->GetWindowOption();
			m_anchor.pixelPos = {
				static_cast<float>(_winOp.windowWidth) * 0.5f,
				static_cast<float>(_winOp.windowHeight) * 0.5f
			};
		}
	}

	//======================================================================================
	// リソースの要求
	//======================================================================================
	void AimReticleHUD::Awake(Engine::GameObject::ObjectContext& a_context)
	{
		// 飾りは差し替え前提なので既定の絵は持たない。
		// 実体の到着は待たない(描画側が IsReady を見てスキップする)
		RequestDecorationResources(a_context);
	}

	float AimReticleHUD::CalcArtRadius() const
	{
		if (!m_isUseTextureSize) return std::max(m_lockRadius, 0.0f);

		// 縦横で違う場合は小さい方。円としてはみ出さない側に合わせる
		const float _half = std::min(m_anchor.pixelSize.x, m_anchor.pixelSize.y) * 0.5f;
		return std::max(_half * m_radiusScale, 0.0f);
	}

	void AimReticleHUD::Update(Engine::GameObject::ObjectContext& a_context)
	{
		// 飾りのアニメーションを進める
		UIBase::Update(a_context);

		m_hasReticle = false;

		auto* _pWorld = a_context.pWorld;
		if (!_pWorld) return;
		if (!_pWorld->HasResource<InstanceResource::PlayerHUDResource>()) return;

		// 判定の円は LockOnTargetComponent のもの(HUDGatherSystem が集めてある)。
		// ここからは書き込まない : 判定はゲーム側が持ち、UI は見た目を合わせるだけ
		const auto& _hud = _pWorld->GetResource<InstanceResource::PlayerHUDResource>();
		if (!_hud.aimReticle.isValid) return;

		m_reticleCenter = _hud.aimReticle.center;
		m_reticleRadius = _hud.aimReticle.radius;
		m_hasReticle = true;
	}

	void AimReticleHUD::Draw(Engine::GameObject::ObjectContext& a_context)
	{
		// 判定の円が届いていない(プレイ中でない・プレイヤーが居ない)ときは置いたとおりに出す
		if (!m_hasReticle)
		{
			UIBase::Draw(a_context);
			return;
		}

		// アンカーの上の「判定の円に当たる半径」が、判定の半径と同じ大きさになるよう拡大して描く
		Decoration::DrawOverride _override = {};
		_override.isUsePos = true;
		_override.pixelPos = m_reticleCenter;

		const float _artRadius = CalcArtRadius();
		if (_artRadius > 0.0f) _override.scale = m_reticleRadius / _artRadius;

		DrawDecorations(a_context, _override);
	}

	void AimReticleHUD::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context)
	{
		// テクスチャ・色・サイズなどの共通ぶん
		UIBase::Archive(a_ar, a_context);

		// ここから下は AimReticleHUD のぶん。基底(UIBase)とは区切りを分けてあるので、
		// どちらに足しても互いの読み出しはずれない。足すときはこの区切りの末尾へ
		Engine::Persistence::ArchiveSection _section(a_ar, "AimReticleHUD");

		a_ar.Field("IsUseTextureSize", m_isUseTextureSize);
		a_ar.Field("RadiusScale", m_radiusScale);
		a_ar.Field("LockRadius", m_lockRadius);
	}

	void AimReticleHUD::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		UIBase::DrawInspector(a_context);

		Engine::EditorField::Header("AutoAim");

		Engine::EditorField::HelpText("判定の円はプレイヤーの LockOnTargetComponent(ReticleRadius)。ここは絵を合わせるだけ");

		// 絵の上で、判定の円に当たる半径の作り方
		Engine::EditorField::Field("UseTextureSize", m_isUseTextureSize);
		Engine::EditorField::Tooltip("アンカーの PixelSize から作る(飾りの大きさではない)");
		if (m_isUseTextureSize)
		{
			Engine::EditorField::Field("RadiusScale", m_radiusScale, 0.01f, 0.0f, 4.0f);
		}
		else
		{
			Engine::EditorField::Field("LockRadius", m_lockRadius, 1.0f, 0.0f, 4096.0f);
		}

		Engine::EditorField::Value("ArtRadius", "%.1f px", CalcArtRadius());
		Engine::EditorField::Tooltip("置いたままの絵で、判定の円に当たる半径。\nプレイ中はこれが判定の半径と同じ大きさになるよう拡大して、画面中央へ出す");

		if (m_hasReticle)
		{
			Engine::EditorField::Value("JudgeRadius", "%.1f px", m_reticleRadius);
		}
	}
}
