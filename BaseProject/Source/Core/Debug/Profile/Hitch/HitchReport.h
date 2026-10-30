#pragma once
namespace Core::Debug
{
	//==========================================================================================
	// 遅いフレームの記録(カクつきの原因探し)へ送る出来事
	//
	// 受け手(Engine の HitchMonitor)は、60FPS を割ったフレームのときだけ
	// そのフレームに届いた出来事を書き出す。送る側が受け手を知らずに済むよう、
	// ログ(g_logCallback)・計測(g_profileCallback)と同じく口だけを Core に置く
	//==========================================================================================
	struct HitchEvent
	{
		const char*	category = "";	// 種類(PSO / Load / Physics など)。文字列リテラルを渡す
		std::string	label = {};		// 何をしたか
		double		ms = 0.0;		// かかった時間
	};

	using HitchCallback = std::function<void(const HitchEvent&)>;

	// 受け手 : 起動時に1度だけ登録し、ワーカーを止めるまで差し替えない(ワーカーからも呼ばれるため)
	inline HitchCallback g_hitchCallback = nullptr;

	// 記録中か : 切っている間は時計も読まず、名前の文字列も組まない
	inline std::atomic<bool> g_isHitchRecording = false;

	inline void SetHitchCallback(HitchCallback a_cb) { g_hitchCallback = std::move(a_cb); }
	inline void SetHitchRecording(bool a_isRecording) { g_isHitchRecording.store(a_isRecording, std::memory_order_release); }
	inline bool IsHitchRecording() { return g_isHitchRecording.load(std::memory_order_acquire); }

	inline void ReportHitch(const char* a_category, std::string a_label, double a_ms)
	{
		if (!IsHitchRecording() || !g_hitchCallback) return;
		g_hitchCallback(HitchEvent{ a_category, std::move(a_label), a_ms });
	}

	//==========================================================================================
	// スコープの時間を測って送る
	//
	// 名前は送るときにだけ作る(a_labelFunc はスコープを抜けるときに呼ばれる)。
	// a_minMs より短かったものは送らない
	//==========================================================================================
	template<typename LabelFunc>
	class HitchScope
	{
	public:

		HitchScope(const char* a_category, double a_minMs, LabelFunc a_labelFunc)
			: m_category(a_category), m_minMs(a_minMs), m_labelFunc(std::move(a_labelFunc)), m_isActive(IsHitchRecording())
		{
			if (m_isActive) m_start = std::chrono::steady_clock::now();
		}

		~HitchScope()
		{
			if (!m_isActive) return;

			const double _ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_start).count();
			if (_ms < m_minMs) return;

			ReportHitch(m_category, m_labelFunc(), _ms);
		}

		HitchScope(const HitchScope&) = delete;
		HitchScope& operator=(const HitchScope&) = delete;

	private:

		const char*		m_category = "";
		double			m_minMs = 0.0;
		LabelFunc		m_labelFunc;
		bool			m_isActive = false;

		std::chrono::steady_clock::time_point m_start = {};
	};
}

// =========================================================
// 計測呼び出し用マクロ
//
// a_labelExpr は std::string になる式。参照で捕まえるので、スコープ内の変数を書いてよい
// (抜けるときの値で名前を作る)
// =========================================================
#define ENGINE_HITCH_SCOPE_CONCAT_IMPL(a_lhs, a_rhs) a_lhs##a_rhs
#define ENGINE_HITCH_SCOPE_CONCAT(a_lhs, a_rhs) ENGINE_HITCH_SCOPE_CONCAT_IMPL(a_lhs, a_rhs)

#define ENGINE_HITCH_SCOPE(a_category, a_minMs, a_labelExpr) \
	Core::Debug::HitchScope ENGINE_HITCH_SCOPE_CONCAT(_hitchScope_, __LINE__)( \
		a_category, a_minMs, [&]() -> std::string { return a_labelExpr; })
