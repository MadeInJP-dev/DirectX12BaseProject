#include "ScoreHUD.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/ECS/World/World.h"
#include "Engine/EditorField/EditorField.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/Font/Font.h"

#include "Application/InstanceResource/GameDataResource.h"

namespace App::Object
{
	ScoreHUD::ScoreHUD() : UIBase(false)
	{
		// 新しく置いたときの弾み方 : 増えた瞬間に大きくなって戻る
		MigrateLegacyTransition();
	}

	//======================================================================================
	// 旧形式からの引き継ぎ
	//--------------------------------------------------------------------------------------
	// 以前は「拡大率・戻るまでの秒数」を自分で持ち、残り時間の割合で倍率を直線で戻していた。
	// 同じ動きになるよう演出の合図へ移す(色は変えない)
	//======================================================================================
	void ScoreHUD::MigrateLegacyTransition()
	{
		UITransitionClip& _pulse = m_transition.pulse;
		_pulse.duration = m_legacyPunchTime;
		_pulse.ease = Decoration::EEase::Linear;
		_pulse.startAlpha = 1.0f;
		_pulse.endAlpha = 1.0f;
		_pulse.startScale = m_legacyPunchScale;
		_pulse.endScale = 1.0f;
	}

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
		// 飾りのアニメーションと演出を進める
		UIBase::Update(a_context);

		const Game::GlobalGameContext* _pGameData = InstanceResource::GameDataResource::Find(a_context.pWorld);
		m_value = PickValue(_pGameData);

		// 文字で出すときは、ここで流し込んでおく(描くのは飾りの側)
		if (m_drawMode == EScoreDrawMode::Text)
		{
			ApplyValueText(MakeValueText(_pGameData));
		}

		//==================================================================
		// 増えたフレームだけ弾ませる
		//------------------------------------------------------------------
		// 前フレームの数と比べるだけにしてある。加算した側に
		// 「このフレームで入ったぶん」を持たせると、
		// 加算する側が居ないシーン(リザルト)で立ちっぱなしになるため。
		//
		// 出た最初のフレームは比べる相手が無いので弾ませない
		// (リザルトへ来た瞬間にスコアが跳ねてしまう)
		//
		// タイムは毎秒増えるので弾ませない(弾み続けて読みにくくなる)
		//==================================================================
		const bool _isPulseTarget = (m_valueKind != EScoreValueKind::Time);
		if (_isPulseTarget && !m_isFirst && m_value > m_prevValue)
		{
			PlayPulse();
		}

		m_prevValue = m_value;
		m_isFirst = false;
	}

	//======================================================================================
	// 出す文字列を組む(Text のとき)
	//--------------------------------------------------------------------------------------
	// 桁数(DigitCount)に満たないぶんは 0 で埋める。Atlas と同じく、
	// 桁数で幅が変わると加算のたびに数字が横へずれて読みにくいため。
	// タイムの Clock だけは書式が決まっているので桁数を見ない
	//======================================================================================
	std::string ScoreHUD::MakeValueText(const Game::GlobalGameContext* a_pGameData) const
	{
		char _buffer[32] = {};

		if (m_valueKind == EScoreValueKind::Time && m_timeFormat == EScoreTimeFormat::Clock)
		{
			const float _time = a_pGameData ? std::max(a_pGameData->time, 0.0f) : 0.0f;

			// 1/100秒の単位へ直してから割る(浮動小数のまま割ると 59.999 が 60 秒に見えることがある)
			const int _totalCenti = static_cast<int>(_time * 100.0f);
			const int _minutes = _totalCenti / 6000;
			const int _seconds = (_totalCenti / 100) % 60;
			const int _centi   = _totalCenti % 100;

			std::snprintf(_buffer, sizeof(_buffer), "%02d:%02d.%02d", _minutes, _seconds, _centi);
			return _buffer;
		}

		const int _digitCount = std::clamp(m_digitCount, 1, 9);
		std::snprintf(_buffer, sizeof(_buffer), "%0*d", _digitCount, std::max(PickValue(a_pGameData), 0));
		return _buffer;
	}

	//======================================================================================
	// 選んだ Text の飾りへ文字列を入れる
	//--------------------------------------------------------------------------------------
	// 毎フレーム書き換えると、値が変わっていないのに文字列を作り直すことになる。
	// 変わったときだけ書き換える(UIGauge の数値と同じ)
	//======================================================================================
	void ScoreHUD::ApplyValueText(const std::string& a_text)
	{
		Decoration::Decoration* _pDecoration = FindDecorationById(m_textDecorationId);
		if (_pDecoration == nullptr) return;

		Decoration::TextData* _pText = _pDecoration->RefText();
		if (_pText == nullptr) return;

		if (a_text == m_appliedText && _pText->text == a_text) return;

		_pText->text = a_text;
		m_appliedText = a_text;
	}

	//======================================================================================
	// 1文字ずつのマスの幅
	//--------------------------------------------------------------------------------------
	// 数字は 0〜9 のうち一番広い送り幅にそろえる(CharPitch を入れていればそれ)。
	// 記号は自分の送り幅のまま : 「:」まで数字の幅を取ると間が空きすぎるため。
	// 記号は書式で位置が決まっていて、値が変わっても同じ字のままなので揺れない。
	//
	// 幅は倍率を掛ける前の px。演出の拡大は飾りの位置のずれにも掛かるので、
	// ここで掛けなくても文字と一緒に広がる
	//======================================================================================
	bool ScoreHUD::CalcCharWidths(
		Engine::GameObject::ObjectContext& a_context,
		const Decoration::TextData& a_text,
		std::vector<float>& a_outWidthVec) const
	{
		a_outWidthVec.clear();

		if (!a_context.pServices || !a_context.pServices->pResourceManager) return false;
		Engine::Resource::ResourceManager& _resourceManager = *a_context.pServices->pResourceManager;

		if (!a_text.fontRef.IsValid()) return false;
		if (!_resourceManager.IsReady(a_text.fontRef)) return false;

		// グリフは要求された時点で焼くので、参照は書き込み可能で引く(描く側と同じ)
		Engine::Resource::Font* _pFont = _resourceManager.Ref(a_text.fontRef.GetRaw());
		if (_pFont == nullptr || !_pFont->IsValid()) return false;

		const float _fontScale = _pFont->GetScaleForSize(a_text.fontPixelSize);

		// 1文字ぶんの幅(送り幅 + 字間)
		auto _calcAdvance = [&](uint32_t a_codePoint) -> float
		{
			const Engine::Resource::Glyph* _pGlyph = _pFont->RequestGlyph(a_codePoint);
			if (_pGlyph == nullptr) return 0.0f;
			return _pGlyph->xAdvance * _fontScale + a_text.charSpacing;
		};

		// 数字のマス
		float _digitWidth = m_charPitch;
		if (_digitWidth <= 0.0f)
		{
			for (uint32_t _c = '0'; _c <= '9'; ++_c)
			{
				_digitWidth = std::max(_digitWidth, _calcAdvance(_c));
			}
		}

		// 流し込む文字列はこのクラスが組んだもの(数字と : . だけ)なので、1バイトを1文字として扱える
		a_outWidthVec.reserve(a_text.text.size());
		for (const char _c : a_text.text)
		{
			const bool _isDigit = (_c >= '0' && _c <= '9');
			a_outWidthVec.push_back(_isDigit ? _digitWidth : _calcAdvance(static_cast<uint32_t>(static_cast<unsigned char>(_c))));
		}

		return true;
	}

	//======================================================================================
	// 数字の飾りを1文字ずつ並べて描く
	//--------------------------------------------------------------------------------------
	// 文字列のまま描くと、Pivot(右寄せなど)が文字列全体の幅に掛かる。
	// 数字は字ごとに幅が違うので、値が変わるたびに全体の幅が変わり、左右へガクガク揺れる。
	//
	// そこで飾りの Pivot と OffsetPos は「マスを並べた塊」に掛け、
	// 1文字ずつ自分のマスの真ん中へ置いて描く。
	// 飾りを一時的に1文字ぶんへ書き換えて描き、終わったら元へ戻す
	// (描画命令は描いた時点で積まれるので、戻しても描いたものは変わらない)
	//======================================================================================
	void ScoreHUD::DrawValueByChar(Engine::GameObject::ObjectContext& a_context, size_t a_index)
	{
		Decoration::Decoration& _decoration = RefDecorations()[a_index];

		Decoration::TextData* _pText = _decoration.RefText();
		if (_pText == nullptr || _pText->text.empty())
		{
			DrawDecorationAt(a_context, a_index);
			return;
		}

		// フォントがまだ読めていなければ描かない(文字列のまま描く側も同じく何も出ない)
		std::vector<float> _widthVec;
		if (!CalcCharWidths(a_context, *_pText, _widthVec)) return;

		float _totalWidth = 0.0f;
		for (const float _width : _widthVec) _totalWidth += _width;

		// 元の値を退避
		const std::string   _text   = _pText->text;
		const Math::Vector2 _offset = _decoration.offsetPos;
		const Math::Vector2 _pivot  = _decoration.pivot;

		// 塊の左端。縦の Pivot は1文字でも行の高さで効くので、そのまま使う
		float _x = _offset.x - _pivot.x * _totalWidth;
		_decoration.pivot = { 0.5f, _pivot.y };

		for (size_t _i = 0; _i < _text.size(); ++_i)
		{
			const float _width = _widthVec[_i];

			_pText->text.assign(1, _text[_i]);
			_decoration.offsetPos = { _x + _width * 0.5f, _offset.y };
			DrawDecorationAt(a_context, a_index);

			_x += _width;
		}

		// 元へ戻す
		_pText->text = _text;
		_decoration.offsetPos = _offset;
		_decoration.pivot = _pivot;
	}

	void ScoreHUD::DrawDigit(
		Engine::GameObject::ObjectContext& a_context,
		int a_digit,
		int a_index)
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
		_override.pixelPos = GetWorldPixelPos();	// 差し替える位置は画面の座標
		_override.pixelPos.x += _step * static_cast<float>(a_index);

		_override.isUseUvScale = true;
		_override.uvScale = { _cellWidth, 1.0f };
		_override.uvOffsetAdd = { _cellWidth * static_cast<float>(a_digit), 0.0f };

		DrawDecorations(a_context, _override);
	}

	void ScoreHUD::Draw(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pServices || !a_context.pServices->pMainEngine) return;

		//--------------------------------------------------------------
		// 文字で出すとき
		//
		// 見出しや下敷きは普通に描き、数字の飾りだけ1文字ずつマスへ並べる。
		// 配列の順に描くので、重なり(後ろほど手前)は飾りの並びのまま
		//--------------------------------------------------------------
		if (m_drawMode == EScoreDrawMode::Text)
		{
			const size_t _count = GetDecorations().size();
			for (size_t _i = 0; _i < _count; ++_i)
			{
				const bool _isValue = (m_textDecorationId != 0) && (GetDecorations()[_i].id == m_textDecorationId);
				if (_isValue)
				{
					DrawValueByChar(a_context, _i);
				}
				else
				{
					DrawDecorationAt(a_context, _i);
				}
			}
			return;
		}

		if (m_digitCount <= 0 || m_atlasCount <= 0) return;

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
			DrawDigit(a_context, _digit, _i);

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
		a_ar.Field("PunchScale", m_legacyPunchScale);
		a_ar.Field("PunchTime", m_legacyPunchTime);

		// ---- ここから下は区切りを入れた後に足したもの : 追加は必ず末尾へ ----
		// 区切りを持たない古い .ob* には無いので読まない
		if (!a_ar.IsLegacyLayout())
		{
			a_ar.Field("DrawMode", m_drawMode);
			a_ar.Field("TextDecorationId", m_textDecorationId);
			a_ar.Field("TimeFormat", m_timeFormat);
			a_ar.Field("CharPitch", m_charPitch);
		}

		if (a_ar.IsLoading())
		{
			// 演出を持つ前のシーンは、自分で持っていた弾み方を移す
			if (!m_isTransitionLoaded) MigrateLegacyTransition();

			// 流し込み直させる
			m_appliedText.clear();
		}
	}

	void ScoreHUD::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		UIBase::DrawInspector(a_context);

		Engine::EditorField::Header("Score");

		// 何を出すか。中身は GlobalGameContext から貰う
		Engine::EditorField::Field("ValueKind", m_valueKind);
		Engine::EditorField::Tooltip("Score / Time / KillCount のどれを出すか");

		if (Engine::EditorField::Field("DrawMode", m_drawMode))
		{
			m_appliedText.clear();	// 切り替えたらすぐ流し込む
		}
		Engine::EditorField::Tooltip("Atlas : 数字の絵から切り出す / Text : Text の飾りへ文字で流し込む");

		if (Engine::EditorField::Field("DigitCount", m_digitCount, 1, 1, 9))
		{
			m_digitCount = std::clamp(m_digitCount, 1, 9);
			m_appliedText.clear();	// 桁を変えたらすぐ出し直す
		}
		Engine::EditorField::Tooltip("表示する桁数。足りないぶんは 0 で埋める(Time の Clock では使わない)");

		if (m_drawMode == EScoreDrawMode::Text)
		{
			Engine::EditorField::HelpText("Text の飾りを置いて下で選ぶこと。見出しや下敷きは別の飾りで並べる");

			if (DrawDecorationPicker("TextDecoration", m_textDecorationId, Decoration::EDecorationType::Text))
			{
				m_appliedText.clear();	// 選び直したらすぐ流し込む
			}
			Engine::EditorField::Tooltip("数字を流し込む Text 飾り。置き場所はその飾りの OffsetPos / Pivot");

			const Decoration::Decoration* _pText = FindDecorationById(m_textDecorationId);
			if (_pText == nullptr)
			{
				Engine::EditorField::ErrorText("数字の飾りが選ばれていません");
			}
			else if (_pText->GetType() != Decoration::EDecorationType::Text)
			{
				Engine::EditorField::WarningText("その飾りが Text ではありません");
			}

			if (Engine::EditorField::Field("CharPitch", m_charPitch, 0.5f))
			{
				m_charPitch = std::max(m_charPitch, 0.0f);
			}
			Engine::EditorField::Tooltip("数字1文字ぶんのマスの幅(px)。0 でフォントの一番広い数字に合わせる");

			if (m_valueKind == EScoreValueKind::Time)
			{
				if (Engine::EditorField::Field("TimeFormat", m_timeFormat))
				{
					m_appliedText.clear();
				}
				Engine::EditorField::Tooltip("Seconds : 秒だけ / Clock : 分:秒.1/100秒(01:23.45)");
			}
		}
		else
		{
			Engine::EditorField::HelpText("画像の飾りを1つ置き、0〜9 を横一列に並べたテクスチャを指定すること");
			Engine::EditorField::HelpText("コマの切り出し(UVScale / UVOffset)は AtlasCount から自動で決まる");
			Engine::EditorField::HelpText("Time は秒だけ(小数は切り捨て)");

			if (Engine::EditorField::Field("AtlasCount", m_atlasCount, 1, 1, 64))
			{
				m_atlasCount = std::max(m_atlasCount, 1);
			}
			Engine::EditorField::Tooltip("テクスチャに並んでいるコマ数(0〜9 だけなら 10)");

			Engine::EditorField::Field("DigitSpacing", m_digitSpacing, 0.5f);
			Engine::EditorField::Tooltip("桁と桁の間隔(px)。アンカーの PixelSize.x が1桁ぶんの送り幅");
		}

		Engine::EditorField::HelpText("増えたフレームの弾み方は Transition の Pulse で決めます");

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
