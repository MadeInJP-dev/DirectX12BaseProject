#pragma once
namespace Editor
{
	//==========================================================================================
	//
	// モーダルなポップアップ1つ分の開閉と描画
	//
	// ImGui のポップアップは「開く要求(OpenPopup)を出したときの ID スタック」に紐づくので、
	// メニューやボタンの中で直接開くと、描く側と ID が食い違って開かないことがある。
	// ここでは Open() で要求だけを受け、Draw() の中で開く。呼ぶ側は好きな場所で Open() してよい。
	//
	// 中身は SetContent で預けるか、Draw に直接渡す。中身から閉じるときは Close()。
	// 名前の入力・確認はこれを使った NameInputPopup / ConfirmPopup がある。
	//
	// ボタンの下に出る小さな一覧(AddEntity など)は、その場で開いて閉じるだけなので
	// ImGui::OpenPopup / BeginPopup をそのまま使う
	//
	//==========================================================================================
	class Popup
	{
	public:

		explicit Popup(const std::string& a_name) : m_name(a_name) {}
		~Popup() = default;

		// 表示する内容
		void SetContent(const std::function<void()>& a_func) { m_content = a_func; }

		// 見出し。ID は名前のまま変えない(未保存の印などで見出しが変わっても同じ窓として扱う)
		void SetTitle(const std::string& a_title) { m_title = a_title; }

		// 画面に対する大きさ(0 なら中身に合わせる)。開いた最初の1回だけ効く
		void SetScreenRatio(float a_ratio) { m_screenRatio = a_ratio; }

		// 見出しの × を出すか。出さない間は中身のボタンからしか閉じられない
		void SetClosable(bool a_isClosable) { m_isClosable = a_isClosable; }

		// × で閉じられたときの処理
		void SetOnClosed(const std::function<void()>& a_func) { m_onClosed = a_func; }

		// 開く要求
		void Open() { m_isOpenRequested = true; }

		// 閉じる要求(中身の中から呼ぶ。描き終わった後に閉じる)
		void Close() { m_isCloseRequested = true; }

		// 開いているか(閉じた次のフレームから false)
		bool IsOpen() const { return m_isOpen; }

		// 描画
		void Draw();

		// その場の中身で描画(中身が毎フレームの引数をつかむとき用)
		void Draw(const std::function<void()>& a_content);

	private:

		// ImGui に渡す名前(見出しがあれば「見出し###名前」)
		std::string MakeWindowName() const;

	private:

		// ウィンドウ情報
		std::string m_name;					// 名前(ImGui の ID)
		std::string m_title;				// 見出し(空なら名前を出す)
		std::function<void()> m_content;	// 中身の処理
		std::function<void()> m_onClosed;	// × で閉じられたときの処理

		float m_screenRatio = 0.0f;			// 画面に対する大きさ
		bool m_isClosable = true;			// × を出すか

		bool m_isOpenRequested = false;		// 開く要求
		bool m_isCloseRequested = false;	// 閉じる要求
		bool m_isOpen = false;				// 開いているかどうか
	};
}
