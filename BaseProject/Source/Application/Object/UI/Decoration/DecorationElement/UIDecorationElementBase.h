#pragma once

#include "Application/AppCommon.h"
#include "Application/Object/UI/Decoration/DecorationAnimation.h"
#include "Application/Object/UI/Decoration/DecorationReaction.h"
#include "Application/Object/UI/Decoration/DecorationPreset.h"
#include "Application/Object/UI/Decoration/DecorationDirection.h"
#include "UIHitBody.h"

#include "../UIDecorationDrawContext.h"

namespace Engine::ECS { struct EngineServices; }

namespace App::Object::Decoration
{
	class UIDecorationElementBase;

	namespace Internal
	{
		struct Resolved;
	}

	// エレメントの番号 : 持ち主の UIDecoration の中で一意。消した番号は使い回さない
	using ElementID = Engine::ID<UIDecorationElementBase>;

	//======================================================================================
	// エレメントの種類
	//
	// ※ 値は保存される。増やすときは末尾へ(UIDecoration::CreateElement にも足す)
	//======================================================================================
	enum class EElementType : uint32_t
	{
		Sprite,		// 矩形 : 画像が無ければ組み込みの白テクスチャを色で染めて出す
		Text,		// 文字 : フォントから文字列を組んで出す
	};

	//======================================================================================
	// 各エレメントの共通仕様
	//
	// 値はすべて親(UIDecoration を持つ UI)からの相対。
	// 親を動かせば一緒に動き、回せば一緒に回り、倍率を掛ければずれごと伸びる。
	// 種類ごとの中身(矩形・文字)は派生クラスが持つ
	//======================================================================================
	class UIDecorationElementBase
	{
	public:

		UIDecorationElementBase() = default;
		virtual ~UIDecorationElementBase() = default;

		//----------------------------------------------------------------------------------
		// 種類
		//----------------------------------------------------------------------------------
		virtual EElementType GetType() const = 0;
		virtual const char* GetTypeName() const = 0;		// 名前の既定値・エディターの見出し用

		// 複製 : 参照(ResourceRef)も写すので、複製した側もそのまま描ける
		virtual std::unique_ptr<UIDecorationElementBase> Clone() const = 0;

		//----------------------------------------------------------------------------------
		// メイン処理
		//----------------------------------------------------------------------------------

		/// <summary>
		/// アニメーションと反応を進める : 出していないフレームも進める
		/// </summary>
		virtual void Update(const UpdateContext& a_context);

		// 描画
		virtual void Draw(const DrawContext& a_context) = 0;

		/// <summary>
		/// 保存されているGUIDから、テクスチャ・フォントの参照を引き直す
		/// </summary>
		/// <remarks>読み込み直後とエディターでの差し替え時に呼ぶ。実体の到着は待たない</remarks>
		virtual void RequestResources(Engine::Resource::ResourceManager* a_pResourceManager) = 0;

		/// <summary>
		/// アーカイブ(1つぶん)
		/// </summary>
		/// <remarks>
		/// 共通部分を書いてから、種類ごとの中身(ArchiveBody)を区切りの中に書く。
		/// 種類は持ち主(UIDecoration)が先に書く(読むときは種類を見て作ってから呼ぶため)
		/// </remarks>
		void Archive(Engine::Persistence::Archive& a_ar);

		/// <summary>
		/// インスペクター(エディター用) : 共通部分・種類ごとの中身・反応・アニメーション
		/// </summary>
		/// <remarks>プリセットのつながりは持ち主(UIDecoration)が出す。付いていく部分はここで触れなくする</remarks>
		/// <returns>値が変わったら true</returns>
		bool DrawInspector(const Engine::ECS::EngineServices& a_services);

		//----------------------------------------------------------------------------------
		// 当たり判定
		//----------------------------------------------------------------------------------

		/// <summary>
		/// ボディに当たっているか : ボディを持たない・出していないエレメントは当たらない
		/// </summary>
		/// <param name="a_pos">調べる位置(画面のピクセル座標)</param>
		/// <param name="a_parentTr">親の画面での置き場所(倍率はデコレーション全体の倍率まで掛けたもの)</param>
		/// <param name="a_parentOp">親の設定(親の矩形に当てるボディが大きさとピボットを読む)</param>
		bool IsHit(const Math::Vector2& a_pos, const ParentTransform& a_parentTr, const ParentOption& a_parentOp) const;

		/// <summary>
		/// ボディの輪郭(画面のピクセル座標)を並べる : エディターの表示用。アニメーションは掛けない
		/// </summary>
		/// <returns>ボディを持っていなければ false</returns>
		bool CalcHitOutline(const ParentTransform& a_parentTr, const ParentOption& a_parentOp, std::vector<Math::Vector2>& a_outPointVec) const;

		//----------------------------------------------------------------------------------
		// アクセサ
		//----------------------------------------------------------------------------------

		// 識別
		ElementID GetId() const { return m_id; }
		const std::string& GetName() const { return m_name; }
		void SetName(const std::string& a_name) { m_name = a_name; }
		bool IsVisible() const { return m_isVisible; }
		void SetVisible(bool a_isVisible) { m_isVisible = a_isVisible; }
		uint32_t GetGroup() const { return m_group; }
		void SetGroup(uint32_t a_group) { m_group = a_group; }

		// トランスフォーム
		const Math::Vector2& GetOffsetPos() const { return m_offsetPos; }
		void SetOffsetPos(const Math::Vector2& a_offsetPos) { m_offsetPos = a_offsetPos; }
		const Math::Vector2& GetPixelSize() const { return m_pixelSize; }
		void SetPixelSize(const Math::Vector2& a_pixelSize) { m_pixelSize = a_pixelSize; }
		float GetRotation() const { return m_rotation; }
		void SetRotation(float a_rotation) { m_rotation = a_rotation; }
		float GetScale() const { return m_scale; }
		void SetScale(float a_scale) { m_scale = a_scale; }
		float GetLayerOffset() const { return m_layerOffset; }
		void SetLayerOffset(float a_layerOffset) { m_layerOffset = a_layerOffset; }
		const Math::Vector2& GetPivot() const { return m_pivotPos; }
		void SetPivot(const Math::Vector2& a_pivot) { m_pivotPos = a_pivot; }

		// 色
		const Math::Color& GetColor() const { return m_color; }
		void SetColor(const Math::Color& a_color) { m_color = a_color; }
		bool IsParentColor() const { return m_isParentColor; }
		void SetParentColor(bool a_isParentColor) { m_isParentColor = a_isParentColor; }

		// アニメーション・反応・プリセット : 付けるときは emplace、外すときは reset
		std::optional<UIAnimation>& RefTweenAnim() { return m_opTweenAnim; }
		const std::optional<UIAnimation>& GetTweenAnim() const { return m_opTweenAnim; }
		std::optional<UIProceduralAnimation>& RefOscillationAnim() { return m_opOscillationAnim; }
		const std::optional<UIProceduralAnimation>& GetOscillationAnim() const { return m_opOscillationAnim; }
		std::optional<UIReaction>& RefReaction() { return m_opReaction; }
		const std::optional<UIReaction>& GetReaction() const { return m_opReaction; }
		PresetLink& RefPreset() { return m_preset; }
		const PresetLink& GetPreset() const { return m_preset; }

		// 当たり判定のボディ : 付けるときは emplace、外すときは reset
		std::optional<UIHitBody>& RefHitBody() { return m_opHitBody; }
		const std::optional<UIHitBody>& GetHitBody() const { return m_opHitBody; }

	protected:

		// 種類ごとの中身の保存。フィールドを足すときは必ず末尾へ
		virtual void ArchiveBody(Engine::Persistence::Archive& a_ar) = 0;

		// 種類ごとの中身のインスペクター : 値が変わったら true
		virtual bool DrawBodyInspector(const Engine::ECS::EngineServices& a_services) = 0;

		// 矩形の大きさ(pixelSize)を使うか : 文字は文字の大きさで決まるので使わない
		virtual bool IsUsePixelSize() const { return true; }

		// この描画で出すか(表示・群の絞り込み)
		bool IsDrawn(const DrawContext& a_context) const;

		/// <summary>
		/// 親と合成する : アニメーション・反応も掛けた、描く直前の状態
		/// </summary>
		/// <param name="a_uvOffset">UVオフセット(矩形のときだけ意味がある)</param>
		/// <param name="a_uvScale">UV倍率(矩形のときだけ意味がある)</param>
		Internal::Resolved Resolve(
			const DrawContext& a_context,
			const Math::Vector2& a_uvOffset = {},
			const Math::Vector2& a_uvScale = { 1.0f, 1.0f }) const;

		// 合成後の大きさに対する、矩形の左上(アンカーからのずれ)
		Math::Vector2 CalcLocalTopLeft(const Math::Vector2& a_size) const;

	protected:
		//----------------------------------------------------------------------------------
		// 識別情報
		//----------------------------------------------------------------------------------
		ElementID m_id = {};							// デコレーションクラスで生成された際に割り当てられる番号
		std::string m_name;								// 自身の名前
		bool m_isVisible = true;						// 表示させるかどうか
		uint32_t m_group = 0;							// 群の番号 : 別の場所へ出す際に絞り込む(DrawOverride::isUseGroup)

		//----------------------------------------------------------------------------------
		// トランスフォーム : 親からの相対
		//----------------------------------------------------------------------------------
		Core::Math::Vector2 m_offsetPos = {};					// 親のピボット位置からのずれ(px, 親の回転前)
		Core::Math::Vector2 m_pixelSize = {64.0f,64.0f};		// 大きさ(px) ※文字は文字の大きさが優先
		float m_rotation = 0.0f;								// 回転 : 親の回転へ加算(度)
		float m_scale = 1.0f;									// 倍率 : 親の倍率へ乗算
		float m_layerOffset = 0.0f;								// 親内のZ基準 : 親が超えていないものは超えれない。
		Core::Math::Vector2 m_pivotPos = { 0.5f, 0.5f };		// ピボット位置(正規化[0,1], 0.5=中心)

		Core::Math::Color m_color = Engine::Color::WHITE;		// 色
		bool m_isParentColor = true;							// 親の色の影響を受けるかどうか

		//----------------------------------------------------------------------------------
		// アニメーション
		//----------------------------------------------------------------------------------
		std::optional<UIAnimation>				m_opTweenAnim = std::nullopt;			// 始点から終点へ動かすアニメーション
		std::optional<UIProceduralAnimation>	m_opOscillationAnim = std::nullopt;		// 揺らし続けるアニメーション

		//----------------------------------------------------------------------------------
		// カーソルへの反応
		//----------------------------------------------------------------------------------
		std::optional<UIReaction>				m_opReaction = std::nullopt;			// カーソルに反応させるなら付与

		//----------------------------------------------------------------------------------
		// プリセット : 指していれば、付いていく部分をプリセットから写す
		//----------------------------------------------------------------------------------
		PresetLink m_preset = {};

		//----------------------------------------------------------------------------------
		// 当たり判定 : 持っていれば、持ち主の UI がカーソルの位置を問い合わせる
		//----------------------------------------------------------------------------------
		std::optional<UIHitBody> m_opHitBody = std::nullopt;

	private:

		// 番号を振るのは持ち主だけ
		friend class UIDecoration;
	};
}
