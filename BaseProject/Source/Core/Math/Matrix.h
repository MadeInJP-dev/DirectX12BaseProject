#pragma once

//==========================================================================================
// Math::Matrix
//
// ECS のコンポーネントにそのまま置ける POD の 4x4 行列(行優先)。
//
// ・メモリ配置は XMFLOAT4X4 とまったく同じ 16 float。
//   そのため定数バッファへ memcpy する経路や、保存済みの .ob* とも互換がある。
// ・行に軸が入る並び。第1行=Right / 第2行=Up / 第3行=Forward(+Z) / 第4行=平行移動。
//   既存コードが worldMat._31.._33 を前方として読んでいるのと同じ。
// ・掛け算の順序は DirectXMath に合わせる。
//     a * b = 「a を適用してから b を適用する」(Scale * Rotation * Translation の並び)
// ・Vector3 / Quaternion に依存する処理は前方宣言にして .cpp で実装する。
//==========================================================================================
namespace Core::Math
{
	struct Vector3;
	struct Quaternion;

	struct Matrix
	{
		//-----------------------------------------------------------------------------------------------------
		// データ : XMFLOAT4X4 と同じ union 構成
		// _11 などで直接触れるよう無名の構造体にしてあるので、C4201(非標準の拡張)を宣言の間だけ抑止する
#pragma warning(push)
#pragma warning(disable : 4201)
		union
		{
			struct
			{
				float _11, _12, _13, _14;
				float _21, _22, _23, _24;
				float _31, _32, _33, _34;
				float _41, _42, _43, _44;
			};
			float m[4][4];
		};
#pragma warning(pop)

		//-----------------------------------------------------------------------------------------------------
		// コンストラクタ : 既定は単位行列
		constexpr Matrix() noexcept
			: _11(1.0f), _12(0.0f), _13(0.0f), _14(0.0f)
			, _21(0.0f), _22(1.0f), _23(0.0f), _24(0.0f)
			, _31(0.0f), _32(0.0f), _33(1.0f), _34(0.0f)
			, _41(0.0f), _42(0.0f), _43(0.0f), _44(1.0f) {}

		constexpr Matrix(
			float a_11, float a_12, float a_13, float a_14,
			float a_21, float a_22, float a_23, float a_24,
			float a_31, float a_32, float a_33, float a_34,
			float a_41, float a_42, float a_43, float a_44) noexcept
			: _11(a_11), _12(a_12), _13(a_13), _14(a_14)
			, _21(a_21), _22(a_22), _23(a_23), _24(a_24)
			, _31(a_31), _32(a_32), _33(a_33), _34(a_34)
			, _41(a_41), _42(a_42), _43(a_43), _44(a_44) {}

		//-----------------------------------------------------------------------------------------------------
		// DirectX 相互変換
		constexpr Matrix(const DirectX::XMFLOAT4X4& a_value) noexcept
			: _11(a_value._11), _12(a_value._12), _13(a_value._13), _14(a_value._14)
			, _21(a_value._21), _22(a_value._22), _23(a_value._23), _24(a_value._24)
			, _31(a_value._31), _32(a_value._32), _33(a_value._33), _34(a_value._34)
			, _41(a_value._41), _42(a_value._42), _43(a_value._43), _44(a_value._44) {}

		operator DXSM::Matrix() const noexcept
		{
			return DXSM::Matrix(
				_11, _12, _13, _14,
				_21, _22, _23, _24,
				_31, _32, _33, _34,
				_41, _42, _43, _44);
		}

		//-----------------------------------------------------------------------------------------------------
		// Operators
		Matrix operator*(const Matrix& a_other) const noexcept;
		Matrix& operator*=(const Matrix& a_other) noexcept;

		// 成分がすべて同じか(誤差は見ない)
		bool operator==(const Matrix& a_other) const noexcept
		{
			for (int _row = 0; _row < 4; ++_row)
			{
				for (int _col = 0; _col < 4; ++_col)
				{
					if (m[_row][_col] != a_other.m[_row][_col]) return false;
				}
			}
			return true;
		}

		//-----------------------------------------------------------------------------------------------------
		// 行の読み書き : 軸の取り出しに使う
		//-----------------------------------------------------------------------------------------------------
		Vector3 Right()       const noexcept;	// 第1行
		Vector3 Up()          const noexcept;	// 第2行
		Vector3 Forward()     const noexcept;	// 第3行(左手系 +Z)
		Vector3 Translation() const noexcept;	// 第4行

		void SetTranslation(const Vector3& a_pos) noexcept;

		//-----------------------------------------------------------------------------------------------------
		// 行列演算
		//-----------------------------------------------------------------------------------------------------
		// ※ どちらも自分は書き換えず、結果を返す。捨てたらコンパイルエラーにする
		[[nodiscard]] Matrix Invert()    const noexcept;

		/// <summary>
		/// 逆行列を作り、行列式も受け取る
		/// </summary>
		/// <remarks>
		/// 逆行列が作れたかを呼ぶ側で判定したいとき用。
		/// スケール0やNaNが混ざった行列は行列式が 0 / NaN になるので、
		/// 使う前に std::isfinite と 0 の両方を見ること(NaN は == 0.0f を素通りする)
		/// </remarks>
		[[nodiscard]] Matrix Invert(float& a_outDeterminant) const noexcept;
		[[nodiscard]] Matrix Transpose() const noexcept;

		/// <summary>スケール / 回転 / 平行移動へ分解する</summary>
		/// <returns>分解できたら true(スケールが 0 の軸があると false)</returns>
		bool Decompose(Vector3& a_outScale, Quaternion& a_outRotation, Vector3& a_outTranslation) const noexcept;

		//-----------------------------------------------------------------------------------------------------
		// 生成
		//-----------------------------------------------------------------------------------------------------
		static constexpr Matrix Identity() noexcept { return Matrix(); }

		static Matrix CreateTranslation(const Vector3& a_pos) noexcept;
		static Matrix CreateTranslation(float a_x, float a_y, float a_z) noexcept;
		static Matrix CreateScale(const Vector3& a_scale) noexcept;
		static Matrix CreateScale(float a_scale) noexcept;
		static Matrix CreateScale(float a_x, float a_y, float a_z) noexcept;
		static Matrix CreateFromQuaternion(const Quaternion& a_rotation) noexcept;
		static Matrix CreateFromYawPitchRoll(float a_yaw, float a_pitch, float a_roll) noexcept;

		/// <summary>スケール→回転→平行移動をまとめて作る(この順で合成される)</summary>
		static Matrix CreateTRS(const Vector3& a_pos, const Quaternion& a_rotation, const Vector3& a_scale) noexcept;

		/// <summary>左手系のビュー行列。このエンジンは左手系なので LookAt は必ずこちら</summary>
		/// <summary>
		/// 位置と向きからワールド行列を作る
		/// </summary>
		/// <param name="a_forward">前方。このエンジンの前方(+Z)がそのまま第3行に入る</param>
		/// <remarks>
		/// SimpleMath の CreateWorld は右手系で、渡した forward を反転して Z 軸に入れる。
		/// こちらは渡した forward をそのまま Z 軸にするので、-forward を渡さないこと
		/// </remarks>
		static Matrix CreateWorld(const Vector3& a_pos, const Vector3& a_forward, const Vector3& a_up) noexcept;

		static Matrix CreateLookAt(const Vector3& a_eye, const Vector3& a_target, const Vector3& a_up) noexcept;

		/// <summary>左手系の透視投影行列(画角はラジアン)</summary>
		static Matrix CreatePerspectiveFieldOfView(float a_fovY, float a_aspect, float a_nearZ, float a_farZ) noexcept;

		/// <summary>左手系の正射影行列(範囲を左右上下で指定する)</summary>
		static Matrix CreateOrthographicOffCenter(float a_left, float a_right, float a_bottom, float a_top, float a_nearZ, float a_farZ) noexcept;
	};

}

//==========================================================================================
// 保存データとの互換チェック
//------------------------------------------------------------------------------------------
// Archive のバイナリは構造体をそのまま memcpy している。
// XMFLOAT系と1バイトでもズレると、既存の .ob* が静かに壊れる(例外も出ない)ので、
// 並びが変わったらここでコンパイルエラーにする。
//==========================================================================================
static_assert(sizeof(Core::Math::Matrix) == sizeof(DirectX::XMFLOAT4X4), "Math::Matrix のサイズが XMFLOAT4X4 と違う");
static_assert(offsetof(Core::Math::Matrix, _44) == offsetof(DirectX::XMFLOAT4X4, _44), "Math::Matrix の並びが XMFLOAT4X4 と違う");
