#pragma once
namespace App::Game
{
	/// <summary>
	/// プレイの結末
	/// </summary>
	enum class EGameResult : uint32_t
	{
		None,		// まだ決着していない(プレイ中)
		Clear,		// 生き残ってウェーブを出し切った
		GameOver,	// プレイヤーが倒された
	};

	/// <summary>
	/// 出撃する機体の構成(プレイヤーの本体と、取り付け口ごとの武器のプレハブ)
	/// </summary>
	/// <remarks>
	/// ゲームシーンの PlayerSpawner がこれを見て、本体を出してから武器を取り付け口へ付ける。
	/// 初期値はゲーム設定(GameData)の既定の構成。ホームで武器を選んだらここを書き換える。
	/// 本体が未設定なら、スポナーは自分に設定されたプレハブ(武器込みの一体物)をそのまま出す。
	/// </remarks>
	struct PlayerLoadout
	{
		Core::GUID bodyPrefabGUID = {};				// 本体(取り付け口 WeaponMountComponent を持つ機体)
		Core::GUID rightHandWeaponPrefabGUID = {};	// 右手の武器(手持ち : WeaponStateComponent)
		Core::GUID leftHandWeaponPrefabGUID = {};	// 左手の武器(手持ち : WeaponStateComponent)
		Core::GUID shoulderWeaponPrefabGUID = {};	// 右肩の武器(MissileStateComponent)。E で撃つ
		Core::GUID leftShoulderWeaponPrefabGUID = {};	// 左肩の武器(MissileStateComponent)。Q で撃つ

		// 構成として使えるか(本体が決まっていれば使う。武器は空でもよい)
		bool IsValid() const { return bodyPrefabGUID.IsValid(); }
	};

	/// <summary>
	/// シーンをまたいで受け取るデータ
	///
	/// 例) メニューで選んだ戦闘ステージ、機体情報、ゲームシーンでのタイム、スコア
	/// </summary>
	/// <remarks>
	/// ワールド(ECS)のリソースはシーンを切り替えると作り直されるので、
	/// リザルトへ持っていきたいものはここへ集める。
	/// 実体は GameManager が1つだけ持っていて、
	/// 各ワールドに置かれた入口(InstanceResource::GameDataResource::Find)から触る。
	///
	/// 書く側 : ScoreSystem(スコア) / SceneSequence(タイム・結末・ウェーブ) / GameManager(出撃構成の初期値)
	/// 読む側 : ScoreHUD、リザルトの表示物、PlayerSpawner(出撃構成)
	/// </remarks>
	struct GlobalGameContext
	{
		// ---- ホームからゲームシーンへ引き継がれる ----
		// 出撃する機体の構成。1プレイの記録ではないので ResetRun では消さない
		PlayerLoadout loadout = {};

		// ---- ゲームシーンからリザルトへ引き継がれる ----
		int   score = 0;			// ゲーム内スコア(倒した相手の ScoreTargetComponent ぶん)
		int   killCount = 0;		// 倒した数
		float time = 0.0f;			// クリアもしくは死亡時のタイム(秒)

		EGameResult result = EGameResult::None;	// どう終わったか

		int clearedWaveCount = 0;	// 出し切ったウェーブ数(ボスのウェーブも1つとして数える)
		int totalWaveCount   = 0;	// ウェーブの総数

		/// <summary>
		/// 1プレイぶんの記録を消す
		/// </summary>
		/// <remarks>
		/// ゲームシーンが始まったときに SceneSequence が呼ぶ。
		/// リザルトからやり直しても前回のスコアが残らないようにするため。
		/// </remarks>
		void ResetRun()
		{
			score = 0;
			killCount = 0;
			time = 0.0f;
			result = EGameResult::None;
			clearedWaveCount = 0;
			totalWaveCount = 0;
		}

		/// <summary>
		/// スコアを加算する。0 以下は数えるだけで足さない
		/// </summary>
		void AddScore(int a_score)
		{
			++killCount;

			if (a_score <= 0) return;
			score += a_score;
		}
	};
}
