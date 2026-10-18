#pragma once

#include "DebugCommands.h"
#include "Application/Object/UI/Decoration/Decoration.h"

namespace Engine
{
	class MainEngine;
}

namespace Engine::Graphics
{
	class GraphicsEngine;
}

namespace App::Game
{
	class UserData;

	/// <summary>
	/// 発表・確認用のデバッグキー(F1〜F7)
	/// </summary>
	/// <remarks>
	/// Shipping 以外(Debug / Development)のときだけ GameManager が作る。
	///
	///   F1        プレイヤーの無敵を切り替え
	///   F2        出ている敵を倒して次のウェーブへ(Shift+F2 で最後のウェーブ = ボスへ)
	///   F3        ボスの体力を死亡の手前まで減らす
	///   F4        時間の倍率を 1 → 0.25 → 0 → 1 と切り替え
	///   F5        倍率 0 のときに1フレームだけ進める
	///   F6        HUD の表示を切り替え(ユーザーデータの IsHUDVisible と同じもの。保存はしない)
	///   F7        画面左上に FPS・エンティティ数・ボイド数・VRAM を出す
	///
	/// 無敵の間は、F7 の表示とは関係なく画面左上に「INVINCIBLE」の札を出し続ける
	/// (HUD を消していても出す。切り忘れたまま進めないように)。
	///
	/// キーはユーザーが割り当てを変える EGameAction には入れず、名前から作った番号で
	/// 別のデバイス("DebugKeys")へ積む。割り当ての候補からは IsReservedKey で外してある。
	/// 取るのは IsPress なので、エディターを操作している間は反応しない。
	///
	/// ウェーブやボスへの指示は予約(DebugCommands)として置くだけで、
	/// 拾うのは SceneSequence / SwarmBossController。ワールドへは DebugCommandResource で入口を置く。
	/// </remarks>
	class DebugKeys
	{
	public:

		DebugKeys();
		~DebugKeys();
		NON_COPYABLE_NON_MOVABLE(DebugKeys);

		/// <summary>
		/// デバッグキーを使えるビルドか(Shipping では使えない)
		/// </summary>
		static bool IsEnabled();

		/// <summary>
		/// デバッグキーが使っているキーか : 操作の割り当ての候補から外す
		/// </summary>
		/// <remarks>Shipping では何も使っていないので常に false</remarks>
		static bool IsReservedKey(int a_keyCode);

		// 初期化 : キーをデバイスへ積み、エディターの窓を登録する
		void Init(Engine::MainEngine& a_engine);

		// フォントの参照を返す。リソースの解放より前に呼ぶこと
		void Release();

		/// <summary>
		/// キーを見て状態を切り替え、予約を立てる : シーンの更新より前に呼ぶ
		/// </summary>
		/// <param name="a_pUserData">HUD の表示を切り替える先(無ければ F6 は効かない)</param>
		void Update(UserData* a_pUserData);

		/// <summary>
		/// シーンの更新の後に呼ぶ : 予約を下ろし、表示する数字を取り直す
		/// </summary>
		void EndUpdate();

		// 情報の表示と無敵の札を積む : シーンの UI を積み終えた後に呼ぶ
		void SubmitUI(Engine::Graphics::GraphicsEngine* a_pGraphicsEngine) const;

		// ---- アクセサ ----
		const DebugCommands& GetCommands() const { return m_commands; }

	private:

		// キーの番号をデバイスへ積む(Init から)
		void RegisterKeys();

		// 時間の倍率を次の段へ
		void StepTimeScale();

		// 表示する数字を集めて文字にする
		void RefreshStatsText();

		// エディターの窓の中身
		void Edit();

	private:

		// ゲームへ渡す命令と状態(ワールドへは DebugCommandResource で入口を置く)
		DebugCommands m_commands = {};

		// ---- 情報の表示(F7) ----
		bool m_isStatsVisible = false;
		float m_statsTimer = 0.0f;				// 次に数字を取り直すまでの残り(倍率を掛けない秒)
		Object::Decoration::Decoration m_statsBackground = {};	// 下敷きの板
		Object::Decoration::Decoration m_statsText = {};		// 数字

		// ---- 無敵の札(無敵の間ずっと出す) ----
		Object::Decoration::Decoration m_invincibleBackground = {};	// 下敷きの板
		Object::Decoration::Decoration m_invincibleText = {};		// 文字

		// HUD の表示を切り替えた先(借り物)。表示の行に出すため覚えておく
		const UserData* m_pUserData = nullptr;

		// エンジン(借り物)
		Engine::MainEngine* m_pEngine = nullptr;
	};
}
