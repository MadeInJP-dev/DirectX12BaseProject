#include "ScoreHUD.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/ECS/World/World.h"
#include "Engine/EditorField/EditorField.h"

#include "Application/InstanceResource/GameDataResource.h"

namespace App::Object
{
	//======================================================================================
	// 出す数を GlobalGameContext から取る
	//======================================================================================
	int ScoreHUD::PickValue(const Game::GlobalGameContext* a_pGameData) const
	{
		if (!a_pGameData) return 0;
		const auto& _gameData = *a_pGameData;

		switch (m_valueKind)
		{
		case EScoreValueKind::Time:
			// 秒だけ出す。小数は切り捨て(桁が細かく動くと読めないため)
			return static_cast<int>(std::max(_gameData.time, 0.0f));

		case EScoreValueKind::KillCount:
			return _gameData.killCount;

		case EScoreValueKind::Score:
		default:
			return _gameData.score;
		}
	}

	void ScoreHUD::Update(Engine::GameObject::ObjectContext& a_context)
	{
		// 飾りのアニメーションを進める
		UIBase::Update(a_context);

		// 戻りを進める
		if (m_punchTimer > 0.0f)
		{
			m_punchTimer = std::max(m_punchTimer - a_context.dt, 0.0f);
		}

		m_value = PickValue(InstanceResource::GameDataResource::Find(a_context.pWorld));

		//==================================================================
		// 増えたフレームだけ弾ませる
		//------------------------------------------------------------------
		// 前フレームの数と比べるだけにしてある。加算した側に
		// 「このフレームで入ったぶん」を持たせると、
		// 加算する側が居ないシーン(リザルト)で立ちっぱなしになるため。
		//
		// 出た最初のフレームは比べる相手が無いので弾ませない
		// (リザルトへ来た瞬間にスコアが跳ねてしまう)
		//==================================================================
		if (!m_isFirst && m_value > m_prevValue)
		{
			m_punchTimer = m_punchTime;
		}

		m_prevValue = m_value;
		m_isFirst = false;
	}

	void ScoreHUD::DrawDigit(
		Engine::GameObject::ObjectContext& a_context,
		int a_digit,
		int a_index,
		float a_scale)
	{
		// 桁の送りは「元の大きさ」で決める。
		// 弾んでいる間の大きさで送ると、拡大するたびに桁が横へ広がってしまう
		const float _step = m_anchor.pixelSize.x + m_digitSpacing;

		//--------------------------------------------------------------
		// 1枚に並んだ数字から1コマだけ切り出す
		//
		// uv * uvScale + uvOffset なので、倍率がコマの幅、
		// オフセットが「何コマ目か」になる。
		// どちらも AtlasCount から出せるので、飾り側の設定には頼らず差し替える
		//--------------------------------------------------------------
		const float _cellWidth = 1.0f / static_cast<float>(m_atlasCount);

		Decoration::DrawOverride _override = {};
		_override.isUsePos = true;
		_override.pixelPos = m_anchor.pixelPos;
		_override.pixelPos.x += _step * static_cast<float>(a_index);
		_override.scale = a_scale;

		_override.isUseUvScale = true;
		_override.uvScale = { _cellWidth, 1.0f };
		_override.uvOffsetAdd = { _cellWidth * static_cast<float>(a_digit), 0.0f };

		DrawDecorations(a_context, _override);
	}

	void ScoreHUD::Draw(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pServices || !a_context.pServices->pMainEngine) return;
		if (m_digitCount <= 0 || m_atlasCount <= 0) return;

		// 弾みの割合(1 → 0)。増えた瞬間が 1
		float _rate = 0.0f;
		if (m_punchTime > 1e-4f)
		{
			_rate = std::clamp(m_punchTimer / m_punchTime, 0.0f, 1.0f);
		}
		const float _scale = 1.0f + (m_punchScale - 1.0f) * _rate;

		//--------------------------------------------------------------
		// 桁を左から並べる
		//
		// 上の桁から順に取り出す。表示桁を超えたぶんは出せないので、
		// 一番上の桁で頭打ちにする(999999 のまま止まる)
		//--------------------------------------------------------------
		int _value = std::max(m_value, 0);

		// 表示できる最大値でクランプ(10^digitCount - 1)
		int _max = 1;
		for (int _i = 0; _i < m_digitCount; ++_i)
		{
			_max *= 10;

			// 桁数を大きくしすぎたときに int があふれないよう止める
			if (_max >= 1000000000) break;
		}
		_value = std::min(_value, _max - 1);

		int _divisor = _max / 10;
		for (int _i = 0; _i < m_digitCount; ++_i)
		{
			if (_divisor <= 0) break;

			const int _digit = (_value / _divisor) % 10;
			DrawDigit(a_context, _digit, _i, _scale);

			_divisor /= 10;
		}
	}

	void ScoreHUD::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context)
	{
		// テクスチャ・色・サイズなどの共通ぶん
		UIBase::Archive(a_ar, a_context);

		// ここから下は ScoreHUD のぶん。基底(UIBase)とは区切りを分けてあるので、
		// どちらに足しても互いの読み出しはずれない。足すときはこの区切りの末尾へ
		Engine::Persistence::ArchiveSection _section(a_ar, "ScoreHUD");

		a_ar.Field("ValueKind", m_valueKind);
		a_ar.Field("DigitCount", m_digitCount);
		a_ar.Field("AtlasCount", m_atlasCount);
		a_ar.Field("DigitSpacing", m_digitSpacing);
		a_ar.Field("PunchScale", m_punchScale);
		a_ar.Field("PunchTime", m_punchTime);
	}

	void ScoreHUD::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		UIBase::DrawInspector(a_context);

		Engine::EditorField::Header("Score");
		Engine::EditorField::HelpText("画像の飾りを1つ置き、0〜9 を横一列に並べたテクスチャを指定すること");
		Engine::EditorField::HelpText("コマの切り出し(UVScale / UVOffset)は AtlasCount から自動で決まる");

		// 何を出すか。中身は GlobalGameContext から貰う
		Engine::EditorField::Field("ValueKind", m_valueKind);
		Engine::EditorField::Tooltip("Time は秒だけ(小数は切り捨て)");

		if (Engine::EditorField::Field("DigitCount", m_digitCount, 1, 1, 9))
		{
			m_digitCount = std::clamp(m_digitCount, 1, 9);
		}
		Engine::EditorField::Tooltip("表示する桁数。足りないぶんは 0 で埋める");

		if (Engine::EditorField::Field("AtlasCount", m_atlasCount, 1, 1, 64))
		{
			m_atlasCount = std::max(m_atlasCount, 1);
		}
		Engine::EditorField::Tooltip("テクスチャに並んでいるコマ数(0〜9 だけなら 10)");

		Engine::EditorField::Field("DigitSpacing", m_digitSpacing, 0.5f);
		Engine::EditorField::Tooltip("桁と桁の間隔(px)。アンカーの PixelSize.x が1桁ぶんの送り幅");

		Engine::EditorField::Line();
		Engine::EditorField::Field("PunchScale", m_punchScale, 0.01f, 1.0f, 4.0f);
		Engine::EditorField::Field("PunchTime", m_punchTime, 0.01f, 0.0f, 2.0f);
		Engine::EditorField::Tooltip("スコアが増えたフレームだけ大きくして戻す");

		Engine::EditorField::Line();

		// 中身はシーンをまたぐグローバル側。ここでは表示と確認だけ
		Engine::EditorField::Value("Value", "%d", m_value);

		auto* _pGameData = InstanceResource::GameDataResource::Find(a_context.pWorld);
		if (!_pGameData) return;
		auto& _gameData = *_pGameData;

		Engine::EditorField::Value("Score", "%d", _gameData.score);
		Engine::EditorField::Value("Kill", "%d", _gameData.killCount);
		Engine::EditorField::Value("Time", "%.2f", _gameData.time);

		// 実際に倒さなくても並びを確かめられるようにしておく
		if (Engine::EditorField::CreateButton("Add 100"))
		{
			_gameData.AddScore(100);
		}
		Engine::EditorField::SameLine();
		if (Engine::EditorField::DeleteButton("Reset"))
		{
			_gameData.ResetRun();
		}
	}
}
