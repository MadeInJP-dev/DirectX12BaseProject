#pragma once

#include "Application/Object/UI/UIBase.h"

namespace App::Object
{
	/// <summary>
	/// ウェーブが出た瞬間に「何番目のウェーブか」を出して、合図の音を鳴らす HUD。
	///
	/// ・きっかけは WaveAnnounceResource(SceneSequence がウェーブを出した瞬間に積む)。
	///   通し番号(serial)が前回見た値と違うフレームだけ反応する。
	///   積む側もこの HUD も GameObjectManager::Update で回るので順番が決まらないが、
	///   通し番号を見る形なら、先に回っても次のフレームで気づけて取りこぼさない。
	///
	/// ・文字は飾り(Decoration)の Text へ流し込む。
	///   種類が Text の飾りすべてに同じ文字列を入れるので、
	///   影付きにしたいときは同じ文字を2枚重ねて色と位置をずらせばよい。
	///   枠や背景は Polygon / Image の飾りとして並べれば一緒に出る。
	///
	/// ・出ている間だけ描く。出た瞬間だけ少し大きく見せ、消えぎわに薄くするのは
	///   HitEffectHUD と同じ作り(掛ける色と倍率の差し替えだけで、飾りの値は触らない)。
	/// </summary>
	class WaveAnnounceHUD : public UIBase
	{
	public:

		// 押されることのない HUD なので、カーソルへの反応を持たない
		WaveAnnounceHUD() : UIBase(false) { m_isHUD = true; }	// ゲーム中の HUD : プレイヤーが HUD を消すと一緒に消える

		// 初期化処理 : 飾りの絵と音を用意する
		void PostDeserialize(Engine::GameObject::ObjectContext& a_context) override;
		void Awake(Engine::GameObject::ObjectContext& a_context) override;

		// 解放処理 : サウンドインスタンスを返す
		void Release(Engine::GameObject::ObjectContext& a_context) override;

		// 更新処理 : ウェーブが出ていないか見て、出ていたら出し直す
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// 描画処理 : 表示時間が残っている間だけ描く
		void Draw(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "WaveAnnounceHUD"; }

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		// ウェーブが出たときの反応(文字の差し替え・表示時間の巻き戻し・発音)
		void OnWaveSpawned(Engine::GameObject::ObjectContext& a_context, int a_waveIndex, int a_waveCount);

		// 出す文字列を組む(例 : "WAVE 3" / "WAVE 3 / 6")
		std::string MakeLabel(int a_waveIndex, int a_waveCount) const;

		// Text の飾りすべてへ文字列を入れる
		void ApplyLabel(const std::string& a_label);

		// サウンドインスタンスを取り直す
		void CreateSound(Engine::GameObject::ObjectContext& a_context);

	private:

		// ---- 音(保存される) ----
		Core::GUID m_soundGUID = Core::DEFAULT_GUID;
		Engine::Handle<Engine::Resource::SoundInstance> m_soundHandle = {};
		float m_volume = 1.0f;

		// ---- 表示(保存される) ----
		float m_showTime  = 2.0f;		// 1回で出しておく時間(秒)
		bool  m_isFadeOut = true;		// 消えぎわに薄くするか
		float m_punchScale = 1.35f;		// 出た瞬間の拡大率(等倍へ戻る)

		// 文字の組み立て。番号は1始まりで出す(データ上は0始まり)
		std::string m_prefix      = "WAVE ";
		bool        m_isShowTotal = false;	// "3 / 6" のように総数も出すか
		std::string m_separator   = " / ";

		// ---- 状態(保存しない) ----
		float    m_remainTime = 0.0f;	// 残りの表示時間(秒)
		uint32_t m_lastSerial = 0;		// 前回反応したときの通し番号
		int      m_lastWaveIndex = -1;	// 最後に出したウェーブ(インスペクター表示用)
	};
}
