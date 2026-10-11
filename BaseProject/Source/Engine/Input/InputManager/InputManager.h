#pragma once

#include "../Core/InputAction.h"
#include "../InputDevice/Button/InputButtonBase.h"

namespace Engine::Option::ProjectOptions
{
	struct InputOption;
}

namespace Engine::Input
{
	class InputCollector;

	/// <summary>
	/// 様々な入力を管理するクラス : 複数のInputCllectorを管理
	/// </summary>
	/// <remarks>
	/// アクションの指定は ActionID(uint32_t)。取得も登録もこの番号を鍵にしている。
	/// 引数の ActionKey は番号・enum class・文字列のどれでも受け取れるので、
	///
	///   a_pInputManager->IsPress(EGameAction::Boost);	// ゲーム側の enum
	///   a_pInputManager->IsPress("Boost");				// 名前(ハッシュを通る)
	///
	/// のどちらでも書ける。名前を渡した場合はハッシュ値が番号になるので、
	/// 登録と取得で同じ書き方を通していれば混ぜても構わない。
	///
	/// 注意 : 番号の衝突はエンジン側では防げない。
	/// ゲーム側で複数の enum を用意する場合は、開始値をずらして重ならないようにすること。
	/// </remarks>
	class InputManager
	{
	public:

		//==================================================================================
		// システム用の入力
		//----------------------------------------------------------------------------------
		// ゲームの操作とは別に、エンジン側が必ず持っておく入力。
		// アプリ側の登録に関係なく使えるように Init() の中で登録している。
		//==================================================================================

		// システム用デバイスの名前(アプリ側が同じ名前で上書きしないこと)
		static constexpr const char* SYSTEM_DEVICE_NAME = "System";

		// エディター / ゲームの切り替え : Ctrl+P
		// 名前から作った番号なので、ゲーム側の enum の値とはまず重ならない
		static constexpr const char* SYSTEM_ACTION_TOGGLE_APPMODE_NAME = "ToggleAppMode";
		static constexpr ActionID SYSTEM_ACTION_TOGGLE_APPMODE = ToActionID(SYSTEM_ACTION_TOGGLE_APPMODE_NAME);

		InputManager();
		~InputManager();
		NON_COPYABLE_NON_MOVABLE(InputManager);

		// 初期化
		// a_pInputOption : 感度とカーソル固定の設定(借り物。持ち主は OptionManager)
		void Init(const Option::ProjectOptions::InputOption* a_pInputOption);

		// 更新
		// 毎フレーム必須
		void Update();

		/// <summary>
		/// 溜まっている入力状態を捨てる
		/// </summary>
		/// <remarks>
		/// アプリのモード切り替えで呼ぶ。押しっぱなしのボタン・マウスの移動量・
		/// カーソル固定の基準を、切り替えの向こう側へ持ち越さないようにする。
		/// </remarks>
		void ResetInput();

		// マウス制御
		void SetCursorCentered(bool a_enable);		// 毎フレーム中央にマウスを固定
		void SetCursorLock();						// 一度だけマウスを中央に戻す

		/// <summary>
		/// プレイヤーの視点の設定(保存はアプリ側のユーザーデータ)
		/// </summary>
		/// <remarks>
		/// 感度は作り手の感度(InputOption::lookSensitivityX/Y)へさらに掛ける倍率。
		/// 上下反転は選ばれていればそちらを使う(nullopt で作り手の InputOption::isInvertLookY のまま)。
		/// エンジン設定は書き換えないので、EngineData を保存してもプレイヤーの値は混ざらない
		/// </remarks>
		void SetUserLookSensitivity(float a_scale) { m_userLookSensitivity = a_scale; }
		void SetUserInvertLookY(std::optional<bool> a_opIsInvert) { m_opUserInvertLookY = a_opIsInvert; }

		// 実際に使う上下反転 : プレイヤーの値があればそちら
		bool IsInvertLookY() const;

		// このフレームでカーソル固定が実際に働いているか
		bool IsCursorLockActive() const { return m_isCursorLockActive; }

		/// <summary>
		/// ゲーム側の入力を受け付けてよい状態か
		/// </summary>
		/// <remarks>
		/// プレイモード以外(エディター操作中)やテキスト入力中は false。
		/// ボタンUIのように「入力を見て自分で判定するもの」が、
		/// エディター操作に反応してしまわないよう確認に使う。
		/// </remarks>
		bool IsGameInputEnable() const;

		/// <summary>
		/// カーソルのクライアント領域内の座標を取得する
		/// </summary>
		/// <param name="a_outPos">左上を原点としたクライアント座標(px)</param>
		/// <returns>取得できたら true</returns>
		/// <remarks>
		/// UIの当たり判定はこの座標を描画解像度へ直して使う。
		/// カーソルを中央へ固定している間(IsCursorLockActive)は毎フレーム中央へ
		/// 戻されるので、位置に意味は無い。メニューを出す場面では固定を切ること。
		/// </remarks>
		bool GetCursorClientPos(Math::Vector2& a_outPos) const;

		// すべての有効な入力装置からのボタン入力状態を取得
		InputButtonBase::EState GetButtonState(ActionKey a_action) const;

		bool IsFree(ActionKey a_action) const;
		bool IsPress(ActionKey a_action) const;
		bool IsHold(ActionKey a_action) const;
		bool IsRelease(ActionKey a_action) const;

		/// <summary>
		/// プレイモードでなくても拾いたい入力の状態を取得する
		/// </summary>
		/// <remarks>
		/// 上の GetButtonState はプレイモード以外だと必ず「入力なし」を返す。
		/// エディター操作中にゲームが動いてしまわないための決まりだが、
		/// モードの切り替えそのものはエディター側に居るときに押すので、
		/// それだとエディターから戻れない。
		///
		/// こちらはその一段外側で、アプリのモードを見ずに状態を返す。
		/// ゲームの操作には使わないこと(エディター操作中にも反応してしまう)。
		/// </remarks>
		InputButtonBase::EState GetSystemButtonState(ActionKey a_action) const;
		bool IsSystemPress(ActionKey a_action) const;
		bool IsSystemHold(ActionKey a_action) const;

		// すべての有効な入力装置からの軸入力状態を取得
		Math::Vector2 GetAxisState(ActionKey a_action) const;

		/// <summary>
		/// このフレームのホイールの回転量を取得する
		/// </summary>
		/// <returns>ノッチ数。奥へ回すと正(高精度のホイールでは端数が出る)</returns>
		/// <remarks>
		/// アクションの割り当てを通さない生の値。スクロールする一覧のように
		/// 「どこで回されたか」で受け取り手が決まるものはこちらを見る。
		/// ボタンと同じく、プレイモード以外やテキスト入力中は 0 を返す
		/// </remarks>
		float GetWheelDelta() const;

		// 入力装置の登録
		void AddDevice(std::string_view a_name,InputCollector* a_pInputDevice);
		void AddDevice(std::string_view a_name,std::unique_ptr<InputCollector> a_upInputDevice);

		// アクセサ
		const std::unique_ptr<InputCollector>& GetDevice(std::string_view a_name) const;
		std::unique_ptr<InputCollector>& RefDevice(std::string_view a_name);

		bool IsActive() const { return m_isActive; }
		void SetActive(bool a_active) { m_isActive = a_active; }

		// 解放
		void Release();

	private:

		// クライアント領域の中心をスクリーン座標で取得する
		// ウィンドウの移動・リサイズ・フルスクリーン切替に追従するため毎回実測する
		bool GetClientCenterPos(POINT& a_outPos) const;

		// システム用の入力を登録する(Init から呼ぶ)
		void RegisterSystemDevice();

	private:

		// ウィンドウが選択されていたら入力を受け付ける
		bool m_isActive = true;

		std::unordered_map<std::string, std::unique_ptr<InputCollector>> m_upInputDeviceMap = {};

		// マウス制御
		bool m_isCursorLockedToCenter = false;	// 固定する設定かどうか
		bool m_isCursorLockActive = false;		// このフレームで実際に固定が働いているか
		bool m_needCursorLockReset = true;		// 固定開始直後 : 基準を取り直し移動量を0にする
		POINT m_lockAnchorPos = {};				// 前フレームに実際にカーソルを移動させたスクリーン座標
		// 移動量(感度を掛けた後)。生のマウス入力(WM_INPUT)から作る。
		// 感度を下げたときに端数を捨てないよう float で持つ
		float m_deltaX = 0.0f;					// 移動量X
		float m_deltaY = 0.0f;					// 移動量Y

		// このフレームのホイールの回転量(ノッチ数, 奥が正)
		float m_wheelDelta = 0.0f;

		// プレイヤーの視点の設定(作り手の設定に重ねる)
		float m_userLookSensitivity = 1.0f;					// 感度へ掛ける倍率
		std::optional<bool> m_opUserInvertLookY = std::nullopt;	// 上下反転(未選択なら作り手の値)

		// 入力設定(感度・カーソル固定)。毎フレーム読むので借りたまま持つ
		const Option::ProjectOptions::InputOption* m_pInputOption = nullptr;
	};
}