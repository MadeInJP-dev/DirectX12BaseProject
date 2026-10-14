#include "UITransition.h"

#include "Engine/EditorField/EditorField.h"

namespace App::Object
{
	namespace
	{
		float Lerp(float a_start, float a_end, float a_rate)
		{
			return a_start + (a_end - a_start) * a_rate;
		}

		//----------------------------------------------------------------------------------
		// 演出1回ぶんの、経過時間での値
		//----------------------------------------------------------------------------------
		UITransitionValue CalcClipValue(const UITransitionClip& a_clip, float a_time)
		{
			const float _rate = (a_clip.duration > 0.0f)
				? std::clamp(a_time / a_clip.duration, 0.0f, 1.0f)
				: 1.0f;
			const float _eased = Decoration::Ease(a_clip.ease, _rate);

			UITransitionValue _value = {};
			_value.alpha = Lerp(a_clip.startAlpha, a_clip.endAlpha, _eased);
			_value.scale = Lerp(a_clip.startScale, a_clip.endScale, _eased);
			_value.dissolve = Lerp(a_clip.startDissolve, a_clip.endDissolve, _eased);
			return _value;
		}

		//----------------------------------------------------------------------------------
		// 演出1回ぶんの保存 : キーは頭に持ち主の名前を付けて平らに並べる
		//----------------------------------------------------------------------------------
		void ArchiveClip(Engine::Persistence::Archive& a_ar, const std::string& a_prefix, UITransitionClip& a_clip)
		{
			a_ar.Field(a_prefix + "Duration", a_clip.duration);
			a_ar.Field(a_prefix + "Ease", a_clip.ease);
			a_ar.Field(a_prefix + "StartAlpha", a_clip.startAlpha);
			a_ar.Field(a_prefix + "EndAlpha", a_clip.endAlpha);
			a_ar.Field(a_prefix + "StartScale", a_clip.startScale);
			a_ar.Field(a_prefix + "EndScale", a_clip.endScale);
		}

		//----------------------------------------------------------------------------------
		// 演出1回ぶんの編集欄
		//----------------------------------------------------------------------------------
		void DrawClipInspector(const char* a_id, UITransitionClip& a_clip)
		{
			Engine::EditorField::IDScope _id(a_id);
			Engine::EditorField::IndentScope _indent;

			Engine::EditorField::Field("Duration", a_clip.duration, 0.01f, 0.0f, 10.0f);
			Engine::EditorField::Field("Ease", a_clip.ease);
			// 始まりと終わりは大小が逆にもなる(薄くなって消えるなど)ので、範囲の欄は使わず2つ並べる
			Engine::EditorField::Field("StartAlpha", a_clip.startAlpha, 0.01f, 0.0f, 1.0f);
			Engine::EditorField::Field("EndAlpha", a_clip.endAlpha, 0.01f, 0.0f, 1.0f);
			Engine::EditorField::Tooltip("色の a へ掛ける");
			Engine::EditorField::Field("StartScale", a_clip.startScale, 0.01f, 0.0f, 4.0f);
			Engine::EditorField::Field("EndScale", a_clip.endScale, 0.01f, 0.0f, 4.0f);
			Engine::EditorField::Tooltip("大きさへ掛ける(1 で等倍)");
			Engine::EditorField::Field("StartDissolve", a_clip.startDissolve, 0.01f, 0.0f, 1.0f);
			Engine::EditorField::Field("EndDissolve", a_clip.endDissolve, 0.01f, 0.0f, 1.0f);
			Engine::EditorField::Tooltip("ディゾルブの進み具合(0 で変化なし・1 で全部消える)\nディゾルブの UIマテリアルを付けたときだけ効く");
		}
	}

	//======================================================================================
	// 操作
	//======================================================================================
	void UITransition::OnVisibleChanged(bool a_isVisible)
	{
		if (a_isVisible)
		{
			if (isUseShow) PlayShow();
			else           visiblePhase = EVisiblePhase::Idle;
		}
		else
		{
			if (isUseHide) PlayHide();
			else           visiblePhase = EVisiblePhase::Idle;
		}
	}

	void UITransition::PlayShow()
	{
		// 時間が 0 のものは流さない(流すと1フレームだけ始まりの値が出る)
		visiblePhase = (show.duration > 0.0f) ? EVisiblePhase::Showing : EVisiblePhase::Idle;
		visibleTime = 0.0f;
	}

	void UITransition::PlayHide()
	{
		visiblePhase = (hide.duration > 0.0f) ? EVisiblePhase::Hiding : EVisiblePhase::Idle;
		visibleTime = 0.0f;
	}

	void UITransition::PlayPulse()
	{
		isPulsePlaying = (pulse.duration > 0.0f);
		pulseTime = 0.0f;
	}

	void UITransition::Advance(float a_deltaTime)
	{
		if (visiblePhase != EVisiblePhase::Idle)
		{
			visibleTime += a_deltaTime;

			const UITransitionClip& _clip = (visiblePhase == EVisiblePhase::Showing) ? show : hide;
			if (visibleTime >= _clip.duration) visiblePhase = EVisiblePhase::Idle;
		}

		if (isPulsePlaying)
		{
			pulseTime += a_deltaTime;
			if (pulseTime >= pulse.duration) isPulsePlaying = false;
		}
	}

	UITransitionValue UITransition::CalcValue() const
	{
		UITransitionValue _value = {};

		if (visiblePhase != EVisiblePhase::Idle)
		{
			const UITransitionClip& _clip = (visiblePhase == EVisiblePhase::Showing) ? show : hide;
			_value = CalcClipValue(_clip, visibleTime);
		}

		if (isPulsePlaying)
		{
			const UITransitionValue _pulse = CalcClipValue(pulse, pulseTime);
			_value.alpha *= _pulse.alpha;
			_value.scale *= _pulse.scale;

			// 消え具合は「残っている割合」を掛け合わせる(どちらかが 1 なら全部消える)
			_value.dissolve = 1.0f - (1.0f - _value.dissolve) * (1.0f - _pulse.dissolve);
		}

		return _value;
	}

	//======================================================================================
	// 保存
	//--------------------------------------------------------------------------------------
	// 自分の区切りを持つので、ここへ足しても持ち主(UIBase)の読み出しはずれない。
	// 演出を持つ前のデータは区切りごと無いので、印(HasTransition)が読めずに false のまま残る
	//======================================================================================
	bool UITransition::Archive(Engine::Persistence::Archive& a_ar)
	{
		Engine::Persistence::ArchiveSection _section(a_ar, "UITransition");

		// 書くときは必ず立てる。読めなければ既定の false のまま
		bool _hasTransition = a_ar.IsSaving();
		a_ar.Field("HasTransition", _hasTransition);

		a_ar.Field("TransitionUseShow", isUseShow);
		a_ar.Field("TransitionUseHide", isUseHide);
		a_ar.Field("TransitionPulseOnly", isPulseOnly);
		ArchiveClip(a_ar, "TransitionShow", show);
		ArchiveClip(a_ar, "TransitionHide", hide);
		ArchiveClip(a_ar, "TransitionPulse", pulse);

		// ---- ここから下は後から足したもの : 追加は必ず末尾へ ----

		// ディゾルブ(区切りの中は並び順で読むので、演出1回ぶんの保存には混ぜずにここへ並べる)
		a_ar.Field("TransitionShowStartDissolve", show.startDissolve);
		a_ar.Field("TransitionShowEndDissolve", show.endDissolve);
		a_ar.Field("TransitionHideStartDissolve", hide.startDissolve);
		a_ar.Field("TransitionHideEndDissolve", hide.endDissolve);
		a_ar.Field("TransitionPulseStartDissolve", pulse.startDissolve);
		a_ar.Field("TransitionPulseEndDissolve", pulse.endDissolve);

		return a_ar.IsLoading() && _hasTransition;
	}

	//======================================================================================
	// 編集欄
	//======================================================================================
	void UITransition::DrawSettingsInspector()
	{
		Engine::EditorField::Field("Show", isUseShow);
		Engine::EditorField::Tooltip("Visible を入れたときに流す");
		if (isUseShow) DrawClipInspector("Show", show);

		Engine::EditorField::Field("Hide", isUseHide);
		Engine::EditorField::Tooltip("Visible を切ったときに流す。流れ終わるまで描き続ける(押せない)");
		if (isUseHide) DrawClipInspector("Hide", hide);

		Engine::EditorField::Text("Pulse");
		Engine::EditorField::Tooltip("持ち主のコードが合図を出したときに流す(当たった・増えた など)");
		DrawClipInspector("Pulse", pulse);

		Engine::EditorField::Field("PulseOnly", isPulseOnly);
		Engine::EditorField::Tooltip("Pulse を流している間だけ描く(ヒットマーカーのように一瞬出して消えるもの)");
	}
}
