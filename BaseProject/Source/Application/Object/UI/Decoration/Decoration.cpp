#include "Application/Object/UI/Decoration/Internal/DecorationInternal.h"

//==========================================================================================
// デコレーション : 種類の切り替えと、アニメーション・反応の進行
//
// 描画は DecorationDraw.cpp、保存は DecorationArchive.cpp、
// インスペクターは DecorationInspector.cpp
//==========================================================================================
namespace App::Object::Decoration
{
	using Internal::Lerp;

	//======================================================================================
	// 種類
	//======================================================================================
	void Decoration::SetType(EDecorationType a_type)
	{
		if (GetType() == a_type) return;

		// 板ポリ⇔画像は UV と枠をそのまま使えるので引き継ぐ
		// (板ポリで枠を作ってから画像へ変える、といった作り方で値が消えないように)
		const QuadStyle _quad = GetQuad() ? *GetQuad() : QuadStyle{};

		switch (a_type)
		{
		case EDecorationType::Image:
		{
			ImageData _image = {};
			_image.quad = _quad;
			body = std::move(_image);
			break;
		}
		case EDecorationType::Text:
			body = TextData{};
			break;

		case EDecorationType::Polygon:
		default:
		{
			PolygonData _polygon = {};
			_polygon.quad = _quad;
			body = std::move(_polygon);
			break;
		}
		}
	}

	const QuadStyle* Decoration::GetQuad() const
	{
		if (const PolygonData* _pPolygon = std::get_if<PolygonData>(&body)) return &_pPolygon->quad;
		if (const ImageData* _pImage = std::get_if<ImageData>(&body)) return &_pImage->quad;
		return nullptr;
	}

	QuadStyle* Decoration::RefQuad()
	{
		return const_cast<QuadStyle*>(static_cast<const Decoration*>(this)->GetQuad());
	}

	//======================================================================================
	// イージング
	//======================================================================================
	float Ease(EEase a_ease, float a_rate)
	{
		const float _t = std::clamp(a_rate, 0.0f, 1.0f);

		switch (a_ease)
		{
		case EEase::InQuad:		return _t * _t;
		case EEase::OutQuad:	return 1.0f - (1.0f - _t) * (1.0f - _t);
		case EEase::InOutQuad:
			return (_t < 0.5f)
				? (2.0f * _t * _t)
				: (1.0f - 2.0f * (1.0f - _t) * (1.0f - _t));

		case EEase::InCubic:	return _t * _t * _t;
		case EEase::OutCubic:
		{
			const float _inv = 1.0f - _t;
			return 1.0f - _inv * _inv * _inv;
		}
		case EEase::InOutCubic:
		{
			if (_t < 0.5f) return 4.0f * _t * _t * _t;
			const float _inv = -2.0f * _t + 2.0f;
			return 1.0f - (_inv * _inv * _inv) * 0.5f;
		}

		case EEase::OutBack:
		{
			// 行き過ぎてから戻る。定数は一般的なイージング表の値
			constexpr float C1 = 1.70158f;
			constexpr float C3 = C1 + 1.0f;
			const float _inv = _t - 1.0f;
			return 1.0f + C3 * _inv * _inv * _inv + C1 * _inv * _inv;
		}
		case EEase::OutElastic:
		{
			if (_t <= 0.0f) return 0.0f;
			if (_t >= 1.0f) return 1.0f;

			constexpr float C4 = 6.283185307f / 3.0f;
			return std::pow(2.0f, -10.0f * _t) * std::sin((_t * 10.0f - 0.75f) * C4) + 1.0f;
		}

		case EEase::Linear:
		default:
			return _t;
		}
	}

	//======================================================================================
	// アニメーションの合成
	//======================================================================================
	namespace
	{
		// トゥイーンの進み具合(0〜1)を出す
		float CalcTweenRate(const UIAnimation& a_anim)
		{
			if (a_anim.durationTime <= 1e-6f) return 1.0f;

			float _rate = std::clamp(a_anim.currentTime / a_anim.durationTime, 0.0f, 1.0f);

			// 往復 : 前半で end まで行き、後半で start へ戻る
			if (a_anim.isPingPong)
			{
				_rate = (_rate <= 0.5f) ? (_rate * 2.0f) : ((1.0f - _rate) * 2.0f);
			}

			return Ease(a_anim.ease, _rate);
		}

		void ApplyTween(const UIAnimation& a_anim, Internal::AnimResult& a_inoutResult)
		{
			using Core::HasFlag;

			const float _rate = CalcTweenRate(a_anim);

			// channels で選ばれていないものは触らない。
			// 触ってしまうと、設定した覚えのない end(既定値0)へ寄っていく
			if (HasFlag(a_anim.channels, EAnimChannel::COLOR))
			{
				a_inoutResult.colorMul *= Lerp(a_anim.color.start, a_anim.color.end, _rate);
			}
			if (HasFlag(a_anim.channels, EAnimChannel::POSITION))
			{
				a_inoutResult.positionAdd += Lerp(a_anim.position.start, a_anim.position.end, _rate);
			}
			if (HasFlag(a_anim.channels, EAnimChannel::SCALE))
			{
				a_inoutResult.scaleMul *= Lerp(a_anim.scale.start, a_anim.scale.end, _rate);
			}
			if (HasFlag(a_anim.channels, EAnimChannel::ROTATION))
			{
				a_inoutResult.rotationAdd += Lerp(a_anim.rotation.start, a_anim.rotation.end, _rate);
			}
			if (HasFlag(a_anim.channels, EAnimChannel::UV))
			{
				a_inoutResult.uvAdd += Lerp(a_anim.uv.start, a_anim.uv.end, _rate);
			}
		}

		//----------------------------------------------------------------------------------
		// 周期運動の波
		//
		// sin(2π(f*t + phase))。位相は 0〜1 で1周。
		//----------------------------------------------------------------------------------
		float WaveSigned(float a_frequency, float a_phase, float a_time)
		{
			constexpr float TWO_PI = 6.283185307f;
			return std::sin(TWO_PI * (a_frequency * a_time + a_phase));
		}

		// 足すチャンネル(位置・回転)の値を出す
		template<typename T>
		T OscillationAdd(const Oscillation<T>& a_oscillation, float a_time)
		{
			const float _signed = WaveSigned(a_oscillation.frequency, a_oscillation.phase, a_time);

			// 上下限を使わないときは 0 を中心に ±amplitude
			if (!a_oscillation.isUseLimit) return a_oscillation.amplitude * _signed;

			// -1〜1 を 0〜1 へ均してから、下限と上限の間へ写す
			return Lerp(a_oscillation.minValue, a_oscillation.maxValue, (_signed + 1.0f) * 0.5f);
		}

		// 掛けるチャンネル(大きさ)の値を出す : 1 が等倍
		Math::Vector2 OscillationMul(const Oscillation<Math::Vector2>& a_oscillation, float a_time)
		{
			const float _signed = WaveSigned(a_oscillation.frequency, a_oscillation.phase, a_time);

			if (a_oscillation.isUseLimit)
			{
				return Lerp(a_oscillation.minValue, a_oscillation.maxValue, (_signed + 1.0f) * 0.5f);
			}

			// 振幅0で等倍のままになるよう、1を中心に揺らす
			return {
				1.0f + a_oscillation.amplitude.x * _signed,
				1.0f + a_oscillation.amplitude.y * _signed
			};
		}

		// 掛けるチャンネル(色)の値を出す : 1 が元の色のまま
		//
		// 加算ではなく乗算にしてあるのは、上下限で「アルファは 0.8 までしか上げない」と
		// 書いたときに、元の色が何であっても意味が変わらないようにするため
		Math::Color OscillationMul(const Oscillation<Math::Color>& a_oscillation, float a_time)
		{
			const float _signed = WaveSigned(a_oscillation.frequency, a_oscillation.phase, a_time);

			if (a_oscillation.isUseLimit)
			{
				return Lerp(a_oscillation.minValue, a_oscillation.maxValue, (_signed + 1.0f) * 0.5f);
			}

			return {
				1.0f + a_oscillation.amplitude.r * _signed,
				1.0f + a_oscillation.amplitude.g * _signed,
				1.0f + a_oscillation.amplitude.b * _signed,
				1.0f + a_oscillation.amplitude.a * _signed
			};
		}

		void ApplyOscillation(const UIProceduralAnimation& a_anim, Internal::AnimResult& a_inoutResult)
		{
			using Core::HasFlag;

			const float _time = a_anim.currentTime;

			if (HasFlag(a_anim.channels, EAnimChannel::POSITION))
			{
				a_inoutResult.positionAdd += OscillationAdd(a_anim.position, _time);
			}
			if (HasFlag(a_anim.channels, EAnimChannel::SCALE))
			{
				a_inoutResult.scaleMul *= OscillationMul(a_anim.scale, _time);
			}
			if (HasFlag(a_anim.channels, EAnimChannel::ROTATION))
			{
				a_inoutResult.rotationAdd += OscillationAdd(a_anim.rotation, _time);
			}
			if (HasFlag(a_anim.channels, EAnimChannel::COLOR))
			{
				a_inoutResult.colorMul *= OscillationMul(a_anim.color, _time);
			}
		}
	}

	Internal::AnimResult Internal::MakeAnimResult(
		const std::optional<UIAnimation>& a_opTween,
		const std::optional<UIProceduralAnimation>& a_opOscillation,
		const std::optional<UIReaction>& a_opReaction)
	{
		AnimResult _anim = {};
		if (a_opTween.has_value())       ApplyTween(*a_opTween, _anim);
		if (a_opOscillation.has_value()) ApplyOscillation(*a_opOscillation, _anim);

		// カーソルへの反応 : 目標へ寄せた結果(current)を掛ける。
		// 寄せる処理そのものは AdvanceAnimation が行う
		if (a_opReaction.has_value())
		{
			const UIReaction& _reaction = *a_opReaction;

			_anim.colorMul *= _reaction.current.color;
			_anim.scaleMul *= _reaction.current.scale;
			_anim.positionAdd += _reaction.current.offsetAdd;

			// 出さない状態は透明にして消す。
			// 描画側で弾かずアルファで消しているのは、途中の割合で薄く出せるようにするため
			_anim.colorMul.a *= _reaction.visibleRate;
		}

		return _anim;
	}

	Internal::AnimResult Internal::MakeAnimResult(const Decoration& a_decoration)
	{
		return MakeAnimResult(a_decoration.opTweenAnim, a_decoration.opOscillationAnim, a_decoration.opReaction);
	}

	//======================================================================================
	// 時間を進める
	//======================================================================================
	namespace
	{
		// 親の状態に対応する見た目を選ぶ
		UIStateStyle PickStateStyle(const UIReaction& a_reaction, EUIState a_state)
		{
			switch (a_state)
			{
			case EUIState::Hovered:  return a_reaction.hovered;
			case EUIState::Pressed:  return a_reaction.pressed;
			case EUIState::Disabled: return a_reaction.disabled;

			case EUIState::Normal:
			default:
				// 素のまま(掛けても足しても変わらない値)
				return UIStateStyle();
			}
		}

		// 反応を目標へ寄せる
		void AdvanceReaction(UIReaction& a_reaction, EUIState a_parentState, float a_deltaTime)
		{
			const UIStateStyle _target = PickStateStyle(a_reaction, a_parentState);

			const bool _isVisibleState =
				Core::HasFlag(a_reaction.visibleState, ToStateFlag(a_parentState));
			const float _targetVisible = _isVisibleState ? 1.0f : 0.0f;

			//----------------------------------------------------------------------
			// 寄せる割合
			//
			// 1 - exp(-speed * dt) にしてあるのは、フレームレートが変わっても
			// 同じ速さで寄るようにするため(dt をそのまま掛けると重いときほど速く寄る)。
			// 初回だけ補間しないのは、出た瞬間に Normal から寄り始めてちらつくのを避けるため
			//----------------------------------------------------------------------
			float _rate = 1.0f;
			if (a_reaction.blendSpeed > 0.0f && a_reaction.isInitialized)
			{
				_rate = 1.0f - std::exp(-a_reaction.blendSpeed * a_deltaTime);
			}

			a_reaction.current.color     = Lerp(a_reaction.current.color, _target.color, _rate);
			a_reaction.current.scale     = Lerp(a_reaction.current.scale, _target.scale, _rate);
			a_reaction.current.offsetAdd = Lerp(a_reaction.current.offsetAdd, _target.offsetAdd, _rate);
			a_reaction.visibleRate       = Lerp(a_reaction.visibleRate, _targetVisible, _rate);

			a_reaction.isInitialized = true;
		}
	}

	void Internal::AdvanceAnimation(
		std::optional<UIAnimation>& a_opTween,
		std::optional<UIProceduralAnimation>& a_opOscillation,
		std::optional<UIReaction>& a_opReaction,
		EUIState a_parentState,
		float a_deltaTime)
	{
		// カーソルへの反応
		if (a_opReaction.has_value())
		{
			AdvanceReaction(*a_opReaction, a_parentState, a_deltaTime);
		}

		if (a_opTween.has_value())
		{
			UIAnimation& _anim = *a_opTween;
			_anim.currentTime += a_deltaTime;

			if (_anim.durationTime > 1e-6f && _anim.currentTime >= _anim.durationTime)
			{
				// ループは余りを持ち越す。切り捨てるとフレームレートで速さが変わる
				_anim.currentTime = _anim.isLoop
					? std::fmod(_anim.currentTime, _anim.durationTime)
					: _anim.durationTime;
			}
		}

		if (a_opOscillation.has_value())
		{
			UIProceduralAnimation& _anim = *a_opOscillation;
			_anim.currentTime += a_deltaTime;

			// 揺れは終わらないので、精度が落ちないところで巻き戻す
			if (_anim.currentTime > 3600.0f) _anim.currentTime -= 3600.0f;
		}
	}
}
