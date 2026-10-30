#pragma once

namespace Engine::Time
{
	//==========================================================================================
	// 60FPS を割ったフレームの中身を書き出す(カクつきの原因探し)。持ち主は MainEngine
	//
	// 届いた出来事(Core::Debug::ReportHitch / ENGINE_HITCH_SCOPE)はフレームの間だけ溜め、
	// フレームの終わりに捨てる。フレームの間隔が閾値を超えたときだけ、
	// 溜まっていたものをログと HitchLog.txt(作業ディレクトリ)へ書く。
	//
	// ・計測スコープ(ENGINE_PROFILE_SCOPE)はメインスレッドのものだけ数える。
	//   ワーカーのものはチャンクごとに届き、フレームの長さとは関係がないため
	// ・それ以外の出来事はワーカーで起きたものも残し、印を付けて出す
	//   (描画の積み込みのように、メインスレッドが終わりを待つジョブもあるため)
	// ・シーンの読み込み中は数えるだけで書き出さない(ロード画面で止まるのは分かっているため)
	//==========================================================================================
	class HitchMonitor
	{
	public:

		HitchMonitor() = default;
		~HitchMonitor() = default;

		NON_COPYABLE_NON_MOVABLE(HitchMonitor);

		// 受け口を登録する : メインスレッドから、ワーカーが動き出す前に呼ぶ
		void Init();

		// 受け口を外し、まとめをファイルへ書く : ワーカーを止めた後に呼ぶ
		void Release();

		// 記録するか(起動設定の isHitchLog)。毎フレーム呼んでよい
		void SetEnabled(bool a_isEnabled);

		// FPS 制限の待ちに入る直前 : ここまでを「処理の時間」として出す
		void MarkWorkEnd();

		// フレームの終わり(FPS 制限の待ちの後)。a_isLoading なら書き出さずに数えるだけ
		void EndFrame(const std::string& a_sceneName, bool a_isLoading);

	private:

		// 溜めておく出来事1つ
		struct Record
		{
			const char*	category = "";
			std::string	label = {};
			double		ms = 0.0;
			bool		isWorker = false;	// メインスレッド以外で起きたか
		};

		// 受け口 : どのスレッドからも呼ばれる
		void OnEvent(const Core::Debug::HitchEvent& a_event);

		// 1フレームぶんを書き出す
		void WriteReport(const std::vector<Record>& a_recordVec, double a_intervalMs, double a_workMs, const std::string& a_sceneName);

		// ログとファイルの両方へ1行書く
		void WriteLine(const std::string& a_line);

	private:

		// 60FPS(16.67ms)を割ったとみなす間隔。FPS 制限の待ちのぶれ(0.5ms 程度)は数えない
		static constexpr double THRESHOLD_MS = 17.5;

		// 書き出すフレームの上限 : ずっと重い場面でログが埋まらないように
		static constexpr uint32_t MAX_REPORT_COUNT = 300;

		// 1フレームに溜める出来事の上限
		static constexpr size_t MAX_RECORD_COUNT = 4096;

		std::thread::id	m_mainThreadID = {};
		bool			m_isEnabled = false;
		bool			m_hasPrevFrame = false;		// 前のフレームの終わりを持っているか(無ければ間隔を測らない)
		uint64_t		m_frameIndex = 0;

		std::chrono::steady_clock::time_point m_startTime = {};
		std::chrono::steady_clock::time_point m_prevFrameEnd = {};
		std::chrono::steady_clock::time_point m_workEnd = {};

		// 溜めている出来事 : ワーカーからも積まれるのでロックで守る
		std::mutex			m_mutex;
		std::vector<Record>	m_recordVec = {};
		size_t				m_droppedCount = 0;

		// まとめ
		uint32_t	m_reportCount = 0;			// 書き出したフレーム数
		uint32_t	m_loadingHitchCount = 0;	// 読み込み中に割ったフレーム数(書き出していない)
		double		m_worstMs = 0.0;

		std::ofstream m_file;
	};
}
