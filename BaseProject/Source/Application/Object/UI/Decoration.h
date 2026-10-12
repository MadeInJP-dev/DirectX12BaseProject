#pragma once

#include "Application/AppCommon.h"
#include "Engine/Graphics/Frame/DrawList/UIData.h"	// 切り抜き矩形(UIClipRect)

#include <variant>

//==========================================================================================
// UIのデコレーション
//
// UI 1つは「アンカー(位置・回転・倍率・色)」だけを持ち、実際に見えるものは
// このデコレーションを配列で生やして作る。
//
//   UIAnchor   … 画面のどこに、どの向き・どの大きさで置くか
//   Decoration … そこから相対でどんな絵を出すか(枠・画像・文字)
//
// 値はすべて親からの相対。親を動かせば飾りも一緒に動き、回せば一緒に回り、
// 倍率を掛ければオフセットごと伸びる。1枚のUIを小さい絵の重ね合わせで作れるようにするため。
//
// 共通部分(位置・大きさ・色・アニメ・反応)は1つの struct に持ち、
// 種類ごとの中身(板ポリ / 画像 / 文字)だけを variant で分けてある。
// 配列1本で持てるので、描画順が配列順そのままになり、
// インスペクターの追加・削除・並べ替えも Archive も1つのリストで済む。
//
// 実装は役割ごとにファイルを分けてある :
//   Decoration.cpp          … 種類の切り替え・アニメーションと反応の進行
//   DecorationDraw.cpp      … 描画・リソースの引き直し
//   DecorationArchive.cpp   … 保存
//   DecorationInspector.cpp … インスペクター(エディター用)
//==========================================================================================
// 描画命令の積み先。ヘッダーでは前方宣言だけにして、実装側で GraphicsEngine.h を読む
namespace Engine::Graphics { class GraphicsEngine; }
namespace Engine::Resource { class ResourceManager; }
namespace Engine::ECS { struct EngineServices; }

namespace App::Object::Decoration
{
	//======================================================================================
	// イージング
	//======================================================================================
	enum class EEase : uint32_t
	{
		Linear,			// 等速
		InQuad,			// ゆっくり始まる
		OutQuad,		// ゆっくり終わる
		InOutQuad,		// 両端がゆっくり
		InCubic,
		OutCubic,
		InOutCubic,
		OutBack,		// 行き過ぎて戻る
		OutElastic,		// 弾んで止まる
	};

	/// 0〜1 の進み具合にイージングを掛ける
	float Ease(EEase a_ease, float a_rate);

	//======================================================================================
	// アニメーションで動かすチャンネル
	//======================================================================================
	enum class EAnimChannel : uint32_t
	{
		NONE		= 0,
		COLOR		= 1 << 0,	// 色(乗算)
		POSITION	= 1 << 1,	// 位置(px, 加算)
		SCALE		= 1 << 2,	// 大きさ(倍率, 乗算)
		ROTATION	= 1 << 3,	// 回転(度, 加算)
		UV			= 1 << 4,	// UVオフセット(加算)

		ALL			= COLOR | POSITION | SCALE | ROTATION | UV,
	};
	ENUM_ATTR_BITFLAG(EAnimChannel);

	//--------------------------------------------------------------------------------------
	// イージングアニメーション
	//--------------------------------------------------------------------------------------
	template<typename T>
	struct AnimElement
	{
		T start = {};
		T end = {};
	};

	//--------------------------------------------------------------------------------------
	// 周期運動
	//--------------------------------------------------------------------------------------
	template<typename T>
	struct Oscillation
	{
		T amplitude = {};
		float frequency = 1.0f;		// 1秒あたりの往復回数
		float phase = 0.0f;			// 位相のずれ(0〜1で1周)

		// 上下限で挟む
		bool isUseLimit = false;
		T minValue = {};			// 下限(波の底で届く値)
		T maxValue = {};			// 上限(波の頂点で届く値)
	};

	//--------------------------------------------------------------------------------------
	// 始点から終点へ1回だけ動かす(トゥイーン)
	//--------------------------------------------------------------------------------------
	struct UIAnimation
	{
		float currentTime = 0.0f;		// 経過時間
		float durationTime = 1.0f;		// 変化するのにかかる時間
		bool isLoop = false;			// ループ設定
		bool isPingPong = false;		// 往復するか(往きで end、復りで start へ戻る)

		EEase ease = EEase::Linear;					// 進み具合の曲線
		EAnimChannel channels = EAnimChannel::NONE;	// 動かすチャンネル

		// TweenAnimデータ : channels で選んだものだけが効く
		AnimElement<Math::Color> color = {};		// 元の色へ乗算
		AnimElement<Math::Vector2> position = {};	// 位置へ加算(px)
		AnimElement<Math::Vector2> scale = {};		// 大きさへ乗算(1で等倍)
		AnimElement<float> rotation = {};			// 回転へ加算(度)
		AnimElement<Math::Vector2> uv = {};			// UVオフセットへ加算
	};

	//--------------------------------------------------------------------------------------
	// ずっと揺らし続ける(周期運動)
	//--------------------------------------------------------------------------------------
	struct UIProceduralAnimation
	{
		float currentTime = 0.0f;					// 経過時間
		EAnimChannel channels = EAnimChannel::NONE;	// 動かすチャンネル

		Oscillation<Math::Vector2> position = {};	// 位置へ加算(px)
		Oscillation<Math::Vector2> scale = {};		// 大きさへ乗算(1が等倍)
		Oscillation<float> rotation = {};			// 回転へ加算(度)
		Oscillation<Math::Color> color = {};		// 色へ乗算(1が元の色)
	};

	//======================================================================================
	// 方向 : 枠をどの辺に出すか
	//======================================================================================
	enum class EDirection : uint32_t
	{
		NONE	= 0,
		UP		= 1 << 0,
		DOWN	= 1 << 1,
		LEFT	= 1 << 2,
		RIGHT	= 1 << 3,

		ALL		= UP | DOWN | LEFT | RIGHT,
	};
	ENUM_ATTR_BITFLAG(EDirection);

	// 文字列の行揃え : ブロック全体の位置は pivot が決めるので、ここは行同士の揃え方
	enum class ETextAlign : uint32_t
	{
		Left,
		Center,
		Right,
	};

	//======================================================================================
	// UIの状態 : 飾りが親から受け取る
	//======================================================================================
	enum class EUIState : uint32_t
	{
		Normal = 0,		// 何もされていない
		Hovered,		// カーソルが乗っている
		Pressed,		// 押されている最中
		Disabled,		// 押せない
	};

	// 出す状態の絞り込み用(ビットフラグなので値は2の冪)
	enum class EUIStateFlag : uint32_t
	{
		NONE		= 0,
		NORMAL		= 1 << 0,
		HOVERED		= 1 << 1,
		PRESSED		= 1 << 2,
		DISABLED	= 1 << 3,

		ALL			= NORMAL | HOVERED | PRESSED | DISABLED,
	};
	ENUM_ATTR_BITFLAG(EUIStateFlag);

	/// 状態を絞り込み用のビットへ
	constexpr EUIStateFlag ToStateFlag(EUIState a_state)
	{
		switch (a_state)
		{
		case EUIState::Hovered:  return EUIStateFlag::HOVERED;
		case EUIState::Pressed:  return EUIStateFlag::PRESSED;
		case EUIState::Disabled: return EUIStateFlag::DISABLED;

		case EUIState::Normal:
		default:                 return EUIStateFlag::NORMAL;
		}
	}

	//--------------------------------------------------------------------------------------
	// 状態1つぶんの見た目 : Normal からの差
	//--------------------------------------------------------------------------------------
	struct UIStateStyle
	{
		Math::Color color = Engine::Color::WHITE;	// 元の色へ乗算(白で変化なし)
		Math::Vector2 scale = { 1.0f, 1.0f };		// 大きさへ乗算(1で等倍)
		Math::Vector2 offsetAdd = {};				// 位置へ加算(px)
	};

	//--------------------------------------------------------------------------------------
	// 親の状態に対する反応
	// アニメーションと同じく 付いている飾りだけがカーソルに反応する。
	//--------------------------------------------------------------------------------------
	struct UIReaction
	{
		// 出す状態 : ここに入っていない状態では描かない
		EUIStateFlag visibleState = EUIStateFlag::ALL;

		// 状態ごとの見た目(Normal は素のまま)
		UIStateStyle hovered = {};	// 重なっている
		UIStateStyle pressed = {};	// 押された
		UIStateStyle disabled = {};	// 押されることがない

		float blendSpeed = 14.0f;

		//---- ランタイム(保存しない) ----
		UIStateStyle current = {};	// いま適用している値。ここを目標へ寄せていく
		float visibleRate = 1.0f;	// 出ている割合(0で完全に透明)
		bool isInitialized = false;	// 初回は補間せずに合わせる(出た瞬間に寄り始めない)
	};

	//======================================================================================
	// デコレーションの種類
	//
	// ※ 値は保存されるうえ、DecorationBody(下)の並びと一致させてある。増やすときは両方の末尾へ
	//======================================================================================
	enum class EDecorationType : uint32_t
	{
		Polygon,	// 板ポリ : 組み込みの白テクスチャを色で染めて出す。テクスチャを用意しなくてよい
		Image,		// 画像 : 指定したテクスチャを出す
		Text,		// 文字 : フォントから文字列を組んで出す
	};

	//======================================================================================
	// 種類ごとの中身
	//
	// 以前は全種類のフィールドを1つの struct に並べていたので、
	// 文字の飾りもテクスチャを、画像の飾りもフォントを持っていた。
	// どの値が効くのかが型から読めないので、種類ごとに分けて variant で持つ
	//======================================================================================

	//--------------------------------------------------------------------------------------
	// 矩形の見た目 : 板ポリと画像で共通(UV と枠)
	//--------------------------------------------------------------------------------------
	struct QuadStyle
	{
		Math::Vector2	uvOffset	= {};						// UVスクロール・コマ送り
		Math::Vector2	uvScale		= { 1.0f, 1.0f };			// 1枚に並べた絵から1コマ切り出すときの倍率

		// 枠
		bool		isFill		= true;					// 中を塗るか(false で枠だけ)
		Math::Color edgeColor	= Engine::Color::WHITE;	// 枠の色(親の色へ乗算)
		float		edgePixel	= 0.0f;					// 枠の太さ(px)。0 で枠なし
		EDirection	edgeSide	= EDirection::ALL;		// どの辺に出すか
	};

	// 板ポリ
	struct PolygonData
	{
		QuadStyle quad = {};
	};

	// 画像
	struct ImageData
	{
		QuadStyle quad = {};

		Core::GUID texGUID = {};										// 保存用
		Engine::ResourceRef<Engine::Resource::Texture> texRef = {};		// ランタイム用
	};

	// 文字
	struct TextData
	{
		std::string	text = "Text";
		Core::GUID fontGUID = {};
		Engine::ResourceRef<Engine::Resource::Font> fontRef = {};

		float		fontPixelSize	= 32.0f;				// 出したい文字の高さ(px)
		float		lineSpacing		= 1.0f;					// 行送りの倍率(1.0 でフォントの既定)
		float		charSpacing		= 0.0f;					// 字間へ足す量(px)
		ETextAlign	textAlign		= ETextAlign::Center;	// 出現位置
	};

	// 並びは EDecorationType と同じ(index がそのまま種類になる)
	using DecorationBody = std::variant<PolygonData, ImageData, TextData>;

	//======================================================================================
	// デコレーション : UIに付属する飾り
	//======================================================================================
	struct Decoration
	{
		// 持ち主の UI の中で一意な番号(0 は未採番)。
		// 名前は書き換えられるので、他から飾りを指すときはこちらを使う(UIGauge の中身など)。
		// 振るのは UIBase(AddDecoration と読み込みの後)
		uint32_t id = 0;

		std::string name		= "Decoration";	// エディターの見出し用
		bool		isVisible	= true;			// 出すか
		uint32_t	group		= 0;			// グループ番号、別の場所へ出す際に使う

		//----------------------------------------------------------------------------------
		// 親(UI)からの相対トランスフォーム
		//----------------------------------------------------------------------------------
		Math::Vector2	offsetPos	= {};						// 親のピボット位置からのずれ(px, 親の回転前)
		Math::Vector2	pixelSize	= { 64.0f, 64.0f };			// 大きさ(px) ※Textは fontPixelSize が優先
		float			rotation	= 0.0f;						// 親の回転へ加算(度)
		float			scale		= 1.0f;						// 親の倍率へ乗算
		Math::Vector2	pivot		= { 0.5f, 0.5f };			// 回転軸/基準点(正規化[0,1], 0.5=中心)
		float			layerOffset = 0.0f;						// 親のZへ加算
		Math::Color		color		= Engine::Color::WHITE;		// 親の色へ乗算

		//----------------------------------------------------------------------------------
		// 種類ごとの中身
		//----------------------------------------------------------------------------------
		DecorationBody body = PolygonData{};

		//----------------------------------------------------------------------------------
		// アニメーション
		//----------------------------------------------------------------------------------
		std::optional<UIAnimation>				opTweenAnim;		// 始点から終点へ動かすなら付与
		std::optional<UIProceduralAnimation>	opOscillationAnim;	// 揺らし続けるなら付与

		//----------------------------------------------------------------------------------
		// カーソルへの反応
		//----------------------------------------------------------------------------------
		std::optional<UIReaction>				opReaction;			// カーソルに反応させるなら付与

		//==================================================================================

		// 種類
		EDecorationType GetType() const { return static_cast<EDecorationType>(body.index()); }

		/// <summary>
		/// 種類を変える
		/// </summary>
		/// <remarks>
		/// 中身は作り直す。板ポリと画像の間では UV と枠(QuadStyle)を引き継ぐ。
		/// 同じ種類を渡したときは何もしない
		/// </remarks>
		void SetType(EDecorationType a_type);

		// 種類ごとの中身 : その種類でなければ nullptr
		const ImageData* GetImage() const { return std::get_if<ImageData>(&body); }
		ImageData* RefImage() { return std::get_if<ImageData>(&body); }

		const TextData* GetText() const { return std::get_if<TextData>(&body); }
		TextData* RefText() { return std::get_if<TextData>(&body); }

		// 矩形の見た目 : 板ポリと画像なら持っている(文字は nullptr)
		const QuadStyle* GetQuad() const;
		QuadStyle* RefQuad();
	};

	//======================================================================================
	// 親(UI)のトランスフォーム : デコレーションを合成する土台
	//======================================================================================
	struct ParentTransform
	{
		Math::Vector2 pixelPos = {};				// ピボットのスクリーン座標(px)
		float rotation = 0.0f;						// 回転(度)
		float scale = 1.0f;							// 倍率
		float layer = 0.0f;							// Z順
		Math::Color color = Engine::Color::WHITE;	// 全デコレーションへ掛かる色
	};

	// 親しか持たない固有の設定、子供全体にかかる
	//
	// 湾曲は「UI1つにつき弧が1本」。枠も中身も文字も同じ弧に乗せたいので、
	// 弧そのものの形は飾りではなく親が持つ
	struct ParentOption
	{
		// 弧が張る幅の基準になる、親自身の矩形(px)。
		// これが弧の -1..1 になる。UIAnchor::pixelSize がそのまま入る
		Math::Vector2 parentSize = {};

		// 弧の頂点(反りの中心)。親の矩形を -1..1 とした座標。
		//   x … 頂点を横のどこに置くか(0で中央)
		//   y … 曲げたときに全体を上下へずらす量
		Math::Vector2 curveCenter = {};

		float curveRadius = 1.0f;			// 反りの深さの倍率(1で素直な円弧。0も1として扱う)
		float curveAngle = 0.0f;			// 開き角(ラジアン)。0で曲げない。正で山なり、負で谷

		// 切り抜き(px)。持っていれば、この矩形の外へ出たぶんは描かない。
		// アンカーの設定ではなく、持ち主の UI が実行中だけ持つもの
		// (スクロールする一覧の項目など。UIBase::SetClipRect)
		std::optional<Engine::Graphics::UIClipRect> opClipRect = std::nullopt;

		// スキャンライン(ScanLinePass)を掛けるか。UIAnchor::isScanLine がそのまま入る
		bool isScanLine = false;
	};

	//======================================================================================
	// 描くときの一時的な上書き
	//
	// 「同じ見た目を敵の数だけ別の場所へ出す」「桁ごとにUVをずらす」といった、
	// 保存しないその場かぎりの差し替えをここへまとめる。
	// デコレーション側の値を書き換えて描くと、次のフレームまで汚れが残ってしまうため
	//======================================================================================
	struct DrawOverride
	{
		// 位置の差し替え : 親のピクセル座標を無視してここへ出す
		bool isUsePos = false;
		Math::Vector2 pixelPos = {};

		float scale = 1.0f;							// さらに掛ける倍率(位置のずれにも掛かる)

		/// <summary>大きさだけへ掛ける倍率(軸ごと)</summary>
		/// <remarks>
		/// ゲージのように「横だけ縮める」ために使う。
		/// scale と違って位置のずれや枠の太さには掛からないので、
		/// 縮めても枠の太さや文字の大きさは変わらない
		/// </remarks>
		Math::Vector2 sizeScale = { 1.0f, 1.0f };

		Math::Color tint = Engine::Color::WHITE;	// さらに掛ける色
		Math::Vector2 uvOffsetAdd = {};				// UVオフセットへ足す

		// UV倍率の差し替え : 立てると飾り側の UVScale を無視してこちらを使う
		//
		// 「1枚に並べた絵から1コマ切り出す」ような、コマの幅を出す側が知っている場合に使う
		// (ScoreHUD が桁数から 1/AtlasCount を出すなど)
		bool isUseUvScale = false;
		Math::Vector2 uvScale = { 1.0f, 1.0f };

		// 群の絞り込み : 立てるとこの群のデコレーションだけ描く
		bool isUseGroup = false;
		uint32_t group = 0;
	};

	//======================================================================================
	// 今の見た目の値(アニメーション・反応を掛けたあと)
	//
	// 当たり判定を絵に合わせるためのもの。描画側も同じものを組み立てて使っている。
	// 色とUVは判定に関係しないので入れていない
	//======================================================================================
	struct DecorationTransform
	{
		Math::Vector2 offsetAdd = {};				// offsetPos へ足す量(px, 親の倍率が掛かる前)
		Math::Vector2 scaleMul = { 1.0f, 1.0f };	// pixelSize へ掛ける倍率
		float rotationAdd = 0.0f;					// rotation へ足す角度(度)
	};

	/// <summary>
	/// いま効いているアニメーション・反応の量を取り出す
	/// </summary>
	/// <remarks>
	/// 進行そのものは AdvanceAnimation が持つので、ここは覗くだけ。
	/// トゥイーン・周期運動・カーソルへの反応の3つを掛け合わせた結果が返る
	/// </remarks>
	DecorationTransform CalcCurrentTransform(const Decoration& a_decoration);

	//======================================================================================
	// 操作
	//======================================================================================

	/// <summary>
	/// アニメーションと反応を進める : 出していないフレームも進める(UIBase::Update から)
	/// </summary>
	/// <param name="a_parentState">親(UI)の今の状態。反応を付けていない飾りでは使わない</param>
	void AdvanceAnimation(Decoration& a_decoration, EUIState a_parentState, float a_deltaTime);

	/// <summary>
	/// デコレーションを1つ描く
	/// </summary>
	/// <param name="a_pGraphicsEngine">描画命令の積み先</param>
	/// <param name="a_pResourceManager">テクスチャ・フォントを引くのに使う</param>
	/// <param name="a_decoration">描くもの</param>
	/// <param name="a_parent">親(UI)のトランスフォーム</param>
	/// <param name="a_override">その場かぎりの差し替え</param>
	void DrawDecoration(
		Engine::Graphics::GraphicsEngine* a_pGraphicsEngine,
		Engine::Resource::ResourceManager* a_pResourceManager,
		const Decoration& a_decoration,
		const ParentTransform& a_parent,
		const ParentOption& a_parentOp,
		const DrawOverride& a_override = {});

	/// <summary>
	/// 保存されているGUIDから、テクスチャとフォントの参照を引き直す
	/// </summary>
	/// <remarks>読み込み直後とエディターでの差し替え時に呼ぶ。実体の到着は待たない</remarks>
	void RequestResources(Decoration& a_decoration, Engine::Resource::ResourceManager* a_pResourceManager);

	/// <summary>
	/// アーカイブ(1つぶん)
	/// </summary>
	/// <remarks>
	/// 配列の1要素として呼ばれる前提。BeginObject / EndObject と区切り(ArchiveSection)は呼び出し側が行う。
	/// フィールドを足すときは必ず末尾へ(区切りの中は並び順で読むため)
	/// </remarks>
	void ArchiveDecoration(Engine::Persistence::Archive& a_ar, Decoration& a_decoration);

	/// <summary>
	/// インスペクター(1つぶん)
	/// </summary>
	/// <param name="a_services">アセットの一覧と、選び直したときの読み込み先</param>
	/// <returns>値が変わったら true</returns>
	bool DrawDecorationInspector(Decoration& a_decoration, const Engine::ECS::EngineServices& a_services);
}
