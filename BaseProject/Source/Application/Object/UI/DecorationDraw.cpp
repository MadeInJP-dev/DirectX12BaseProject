#include "DecorationInternal.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/Texture/IO/TextureIO.h"

//==========================================================================================
// デコレーションの描画
//
// 描画はすべて「ローカル矩形を親の回転で回してから積む」1つの経路に寄せてある
// (SubmitLocalRect)。塗り・枠・文字のどれも、アンカーからのずれを持った矩形の集まりなので、
// ここを共通にしておかないと回転を掛けたときにバラバラにずれる。
//==========================================================================================
namespace App::Object::Decoration
{
	using Internal::RotateDeg;

	namespace
	{
		// 板ポリ用の白テクスチャ
		//
		// 中身は 4x4 の白1色。ResourceManager 側がGUIDでキャッシュしているので、
		// 毎フレーム呼んでも作り直しにはならない
		Engine::Handle<Engine::Resource::Texture> GetWhiteTexture(Engine::Resource::ResourceManager& a_resourceManager)
		{
			const auto _context = Engine::Resource::MakeManagerOnlyContext(&a_resourceManager, &a_resourceManager.RefAssetDatabase());
			return Engine::Resource::TextureIO::LoadTexture(Core::GUID(), Engine::TexColor::WHITE, &_context);
		}

		//----------------------------------------------------------------------------------
		// 親と合成したあとの状態
		//----------------------------------------------------------------------------------
		struct Resolved
		{
			Math::Vector2 anchorPos = {};	// ピボットが乗るスクリーン座標(px)
			Math::Vector2 size = {};		// 大きさ(px)
			float rotation = 0.0f;			// 回転(度)
			float layer = 0.0f;				// Z順
			Math::Color color = {};			// 最終色(飾り自身の色まで掛けたもの)

			/// <summary>飾り自身の色を除いた掛かり具合(親の色 × その場の色 × アニメ)</summary>
			/// <remarks>
			/// 枠は飾り本体とは別の色を持つので、最終色から逆算せずにこれを掛ける。
			/// ここを通さないと、反応で消したはずの枠だけ residual に残る
			/// </remarks>
			Math::Color modulate = {};

			Math::Vector2 uvOffset = {};	// UVオフセット
			Math::Vector2 uvScale = { 1.0f, 1.0f };	// UV倍率
			float uniformScale = 1.0f;		// 枠の太さ・字間など、1軸で効かせたいもの用

			// 湾曲(親の設定を、そのまま渡せる形へ畳んだもの)
			float curveK = 0.0f;			// 反りの強さ(1/px)。0で曲げない
			float curveOriginX = 0.0f;		// 弧の頂点の横位置(親のアンカーからのpx)
			float curveShiftY = 0.0f;		// 曲げたときに全体を上下へずらす量(px)

			// 親の回転を掛ける前の、この飾りのずれ(px)。
			// 弧の中心からの横ずれを測るのに使う(回した後の座標では測れない)
			Math::Vector2 localOffset = {};

			// 切り抜き(親の持ち物を指すだけ)。nullptr なら切らない
			const Engine::Graphics::UIClipRect* pClipRect = nullptr;

			// 掛けるポストプロセスの印(Engine::Graphics::UI_POST_EFFECT_*)
			UINT postEffectFlags = 0;
		};

		Resolved Resolve(
			const Decoration& a_decoration,
			const ParentTransform& a_parentTr,
			const ParentOption& a_parentOp,
			const DrawOverride& a_override)
		{
			const Internal::AnimResult _anim = Internal::MakeAnimResult(a_decoration);

			Resolved _out = {};

			// 親の倍率 × 自分の倍率 × その場の倍率
			_out.uniformScale = a_parentTr.scale * a_decoration.scale * a_override.scale;

			// ずれは親の回転で回してから足す : 親を回すと飾りが親の周りを回る
			const Math::Vector2 _basePos = a_override.isUsePos ? a_override.pixelPos : a_parentTr.pixelPos;
			const Math::Vector2 _offset = (a_decoration.offsetPos + _anim.positionAdd) * _out.uniformScale;
			_out.anchorPos = _basePos + RotateDeg(_offset, a_parentTr.rotation);

			_out.size = a_decoration.pixelSize * _out.uniformScale * _anim.scaleMul * a_override.sizeScale;
			_out.rotation = a_parentTr.rotation + a_decoration.rotation + _anim.rotationAdd;
			_out.layer = a_parentTr.layer + a_decoration.layerOffset;

			_out.modulate = a_parentTr.color * a_override.tint * _anim.colorMul;
			_out.color = _out.modulate * a_decoration.color;

			// UV を持つのは矩形の飾りだけ(文字はグリフごとに自前で切り出す)
			const QuadStyle _quad = a_decoration.GetQuad() ? *a_decoration.GetQuad() : QuadStyle{};
			_out.uvOffset = _quad.uvOffset + a_override.uvOffsetAdd + _anim.uvAdd;
			_out.uvScale = a_override.isUseUvScale ? a_override.uvScale : _quad.uvScale;

			//--------------------------------------------------------------
			// 湾曲
			//
			// 「開き角・深さ・弧の中心」を、シェーダーがそのまま使える
			//   反りの強さ k(1/px) と 弧の頂点の位置(px)
			// へここで畳む。畳んでおけば、枠・中身・文字がどんな大きさでも
			// 同じ k と同じ頂点を見るので、全部が1本の弧に乗る。
			//
			// k は「親の矩形の端で、円弧と同じだけ反る」ように決める。
			//   半幅 W を開き角 A で曲げたときの反り = W * tan(A/4)
			//   反りを k*W^2 で作るので k = tan(A/4) / W
			//
			// 幅ではなく高さを基準にすると、ゲージのような横長で背の低いUIが
			// ほとんど反らない(見た目上まったく曲がらない)ので必ず幅で測る
			//--------------------------------------------------------------
			const float _halfSpanX = a_parentOp.parentSize.x * 0.5f * a_parentTr.scale;
			const float _halfSpanY = a_parentOp.parentSize.y * 0.5f * a_parentTr.scale;

			// 深さの倍率。0(既定値)のままでも曲がるように1として扱う
			const float _curveDepth = (a_parentOp.curveRadius > 0.0f) ? a_parentOp.curveRadius : 1.0f;

			if (a_parentOp.curveAngle != 0.0f && _halfSpanX > 0.0f)
			{
				_out.curveK = std::tan(a_parentOp.curveAngle * 0.25f) * _curveDepth / _halfSpanX;
			}
			_out.curveOriginX = a_parentOp.curveCenter.x * _halfSpanX;
			_out.curveShiftY = a_parentOp.curveCenter.y * _halfSpanY;
			_out.localOffset = _offset;

			// 切り抜きは親(UI)が実行中だけ持つもの。描き終わるまで親の側に残っているので指すだけでよい
			_out.pClipRect = a_parentOp.opClipRect ? &a_parentOp.opClipRect.value() : nullptr;

			// ポストプロセスは UI 単位で選ぶので、飾りはどれも親の設定に従う
			if (a_parentOp.isScanLine) _out.postEffectFlags |= Engine::Graphics::UI_POST_EFFECT_SCAN_LINE;

			return _out;
		}

		//----------------------------------------------------------------------------------
		// 描画の最小単位
		//
		// アンカーからのずれ(回転前)で矩形を指定する。
		// ずれを回してから積み、クアッド自体も同じ角度で回すので、
		// 塗り・枠・文字がバラけずに1枚として回る
		//----------------------------------------------------------------------------------
		void SubmitLocalRect(
			Engine::Graphics::GraphicsEngine* a_pGE,
			const Engine::Handle<Engine::Resource::Texture>& a_texHandle,
			const Resolved& a_resolved,
			const Math::Vector2& a_localTopLeft,
			const Math::Vector2& a_size,
			const Math::Color& a_color,
			const Math::Vector2& a_uvOffset,
			const Math::Vector2& a_uvScale
		)
		{
			if (a_size.x <= 0.0f || a_size.y <= 0.0f) return;
			if (a_color.a <= 0.0f) return;

			// 曲げたときの上下のずらしはローカルで足してから回す
			const Math::Vector2 _localTopLeft = {
				a_localTopLeft.x,
				a_localTopLeft.y + a_resolved.curveShiftY
			};
			const Math::Vector2 _pos = a_resolved.anchorPos + RotateDeg(_localTopLeft, a_resolved.rotation);

			// この矩形の中心が、弧の頂点からどれだけ横にずれているか(px)。
			//
			// 飾りのずれ(回転前) + 矩形のずれ + 矩形の半幅 で、親のローカルでの中心が出る。
			// ここを矩形ごとに正しく渡すから、幅の違う枠と中身(ゲージの残量)が
			// 同じ1本の弧に乗る。矩形の中で閉じて曲げると別々の曲がり方になってしまう
			const float _curveOffsetX =
				a_resolved.localOffset.x + a_localTopLeft.x + a_size.x * 0.5f - a_resolved.curveOriginX;

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
			_desc.curveK = a_resolved.curveK;
			_desc.curveOffsetX = _curveOffsetX;
			_desc.pClipRect = a_resolved.pClipRect;
			_desc.postEffectFlags = a_resolved.postEffectFlags;
			a_pGE->RefDrawSubmitter()->SubmitUI(_desc);
		}

		//----------------------------------------------------------------------------------
		// 枠を描く : 矩形の内側に貼り付ける
		//----------------------------------------------------------------------------------
		void DrawEdge(
			Engine::Graphics::GraphicsEngine* a_pGE,
			const QuadStyle& a_quad,
			const Resolved& a_resolved,
			const Math::Vector2& a_localTopLeft,
			const Math::Color& a_edgeColor)
		{
			using Core::HasFlag;

			const float _thickness = a_quad.edgePixel * a_resolved.uniformScale;
			if (_thickness <= 0.0f) return;
			if (a_quad.edgeSide == EDirection::NONE) return;

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

			if (HasFlag(a_quad.edgeSide, EDirection::UP))    _submit({ 0.0f, 0.0f }, { _size.x, _thickY });
			if (HasFlag(a_quad.edgeSide, EDirection::DOWN))  _submit({ 0.0f, _size.y - _thickY }, { _size.x, _thickY });
			if (HasFlag(a_quad.edgeSide, EDirection::LEFT))  _submit({ 0.0f, 0.0f }, { _thickX, _size.y });
			if (HasFlag(a_quad.edgeSide, EDirection::RIGHT)) _submit({ _size.x - _thickX, 0.0f }, { _thickX, _size.y });
		}

		//----------------------------------------------------------------------------------
		// 文字列を行へ切る
		//----------------------------------------------------------------------------------
		std::vector<std::vector<uint32_t>> SplitLines(const std::string& a_utf8Text)
		{
			std::vector<std::vector<uint32_t>> _lines = { {} };

			for (const uint32_t _codePoint : Core::String::ToCodePoints(a_utf8Text))
			{
				if (_codePoint == '\r') continue;		// 復帰は送りを持たない
				if (_codePoint == '\n')
				{
					_lines.emplace_back();
					continue;
				}
				_lines.back().push_back(_codePoint);
			}

			return _lines;
		}

		// 1行ぶんの幅(px, フォント基準サイズ)を測る
		float MeasureLine(
			Engine::Resource::Font* a_pFont,
			const std::vector<uint32_t>& a_line,
			float a_charSpacingInFontUnit)
		{
			float _width = 0.0f;
			uint32_t _prev = 0;

			for (const uint32_t _codePoint : a_line)
			{
				const Engine::Resource::Glyph* _pGlyph = a_pFont->RequestGlyph(_codePoint);
				if (_pGlyph == nullptr) continue;

				if (_prev != 0) _width += a_pFont->GetKerning(_prev, _codePoint);
				_width += _pGlyph->xAdvance + a_charSpacingInFontUnit;

				_prev = _codePoint;
			}

			return _width;
		}

		//----------------------------------------------------------------------------------
		// 文字を描く
		//----------------------------------------------------------------------------------
		void DrawText(
			Engine::Graphics::GraphicsEngine* a_pGE,
			Engine::Resource::ResourceManager* a_pResourceManager,
			const Decoration& a_decoration,
			const TextData& a_text,
			const Resolved& a_resolved)
		{
			if (a_text.text.empty()) return;
			if (!a_text.fontRef.IsValid()) return;
			if (!a_pResourceManager->IsReady(a_text.fontRef)) return;

			// グリフは要求された時点で焼くので、参照は書き込み可能で引く
			Engine::Resource::Font* _pFont = a_pResourceManager->Ref(a_text.fontRef.GetRaw());
			if (_pFont == nullptr || !_pFont->IsValid()) return;

			const float _atlasSize = static_cast<float>(_pFont->GetAtlasSize());
			if (_atlasSize <= 0.0f) return;

			// 基準サイズ(64px)で焼いたものを、出したい大きさへ縮小する
			const float _fontScale =
				_pFont->GetScaleForSize(a_text.fontPixelSize) * a_resolved.uniformScale;
			if (_fontScale <= 0.0f) return;

			// 字間はピクセル指定なので、測るときはフォント基準サイズへ戻して足す
			const float _charSpacingInFontUnit =
				(_fontScale > 1e-6f) ? (a_text.charSpacing * a_resolved.uniformScale / _fontScale) : 0.0f;

			const auto _lines = SplitLines(a_text.text);

			const float _lineHeight = _pFont->GetLineHeight() * a_text.lineSpacing;
			const float _ascent = _pFont->GetAscent();

			// ブロック全体の大きさ : ピボットを当てる基準になる
			float _blockWidth = 0.0f;
			std::vector<float> _lineWidthVec;
			_lineWidthVec.reserve(_lines.size());
			for (const auto& _line : _lines)
			{
				const float _lineWidth = MeasureLine(_pFont, _line, _charSpacingInFontUnit);
				_lineWidthVec.push_back(_lineWidth);
				_blockWidth = std::max(_blockWidth, _lineWidth);
			}
			const float _blockHeight = _lineHeight * static_cast<float>(_lines.size());

			// ピボットはブロック全体に対して効かせる
			const Math::Vector2 _blockTopLeft = {
				-a_decoration.pivot.x * _blockWidth * _fontScale,
				-a_decoration.pivot.y * _blockHeight * _fontScale
			};

			const Engine::Handle<Engine::Resource::Texture> _atlasHandle = _pFont->GetAtlasTextureHandle();

			for (size_t _lineIndex = 0; _lineIndex < _lines.size(); ++_lineIndex)
			{
				const auto& _line = _lines[_lineIndex];

				// 行揃え : ブロック幅に対して行を寄せる
				float _lineStart = 0.0f;
				switch (a_text.textAlign)
				{
				case ETextAlign::Center: _lineStart = (_blockWidth - _lineWidthVec[_lineIndex]) * 0.5f; break;
				case ETextAlign::Right:  _lineStart = (_blockWidth - _lineWidthVec[_lineIndex]);        break;
				case ETextAlign::Left:
				default: break;
				}

				// ベースラインは行の上端から ascent ぶん下
				const float _baselineY = _lineHeight * static_cast<float>(_lineIndex) + _ascent;

				float _penX = _lineStart;
				uint32_t _prev = 0;

				for (const uint32_t _codePoint : _line)
				{
					const Engine::Resource::Glyph* _pGlyph = _pFont->RequestGlyph(_codePoint);
					if (_pGlyph == nullptr) continue;

					if (_prev != 0) _penX += _pFont->GetKerning(_prev, _codePoint);

					if (!_pGlyph->IsEmpty())
					{
						// フォント基準サイズでの位置を出してから、まとめて縮小する
						const Math::Vector2 _localTopLeft = {
							_blockTopLeft.x + (_penX + _pGlyph->xOffset) * _fontScale,
							_blockTopLeft.y + (_baselineY + _pGlyph->yOffset) * _fontScale
						};
						const Math::Vector2 _glyphSize = {
							static_cast<float>(_pGlyph->width) * _fontScale,
							static_cast<float>(_pGlyph->height) * _fontScale
						};

						const Math::Vector2 _uvOffset = {
							static_cast<float>(_pGlyph->x) / _atlasSize,
							static_cast<float>(_pGlyph->y) / _atlasSize
						};
						const Math::Vector2 _uvScale = {
							static_cast<float>(_pGlyph->width) / _atlasSize,
							static_cast<float>(_pGlyph->height) / _atlasSize
						};

						SubmitLocalRect(
							a_pGE, _atlasHandle, a_resolved,
							_localTopLeft, _glyphSize,
							a_resolved.color, _uvOffset, _uvScale);
					}

					_penX += _pGlyph->xAdvance + _charSpacingInFontUnit;
					_prev = _codePoint;
				}
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

		const Resolved _resolved = Resolve(a_decoration, a_parentTr, a_parentOp, a_override);

		// 文字はグリフごとに矩形を組むので別経路
		if (const TextData* _pText = a_decoration.GetText())
		{
			DrawText(a_pGraphicsEngine, a_pResourceManager, a_decoration, *_pText, _resolved);
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
				_texHandle = GetWhiteTexture(*a_pResourceManager);
			}

			if (_texHandle.IsValid())
			{
				SubmitLocalRect(
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
		DrawEdge(a_pGraphicsEngine, *_pQuad, _resolved, _localTopLeft, _edgeColor);
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
