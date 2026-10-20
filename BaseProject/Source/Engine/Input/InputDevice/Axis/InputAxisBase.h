#pragma once

namespace Engine::Input
{
	struct InputContext;

	// 単一の軸入力状態を保持する規定クラス
	// スティックや十字操作などの、二次元入力を保持する
	class InputAxisBase
	{
	public:
		InputAxisBase() = default;
		virtual ~InputAxisBase() = default;

		virtual void PreUpdate() {};
		virtual void Update(InputContext& a_inputContext) = 0;

		// 強制的に入力をなくす
		void NoInput() { m_axis = Math::Vector2::Zero(); }

		/// <summary>
		/// 溜まっている状態を捨てて初期状態へ戻す
		/// </summary>
		/// <remarks>
		/// アプリのモード切り替えで呼ぶ。値を消すだけでよい軸は NoInput のままでよいが、
		/// 前フレームの座標などを覚えている軸(マウス)は、それも捨てないと
		/// 切り替えを跨いだ差分が1フレーム目に一気に出てしまう。
		/// </remarks>
		virtual void ResetInput() { NoInput(); }

		// アクセサ
		void SetValueRate(float a_rate) { m_valueRate = a_rate; }		// レート設定
		void SetLimitValue(float a_limit) { m_limitValue = a_limit; }	// 限界値設定

		/// <summary>
		/// 値を「1秒あたりの量」として扱うか
		/// </summary>
		/// <remarks>
		/// 立てると GetState がこのフレームの経過時間を掛けた量を返す。
		/// スティックやキーのように「倒している間ずっと同じ値」の軸で、
		/// 1フレームに足す量(視点の角度など)を作るとき、FPS で速さが変わらないようにする。
		/// マウスの移動量はもともと1フレームぶんの量なので立てない。
		/// 移動の向きのように、そのまま向きとして使う軸も立てない。
		/// </remarks>
		void SetPerSecond(bool a_isPerSecond) { m_isPerSecond = a_isPerSecond; }

		// このフレームの経過時間(秒)。コレクターが更新の前に渡す
		void SetDeltaTime(float a_deltaTime) { m_deltaTime = a_deltaTime; }

		Math::Vector2 GetState() const;


	protected:
		Math::Vector2 m_axis = {};

		// 軸の数値にかける補正
		float m_valueRate = 1.0f;

		// 軸の限界値
		float m_limitValue = FLT_MAX;

		// 1秒あたりの量として扱うか(GetState で経過時間を掛ける)
		bool m_isPerSecond = false;

		// このフレームの経過時間(秒)
		float m_deltaTime = 0.0f;
	};
}