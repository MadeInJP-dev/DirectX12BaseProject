#pragma once
namespace Core::Debug
{
	struct ProfileResult
	{
		std::string name;
		double ms;
	};

	using ProfileCallback = std::function<void(const ProfileResult&)>;
	inline ProfileCallback g_profileCallback = nullptr;

	inline void SetProfileCallback(ProfileCallback a_cb) { g_profileCallback = a_cb; }

	/// <summary>
	/// RAIIで処理する
	/// スコープに入ってから抜けたときの時間だけを取得するクラス
	/// </summary>
	class TimeProfileScope
	{
	public:

		/// <summary>
		/// 生成されたタイミングで時間を取得
		/// </summary>
		/// <param name="a_name"></param>
		explicit TimeProfileScope(const std::string& a_name)
			: m_name(a_name), 
			m_start(std::chrono::high_resolution_clock::now())
		{}

		/// <summary>
		/// 破棄されたタイミングで計測終了
		/// </summary>
		~TimeProfileScope()
		{
			// 終了時間
			const auto _end = std::chrono::high_resolution_clock::now();

			// 計測結果
			const double _ms = std::chrono::duration<double, std::milli>(_end - m_start).count();

			if (g_profileCallback)
			{
				ProfileResult _res{
					.name = m_name,
					.ms = _ms
				};
				g_profileCallback(_res);
			}

			// 遅いフレームの記録にも渡す(どの段が長かったかを出すため)。
			// 名前はもう使わないので写さずに渡す
			if (IsHitchRecording())
			{
				ReportHitch("Scope", std::move(m_name), _ms);
			}
		}


	private:

		std::string m_name;

		std::chrono::high_resolution_clock::time_point m_start;
	};
}

// リリースビルド時でも計測してほしいフラグ
#define ENABLE_RELEASE_PROFILE

// =========================================================
// 計測呼び出し用マクロ
//
// 変数名に行番号を混ぜているので、同じスコープに複数置いても名前がぶつからない。
// __LINE__ は ## と同じ段で使うと展開されないため、
// CONCAT を挟んで一度展開させてから連結する
// =========================================================
#define ENGINE_PROFILE_SCOPE_CONCAT_IMPL(a_lhs, a_rhs) a_lhs##a_rhs
#define ENGINE_PROFILE_SCOPE_CONCAT(a_lhs, a_rhs) ENGINE_PROFILE_SCOPE_CONCAT_IMPL(a_lhs, a_rhs)

// _DEBUG または 独自の ENABLE_RELEASE_PROFILE が定義されている場合のみ有効化
#if defined(_DEBUG) || defined(DEBUG) || defined(ENABLE_RELEASE_PROFILE)
#define ENGINE_PROFILE_SCOPE(a_name) 	Core::Debug::TimeProfileScope ENGINE_PROFILE_SCOPE_CONCAT(_profileScope_, __LINE__)(a_name)
#else
	// 完全無効化（コストゼロ）
#define ENGINE_PROFILE_SCOPE(a_name)  __noop
#endif