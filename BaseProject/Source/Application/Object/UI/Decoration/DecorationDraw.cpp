#include "Application/Object/UI/Decoration/Internal/DecorationInternal.h"
#include "Application/Object/UI/Decoration/DecorationDraw.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/Texture/IO/TextureIO.h"
#include "Engine/Resource/Data/UIMaterial/UIMaterial.h"

//==========================================================================================
// デコレーションの描画
//
// 描画はすべて「ローカル矩形を親の回転で回してから積む」1つの経路に寄せてある
// (SubmitLocalRect)。塗り・枠・文字のどれも、アンカーからのずれを持った矩形の集まりなので、
// ここを共通にしておかないと回転を掛けたときにバラバラにずれる。
//
// 合成・矩形・枠・文字は Internal に置き、新しいエレメント(UI*Element)もここを通る
//==========================================================================================
namespace App::Object::Decoration
{
	using Internal::RotateDeg;
	using Internal::Resolved;

	//======================================================================================
	// 共有部分(Internal)
	//======================================================================================

	// 中身は 4x4 の白1色。ResourceManager 側がGUIDでキャッシュしているので、
	// 毎フレーム呼んでも作り直しにはならない
	Engine::Handle<Engine::Resource::Texture> Internal::GetWhiteTexture(Engine::Resource::ResourceManager& a_resourceManager)
	{
		const auto _context = Engine::Resource::MakeManagerOnlyContext(&a_resourceManager, &a_resourceManager.RefAssetDatabase());
		return Engine::Resource::TextureIO::LoadTexture(Core::GUID(), Engine::TexColor::WHITE, &_context);
	}

	//--------------------------------------------------------------------------------------
	// 親と合成する
	//--------------------------------------------------------------------------------------
	Resolved Internal::Resolve(
		const ResolveSource& a_source,
		const AnimResult& a_anim,
		const ParentTransform& a_parentTr,
		const ParentOption& a_parentOp,
		const DrawOverride& a_override)
	{
		Resolved _out = {};

		// 親の倍率 × 自分の倍率 × その場の倍率
		_out.uniformScale = a_parentTr.scale * a_source.scale * a_override.scale;

		// ずれは親の回転で回してから足す : 親を回すと飾りが親の周りを回る
		const Math::Vector2 _basePos = a_override.isUsePos ? a_override.pixelPos : a_parentTr.pixelPos;
		const Math::Vector2 _offset = (a_source.offsetPos + a_anim.positionAdd) * _out.uniformScale;
		_out.anchorPos = _basePos + RotateDeg(_offset, a_parentTr.rotation);

		_out.size = a_source.pixelSize * _out.uniformScale * a_anim.scaleMul * a_override.sizeScale;
		_out.rotation = a_parentTr.rotation + a_source.rotation + a_anim.rotationAdd;
		_out.layer = a_parentTr.layer + a_source.layerOffset;

		// 親の色を受けない飾りでも、その場の色(演出の濃さなど)とアニメは掛ける
		const Math::Color _parentColor = a_source.isParentColor ? a_parentTr.color : Engine::Color::WHITE;
		_out.modulate = _parentColor * a_override.tint * a_anim.colorMul;
		_out.color = _out.modulate * a_source.color;

		_out.uvOffset = a_source.uvOffset + a_override.uvOffsetAdd + a_anim.uvAdd;
		_out.uvScale = a_override.isUseUvScale ? a_override.uvScale : a_source.uvScale;

		//--------------------------------------------------------------
		// 湾曲
		//
		// 「開き角・深さ・弧の頂点」を、シェーダーがそのまま使える
		//   曲率 c(1/px, = 1/円の半径) と 弧の頂点の位置(px)
		// へここで畳む。畳んでおけば、枠・中身・文字がどんな大きさでも
		// 同じ c と同じ頂点を見るので、全部が1つの円に乗る。
		//
		// c は「親の矩形の幅が、ちょうど開き角 A ぶんの弧になる」ように決める。
		//   弧の長さ 2W = 半径 R × 角度 A なので c = 1/R = A / 2W
		// 深さの倍率は角度へ掛ける(倍率 2 なら同じ幅で倍の角度まで巻く)。
		//
		// 幅ではなく高さを基準にすると、ゲージのような横長で背の低いUIが
		// ほとんど曲がらないので必ず幅で測る
		//--------------------------------------------------------------
		const float _halfSpanX = a_parentOp.parentSize.x * 0.5f * a_parentTr.scale;
		const float _halfSpanY = a_parentOp.parentSize.y * 0.5f * a_parentTr.scale;

		// 深さの倍率。0(既定値)のままでも曲がるように1として扱う
		const float _curveDepth = (a_parentOp.curveRadius > 0.0f) ? a_parentOp.curveRadius : 1.0f;

		if (a_parentOp.curveAngle != 0.0f && _halfSpanX > 0.0f)
		{
			_out.curvature = a_parentOp.curveAngle * _curveDepth / (2.0f * _halfSpanX);
		}
		_out.curveOriginX = a_parentOp.curveCenter.x * _halfSpanX;
		_out.curveShiftY = a_parentOp.curveCenter.y * _halfSpanY;
		_out.localOffset = _offset;

		// 切り抜きは親(UI)が実行中だけ持つもの。描き終わるまで親の側に残っているので指すだけでよい
		_out.pClipRect = a_parentOp.opClipRect ? &a_parentOp.opClipRect.value() : nullptr;

		// ポストプロセスは UI 単位で選ぶので、飾りはどれも親の設定に従う
		if (a_parentOp.isScanLine) _out.postEffectFlags |= Engine::Graphics::UI_POST_EFFECT_SCAN_LINE;

		//--------------------------------------------------------------
		// 効果(UIマテリアル)
		//
		// ノイズの原点は親のピボット(位置の差し替えがあっても親のもの)。
		// 枠・中身・文字・桁ごとの数字が同じ原点から測るので、UI が1枚の絵として崩れる
		//--------------------------------------------------------------
		if (const Engine::Resource::UIMaterial* _pMaterial = a_parentOp.pMaterial)
		{
			Engine::Graphics::UIEffectDesc& _effect = _out.effect;
			_effect.effect = static_cast<uint32_t>(_pMaterial->effect);
			_effect.noiseTexHandle = _pMaterial->noiseTexRef.GetRaw();
			_effect.progress = std::clamp(a_override.dissolve, 0.0f, 1.0f);
			_effect.noisePixel = _pMaterial->noisePixel;
			_effect.edgeWidth = _pMaterial->edgeWidth;
			_effect.edgeColor = _pMaterial->edgeColor;
			_effect.originPixel = a_parentTr.pixelPos;

			if (_pMaterial->isScanLine) _out.postEffectFlags |= Engine::Graphics::UI_POST_EFFECT_SCAN_LINE;
		}

		return _out;
	}

	//--------------------------------------------------------------------------------------
	// 描画の最小単位
	//
	// アンカーからのずれ(回転前)で矩形を指定する。
	// ずれを回してから積み、クアッド自体も同じ角度で回すので、
	// 塗り・枠・文字がバラけずに1枚として回る
	//--------------------------------------------------------------------------------------
	void Internal::SubmitLocalRect(
		Engine::Graphics::GraphicsEngine* a_pGE,
		const Engine::Handle<Engine::Resource::Texture>& a_texHandle,
		const Resolved& a_resolved,
		const Math::Vector2& a_localTopLeft,
		const Math::Vector2& a_size,
		const Math::Color& a_color,
		const Math::Vector2& a_uvOffset,
		const Math::Vector2& a_uvScale)
	{
		if (a_size.x <= 0.0f || a_size.y <= 0.0f) return;
		if (a_color.a <= 0.0f) return;

		// 曲げたときの上下のずらしはローカルで足してから回す
		const Math::Vector2 _localTopLeft = {
			a_localTopLeft.x,
			a_localTopLeft.y + a_resolved.curveShiftY
		};
		const Math::Vector2 _pos = a_resolved.anchorPos + RotateDeg(_localTopLeft, a_resolved.rotation);

		// この矩形の中心が、弧の頂点からどれだけずれているか(px, 右・下が正)。
		//
		// 飾りのずれ(回転前) + 矩形のずれ + 矩形の半分の大きさ で、親のローカルでの中心が出る。
		// ここを矩形ごとに正しく渡すから、幅の違う枠と中身(ゲージの残量)や文字が
		// 同じ1つの円に乗る。矩形の中で閉じて曲げると別々の曲がり方になってしまう。
		//
		// 縦は上下のずらし(curveShiftY)を入れる前で測る。ずらしは曲げた後の UI 全体を
		// 動かすためのもので、半径(どこまで内側か)を変えるためのものではないため
		const float _curveOffsetX =
			a_resolved.localOffset.x + a_localTopLeft.x + a_size.x * 0.5f - a_resolved.curveOriginX;
		const float _curveOffsetY =
			a_resolved.localOffset.y + a_localTopLeft.y + a_size.y * 0.5f;

		Engine::Graphics::SpriteSubmitDesc _desc = {};
		_desc.texHandle = a_texHandle;
		_desc.pixelPos = _pos;
		_desc.pixelSize = a_size;
		_desc.rotationDeg = a_resolved.rotation;
		_desc.Zlayer = a_resolved.layer;
		_desc.color = a_color;
		_desc.uvOffset = a_uvOffset;
		_desc.pivot = { 0.0f, 0.0f };		// ずれで位置を決めているのでピボットは左上固定
		_desc.uvScale = a_uvScale;
		_desc.curvature = a_resolved.curvature;
		_desc.curveOffsetX = _curveOffsetX;
		_desc.curveOffsetY = _curveOffsetY;
		_desc.pClipRect = a_resolved.pClipRect;
		_desc.postEffectFlags = a_resolved.postEffectFlags;
		_desc.effect = a_resolved.effect;
		a_pGE->RefDrawSubmitter()->SubmitUI(_desc);
	}

	//--------------------------------------------------------------------------------------
	// 枠を描く : 矩形の内側に貼り付ける
	//--------------------------------------------------------------------------------------
	void Internal::DrawEdge(
		Engine::Graphics::GraphicsEngine* a_pGE,
		float a_edgePixel,
		EDirection a_edgeSide,
		const Resolved& a_resolved,
		const Math::Vector2& a_localTopLeft,
		const Math::Color& a_edgeColor)
	{
		using Core::HasFlag;

		const float _thickness = a_edgePixel * a_resolved.uniformScale;
		if (_thickness <= 0.0f) return;
		if (a_edgeSide == EDirection::NONE) return;

		const Engine::Handle<Engine::Resource::Texture> _white = GetWhiteTexture(*a_pGE->RefResourceManager());
		const Math::Vector2& _size = a_resolved.size;

		// 太さが矩形を超えたら塗りつぶしと同じになるので詰める
		const float _thickX = std::min(_thickness, _size.x);
		const float _thickY = std::min(_thickness, _size.y);

		auto _submit = [&](const Math::Vector2& a_offset, const Math::Vector2& a_edgeSize)
			{
				SubmitLocalRect(
					a_pGE, _white, a_resolved,
					a_localTopLeft + a_offset, a_edgeSize,
					a_edgeColor, {}, { 1.0f, 1.0f });
			};

		if (HasFlag(a_edgeSide, EDirection::UP))    _submit({ 0.0f, 0.0f }, { _size.x, _thickY });
		if (HasFlag(a_edgeSide, EDirection::DOWN))  _submit({ 0.0f, _size.y - _thickY }, { _size.x, _thickY });
		if (HasFlag(a_edgeSide, EDirection::LEFT))  _submit({ 0.0f, 0.0f }, { _thickX, _size.y });
		if (HasFlag(a_edgeSide, EDirection::RIGHT)) _submit({ _size.x - _thickX, 0.0f }, { _thickX, _size.y });
	}

	//--------------------------------------------------------------------------------------
	// 文字を描く
	//
	// グリフの並べ方はエンジン側(DrawSubmitter::SubmitString)が持つ。
	// ここでは親と合成した状態を、文字列の描画命令の形へ直して渡すだけ
	//--------------------------------------------------------------------------------------
	void Internal::SubmitText(
		Engine::Graphics::GraphicsEngine* a_pGE,
		const Resolved& a_resolved,
		const Math::Vector2& a_pivot,
		const TextLayout& a_layout)
	{
		if (a_layout.text.empty()) return;
		if (!a_layout.fontHandle.IsValid()) return;

		Engine::Graphics::FontSubmitDesc _desc = {};
		_desc.fontHandle = a_layout.fontHandle;
		_desc.text = a_layout.text;

		// 曲げたときの上下のずらしは、回す前に足しておく(他の矩形と同じ)
		_desc.pixelPos = a_resolved.anchorPos + RotateDeg({ 0.0f, a_resolved.curveShiftY }, a_resolved.rotation);

		_desc.fontPixelSize = a_layout.fontPixelSize * a_resolved.uniformScale;
		_desc.rotationDeg = a_resolved.rotation;
		_desc.Zlayer = a_resolved.layer;
		_desc.color = a_resolved.color;
		_desc.pivot = a_pivot;
		_desc.textAlign = a_layout.textAlign;
		_desc.charSpacing = a_layout.charSpacing * a_resolved.uniformScale;
		_desc.lineSpacing = a_layout.lineSpacing;

		// 弧の頂点からアンカーまでのずれ(回転前)。縦は上下のずらしを入れる前で測る
		_desc.curvature = a_resolved.curvature;
		_desc.curveOffsetX = a_resolved.localOffset.x - a_resolved.curveOriginX;
		_desc.curveOffsetY = a_resolved.localOffset.y;

		_desc.pClipRect = a_resolved.pClipRect;
		_desc.postEffectFlags = a_resolved.postEffectFlags;
		_desc.effect = a_resolved.effect;

		a_pGE->RefDrawSubmitter()->SubmitString(_desc);
	}

	//======================================================================================
	// 旧 Decoration
	//======================================================================================
	namespace
	{
		// 旧 Decoration の値を合成の元へ写す
		Internal::ResolveSource MakeResolveSource(const Decoration& a_decoration)
		{
			// UV を持つのは矩形の飾りだけ(文字はグリフごとに自前で切り出す)
			const QuadStyle _quad = a_decoration.GetQuad() ? *a_decoration.GetQuad() : QuadStyle{};

			Internal::ResolveSource _source = {};
			_source.offsetPos = a_decoration.offsetPos;
			_source.pixelSize = a_decoration.pixelSize;
			_source.rotation = a_decoration.rotation;
			_source.scale = a_decoration.scale;
			_source.layerOffset = a_decoration.layerOffset;
			_source.color = a_decoration.color;
			_source.uvOffset = _quad.uvOffset;
			_source.uvScale = _quad.uvScale;
			return _source;
		}

		Engine::Graphics::ETextAlign ToEngineTextAlign(ETextAlign a_textAlign)
		{
			switch (a_textAlign)
			{
			case ETextAlign::Center: return Engine::Graphics::ETextAlign::Center;
			case ETextAlign::Right:  return Engine::Graphics::ETextAlign::Right;
			case ETextAlign::Left:
			default:                 return Engine::Graphics::ETextAlign::Left;
			}
		}
	}

	//======================================================================================
	// 描画
	//======================================================================================
	void DrawDecoration(
		Engine::Graphics::GraphicsEngine* a_pGraphicsEngine,
		Engine::Resource::ResourceManager* a_pResourceManager,
		const Decoration& a_decoration,
		const ParentTransform& a_parentTr,
		const ParentOption& a_parentOp,
		const DrawOverride& a_override)
	{
		if (a_pGraphicsEngine == nullptr || a_pResourceManager == nullptr) return;
		if (!a_decoration.isVisible) return;

		// 群で絞られているなら、対象外は描かない
		if (a_override.isUseGroup && a_decoration.group != a_override.group) return;

		const Resolved _resolved = Internal::Resolve(
			MakeResolveSource(a_decoration),
			Internal::MakeAnimResult(a_decoration),
			a_parentTr, a_parentOp, a_override);

		// 文字はグリフごとに矩形を組むので別経路
		if (const TextData* _pText = a_decoration.GetText())
		{
			Internal::TextLayout _layout = {};
			_layout.fontHandle = _pText->fontRef.GetRaw();
			_layout.text = _pText->text;
			_layout.fontPixelSize = _pText->fontPixelSize;
			_layout.lineSpacing = _pText->lineSpacing;
			_layout.charSpacing = _pText->charSpacing;
			_layout.textAlign = ToEngineTextAlign(_pText->textAlign);
			Internal::SubmitText(a_pGraphicsEngine, _resolved, a_decoration.pivot, _layout);
			return;
		}

		const QuadStyle* _pQuad = a_decoration.GetQuad();
		if (_pQuad == nullptr) return;

		// 矩形の左上(アンカーからのずれ)
		const Math::Vector2 _localTopLeft = {
			-a_decoration.pivot.x * _resolved.size.x,
			-a_decoration.pivot.y * _resolved.size.y
		};

		// ---- 塗り ----
		if (_pQuad->isFill)
		{
			// 板ポリは組み込みの白テクスチャを色で染める。
			// 画像は届くまで描かない(空ハンドルで積むとテクスチャ取得で落ちる)
			Engine::Handle<Engine::Resource::Texture> _texHandle = {};

			if (const ImageData* _pImage = a_decoration.GetImage())
			{
				if (a_pResourceManager->IsReady(_pImage->texRef))
				{
					_texHandle = _pImage->texRef.GetRaw();
				}
			}
			else
			{
				_texHandle = Internal::GetWhiteTexture(*a_pResourceManager);
			}

			if (_texHandle.IsValid())
			{
				Internal::SubmitLocalRect(
					a_pGraphicsEngine, _texHandle, _resolved,
					_localTopLeft, _resolved.size,
					_resolved.color, _resolved.uvOffset, _resolved.uvScale
				);
			}
		}

		// ---- 枠 ----
		// 塗りと同じ掛かり具合(親の色・その場の色・アニメ・反応)を通す。
		// ここを飛ばすと、反応で消したはずの枠だけ残ってしまう
		const Math::Color _edgeColor = _resolved.modulate * _pQuad->edgeColor;
		Internal::DrawEdge(a_pGraphicsEngine, _pQuad->edgePixel, _pQuad->edgeSide, _resolved, _localTopLeft, _edgeColor);
	}

	//======================================================================================
	// GUIDから参照を引き直す
	//======================================================================================
	void RequestResources(Decoration& a_decoration, Engine::Resource::ResourceManager* a_pResourceManager)
	{
		if (a_pResourceManager == nullptr) return;

		// 実体の到着は待たない。描画側が IsReady を見てスキップする
		if (ImageData* _pImage = a_decoration.RefImage())
		{
			_pImage->texRef = _pImage->texGUID.IsValid()
				? a_pResourceManager->RequestLoad<Engine::Resource::Texture>(_pImage->texGUID)
				: Engine::ResourceRef<Engine::Resource::Texture>();
		}

		if (TextData* _pText = a_decoration.RefText())
		{
			_pText->fontRef = _pText->fontGUID.IsValid()
				? a_pResourceManager->RequestLoad<Engine::Resource::Font>(_pText->fontGUID)
				: Engine::ResourceRef<Engine::Resource::Font>();
		}
	}
}
