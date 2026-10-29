#include "UIAnchor.h"

#include "Engine/EditorField/EditorField.h"

namespace App::Object
{
	Decoration::ParentTransform UIAnchor::MakeParentTransform() const
	{
		Decoration::ParentTransform _parent = {};
		_parent.pixelPos = pixelPos;
		_parent.rotation = rotation;
		_parent.scale = scale;
		_parent.layer = layer;
		_parent.color = color;

		return _parent;
	}

	Decoration::ParentOption UIAnchor::MakeParentOption() const
	{
		Decoration::ParentOption _parent = {};

		// 弧はUIにつき1本。飾りごとの大きさではなくアンカーの矩形を基準に張るので、
		// 幅の違う枠と中身(ゲージの残量など)が同じ弧に乗る
		_parent.parentSize = pixelSize;
		_parent.parentPivot = pivot;

		_parent.curveCenter = curveCenter;
		_parent.curveRadius = curveRadius;
		_parent.curveAngle = curveAngle;

		_parent.isScanLine = isScanLine;

		return _parent;
	}

	Math::Vector2 UIAnchor::ToScreenPos(const Math::Vector2& a_localOffset) const
	{
		return pixelPos + RotateDeg(a_localOffset * scale, rotation);
	}

	Math::Vector2 UIAnchor::RotateDeg(const Math::Vector2& a_value, float a_degree)
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

	//======================================================================================
	// 矩形の内側か
	//--------------------------------------------------------------------------------------
	// GraphicsEngine::PushUIData がクアッドを組み立てるのと同じ式で軸を作り、
	// その軸へ射影した長さで判定する。回転もピボットもそのまま効く。
	// (判定用に別の値を持たせると「絵はここなのに押せない」ズレが必ず出るため、
	//  描画に渡すものと同じ値をそのまま受け取る形にしてある)
	//
	//   ローカル+X(画面右) を回したもの : ( cos,  sin)
	//   ローカル+Y(画面上) を回したもの : ( sin, -cos)
	//   クアッド中心 : ピボット位置 + R * ピボットからのずれ
	//======================================================================================
	bool UIAnchor::IsPointInside(
		const Math::Vector2& a_uiPos,
		const Math::Vector2& a_pixelPos,
		const Math::Vector2& a_pixelSize,
		const Math::Vector2& a_pivot,
		float a_rotationDeg,
		const Math::Vector2& a_hitPadding)
	{
		// 判定の半サイズ(余白ぶんを足す)
		const Math::Vector2 _half = {
			a_pixelSize.x * 0.5f + a_hitPadding.x,
			a_pixelSize.y * 0.5f + a_hitPadding.y
		};

		// 大きさが無ければ触りようがない
		if (_half.x <= 0.0f || _half.y <= 0.0f) return false;

		const float _rad = DirectX::XMConvertToRadians(a_rotationDeg);
		const float _cos = std::cos(_rad);
		const float _sin = std::sin(_rad);

		// ピボットからクアッド中心までのずれ(回転前)
		const Math::Vector2 _pivotOff = {
			(0.5f - a_pivot.x) * a_pixelSize.x,
			(0.5f - a_pivot.y) * a_pixelSize.y
		};

		// 回転はピボットを中心に行われる
		const Math::Vector2 _center = {
			a_pixelPos.x + (_pivotOff.x * _cos - _pivotOff.y * _sin),
			a_pixelPos.y + (_pivotOff.x * _sin + _pivotOff.y * _cos)
		};

		const Math::Vector2 _diff = { a_uiPos.x - _center.x, a_uiPos.y - _center.y };

		// 各軸へ射影した長さ(軸はどちらも単位ベクトル)
		const float _u = _diff.x * _cos + _diff.y * _sin;
		const float _v = _diff.x * _sin - _diff.y * _cos;

		return (std::fabs(_u) <= _half.x) && (std::fabs(_v) <= _half.y);
	}


	//======================================================================================
	// インスペクター
	//======================================================================================
	void UIAnchor::DrawInspector(float a_screenW, float a_screenH)
	{
		// 色 : 全ての飾りへ乗算で掛かる。畳まずに常に出しておく
		// (白い板ポリを1つ置いて、色だけで作り分けられるようにするため)
		Engine::EditorField::Field("Color", color);

		Engine::EditorField::Line();

		// 座標系
		Engine::EditorField::Field("PixelPos", pixelPos, 1.0f);						// スクリーン座標

		Engine::EditorField::Field("Rotation", rotation, 0.1f, -360.0f, 360.0f);
		if (rotation >= 360) rotation -= 360;
		if (rotation <= -360) rotation += 360;

		if (Engine::EditorField::Field("Scale", scale, 0.01f, 0.0f))						// 等倍拡縮
		{
			pixelSize = editSize * scale;
		}
		if (Engine::EditorField::Field("PixelSize", pixelSize, 1.0f, 0.0f, 8192.0f))	// ピクセルサイズ
		{
			editSize = pixelSize / scale;
		}
		Engine::EditorField::Tooltip("アンカー自身の矩形(当たり判定・判定円の基準)。見た目は飾り側のサイズ");

		// 湾曲オプション
		// 上の PixelSize の幅を、開き角ぶんの円弧へ巻き付ける(弧の長さは幅のまま)。
		// 幅0だと曲がらない
		Engine::EditorField::Field("CurveAngle", curveAngle, 0.01f, -3.0f, 3.0f);
		Engine::EditorField::Tooltip("開き角(ラジアン)。PixelSize の幅がこの角度の円弧になる\n0で曲げない / 正で山なり・負で谷");
		Engine::EditorField::Field("CurveRadius", curveRadius, 0.01f, 0.0f, 4.0f);
		Engine::EditorField::Tooltip("巻き付ける角度の倍率。1で開き角そのまま(0も1として扱う)");
		Engine::EditorField::Field("CurveCenter", curveCenter, 0.01f);
		Engine::EditorField::Tooltip("弧の頂点。PixelSizeを-1..1とした座標(x=横位置 / y=上下のずらし)");

		// 円の半径と、端がどれだけ下がるかを出しておく : 角度だけだと効き具合が読めない
		//   半径 R = 幅 / 角度、端の反り = R * (1 - cos(角度 / 2))
		if (curveAngle != 0.0f && pixelSize.x > 0.0f)
		{
			const float _depth = (curveRadius > 0.0f) ? curveRadius : 1.0f;
			const float _angle = curveAngle * _depth;
			const float _radius = pixelSize.x / _angle;
			const float _sag = _radius * (1.0f - std::cos(_angle * 0.5f));
			Engine::EditorField::Value("円の半径", "%.1f px", std::abs(_radius));
			Engine::EditorField::Value("端の反り", "%.1f px", _sag);
		}

		// 初期化用ボタン
		if (Engine::EditorField::Button("RefreshTransform"))
		{
			pixelPos = { a_screenW / 2.0f, a_screenH / 2.0f };
			pixelSize = { a_screenW / 4, a_screenH / 4 };
			rotation = 0.0f;
		}

		Engine::EditorField::Line();

		// ピボット : 正規化[0,1]。(0.5,0.5)=中心, (0,0)=左上, (1,1)=右下。
		// この点が PixelPos に配置され、回転の中心にもなる。
		Engine::EditorField::Field("Pivot (0-1)", pivot, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("Layer", layer, 0.1f);
		Engine::EditorField::Tooltip("重なり順。大きいほど手前(同じ値なら置いた順)");

		Engine::EditorField::Line();

		// ポストプロセス
		Engine::EditorField::Field("ScanLine", isScanLine);
		Engine::EditorField::Tooltip("スキャンラインを掛ける。UIPass の ScanLineMask を ScanLinePass の Mask へ繋いだパイプラインで効く");
	}
}
