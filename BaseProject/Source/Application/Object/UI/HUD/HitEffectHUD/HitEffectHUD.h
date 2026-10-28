#pragma once

#include "Application/Object/UI/UIBase.h"

namespace App::Object
{
	/// <summary>
	/// 自分の撃った弾が当たったときに出す手応え(ヒットマーカー)。
	///
	/// クロスマークのUIを一定時間出しつつ、同時にヒット音を鳴らす。
	///
	/// ・どのヒットで反応するかは HUDGatherSystem が決める。
	///   プレイヤーの弾(HitEvent.shooter)が、ダメージの通る相手(HealthComponent を持つもの)に
	///   当たったフレームに PlayerHUDResource::hitSerial が1つ進むので、ここは番号の変化を見るだけ。
	///   壁や地面に当てても手応えが出ると、当たった合図として意味を成さないため。
	/// ・テクスチャと音はインスペクターから選ぶ。
	/// ・出し方(拡大して戻る・薄くなって消える)は UIBase の演出(Transition の Pulse)で決める。
	///   ヒットのたびに合図を流し、流している間だけ描く(PulseOnly)。
	/// </summary>
	class HitEffectHUD : public UIBase
	{
	public:

		// 押されることのない HUD なので、カーソルへの反応を持たない。
		// ゲーム中の HUD : プレイヤーが HUD を消すと一緒に消える
		HitEffectHUD();

		// 初期化処理 : テクスチャとサウンドインスタンスの用意
		void PostDeserialize(Engine::GameObject::ObjectContext& a_context) override;
		void Awake(Engine::GameObject::ObjectContext& a_context) override;

		// 解放処理 : サウンドインスタンスを返す
		void Release(Engine::GameObject::ObjectContext& a_context) override;

		// 更新処理 : 自分の弾のヒットを拾って合図を流す
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "HitEffectHUD"; }

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		// ヒットに反応する(合図の出し直しと発音)
		void OnHit(Engine::GameObject::ObjectContext& a_context);

		// 新しく置いたときの出し方を演出へ入れる
		void SetDefaultTransition();

		// サウンドインスタンスを取り直す
		void CreateSound(Engine::GameObject::ObjectContext& a_context);

	private:

		// ---- 音(保存される) ----
		Core::GUID m_soundGUID = Core::DEFAULT_GUID;
		Engine::Handle<Engine::Resource::SoundInstance> m_soundHandle = {};
		float m_volume = 1.0f;

		// ---- 音の間引き(保存される) ----
		float m_minInterval = 0.03f;	// 音を鳴らし直す最短間隔(秒)。
										// インスタンスは1つで Play は頭出しの鳴らし直しになるため、
										// 連射で潰れ続けないよう間引く

		// ---- ランタイム ----
		float m_coolTime   = 0.0f;		// 次に鳴らせるまでの残り時間(秒)
		int   m_hitCount   = 0;			// 出した回数(確認用)

		// 最後に見たヒットの通し番号(PlayerHUDResource::hitSerial)。変わったフレームに反応する
		uint32_t m_lastHitSerial = 0;
		bool     m_isHitSerialSynced = false;	// 一度でも番号を見たか
	};
}
