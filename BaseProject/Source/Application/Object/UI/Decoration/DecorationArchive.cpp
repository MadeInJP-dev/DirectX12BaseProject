#include "Application/Object/UI/Decoration/Internal/DecorationInternal.h"

//==========================================================================================
// デコレーションの保存
//
// 種類ごとの中身は variant で持っているが、保存の並びは全種類のフィールドを
// 1列に並べていた頃のまま残してある。区切りを持たない古い .ob* は並び順でしか
// 読めないため。持っていない種類のぶんは既定値を書き、読んだ値は種類に合うものだけ使う。
//==========================================================================================
namespace App::Object::Decoration
{
	namespace
	{
		template<typename T>
		void ArchiveAnimElement(Engine::Persistence::Archive& a_ar, const std::string& a_name, AnimElement<T>& a_element)
		{
			a_ar.Field(a_name + "Start", a_element.start);
			a_ar.Field(a_name + "End", a_element.end);
		}

		void ArchiveStateStyle(Engine::Persistence::Archive& a_ar, const std::string& a_name, UIStateStyle& a_style)
		{
			a_ar.Field(a_name + "Color", a_style.color);
			a_ar.Field(a_name + "Scale", a_style.scale);
			a_ar.Field(a_name + "Offset", a_style.offsetAdd);
		}

		template<typename T>
		void ArchiveOscillation(Engine::Persistence::Archive& a_ar, const std::string& a_name, Oscillation<T>& a_oscillation)
		{
			a_ar.Field(a_name + "Amplitude", a_oscillation.amplitude);
			a_ar.Field(a_name + "Frequency", a_oscillation.frequency);
			a_ar.Field(a_name + "Phase", a_oscillation.phase);

			a_ar.Field(a_name + "UseLimit", a_oscillation.isUseLimit);
			a_ar.Field(a_name + "Min", a_oscillation.minValue);
			a_ar.Field(a_name + "Max", a_oscillation.maxValue);
		}

		//----------------------------------------------------------------------------------
		// 種類ごとの中身
		//
		// 保存 : 今の中身を3種類ぶんの入れ物へ写して(無い種類は既定値のまま)全部書く
		// 読込 : 3種類ぶん全部読んでから、読んだ種類の中身だけを組み立てる
		//----------------------------------------------------------------------------------
		void ArchiveBody(Engine::Persistence::Archive& a_ar, Decoration& a_decoration, EDecorationType a_type)
		{
			// 保存する値だけを写す。参照(texRef / fontRef)は写さない
			// (写すと参照の数え上げが動くうえ、読み込みの後は RequestResources で引き直すため)
			QuadStyle _quad = a_decoration.GetQuad() ? *a_decoration.GetQuad() : QuadStyle{};

			Core::GUID _texGUID = a_decoration.GetImage() ? a_decoration.GetImage()->texGUID : Core::GUID{};

			TextData _text = {};
			if (const TextData* _pText = a_decoration.GetText())
			{
				_text.text = _pText->text;
				_text.fontGUID = _pText->fontGUID;
				_text.fontPixelSize = _pText->fontPixelSize;
				_text.lineSpacing = _pText->lineSpacing;
				_text.charSpacing = _pText->charSpacing;
				_text.textAlign = _pText->textAlign;
			}

			a_ar.Field("UVOffset", _quad.uvOffset);
			a_ar.Field("UVScale", _quad.uvScale);

			a_ar.GUIDField("TexGUID", _texGUID);

			a_ar.Field("IsFill", _quad.isFill);
			a_ar.Field("EdgeColor", _quad.edgeColor);
			a_ar.Field("EdgePixel", _quad.edgePixel);
			a_ar.Field("EdgeSide", _quad.edgeSide);

			a_ar.StringField("Text", _text.text);
			a_ar.GUIDField("FontGUID", _text.fontGUID);
			a_ar.Field("FontPixelSize", _text.fontPixelSize);
			a_ar.Field("LineSpacing", _text.lineSpacing);
			a_ar.Field("CharSpacing", _text.charSpacing);
			a_ar.Field("TextAlign", _text.textAlign);

			if (!a_ar.IsLoading()) return;

			// 読んだ種類の中身だけを組み立てる
			switch (a_type)
			{
			case EDecorationType::Image:
			{
				ImageData _image = {};
				_image.quad = _quad;
				_image.texGUID = _texGUID;
				a_decoration.body = std::move(_image);
				break;
			}

			case EDecorationType::Text:
				a_decoration.body = std::move(_text);
				break;

			case EDecorationType::Polygon:
			default:
			{
				PolygonData _polygon = {};
				_polygon.quad = _quad;
				a_decoration.body = std::move(_polygon);
				break;
			}
			}
		}
	}

	void ArchiveDecoration(Engine::Persistence::Archive& a_ar, Decoration& a_decoration)
	{
		EDecorationType _type = a_decoration.GetType();
		a_ar.Field("Type", _type);
		a_ar.StringField("Name", a_decoration.name);
		a_ar.Field("IsVisible", a_decoration.isVisible);
		a_ar.Field("Group", a_decoration.group);

		a_ar.Field("OffsetPos", a_decoration.offsetPos);
		a_ar.Field("PixelSize", a_decoration.pixelSize);
		a_ar.Field("Rotation", a_decoration.rotation);
		a_ar.Field("Scale", a_decoration.scale);
		a_ar.Field("Pivot", a_decoration.pivot);
		a_ar.Field("LayerOffset", a_decoration.layerOffset);
		a_ar.Field("Color", a_decoration.color);

		ArchiveBody(a_ar, a_decoration, _type);

		//----------------------------------------------------------------------------------
		// アニメーション
		//
		// optional は「持っているか」を先に書いてから中身を書く。
		// 持っていない場合は中身を一切読み書きしないので、保存側と読込側で必ず対になる
		//----------------------------------------------------------------------------------
		bool _hasTween = a_decoration.opTweenAnim.has_value();
		a_ar.Field("HasTween", _hasTween);
		if (_hasTween)
		{
			if (!a_decoration.opTweenAnim.has_value()) a_decoration.opTweenAnim = UIAnimation();

			UIAnimation& _anim = *a_decoration.opTweenAnim;
			a_ar.Field("TweenDuration", _anim.durationTime);
			a_ar.Field("TweenIsLoop", _anim.isLoop);
			a_ar.Field("TweenIsPingPong", _anim.isPingPong);
			a_ar.Field("TweenEase", _anim.ease);
			a_ar.Field("TweenChannels", _anim.channels);

			ArchiveAnimElement(a_ar, "TweenColor", _anim.color);
			ArchiveAnimElement(a_ar, "TweenPosition", _anim.position);
			ArchiveAnimElement(a_ar, "TweenScale", _anim.scale);
			ArchiveAnimElement(a_ar, "TweenRotation", _anim.rotation);
			ArchiveAnimElement(a_ar, "TweenUV", _anim.uv);
		}
		else
		{
			a_decoration.opTweenAnim.reset();
		}

		//----------------------------------------------------------------------------------
		// カーソルへの反応
		//----------------------------------------------------------------------------------
		bool _hasReaction = a_decoration.opReaction.has_value();
		a_ar.Field("HasReaction", _hasReaction);
		if (_hasReaction)
		{
			if (!a_decoration.opReaction.has_value()) a_decoration.opReaction = UIReaction();

			UIReaction& _reaction = *a_decoration.opReaction;
			a_ar.Field("ReactionVisibleState", _reaction.visibleState);
			a_ar.Field("ReactionBlendSpeed", _reaction.blendSpeed);

			ArchiveStateStyle(a_ar, "ReactionHovered", _reaction.hovered);
			ArchiveStateStyle(a_ar, "ReactionPressed", _reaction.pressed);
			ArchiveStateStyle(a_ar, "ReactionDisabled", _reaction.disabled);
		}
		else
		{
			a_decoration.opReaction.reset();
		}

		bool _hasOscillation = a_decoration.opOscillationAnim.has_value();
		a_ar.Field("HasOscillation", _hasOscillation);
		if (_hasOscillation)
		{
			if (!a_decoration.opOscillationAnim.has_value()) a_decoration.opOscillationAnim = UIProceduralAnimation();

			UIProceduralAnimation& _anim = *a_decoration.opOscillationAnim;
			a_ar.Field("OscChannels", _anim.channels);

			ArchiveOscillation(a_ar, "OscPosition", _anim.position);
			ArchiveOscillation(a_ar, "OscScale", _anim.scale);
			ArchiveOscillation(a_ar, "OscRotation", _anim.rotation);
			ArchiveOscillation(a_ar, "OscColor", _anim.color);
		}
		else
		{
			a_decoration.opOscillationAnim.reset();
		}

		// ---- ここから下は区切りを入れた後に足したもの : 追加は必ず末尾へ ----
		// 区切りを持たない古い .ob* には無いので読まない(読むと後ろの飾りを食う)
		if (a_ar.IsLegacyLayout()) return;

		// 番号。持っていない古いデータは 0 のまま読まれ、UIBase が振り直す
		a_ar.Field("ID", a_decoration.id);
	}
}
