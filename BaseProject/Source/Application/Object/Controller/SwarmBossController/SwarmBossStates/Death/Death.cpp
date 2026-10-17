#include "Death.h"

// エンジン
#include "Engine/GameObject/BaseObject/BaseObject.h"
#include "Engine/Persistence/Archive/Archive.h"
#include "Engine/ECS/System/SystemContext.h"
#include "Engine/EditorField/EditorField.h"	// コンポーネントの Traits が使うので先に置く

// App
#include "../../../../../ECS/World/APPWorld.h"
#include "../StateMachine.h"

#include "Application/Components/Movement/MoveIntentComponent.h"

namespace App::Object
{
	void SwarmBossDeathState::Enter(SwarmBossStateContext& a_context)
	{
		m_phase          = EPhase::Gather;
		m_phaseTime      = 0.0f;
		m_waveSpeedScale = 1.0f;
		m_elapsed        = 0.0f;
		m_shakeWeight    = 0.0f;
		m_bgmScale       = 1.0f;

		// 断末魔(画面の震えは Update で頼み続ける)
		a_context.cry = ESwarmBossCry::Death;

		// 中心は地表から決まった高さ
		m_ball.Begin(a_context, std::max(m_centerHeight, 0.0f));
	}

	void SwarmBossDeathState::Update(SwarmBossStateContext& a_context)
	{
		if (!a_context.pObject || !a_context.pObject->pWorld) return;
		const float _dt = a_context.pObject->dt;

		//--------------------------------------------------------------
		// 断末魔の震えと BGM の絞り(爆散した後も続ける)
		//--------------------------------------------------------------
		m_elapsed += _dt;

		const float _in  = (m_cryShakeFadeIn > 0.0f) ? std::clamp(m_elapsed / m_cryShakeFadeIn, 0.0f, 1.0f) : 1.0f;
		const float _out = (m_cryShakeFadeOut > 0.0f) ? std::clamp((m_cryShakeTime - m_elapsed) / m_cryShakeFadeOut, 0.0f, 1.0f) : 1.0f;
		m_shakeWeight = (m_elapsed < m_cryShakeTime) ? std::min(_in, _out) : 0.0f;
		a_context.roarWeight = m_shakeWeight;

		const float _bgmT = (m_bgmFadeTime > 0.0f) ? std::clamp(m_elapsed / m_bgmFadeTime, 0.0f, 1.0f) : 1.0f;
		m_bgmScale = std::lerp(1.0f, std::clamp(m_bgmDuck, 0.0f, 1.0f), _bgmT);
		a_context.bgmDuck = m_bgmScale;

		if (m_phase == EPhase::Burst) return;

		m_phaseTime += _dt;

		//--------------------------------------------------------------
		// まとまる : 経過の割合の2乗で、だんだん速く締めていく
		//--------------------------------------------------------------
		const float _t    = (m_gatherTime > 0.0f) ? std::clamp(m_phaseTime / m_gatherTime, 0.0f, 1.0f) : 1.0f;
		const float _ease = _t * _t;

		m_waveSpeedScale = std::lerp(1.0f, std::max(m_waveSpeedScaleMax, 1.0f), _ease);

		a_context.bodyDefenseRatio = m_defenseRatio;
		a_context.waveSpeedScale   = m_waveSpeedScale;

		m_ball.Update(a_context, _dt, std::lerp(1.0f, std::max(m_endRadiusScale, 0.0f), _ease));
		m_ball.Draw(a_context);

		if (_t < 1.0f) return;

		//--------------------------------------------------------------
		// 爆散 : 球の中心から外へ飛ばすよう頼む(頼むのは一度だけ)
		//--------------------------------------------------------------
		a_context.burst = m_burst;
		a_context.burst.isRequested = true;
		a_context.burst.center      = m_ball.GetCenter();

		m_phase     = EPhase::Burst;
		m_phaseTime = 0.0f;
	}

	void SwarmBossDeathState::Exit(SwarmBossStateContext& a_context)
	{
		// 抜けることは無い想定だが、入力は残さない
		if (!a_context.pObject || !a_context.pObject->pWorld) return;
		auto& _world = *a_context.pObject->pWorld;

		const auto _leader = a_context.leaderEntity;
		if (!_world.IsAliveEntity(_leader)) return;
		if (!_world.HasComponent<Component::MoveIntentComponent>(_leader)) return;

		_world.RefData<Component::MoveIntentComponent>(_leader)->value = Math::Vector3(0.0f, 0.0f, 0.0f);
	}

	void SwarmBossDeathState::Archive(Engine::Persistence::Archive& a_ar)
	{
		// ステートの調整値は同じ階層に並ぶので、名前の頭に Death を付けて区別する
		m_ball.Archive(a_ar, "Death");
		a_ar.Field("DeathCenterHeight", m_centerHeight);
		a_ar.Field("DeathEndRadiusScale", m_endRadiusScale);
		a_ar.Field("DeathGatherTime", m_gatherTime);
		a_ar.Field("DeathWaveSpeedScaleMax", m_waveSpeedScaleMax);
		a_ar.Field("DeathDefenseRatio", m_defenseRatio);

		a_ar.Field("DeathBurstSpeedMin", m_burst.speedMin);
		a_ar.Field("DeathBurstSpeedMax", m_burst.speedMax);
		a_ar.Field("DeathBurstUpBias", m_burst.upBias);
		a_ar.Field("DeathBurstLifeMin", m_burst.lifeMin);
		a_ar.Field("DeathBurstLifeMax", m_burst.lifeMax);
		a_ar.Field("DeathBurstGravity", m_burst.gravity);
		a_ar.Field("DeathBurstDrag", m_burst.drag);
		a_ar.Field("DeathBurstColor", m_burst.color);
		a_ar.Field("DeathBurstIntensity", m_burst.intensity);
	}

	void SwarmBossDeathState::ArchiveAppended(Engine::Persistence::Archive& a_ar)
	{
		a_ar.Field("DeathCryShakeTime", m_cryShakeTime);
		a_ar.Field("DeathCryShakeFadeIn", m_cryShakeFadeIn);
		a_ar.Field("DeathCryShakeFadeOut", m_cryShakeFadeOut);
		a_ar.Field("DeathBgmDuck", m_bgmDuck);
		a_ar.Field("DeathBgmFadeTime", m_bgmFadeTime);
	}

	void SwarmBossDeathState::DrawInspector()
	{
		m_ball.DrawInspector();
		Engine::EditorField::Field("球の中心の高さ", m_centerHeight, 0.5f, 0.0f);
		Engine::EditorField::Tooltip("地表から球の中心までの高さ");
		Engine::EditorField::Field("爆散直前の半径の倍率", m_endRadiusScale, 0.01f, 0.0f);
		Engine::EditorField::Field("爆散までの時間", m_gatherTime, 0.1f, 0.0f);
		Engine::EditorField::Field("ウェーブの最大倍率", m_waveSpeedScaleMax, 0.1f, 1.0f);
		Engine::EditorField::Tooltip("爆散直前のウェーブの速さの倍率(間隔はこの分の1)。経過の割合の2乗で上がる");
		Engine::EditorField::Field("体の防御比率", m_defenseRatio, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("まとまっている間に体が受けるダメージの比率(0 で無敵)");

		Engine::EditorField::Line();
		Engine::EditorField::Field("爆散 : 速さの下限", m_burst.speedMin, 0.5f, 0.0f);
		Engine::EditorField::Field("爆散 : 速さの上限", m_burst.speedMax, 0.5f, 0.0f);
		Engine::EditorField::Field("爆散 : 上向きの寄せ", m_burst.upBias, 0.01f, 0.0f);
		Engine::EditorField::Field("爆散 : 落ちるまでの時間の下限", m_burst.lifeMin, 0.01f, 0.0f);
		Engine::EditorField::Field("爆散 : 落ちるまでの時間の上限", m_burst.lifeMax, 0.01f, 0.0f);
		Engine::EditorField::Field("爆散 : 重力", m_burst.gravity, 0.5f, 0.0f);
		Engine::EditorField::Field("爆散 : 減速", m_burst.drag, 0.01f, 0.0f);
		Engine::EditorField::ColorField("爆散 : 発光色", m_burst.color);
		Engine::EditorField::Field("爆散 : 発光の強さ", m_burst.intensity, 0.1f, 0.0f);

		Engine::EditorField::Line();
		Engine::EditorField::Field("断末魔で震わせる長さ", m_cryShakeTime, 0.1f, 0.0f);
		Engine::EditorField::Tooltip("入った瞬間から。鳴き声の長さに合わせる");
		Engine::EditorField::Field("震えの立ち上がり", m_cryShakeFadeIn, 0.01f, 0.0f);
		Engine::EditorField::Field("震えの収まり", m_cryShakeFadeOut, 0.01f, 0.0f);
		Engine::EditorField::Field("BGM を下げる倍率", m_bgmDuck, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("BGM の音量に掛ける。0 で消える / 1 で下げない。爆散した後も下げたまま");
		Engine::EditorField::Field("BGM を下げきるまでの時間", m_bgmFadeTime, 0.1f, 0.0f);

		// 実行中の状態は表示のみ
		Engine::EditorField::Value("フェーズ", "%s (%.1f 秒)", std::string(magic_enum::enum_name(m_phase)).c_str(), m_phaseTime);
		Engine::EditorField::Value("ウェーブの倍率", "x %.2f", m_waveSpeedScale);
		Engine::EditorField::Value("震え / BGM", "%.2f / x %.2f", m_shakeWeight, m_bgmScale);
	}
}
