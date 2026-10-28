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
	/// ・出し方(出た瞬間だけ少し大きく見せ、消えぎわに薄くする)は UIBase の演出
	///   (Transition の Pulse)で決める。ウェーブが出るたびに合図を流し、流している間だけ描く。
	///   HitEffectHUD と同じ作り。
	/// </summary>
	class WaveAnnounceHUD : public UIBase
	{
	public:

		// 押されることのない HUD なので、カーソルへの反応を持たない。
		// ゲーム中の HUD : プレイヤーが HUD を消すと一緒に消える
		WaveAnnounceHUD();

		// 初期化処理 : 飾りの絵と音を用意する
		void PostDeserialize(Engine::GameObject::ObjectContext& a_context) override;
		void Awake(Engine::GameObject::ObjectContext& a_context) override;

		// 解放処理 : サウンドインスタンスを返す
		void Release(Engine::GameObject::ObjectContext& a_context) override;

		// 更新処理 : ウェーブが出ていないか見て、出ていたら出し直す
		void Update(Engine::GameObject::ObjectContext& a_context) override;

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

		// 新しく置いたときの出し方を演出へ入れる
		void SetDefaultTransition();

		// ウェーブが出たときの反応(文字の差し替え・合図の出し直し・発音)
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

		// 文字の組み立て。番号は1始まりで出す(データ上は0始まり)
		std::string m_prefix      = "WAVE ";
		bool        m_isShowTotal = false;	// "3 / 6" のように総数も出すか
		std::string m_separator   = " / ";

		// ---- 状態(保存しない) ----
		uint32_t m_lastSerial = 0;		// 前回反応したときの通し番号
		int      m_lastWaveIndex = -1;	// 最後に出したウェーブ(インスペクター表示用)
	};
}
