#pragma once

#include "Application/AppCommon.h"
#include "Application/Object/UI/Decoration/DecorationAnimation.h"
#include "Application/Object/UI/Decoration/DecorationReaction.h"
#include "Application/Object/UI/Decoration/DecorationPreset.h"

#include "../UIDecorationDrawContext.h"

namespace App::Object::Decoration
{
	//======================================================================================
	// 方向 : 枠をどの辺に出すか
	//======================================================================================
	enum class EDirection : uint32_t
	{
		NONE = 0,
		UP = 1 << 0,
		DOWN = 1 << 1,
		LEFT = 1 << 2,
		RIGHT = 1 << 3,

		ALL = UP | DOWN | LEFT | RIGHT,
	};
	ENUM_ATTR_BITFLAG(EDirection);

	//======================================================================================
	// 各エレメントの共通仕様
	//======================================================================================
	class UIDecorationElementBase
	{
	public:

		UIDecorationElementBase() = default;
		virtual ~UIDecorationElementBase() = default;

		// 更新
		virtual void Updaate(const UpdateContext& a_context);

		// 描画
		virtual void Draw(const DrawContext&) = 0;

	protected:
		//----------------------------------------------------------------------------------
		// 識別情報
		//----------------------------------------------------------------------------------
		Engine::ID<UIDecorationElementBase> m_id;		// デコレーションクラスで生成された際に割り当てられる番号
		std::string m_name;								// 自身の名前
		bool m_isVisible = true;						// 表示させるかどうか

		//----------------------------------------------------------------------------------
		// トランスフォーム : 親からの相対
		//----------------------------------------------------------------------------------
		Core::Math::Vector2 m_offsetPos = {};					// 親のピボット位置からのずれ
		Core::Math::Vector2 m_pixelSize = {64.0f,64.0f};		// 大きさ
		float m_rotation = 0.0f;								// 回転
		float m_layerOffset = 0.0f;								// 親内のZ基準 : 親が超えていないものは超えれない。
		Core::Math::Vector2 m_pivotPos = {};					// ピボット位置

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
	};
}