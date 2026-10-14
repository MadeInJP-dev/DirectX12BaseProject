#include "HitEffectHUD.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Audio/AudioManager.h"
#include "Engine/Option/OptionManager.h"			// ウィンドウ解像度(px)取得用

#include "Application/ECS/World/APPWorld.h"

#include "Application/InstanceResource/PlayerHUDResource.h"

namespace App::Object
{
	namespace
	{
		// 新規追加時の既定表示サイズ(px)
		constexpr float DEFAULT_MARK_SIZE = 64.0f;
	}

	void HitEffectHUD::PostDeserialize(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pServices || !a_context.pServices->pOptionManager) return;

		// サイズが0のままだと何も見えないので、既定サイズと画面中央を入れておく。
		// 保存値を読み終えた後に見るので、シーンに入っている値は潰さない
		if (m_anchor.pixelSize.x <= 0.0f || m_anchor.pixelSize.y <= 0.0f)
		{
			m_anchor.pixelSize = { DEFAULT_MARK_SIZE, DEFAULT_MARK_SIZE };
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
	void HitEffectHUD::Awake(Engine::GameObject::ObjectContext& a_context)
	{
		// 飾り(クロスマーク)は差し替え前提なので既定の絵は持たない
		RequestDecorationResources(a_context);

		CreateSound(a_context);
	}

	void HitEffectHUD::Release(Engine::GameObject::ObjectContext& a_context)
	{
		UIBase::Release(a_context);

		// サウンドインスタンスのプールはアプリ寿命なので、借りた側が必ず返す
		if (a_context.pServices && a_context.pServices->pAudioManager)
		{
			a_context.pServices->pAudioManager->ReleaseSoundInstance(m_soundHandle);
		}
		m_soundHandle = {};
	}

	void HitEffectHUD::CreateSound(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pServices || !a_context.pServices->pAudioManager) return;

		auto* _pAudioManager = a_context.pServices->pAudioManager;

		// 借りていたものは先に返す(差し替えのたびに溜まらないように)
		_pAudioManager->ReleaseSoundInstance(m_soundHandle);
		m_soundHandle = {};

		if (m_soundGUID == Core::DEFAULT_GUID) return;

		// 画面に出す音なので 2D で発行する(定位を付けない)
		m_soundHandle = _pAudioManager->CreateSoundInstance(m_soundGUID, false);

		if (auto* _pInstance = _pAudioManager->RefInstance(m_soundHandle))
		{
			_pInstance->SetVolume(m_volume);
		}
	}

	void HitEffectHUD::OnHit(Engine::GameObject::ObjectContext& a_context)
	{
		++m_hitCount;

		// 出ている最中に当たったら、そこから出し直す
		m_remainTime = m_showTime;

		// 間引き中は鳴らさない(連射で頭出しを繰り返して潰れるのを防ぐ)
		if (m_coolTime > 0.0f) return;
		m_coolTime = m_minInterval;

		if (!a_context.pServices || !a_context.pServices->pAudioManager) return;

		if (auto* _pInstance = a_context.pServices->pAudioManager->RefInstance(m_soundHandle))
		{
			_pInstance->SetVolume(m_volume);
			_pInstance->Play(false);
		}
	}

	void HitEffectHUD::Update(Engine::GameObject::ObjectContext& a_context)
	{
		// 飾りのアニメーションを進める
		UIBase::Update(a_context);

		// 表示時間と間引きを進める
		if (m_remainTime > 0.0f) m_remainTime = std::max(m_remainTime - a_context.dt, 0.0f);
		if (m_coolTime > 0.0f)   m_coolTime   = std::max(m_coolTime - a_context.dt, 0.0f);

		auto* _pWorld = a_context.pWorld;
		if (!_pWorld) return;
		if (!_pWorld->HasResource<InstanceResource::PlayerHUDResource>()) return;

		//==================================================================
		// プレイヤーの弾が当たったか
		//------------------------------------------------------------------
		// どのヒットを手応えとして数えるか(自分の弾か・ダメージの通る相手か)は
		// HUDGatherSystem が決めて、当たったフレームに hitSerial を1つ進める。
		// ここは前に見た番号と比べるだけ。同じフレームに何発当たっても出し直しは1回
		//==================================================================
		const auto& _hud = _pWorld->GetResource<InstanceResource::PlayerHUDResource>();

		// 初めて見るフレームは合わせるだけ(途中から置かれたときに、出た瞬間に光らないように)
		if (!m_isHitSerialSynced)
		{
			m_lastHitSerial = _hud.hitSerial;
			m_isHitSerialSynced = true;
			return;
		}

		if (_hud.hitSerial == m_lastHitSerial) return;
		m_lastHitSerial = _hud.hitSerial;

		OnHit(a_context);
	}

	void HitEffectHUD::Draw(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_remainTime <= 0.0f) return;

		// 残り時間の割合(1 → 0)。出た瞬間が 1
		const float _rate = (m_showTime > 1e-4f)
			? std::clamp(m_remainTime / m_showTime, 0.0f, 1.0f)
			: 1.0f;

		Decoration::DrawOverride _override = {};

		// 出た瞬間だけ少し大きく見せる(punchScale → 等倍へ戻る)
		_override.scale = 1.0f + (m_punchScale - 1.0f) * _rate;

		// 消えぎわに薄くする : 掛ける色なので、飾りごとの色はそのまま残る
		if (m_isFadeOut) _override.tint.a = _rate;

		DrawDecorations(a_context, _override);
	}

	void HitEffectHUD::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context)
	{
		// テクスチャ・色・サイズなどの共通ぶん
		UIBase::Archive(a_ar, a_context);

		// ここから下は HitEffectHUD のぶん。基底(UIBase)とは区切りを分けてあるので、
		// どちらに足しても互いの読み出しはずれない。足すときはこの区切りの末尾へ
		Engine::Persistence::ArchiveSection _section(a_ar, "HitEffectHUD");

		a_ar.GUIDField("SoundGUID", m_soundGUID);
		a_ar.Field("Volume", m_volume);
		a_ar.Field("ShowTime", m_showTime);
		a_ar.Field("MinInterval", m_minInterval);
		a_ar.Field("IsFadeOut", m_isFadeOut);
		a_ar.Field("PunchScale", m_punchScale);

		// 読み込み時は復元したGUIDでサウンドを取り直す
		if (a_ar.IsLoading())
		{
			CreateSound(a_context);
		}
	}

	void HitEffectHUD::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		UIBase::DrawInspector(a_context);

		Engine::EditorField::Header("HitEffect");

		// ヒット音(アセットDBの Sound 一覧から選ぶ)
		if (Engine::EditorField::AssetField(
			*a_context.pServices,
			"Hit Sound",
			"Sound",
			m_soundGUID))
		{
			CreateSound(a_context);
		}

		if (Engine::EditorField::Field("Volume", m_volume, 0.01f, 0.0f, 1.0f))
		{
			// 鳴らしながら調整できるよう、発行済みインスタンスへ即時反映する
			if (a_context.pServices && a_context.pServices->pAudioManager)
			{
				if (auto* _pInstance = a_context.pServices->pAudioManager->RefInstance(m_soundHandle))
				{
					_pInstance->SetVolume(m_volume);
				}
			}
		}

		Engine::EditorField::Line();
		Engine::EditorField::Field("ShowTime", m_showTime, 0.01f, 0.0f, 5.0f);
		Engine::EditorField::Field("MinInterval", m_minInterval, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("FadeOut", m_isFadeOut);
		Engine::EditorField::Field("PunchScale", m_punchScale, 0.01f, 0.1f, 4.0f);

		// 確認用に鳴らしてみる
		if (Engine::EditorField::Button("Test")) OnHit(a_context);

		Engine::EditorField::Line();
		Engine::EditorField::Value("HitCount", "%d", m_hitCount);
		Engine::EditorField::Value("Remain", "%.2f", m_remainTime);
		Engine::EditorField::Tooltip("自分が撃った弾が HealthComponent 持ちに当たったフレームに反応します");
	}
}
