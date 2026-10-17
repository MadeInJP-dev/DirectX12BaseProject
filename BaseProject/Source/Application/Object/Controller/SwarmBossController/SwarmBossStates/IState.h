#pragma once

namespace Engine::GameObject
{
	struct ObjectContext;
}

namespace Engine::Persistence
{
	class Archive;
}

namespace App::Object
{
	class SwarmBossStateMachine;

	/// <summary>
	/// 体を爆散させる依頼(死亡の最後)。ボイドを中心から外へ飛ばし、しばらくして落とす
	/// </summary>
	struct SwarmBossBurstRequest
	{
		bool isRequested = false;				// 爆散させてほしい(この1フレームだけ立てる)
		Math::Vector3 center = {};				// 爆散の中心(ワールド)
		float speedMin = 40.0f;					// 飛び散る速さの下限(m/秒)
		float speedMax = 120.0f;				// 飛び散る速さの上限(m/秒)
		float upBias   = 0.3f;					// 上向きへの寄せ(0で中心から真っすぐ外へ)
		float lifeMin  = 0.3f;					// 飛んでから落ちるまでの時間の下限(秒)
		float lifeMax  = 1.5f;					// 上限(秒)
		float gravity  = 20.0f;					// 飛んでいる間の重力(m/秒^2)
		float drag     = 0.5f;					// 飛んでいる間の減速(1/秒)
		Math::Vector3 color = { 1.0f, 0.9f, 0.7f };	// 爆散した瞬間の発光色(0〜1)
		float intensity = 20.0f;					// 発光の強さ
	};

	/// <summary>
	/// ステートに渡すもの
	///
	/// ステートはコントローラーを直接見ない。要るものはここに載せて毎フレーム組む
	/// </summary>
	struct SwarmBossStateContext
	{
		Engine::GameObject::ObjectContext* pObject = nullptr;	// dt / ワールド
		SwarmBossStateMachine* pMachine = nullptr;				// 切り替え要求の出し先

		Engine::ECS::Entity leaderEntity = Engine::ECS::Limits::INVALID_ENTITY;	// 入力を書き込む相手
		Math::Vector3 spawnPos = {};											// 行動範囲の中心(ワールド)

		// 体の並び。頭(リーダーの直後)から尾の順。ボイドを切り離す攻撃が使う
		const std::vector<Engine::ECS::Entity>* pPlatoonLeaders = nullptr;
		float wormLength = 0.0f;												// 頭から尾までの長さ(1次元。m)

		//------------------------------------------------------------------------------------------
		// ステートからコントローラーへの依頼(毎フレーム既定値で組み直し、ステートの更新の後に読まれる)
		//
		// 体(小隊長・ボイド)を作り変えるのはコントローラーの仕事なので、ステートは頼むだけにする
		//------------------------------------------------------------------------------------------
		bool isRequestReorganize = false;	// 小隊長を体力の比率まで減らし、ボイドを割り当て直してほしい
		float bodyDefenseRatio   = 1.0f;	// 体(ボイド)の防御比率。1 : そのまま食らう / 0 : 無敵
		float waveSpeedScale     = 1.0f;	// 発光のウェーブを速める倍率(速さに掛け、出す間隔を割る)
		SwarmBossBurstRequest burst = {};	// 体を爆散させてほしい(死亡の最後)

		// 咆哮(鳴き声と、画面のラジアルブラーの震え)。鳴らす音とブラーの調整値はコントローラーが持つ
		bool isRequestRoar = false;			// 鳴き声を鳴らしてほしい(鳴き始めの1フレームだけ立てる)
		float roarWeight   = 0.0f;			// 画面を震わせる強さ(0〜1。鳴いている間、毎フレーム頼む)

		// 体のボイドを、所属する小隊長を起点に胴回りで回す(突進・ダイブ)。
		// 頼んでいる間はコントローラーが回転を最高速まで上げていき、やめると緩めて輪を畳む
		float bodySpinRate = 0.0f;			// 回転の強さ(0〜1。最高速に対する割合。毎フレーム頼む)
	};

	class IState
	{
	public:
		virtual ~IState() = default;

		virtual void Enter(SwarmBossStateContext& a_context) = 0;
		virtual void Update(SwarmBossStateContext& a_context) = 0;
		virtual void Exit(SwarmBossStateContext& a_context) = 0;

		//------------------------------------------------------------------------------------------
		// 調整値の保存 / 表示。持たないステートは何もしない
		//------------------------------------------------------------------------------------------
		virtual void Archive(Engine::Persistence::Archive& /*a_ar*/) {}
		virtual void DrawInspector() {}

		// 後から足した調整値。バイナリは書いた順に読むので、Archive へ混ぜずにこちらへ書く。
		// ステートごとに区切り(ステートの名前)で囲んで、持ち主の保存値の末尾で呼ばれる。
		// 足すときはここの末尾へ
		virtual void ArchiveAppended(Engine::Persistence::Archive& /*a_ar*/) {}
	};
}
