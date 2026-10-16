#pragma once

namespace App::InstanceResource
{
	//==========================================================================================
	//
	// 行ける範囲(軸に沿った箱)。プレイヤーはこの外へ出られない。
	//
	// 書くのは Boundary オブジェクト(置いてある範囲を毎フレーム書き写す)。
	// 読んで押し戻すのは BoundaryClampSystem。
	// 当たり判定(物理のボディ)を置かずに、範囲の比較だけで押し戻す。
	//
	// ・Boundary が居ないシーンでは isActive が false のままで、何も押し戻さない。
	// ・Boundary は1シーンに1つの想定。複数置いたときは、後から書いたものが勝つ。
	//
	//==========================================================================================
	struct BoundaryResource
	{
		Math::Vector3 center = {};		// 範囲の中心(ワールド)
		Math::Vector3 halfSize = {};	// 範囲の半分の大きさ

		// 壁から内側へ空ける距離(機体の半径など)。プレイヤーの位置(足元)はこの分だけ内側に留まる
		float margin = 0.0f;

		// 範囲が置かれているか。false の間は押し戻さない
		bool isActive = false;

		void Clear()
		{
			isActive = false;
		}

		// 点を範囲の内側へ挟む。
		// 余白が範囲の半分より大きい軸は、中心へ寄せる(内側が無いので)
		Math::Vector3 ClampInside(const Math::Vector3& a_pos) const
		{
			auto _clampAxis = [this](float a_value, float a_center, float a_half)
				{
					const float _limit = std::max(a_half - margin, 0.0f);
					return std::clamp(a_value, a_center - _limit, a_center + _limit);
				};

			return {
				_clampAxis(a_pos.x, center.x, halfSize.x),
				_clampAxis(a_pos.y, center.y, halfSize.y),
				_clampAxis(a_pos.z, center.z, halfSize.z),
			};
		}
	};
}
