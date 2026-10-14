#include "Application/Object/UI/Decoration/Internal/DecorationInternal.h"
#include "Application/Object/UI/Decoration/DecorationDraw.h"	// 選び直したときのリソースの引き直し(RequestResources)

#include "Engine/EditorField/EditorField.h"

//==========================================================================================
// デコレーションのインスペクター(エディター用)
//==========================================================================================
namespace App::Object::Decoration
{
	namespace
	{
		// 値の種類ごとの編集
		bool DrawFloatValue(const char* a_label, float& a_value)
		{
			return Engine::EditorField::Field(a_label, a_value, 0.1f);
		}
		bool DrawVectorValue(const char* a_label, Math::Vector2& a_value)
		{
			return Engine::EditorField::Field(a_label, a_value, 0.1f);
		}
		bool DrawColorValue(const char* a_label, Math::Color& a_value)
		{
			return Engine::EditorField::Field(a_label, a_value);
		}

		// 状態1つぶんの見た目
		bool DrawStateStyleUI(const char* a_label, UIStateStyle& a_style)
		{
			Engine::EditorField::TreeScope _tree(a_label);
			if (!_tree) return false;

			bool _isChanged = false;

			if (Engine::EditorField::Field("Color", a_style.color)) _isChanged = true;
			Engine::EditorField::Tooltip("元の色へ乗算(白で変化なし)");

			if (Engine::EditorField::Field("Scale", a_style.scale, 0.01f, 0.0f, 16.0f)) _isChanged = true;
			Engine::EditorField::Tooltip("大きさへ乗算(1で等倍)");

			if (Engine::EditorField::Field("Offset", a_style.offsetAdd, 0.5f)) _isChanged = true;
			Engine::EditorField::Tooltip("位置へ加算(px)");

			return _isChanged;
		}

		// トゥイーンの1チャンネルぶん
		template<typename T, typename DrawFunc>
		bool DrawAnimElementUI(
			const char* a_label,
			EAnimChannel a_channel,
			EAnimChannel& a_inoutChannels,
			AnimElement<T>& a_element,
			DrawFunc a_drawFunc)
		{
			bool _isChanged = false;

			// 立てたチャンネルだけ中身を出す。
			// 動かないものの値を触らせても混乱するだけなので畳んでおく
			bool _isOn = Core::HasFlag(a_inoutChannels, a_channel);
			if (Engine::EditorField::Field(a_label, _isOn))
			{
				a_inoutChannels = _isOn
					? (a_inoutChannels | a_channel)
					: (a_inoutChannels & ~a_channel);
				_isChanged = true;
			}
			if (!_isOn) return _isChanged;

			Engine::EditorField::IndentScope _indent;
			Engine::EditorField::IDScope _id(a_label);
			if (a_drawFunc("Start", a_element.start)) _isChanged = true;
			if (a_drawFunc("End", a_element.end))     _isChanged = true;

			return _isChanged;
		}

		// 揺れの1チャンネルぶん
		//
		// a_defaultMin / a_defaultMax は、上下限を初めて立てたときに入れておく値。
		// 既定の 0 のままだと、掛けるチャンネル(大きさ・色)が真っ黒に潰れて
		// 「立てた瞬間に消えた」ように見えるため
		template<typename T, typename DrawFunc>
		bool DrawOscillationUI(
			const char* a_label,
			EAnimChannel a_channel,
			EAnimChannel& a_inoutChannels,
			Oscillation<T>& a_oscillation,
			DrawFunc a_drawFunc,
			const T& a_defaultMin,
			const T& a_defaultMax)
		{
			bool _isChanged = false;

			bool _isOn = Core::HasFlag(a_inoutChannels, a_channel);
			if (Engine::EditorField::Field(a_label, _isOn))
			{
				a_inoutChannels = _isOn
					? (a_inoutChannels | a_channel)
					: (a_inoutChannels & ~a_channel);
				_isChanged = true;
			}
			if (!_isOn) return _isChanged;

			Engine::EditorField::IndentScope _indent;
			Engine::EditorField::IDScope _id(a_label);

			if (Engine::EditorField::Field("UseLimit", a_oscillation.isUseLimit))
			{
				// まだ一度も触っていないときだけ入れる。
				// 切って入れ直すたびに上書きすると、調整した値が消えてしまう
				if (a_oscillation.isUseLimit &&
					a_oscillation.minValue == T{} && a_oscillation.maxValue == T{})
				{
					a_oscillation.minValue = a_defaultMin;
					a_oscillation.maxValue = a_defaultMax;
				}
				_isChanged = true;
			}
			Engine::EditorField::Tooltip("振れ幅ではなく、届く範囲(下限〜上限)で指定する");

			if (a_oscillation.isUseLimit)
			{
				if (a_drawFunc("Min", a_oscillation.minValue)) _isChanged = true;
				if (a_drawFunc("Max", a_oscillation.maxValue)) _isChanged = true;
			}
			else
			{
				if (a_drawFunc("Amplitude", a_oscillation.amplitude)) _isChanged = true;
			}

			if (Engine::EditorField::Field("Frequency", a_oscillation.frequency, 0.01f, 0.0f, 60.0f)) _isChanged = true;
			if (Engine::EditorField::Field("Phase", a_oscillation.phase, 0.01f, 0.0f, 1.0f))          _isChanged = true;

			return _isChanged;
		}

		//----------------------------------------------------------------------------------
		// 種類ごとの中身
		//----------------------------------------------------------------------------------
		bool DrawImageUI(Decoration& a_decoration, ImageData& a_image, const Engine::ECS::EngineServices& a_services)
		{
			bool _isChanged = false;

			Engine::EditorField::Header("Image");

			if (Engine::EditorField::AssetField(a_services, "Texture", "Texture", a_image.texGUID))
			{
				RequestResources(a_decoration, a_services.pResourceManager);
				_isChanged = true;
			}
			Engine::EditorField::Image(a_services, a_image.texRef, 128, 128);

			if (Engine::EditorField::Field("UVOffset", a_image.quad.uvOffset, 0.01f)) _isChanged = true;
			if (Engine::EditorField::Field("UVScale", a_image.quad.uvScale, 0.01f)) _isChanged = true;
			Engine::EditorField::Tooltip("1枚に並べた絵から1コマ切り出すときの倍率 (uv * UVScale + UVOffset)");

			return _isChanged;
		}

		bool DrawTextUI(Decoration& a_decoration, TextData& a_text, const Engine::ECS::EngineServices& a_services)
		{
			bool _isChanged = false;

			Engine::EditorField::Header("Text");

			if (Engine::EditorField::MultilineField("Text", a_text.text)) _isChanged = true;

			if (Engine::EditorField::AssetField(a_services, "Font", "Font", a_text.fontGUID))
			{
				RequestResources(a_decoration, a_services.pResourceManager);
				_isChanged = true;
			}

			if (Engine::EditorField::Field("FontPixelSize", a_text.fontPixelSize, 0.5f, 1.0f, 512.0f)) _isChanged = true;
			Engine::EditorField::Tooltip("フォントは64pxで焼いてあるので、それより大きくするとぼやける");

			if (Engine::EditorField::Field("LineSpacing", a_text.lineSpacing, 0.01f, 0.1f, 4.0f)) _isChanged = true;
			if (Engine::EditorField::Field("CharSpacing", a_text.charSpacing, 0.1f)) _isChanged = true;
			if (Engine::EditorField::Field("TextAlign", a_text.textAlign)) _isChanged = true;
			Engine::EditorField::Tooltip("ブロック全体の位置は Pivot、行同士の揃えが TextAlign");

			return _isChanged;
		}

		// 枠(板ポリ・画像)
		bool DrawEdgeUI(QuadStyle& a_quad)
		{
			bool _isChanged = false;

			Engine::EditorField::Header("Edge");

			if (Engine::EditorField::Field("Fill", a_quad.isFill)) _isChanged = true;
			Engine::EditorField::Tooltip("切ると枠だけになる");

			if (Engine::EditorField::Field("EdgePixel", a_quad.edgePixel, 0.5f, 0.0f, 256.0f)) _isChanged = true;
			if (a_quad.edgePixel > 0.0f)
			{
				if (Engine::EditorField::Field("EdgeColor", a_quad.edgeColor)) _isChanged = true;
				Engine::EditorField::FlagsField("EdgeSide", a_quad.edgeSide);
			}

			return _isChanged;
		}
	}

	bool DrawDecorationInspector(Decoration& a_decoration, const Engine::ECS::EngineServices& a_services)
	{
		bool _isChanged = false;

		//----------------------------------------------------------------------------------
		// 共通
		//----------------------------------------------------------------------------------
		if (Engine::EditorField::Field("Visible", a_decoration.isVisible)) _isChanged = true;
		if (Engine::EditorField::Field("Name", a_decoration.name)) _isChanged = true;
		Engine::EditorField::Value("ID", "%u", a_decoration.id);
		Engine::EditorField::Tooltip("他から飾りを指すときの番号(名前を変えても変わらない)");

		// 種類を変えると中身は作り直す(板ポリ⇔画像は UV と枠を引き継ぐ)
		EDecorationType _type = a_decoration.GetType();
		if (Engine::EditorField::Field("Type", _type))
		{
			a_decoration.SetType(_type);
			RequestResources(a_decoration, a_services.pResourceManager);
			_isChanged = true;
		}

		int _group = static_cast<int>(a_decoration.group);
		if (Engine::EditorField::Field("Group", _group, 1, 0, 15))
		{
			a_decoration.group = static_cast<uint32_t>(std::max(_group, 0));
			_isChanged = true;
		}
		Engine::EditorField::Tooltip("HUDが飾りを出し分けるための札 (TargetBoxHUD : 0=通常枠 / 1=ロック枠)");

		Engine::EditorField::Header("Transform (親からの相対)");

		if (Engine::EditorField::Field("OffsetPos", a_decoration.offsetPos, 1.0f)) _isChanged = true;
		Engine::EditorField::Tooltip("親のピボット位置からのずれ(px)。シーンビューの小さいハンドルでも動かせる");

		// 文字の大きさは FontPixelSize が決めるので、矩形の大きさは出さない
		if (a_decoration.GetType() != EDecorationType::Text)
		{
			if (Engine::EditorField::Field("PixelSize", a_decoration.pixelSize, 1.0f, 0.0f, 8192.0f)) _isChanged = true;
		}

		if (Engine::EditorField::Field("Rotation", a_decoration.rotation, 0.1f, -360.0f, 360.0f)) _isChanged = true;
		if (Engine::EditorField::Field("Scale", a_decoration.scale, 0.01f, 0.0f, 64.0f)) _isChanged = true;
		if (Engine::EditorField::Field("Pivot (0-1)", a_decoration.pivot, 0.01f, 0.0f, 1.0f)) _isChanged = true;
		if (Engine::EditorField::Field("LayerOffset", a_decoration.layerOffset, 0.1f)) _isChanged = true;
		Engine::EditorField::Tooltip("親のレイヤーへ足す。大きいほど手前");
		if (Engine::EditorField::Field("Color", a_decoration.color)) _isChanged = true;

		//----------------------------------------------------------------------------------
		// 種類ごと
		//----------------------------------------------------------------------------------
		if (ImageData* _pImage = a_decoration.RefImage())
		{
			if (DrawImageUI(a_decoration, *_pImage, a_services)) _isChanged = true;
		}
		else if (TextData* _pText = a_decoration.RefText())
		{
			if (DrawTextUI(a_decoration, *_pText, a_services)) _isChanged = true;
		}
		else
		{
			Engine::EditorField::Header("Polygon");
			Engine::EditorField::HelpText("組み込みの白テクスチャを Color で染めて出します");
		}

		// 枠(文字以外)
		if (QuadStyle* _pQuad = a_decoration.RefQuad())
		{
			if (DrawEdgeUI(*_pQuad)) _isChanged = true;
		}

		//----------------------------------------------------------------------------------
		// カーソルへの反応
		//----------------------------------------------------------------------------------
		Engine::EditorField::Header("Reaction");

		bool _hasReaction = a_decoration.opReaction.has_value();
		if (Engine::EditorField::Field("Reaction", _hasReaction))
		{
			if (_hasReaction) a_decoration.opReaction = UIReaction();
			else              a_decoration.opReaction.reset();
			_isChanged = true;
		}
		Engine::EditorField::Tooltip("親のUIにカーソルが乗った / 押されたときに反応する(押せるUIのみ)");

		if (a_decoration.opReaction.has_value())
		{
			UIReaction& _reaction = *a_decoration.opReaction;

			Engine::EditorField::IndentScope _indent;
			Engine::EditorField::IDScope _id("Reaction");

			Engine::EditorField::FlagsField("VisibleState", _reaction.visibleState);
			Engine::EditorField::Tooltip("この状態のときだけ出す(カーソル時だけ枠を出す等)");

			if (Engine::EditorField::Field("BlendSpeed", _reaction.blendSpeed, 0.5f, 0.0f, 120.0f)) _isChanged = true;
			Engine::EditorField::Tooltip("切り替わりの速さ。0 で即時");

			if (DrawStateStyleUI("Hovered", _reaction.hovered))  _isChanged = true;
			if (DrawStateStyleUI("Pressed", _reaction.pressed))  _isChanged = true;
			if (DrawStateStyleUI("Disabled", _reaction.disabled)) _isChanged = true;
		}

		//----------------------------------------------------------------------------------
		// アニメーション
		//----------------------------------------------------------------------------------
		Engine::EditorField::Header("Animation");

		// ---- トゥイーン ----
		bool _hasTween = a_decoration.opTweenAnim.has_value();
		if (Engine::EditorField::Field("Tween", _hasTween))
		{
			if (_hasTween) a_decoration.opTweenAnim = UIAnimation();
			else           a_decoration.opTweenAnim.reset();
			_isChanged = true;
		}

		if (a_decoration.opTweenAnim.has_value())
		{
			UIAnimation& _anim = *a_decoration.opTweenAnim;

			Engine::EditorField::IndentScope _indent;
			Engine::EditorField::IDScope _id("Tween");

			if (Engine::EditorField::Field("Duration", _anim.durationTime, 0.01f, 0.0f, 60.0f)) _isChanged = true;
			if (Engine::EditorField::Field("Loop", _anim.isLoop)) _isChanged = true;
			if (Engine::EditorField::Field("PingPong", _anim.isPingPong)) _isChanged = true;
			if (Engine::EditorField::Field("Ease", _anim.ease)) _isChanged = true;

			Engine::EditorField::HelpText("チェックを入れたチャンネルだけが動きます");

			if (DrawAnimElementUI("Color##ch", EAnimChannel::COLOR, _anim.channels, _anim.color, DrawColorValue)) _isChanged = true;
			if (DrawAnimElementUI("Position##ch", EAnimChannel::POSITION, _anim.channels, _anim.position, DrawVectorValue)) _isChanged = true;
			if (DrawAnimElementUI("Scale##ch", EAnimChannel::SCALE, _anim.channels, _anim.scale, DrawVectorValue)) _isChanged = true;
			if (DrawAnimElementUI("Rotation##ch", EAnimChannel::ROTATION, _anim.channels, _anim.rotation, DrawFloatValue)) _isChanged = true;
			if (DrawAnimElementUI("UV##ch", EAnimChannel::UV, _anim.channels, _anim.uv, DrawVectorValue)) _isChanged = true;

			if (Engine::EditorField::Button("Replay")) _anim.currentTime = 0.0f;
			Engine::EditorField::SameLine();
			Engine::EditorField::Text("%.2f / %.2f", _anim.currentTime, _anim.durationTime);
		}

		// ---- 揺れ ----
		bool _hasOscillation = a_decoration.opOscillationAnim.has_value();
		if (Engine::EditorField::Field("Oscillation", _hasOscillation))
		{
			if (_hasOscillation) a_decoration.opOscillationAnim = UIProceduralAnimation();
			else                 a_decoration.opOscillationAnim.reset();
			_isChanged = true;
		}

		if (a_decoration.opOscillationAnim.has_value())
		{
			UIProceduralAnimation& _anim = *a_decoration.opOscillationAnim;

			Engine::EditorField::IndentScope _indent;
			Engine::EditorField::IDScope _id("Oscillation");

			Engine::EditorField::HelpText("位置と回転は足す量、大きさと色は掛ける量(1が元のまま)");

			// 上下限を立てたときの初期値 : そのチャンネルらしい範囲を入れておく
			if (DrawOscillationUI("Position##osc", EAnimChannel::POSITION, _anim.channels, _anim.position, DrawVectorValue,
				Math::Vector2(-10.0f, -10.0f), Math::Vector2(10.0f, 10.0f))) _isChanged = true;

			if (DrawOscillationUI("Scale##osc", EAnimChannel::SCALE, _anim.channels, _anim.scale, DrawVectorValue,
				Math::Vector2(0.9f, 0.9f), Math::Vector2(1.1f, 1.1f))) _isChanged = true;

			if (DrawOscillationUI("Rotation##osc", EAnimChannel::ROTATION, _anim.channels, _anim.rotation, DrawFloatValue,
				-10.0f, 10.0f)) _isChanged = true;

			if (DrawOscillationUI("Color##osc", EAnimChannel::COLOR, _anim.channels, _anim.color, DrawColorValue,
				Math::Color(1.0f, 1.0f, 1.0f, 0.3f), Math::Color(1.0f, 1.0f, 1.0f, 1.0f))) _isChanged = true;
		}

		return _isChanged;
	}
}
