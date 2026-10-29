#pragma once

#include "Application/Object/UI/Decoration/Decoration.h"
#include "Application/Object/UI/Decoration/DecorationParent.h"
#include "Engine/Graphics/Frame/DrawSubmitter/Descs/SpriteSubmitDesc.h"	// 効果(UIEffectDesc)
#include "Engine/Graphics/Frame/DrawSubmitter/Descs/FontSubmitDesc.h"		// 行揃え(ETextAlign)

//==========================================================================================
// デコレーションの実装どうしで共有するもの
//
// 旧 Decoration(Decoration*.cpp)と新しいエレメント(UIDecoration・UI*Element.cpp)専用。外からは読まないこと。
// 合成・矩形の積み方・アニメーションの進め方を1か所に置き、
// 移し替えの間に旧と新で見た目がずれないようにしてある
//==========================================================================================
namespace Engine::Graphics { class GraphicsEngine; }
namespace Engine::Resource { class ResourceManager; }

namespace App::Object::Decoration::Internal
{
	// 度で回す : UIAnchor::IsPointInside と同じ向き(時計回り)
	inline Math::Vector2 RotateDeg(const Math::Vector2& a_value, float a_degree)
	{
		if (a_degree == 0.0f) return a_value;

		const float _rad = DirectX::XMConvertToRadians(a_degree);
		const float _cos = std::cos(_rad);
		const float _sin = std::sin(_rad);

		return {
			a_value.x * _cos - a_value.y * _sin,
			a_value.x * _sin + a_value.y * _cos
		};
	}

	inline float Lerp(float a_start, float a_end, float a_rate)
	{
		return a_start + (a_end - a_start) * a_rate;
	}
	inline Math::Vector2 Lerp(const Math::Vector2& a_start, const Math::Vector2& a_end, float a_rate)
	{
		return { Lerp(a_start.x, a_end.x, a_rate), Lerp(a_start.y, a_end.y, a_rate) };
	}
	inline Math::Color Lerp(const Math::Color& a_start, const Math::Color& a_end, float a_rate)
	{
		return {
			Lerp(a_start.r, a_end.r, a_rate),
			Lerp(a_start.g, a_end.g, a_rate),
			Lerp(a_start.b, a_end.b, a_rate),
			Lerp(a_start.a, a_end.a, a_rate)
		};
	}

	//======================================================================================
	// アニメーション(実装は Decoration.cpp)
	//======================================================================================

	//--------------------------------------------------------------------------------------
	// アニメーションを合成した結果
	//--------------------------------------------------------------------------------------
	struct AnimResult
	{
		Math::Vector2 positionAdd = {};				// 位置へ足す(px, 親の倍率が掛かる前)
		Math::Vector2 scaleMul = { 1.0f, 1.0f };	// 大きさへ掛ける
		float rotationAdd = 0.0f;					// 回転へ足す(度)
		Math::Color colorMul = Engine::Color::WHITE;// 色へ掛ける
		Math::Vector2 uvAdd = {};					// UVオフセットへ足す
	};

	/// <summary>
	/// アニメーションと反応を1つにまとめる
	/// </summary>
	/// <remarks>
	/// 描画と当たり判定(UIDecorationElementBase)の両方がここを通る。
	/// 別々に組み立てると、絵は大きくなっているのに判定は素のまま、というずれが起きる
	/// </remarks>
	AnimResult MakeAnimResult(
		const std::optional<UIAnimation>& a_opTween,
		const std::optional<UIProceduralAnimation>& a_opOscillation,
		const std::optional<UIReaction>& a_opReaction);

	// 旧 Decoration 用
	AnimResult MakeAnimResult(const Decoration& a_decoration);

	/// <summary>
	/// アニメーションと反応を進める : 出していないフレームも進める
	/// </summary>
	/// <param name="a_parentState">親(UI)の今の状態。反応を付けていなければ使わない</param>
	void AdvanceAnimation(
		std::optional<UIAnimation>& a_opTween,
		std::optional<UIProceduralAnimation>& a_opOscillation,
		std::optional<UIReaction>& a_opReaction,
		EUIState a_parentState,
		float a_deltaTime);

	/// <summary>
	/// アニメーションと反応の保存(実装は DecorationArchive.cpp)
	/// </summary>
	/// <remarks>optional は「持っているか」を先に書いてから中身を書く</remarks>
	void ArchiveAnimations(
		Engine::Persistence::Archive& a_ar,
		std::optional<UIAnimation>& a_opTween,
		std::optional<UIReaction>& a_opReaction,
		std::optional<UIProceduralAnimation>& a_opOscillation);

	//======================================================================================
	// インスペクター(実装は DecorationInspector.cpp)
	//
	// 見出しから中身までを出す。プリセットで触れなくするときは呼び出し側で DisabledScope を張る
	//======================================================================================

	// カーソルへの反応 : 値が変わったら true
	bool DrawReactionInspector(std::optional<UIReaction>& a_opReaction);

	// トゥイーンと揺れ : 値が変わったら true
	bool DrawAnimationInspector(
		std::optional<UIAnimation>& a_opTween,
		std::optional<UIProceduralAnimation>& a_opOscillation);

	//======================================================================================
	// 描画(実装は DecorationDraw.cpp)
	//======================================================================================

	//--------------------------------------------------------------------------------------
	// 合成の元になる飾り1つぶんの値 : 親からの相対
	//--------------------------------------------------------------------------------------
	struct ResolveSource
	{
		Math::Vector2	offsetPos	= {};					// 親のピボット位置からのずれ(px, 親の回転前)
		Math::Vector2	pixelSize	= { 64.0f, 64.0f };		// 大きさ(px)
		float			rotation	= 0.0f;					// 親の回転へ加算(度)
		float			scale		= 1.0f;					// 親の倍率へ乗算
		float			layerOffset = 0.0f;					// 親のZへ加算
		Math::Color		color		= Engine::Color::WHITE;	// 飾り自身の色
		bool			isParentColor = true;				// 親の色を掛けるか

		Math::Vector2	uvOffset	= {};					// UVオフセット(矩形の飾りだけ)
		Math::Vector2	uvScale		= { 1.0f, 1.0f };		// UV倍率(矩形の飾りだけ)
	};

	//--------------------------------------------------------------------------------------
	// 親と合成したあとの状態
	//--------------------------------------------------------------------------------------
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
		float curvature = 0.0f;			// 曲率(1/px, 符号付き)。0で曲げない
		float curveOriginX = 0.0f;		// 弧の頂点の横位置(親のアンカーからのpx)
		float curveShiftY = 0.0f;		// 曲げたときに全体を上下へずらす量(px)

		// 親の回転を掛ける前の、この飾りのずれ(px)。
		// 弧の頂点からのずれを測るのに使う(回した後の座標では測れない)
		Math::Vector2 localOffset = {};

		// 切り抜き(親の持ち物を指すだけ)。nullptr なら切らない
		const Engine::Graphics::UIClipRect* pClipRect = nullptr;

		// 掛けるポストプロセスの印(Engine::Graphics::UI_POST_EFFECT_*)
		UINT postEffectFlags = 0;

		// 掛ける効果(UIマテリアルと、その場の進み具合)
		Engine::Graphics::UIEffectDesc effect = {};
	};

	// 飾り1つを親と合成する
	Resolved Resolve(
		const ResolveSource& a_source,
		const AnimResult& a_anim,
		const ParentTransform& a_parentTr,
		const ParentOption& a_parentOp,
		const DrawOverride& a_override);

	// 板ポリ用の白テクスチャ
	Engine::Handle<Engine::Resource::Texture> GetWhiteTexture(Engine::Resource::ResourceManager& a_resourceManager);

	/// <summary>
	/// 描画の最小単位 : アンカーからのずれ(回転前)で矩形を指定して積む
	/// </summary>
	void SubmitLocalRect(
		Engine::Graphics::GraphicsEngine* a_pGE,
		const Engine::Handle<Engine::Resource::Texture>& a_texHandle,
		const Resolved& a_resolved,
		const Math::Vector2& a_localTopLeft,
		const Math::Vector2& a_size,
		const Math::Color& a_color,
		const Math::Vector2& a_uvOffset,
		const Math::Vector2& a_uvScale);

	/// <summary>
	/// 枠を描く : 矩形の内側に貼り付ける
	/// </summary>
	/// <param name="a_edgePixel">枠の太さ(px, 倍率を掛ける前)</param>
	void DrawEdge(
		Engine::Graphics::GraphicsEngine* a_pGE,
		float a_edgePixel,
		EDirection a_edgeSide,
		const Resolved& a_resolved,
		const Math::Vector2& a_localTopLeft,
		const Math::Color& a_edgeColor);

	//--------------------------------------------------------------------------------------
	// 文字の組み方 : 倍率を掛ける前の値
	//--------------------------------------------------------------------------------------
	struct TextLayout
	{
		Engine::Handle<Engine::Resource::Font> fontHandle = {};
		std::string_view text = {};
		float fontPixelSize = 32.0f;				// 出したい文字の高さ(px)
		float lineSpacing = 1.0f;					// 行送りの倍率
		float charSpacing = 0.0f;					// 字間へ足す量(px)
		Engine::Graphics::ETextAlign textAlign = Engine::Graphics::ETextAlign::Left;
	};

	/// <summary>
	/// 文字を描く : グリフの並べ方はエンジン側(DrawSubmitter::SubmitString)が持つ
	/// </summary>
	/// <param name="a_pivot">文字列のブロック全体に対する基準点</param>
	void SubmitText(
		Engine::Graphics::GraphicsEngine* a_pGE,
		const Resolved& a_resolved,
		const Math::Vector2& a_pivot,
		const TextLayout& a_layout);
}
