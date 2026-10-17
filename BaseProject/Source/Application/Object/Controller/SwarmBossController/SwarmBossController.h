#pragma once

#include "Application/Object/Sequence/SceneSequence/SequenceActor.h"

#include "SwarmBossStates/StateMachine.h"

#include "Application/Components/Boss/SwarmBossWave.h"

namespace App::Object
{
	/// <summary>
	/// 砂漠ステージのワームボスの管理用クラス
	///
	/// リーダー、小隊長を動かして制御するがボイドは各自に任せる
	/// </summary>
	/// <remarks>
	/// 生成の流れ : オブジェクト生成 → リーダー → 小隊長(最大数ぶん) → 各小隊長のボイド
	///
	/// ・リーダーと小隊長はエンティティIDを握りたいので即時生成で出す
	///   (小隊長に「一つ前の相手」を覚えさせるため。遅延生成だとIDが返らない)。
	///   Awake は ECS の反復の外で呼ばれるので、その場で CreateEntity してよい。
	/// ・移動に要るコンポーネントと役割の印は、プレハブに無ければ生成時に足す。
	/// ・ボイドには SpawnerComponent(このオブジェクトのGUID + 小隊番号)を付ける。
	///   生存数はその印を数えるだけで、ボイドのIDは持ち歩かない。
	/// ・出した一式には TransientTag が付く(BuildSpawnInstanceData)ので、シーンには保存されない。
	///   生成後にシーンを保存しても、読み直したときに一式が重ならない。
	/// ・SceneSequence のウェーブに載せると、そのウェーブが来るまで出ない(SequenceActor)。
	///   載せていなければ今までどおり Awake で出る。
	/// ・地面から出す設定(m_isEmergeOnSpawn)なら、生成位置の真下へ縦一列に埋めて出し、
	///   出現の行動(Emerge)から始める。真上へ飛び出して、とぐろを巻きながら鳴く。
	/// </remarks>
	class SwarmBossController final : public SequenceActor
	{
	public:
		// ステートは調整値を読み込みで受けるので、Archive より前にここで登録しておく
		SwarmBossController() { m_stateMachine.Init(); }

		//------------------------------------------------------------------------------------------
		// 初期化
		//------------------------------------------------------------------------------------------
		void PostDeserialize(Engine::GameObject::ObjectContext& a_context) override;
		void Awake(Engine::GameObject::ObjectContext& a_context) override;
		void Start(Engine::GameObject::ObjectContext& a_context) override;

		// 借りた鳴き声を返し、画面の震えを止める
		void Release(Engine::GameObject::ObjectContext& a_context) override;

		//------------------------------------------------------------------------------------------
		// 更新
		//------------------------------------------------------------------------------------------
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		//------------------------------------------------------------------------------------------
		// シリアライズ / エディター
		//------------------------------------------------------------------------------------------
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		const char* GetEditorName() const override { return "SwarmBossController"; }
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

		//------------------------------------------------------------------------------------------
		// ボスの体力 : 生きているボイドの数
		//
		// 毎フレーム印(SwarmBossBoidTag)を数え直した結果。
		// 体力ゲージのように外から見たい側はここから引く(m_currentBoids を直接触らせない)
		//------------------------------------------------------------------------------------------
		//------------------------------------------------------------------------------------------
		// シーケンスからの呼び出し(SequenceActor)
		//------------------------------------------------------------------------------------------
		void HoldAppear() override { m_isHoldAppear = true; }
		bool Appear(Engine::GameObject::ObjectContext& a_context) override { return Spawn(a_context); }

		// 出ていて、爆散しておらず、体(ボイド)が残っている。
		// 出した直後は体の実体化を待つ間 0 体に見えるので、一度でも数えるまでは生きている扱い
		bool IsAlive() const override
		{
			if (!m_isSpown || m_isBurst) return false;
			return !m_isBodyCounted || m_currentBoids > 0;
		}

		Math::Vector3 GetAppearPos() const override { return m_spawnPos; }

		uint32_t GetHealth() const { return m_currentBoids; }
		uint32_t GetMaxHealth() const { return m_maxBoid; }
		float GetHealthRate() const
		{
			return (m_maxBoid > 0)
				? static_cast<float>(m_currentBoids) / static_cast<float>(m_maxBoid)
				: 0.0f;
		}

	private:
		//------------------------------------------------------------------------------------------
		// 生成
		//------------------------------------------------------------------------------------------
		// リーダー → 小隊長 → ボイドの順にまとめて出す。出し済みなら何もしない
		bool Spawn(Engine::GameObject::ObjectContext& a_context);

		bool CreateLeader(Engine::GameObject::ObjectContext& a_context);
		bool CreatePlatoonLeaders(Engine::GameObject::ObjectContext& a_context);

		// 小隊長1体ぶんのボイドを、その小隊長の BoidSpownerComponent の設定で出す
		bool CreateBoids(
			Engine::GameObject::ObjectContext& a_context,
			Engine::ECS::Entity a_platoonLeader,
			int a_platoonIndex,
			uint32_t a_count,
			const Math::Vector3& a_center);

		// 小隊長 a_platoonIndex 番に振り分けるボイド数(最大数を小隊長の数で割り、余りは前から1体ずつ)
		uint32_t GetBoidCountForPlatoon(size_t a_platoonIndex, size_t a_platoonCount) const;

		// 自分の印(SwarmBossBoidTag)が付いた生存ボイドを数える。これがボスの体力
		uint32_t CountAliveBoids(Engine::GameObject::ObjectContext& a_context) const;

		//------------------------------------------------------------------------------------------
		// リーダーの行動(このクラスが脳。作るのは入力だけ)
		//------------------------------------------------------------------------------------------
		// ステートマシンを1フレームぶん回す。中身の行動は SwarmBossStates の各ステート。
		// 入力を速度に変えるのは SwarmLeaderMoveSystem、座標を進めるのは MovementIntegrationSystem
		void UpdateLeaderBrain(Engine::GameObject::ObjectContext& a_context);

		//------------------------------------------------------------------------------------------
		// 小隊長の整理(体力が一定量減るたび)
		//------------------------------------------------------------------------------------------
		// 体力が次の区切りを切っていたら、今の行動を打ち切って整理の行動(Reorganize)へ切り替える
		void CheckReorganize();

		// 小隊長を「最大数 × 今の体力の比率」まで尾から減らし、ボイドを残った小隊長へ割り当て直す。
		// 整理の行動が頼んできたとき(isRequestReorganize)に呼ぶ
		void ReorganizePlatoons(Engine::GameObject::ObjectContext& a_context);

		//------------------------------------------------------------------------------------------
		// 死亡(体力が一定値を切ったら)
		//------------------------------------------------------------------------------------------
		// 体力が Death Hp 以下になったら、今の行動を打ち切って死亡の行動(Death)へ切り替える(一度だけ)。
		// 爆散の瞬間に読み込みが走らないよう、中心の爆発エフェクトの読み込みもここで始める
		void CheckDeath(Engine::GameObject::ObjectContext& a_context);

		// 中心の爆発エフェクトの読み込みを始める(済んでいれば何もしない)
		void RequestLoadBurstEffect(Engine::GameObject::ObjectContext& a_context);

		// 体のボイドを中心から外へ飛び散らせる(落とすのは SwarmBurstSystem)。
		// リーダーと小隊長はもう要らないので消す。死亡の行動が頼んできたとき(burst)に呼ぶ
		void BurstBody(Engine::GameObject::ObjectContext& a_context, const SwarmBossBurstRequest& a_request);

		// 体(ボイド)の防御比率を書き換える。行動からの依頼が変わったときだけ呼ぶ
		// (4000体を走査するので毎フレームは呼ばない)。切り離したミサイルは 1 のまま
		void ApplyBodyDefense(Engine::GameObject::ObjectContext& a_context, float a_ratio);

		//------------------------------------------------------------------------------------------
		// 体を走る発光のウェーブ
		//------------------------------------------------------------------------------------------
		// ウェーブを進め、周期が来たら頭から新しく出す。結果は WormWaveResource へ書き写す。
		// 4000体への書き込みは BoidWaveSystem(ECS側)が受け持つ
		void UpdateWave(Engine::GameObject::ObjectContext& a_context);

		// 頭から尾までの長さ(1次元)。ウェーブを消す位置に使う
		float GetWormLength() const;

		//------------------------------------------------------------------------------------------
		// 地面の近く・地面の中で炊く砂埃
		//------------------------------------------------------------------------------------------
		// 調整値を WormGroundEffectResource へ書き写す。レイを打って炊くのは BoidGroundEffectSystem
		void UpdateGroundEffect(Engine::GameObject::ObjectContext& a_context);

		// リーダーが地面に潜った / 地面から出た瞬間に、地表へ大きな砂埃(エフェクトプレハブ)を炊く
		void UpdateBurrowEffect(Engine::GameObject::ObjectContext& a_context);

		//------------------------------------------------------------------------------------------
		// 体当たりのダメージ
		//------------------------------------------------------------------------------------------
		// 調整値とプレイヤーのカプセルを SwarmContactDamageResource へ書く。
		// 触れたかを見てダメージを積むのは BoidContactDamageSystem
		void UpdateContactDamage(Engine::GameObject::ObjectContext& a_context);

		//------------------------------------------------------------------------------------------
		// 咆哮(鳴き声と画面の震え)
		//------------------------------------------------------------------------------------------
		// 鳴き声を鳴らす。行動が頼んできたとき(isRequestRoar)に呼ぶ
		void PlayRoar(Engine::GameObject::ObjectContext& a_context);

		// 借りている鳴き声を返す(鳴っていれば止まる)
		void ReleaseRoarSound(Engine::GameObject::ObjectContext& a_context);

		// 画面の震えの強さと調整値を RadialBlurShakeResource へ書く。震わせるのは RadialBlurShakeSystem
		void UpdateRoarBlur(Engine::GameObject::ObjectContext& a_context);

	private:
		// 生成されたかどうか
		bool m_isSpown = false;

		// シーケンスに呼ばれるまで出さないか(保存しない。読み込みのたびにシーケンスが立てる)
		bool m_isHoldAppear = false;

		// 体(ボイド)を一度でも数えたか。出した直後の 0 体を倒された扱いにしないため
		bool m_isBodyCounted = false;

		//------------------------------------------------------------------------------------------
		// ボスの構成要素
		//------------------------------------------------------------------------------------------
		// 先頭のリーダ : このクラスから指示を出す対象
		Core::GUID m_leaderPrefabGUID = {};								// 保存用
		Engine::ResourceRef<Engine::Resource::Prefab> m_leaderPrefabHandle;	// ランタイム用
		Engine::ECS::Entity m_leaderEntity = Engine::ECS::Limits::INVALID_ENTITY;
		Math::Vector3 m_spawnPos = {};										// リーダーの生成位置(ワールド)

		// 地面から出す : 生成位置から m_emergeDepth 下に頭を置き、体をその下へ縦一列に埋める。
		// 列は真下ではなく、体が向いている向き(Pitch は上限まで上げ、Yaw はプレイヤーの方)の真後ろ。
		// 小隊長は「前の相手の真後ろ」を追うので、真下にすると上限の傾きのぶん最初に横へずれるため
		bool m_isEmergeOnSpawn = true;		// 地面から出すか(false なら今までどおり生成位置に横一列で出す)
		float m_emergeDepth    = 60.0f;		// 生成位置から頭を埋める深さ(m)

		// ---- 実行中の状態(保存しない) ----
		float m_spawnYaw = 0.0f;			// 生成したときに向けた Yaw(度)

		// 構成する小隊長 : 基本的にリーダーに追従する処理はECS側
		// 並びはリーダーの後ろ(-Z)へ一列。間隔は小隊長プレハブの PlatoonLeaderComponent.distance
		Core::GUID m_platoonPrefabGUID = {};							// 小隊長のプレハブ(保存用)
		Engine::ResourceRef<Engine::Resource::Prefab> m_platoonPrefab;	// 小隊長のプレハブ(ランタイム用)
		std::vector<Engine::ECS::Entity> m_platoonLeaderEntities = {};	// 生存している小隊長
		uint32_t m_maxPlatoonLeader = 100;								// 最大小隊長数

		// 自身の体を構成しているボイド数 : タグをつけて収集 操作などはしない
		// 出すボイドのプレハブと広さは小隊長プレハブの BoidSpownerComponent が持つ
		uint32_t m_currentBoids = 0;									// 残りの生存数 : HP代わり
		uint32_t m_maxBoid = 4000;											// 最大生成数 : 小隊長の数で割って振り分ける

		//------------------------------------------------------------------------------------------
		// ボイドの当たり判定と体力
		//
		// レイヤーは Layer::Enemy。当たるのはプレイヤーの攻撃だけで、地形などはすり抜ける
		// (地面に潜るアッパー攻撃で、体が地表へ押し出されないように)。
		// 体力を持たせているのは、撃たれた1体が死んで印が1つ減る = ボスの体力が減る
		// という流れにするため。
		//------------------------------------------------------------------------------------------
		float m_boidColliderRadius = 1.0f;	// ボイドの判定半径(m)
		float m_boidHealth         = 10.0f;	// ボイド1体の体力(弾1発で落としたいなら弾のダメージ以下にする)
		float m_boidReleaseDelay   = 0.5f;	// 落ちてから消えるまでの猶予(秒。死亡演出の尺)

		// 体当たり : ボイド1体ずつがプレイヤーに触れたらダメージを与える(BoidContactDamageSystem)。
		// 触れる距離はボイドの判定半径(m_boidColliderRadius) + プレイヤーのカプセルの半径。
		// 体が丸ごと通り抜けると200体ほどが触れるので、1体ぶんは小さめにしてある
		float m_contactDamage         = 5.0f;	// ボイド1体が1回触れたときのダメージ
		float m_contactDamageCooldown = 1.0f;	// ダメージを与えたボイドが、次に判定を始めるまでの時間(秒)

		//------------------------------------------------------------------------------------------
		// 群れの速さ
		//
		// 追従できるかは「後ろほど速いか」で決まるので、1か所で配分を決める。
		// 生成時に各エンティティへ流し込む(プレハブの値より優先する)。
		//   リーダー  … MovementParamsComponent.moveSpeed
		//   小隊長    … MovementParamsComponent.moveSpeed(リーダー × platoonSpeedScale)
		//   ボイド    … BoidSteeringParamsComponent.maxSpeed(リーダー × boidSpeedScale)
		//
		// 既定値はプレイヤー(歩き 25 / ブースト 50 / チャージダッシュ 90 m/秒)に合わせてある。
		//   徘徊   … 35 m/秒。歩きよりは速く、ブーストなら振り切れる
		//   攻撃   … 突進・地中移動 ×1.8 = 63、突き上げ・急降下 ×2.0 = 70 m/秒。
		//            ブーストでは逃げ切れず、チャージダッシュなら避けられる
		//   小隊長 … 攻撃の最大倍率(2.0)より上の ×2.2 にしてある。下回ると攻撃中に列が千切れる
		//------------------------------------------------------------------------------------------
		float m_leaderSpeed       = 35.0f;	// リーダーの移動速度(units/秒)
		float m_platoonSpeedScale = 2.2f;	// 小隊長の速さ(リーダーに対する倍率。1未満だと離される)
		float m_boidSpeedScale    = 3.0f;	// ボイドの速さ(リーダーに対する倍率)

		//------------------------------------------------------------------------------------------
		// ボスの行動
		//
		// 「どう動くか」はステートごとに分けて足していく(調整値も各ステートが持つ)。
		// 動かす側はECSに任せる
		//------------------------------------------------------------------------------------------
		SwarmBossStateMachine m_stateMachine;

		//------------------------------------------------------------------------------------------
		// 小隊長の整理
		//
		// 体力(生存ボイド数)が m_reorganizeHpInterval 減るたびに、球体状にまとまって
		// 小隊長を「最大数 × 今の体力 / 最大体力」まで尾から減らし、ボイドを割り当て直す。
		// まとまる動きと無敵の依頼は整理の行動(Reorganize)、体を作り変えるのはこのクラス
		//------------------------------------------------------------------------------------------
		uint32_t m_reorganizeHpInterval = 1000;	// 何体減るたびに整理するか(0で整理しない)

		// ---- 実行中の状態(保存しない) ----
		uint32_t m_reorganizeCount = 0;			// 越えた区切りの数(整理を始めた回数)
		float m_bodyDefenseRatio = 1.0f;		// 体(ボイド)に今入れてある防御比率

		//------------------------------------------------------------------------------------------
		// 死亡
		//
		// 体力が m_deathHp 以下になったら、地上の高いところで球体状にまとまり、
		// ウェーブを速めていって最後に全身が爆散する。動きとウェーブの倍率は死亡の行動(Death)、
		// ウェーブを走らせる・体を爆散させるのはこのクラス
		//------------------------------------------------------------------------------------------
		uint32_t m_deathHp = 1000;				// 体力がこれ以下になったら死亡へ(0で死亡の行動に入らない)

		// 爆散の瞬間に球の中心へ炊く大きな爆発(EffectAsset。出し切って消えるもの)。
		// 既定は Asset/Effect/Explosion/Explosion_Enemy_02(ボイド1体ずつの死亡は 01)
		Core::GUID m_burstEffectGUID = Core::GUID("c4a70b39-8e21-4d6c-95f8-3b0e7a1d24f5");
		Engine::ResourceRef<Engine::Resource::EffectAsset> m_burstEffectRef = {};	// 読み込んだままにしておく
		float m_burstEffectScale = 10.0f;		// 爆発の大きさ倍率(アセットのままの大きさに掛ける)

		// ---- 実行中の状態(保存しない) ----
		bool m_isDying = false;					// 死亡の行動へ入ったか
		bool m_isBurst = false;					// 爆散したか(以降はリーダーの行動を回さない)
		float m_waveSpeedScale = 1.0f;			// 行動から頼まれているウェーブの倍率

		//------------------------------------------------------------------------------------------
		// ウェーブ
		//
		// 先頭から順にブルームを炊いて光のウェーブにする
		// 各小隊長には一次元でワーム上の距離を持たせてボイドは小隊長との距離を求めて自身の場所を把握する
		//
		// ここは出す側(周期で新しく出し、尾へ進め、抜けたら捨てる)。
		// 4000体の発光を実際に書き換えるのは BoidWaveSystem。
		// 間に WormWaveResource を挟んでいるのは、オブジェクト側で ECS を全走査すると
		// チャンク単位で回れず、体数ぶんそのまま重くなるため
		//------------------------------------------------------------------------------------------
		float m_waveSpeed    = 150.0f;	// ウェーブが尾へ進む速さ(m/秒)
		float m_waveInterval = 0.8f;	// 新しいウェーブを出す周期(秒)
		float m_waveWidth    = 15.0f;	// 帯の幅(m)。ウェーブからこの距離でベース値に戻る
		uint32_t m_maxWave   = 8;		// 同時に走らせる本数の上限(周期が短いと並ぶ)

		float m_waveBaseIntensity = 0.5f;	// ウェーブが来ていないときの発光の強さ
		float m_wavePeakIntensity = 8.0f;	// ウェーブの中心での発光の強さ

		Math::Vector3 m_waveBaseColor = { 1.0f, 0.3f, 0.1f };	// ベースの色(0〜1)
		Math::Vector3 m_wavePeakColor = { 1.0f, 1.0f, 0.9f };	// ピークの色(0〜1)

		// ---- 実行中の状態(保存しない) ----
		std::vector<Component::SwarmBossWave> m_waveVec = {};	// 走っているウェーブ(位置と速さ)
		float m_waveTimer = 0.0f;					// 次に出すまでの残り時間(秒)
		float m_tailAlongWorm = 0.0f;				// 最後尾の小隊長の1次元位置(生成時に決まる)

		//------------------------------------------------------------------------------------------
		// 砂埃
		//
		// ボイドの真下に地面が近ければ、近いほど大きく炊く。地面の中なら真上の地表へ常に炊く。
		// ここは調整値を持つだけで、4000体ぶんのレイとエフェクトの生成は BoidGroundEffectSystem。
		// 1回炊くたびにエフェクトのエンティティが1体増えるので、間隔と1フレームの上限で数を抑える
		//------------------------------------------------------------------------------------------
		// 炊くエフェクト(保存用。単発で消えるもの)。既定は Asset/Effect/Dast/Worm_GroundDust
		Core::GUID m_groundEffectGUID = Core::GUID("a1a7bdfe-2da7-4767-8d22-55844f0a0115");
		Engine::ResourceRef<Engine::Resource::EffectAsset> m_groundEffectRef = {};		// 読み込んだままにしておく(炊くたびに読み直さない)

		float m_groundEffectMaxHeight  = 20.0f;		// 地面からこの高さまでのボイドが炊く(m)
		float m_groundEffectMaxDepth   = 150.0f;	// 地表からこの深さまでのボイドが炊く(m)
		float m_groundEffectNearScale  = 1.0f;		// 地表すれすれでの大きさ
		float m_groundEffectFarScale   = 0.3f;		// 炊く高さぎりぎりでの大きさ
		float m_groundEffectUnderScale = 1.0f;		// 地面の中に居るときの大きさ
		float m_groundEffectInterval   = 1.0f;		// 1体が炊く間隔(秒)
		uint32_t m_groundEffectMaxSpawnPerFrame = 8;	// 1フレームに出す上限

		// ---- 実行中の状態(保存しない) ----
		bool m_isGroundEffectOneShot = false;	// 読み込み済みで、出し切って消える単発のものか(違えば炊かない)

		//------------------------------------------------------------------------------------------
		// 潜る / 出るときの大きな砂埃(リーダーだけ)
		//
		// リーダーの SerchGroundComponent が「地上 ↔ 地中」で切り替わった瞬間に、
		// 真上(真下)の地表へエフェクトプレハブを炊く。潜るときも出るときも同じものを使う。
		// エフェクトプレハブは時間で必ず消えるので、ここは炊くだけで後片付けは要らない。
		// 地表すれすれを泳ぐと切り替わりが続くので、間隔(cooldown)を空ける
		//------------------------------------------------------------------------------------------
		// 炊くエフェクトプレハブ(保存用)。既定は Asset/EffectPrefab/Worm/Worm_BurrowBurst
		Core::GUID m_burrowEffectGUID = Core::GUID("3545c827-95ef-4b39-b3e1-ae2f11dd494d");
		Engine::ResourceRef<Engine::Resource::EffectPrefab> m_burrowEffectRef = {};	// 読み込んだままにしておく

		float m_burrowEffectCooldown = 1.0f;	// 次に炊けるまでの間隔(秒)

		// ---- 実行中の状態(保存しない) ----
		float m_burrowEffectTimer = 0.0f;		// 次に炊けるまでの残り時間(秒)
		bool m_isLeaderGroundKnown = false;		// 地面との関係を1度でも見たか(最初のフレームで炊かないため)
		bool m_wasLeaderUnderGround = false;	// 前に見たときに地中だったか

		//------------------------------------------------------------------------------------------
		// 咆哮(出現の行動などが頼む)
		//
		// 鳴き声は画面に出す音として 2D で鳴らす(どこに居ても同じ大きさで聞こえる)。
		// 画面の震えは RadialBlurShakeResource へ強さと調整値を書くだけで、
		// カメラのラジアルブラーへ足すのは RadialBlurShakeSystem。
		// 調整値はここ、いつ・どれだけ鳴くかは行動が持つ。
		//------------------------------------------------------------------------------------------
		// 鳴き声(Sound)。既定は Asset/Sound/Boss/VoidWorm_Roar(環境/gen_worm_roar.py で作ったもの)
		Core::GUID m_roarSoundGUID = Core::GUID("ce80f545-bef9-4dde-a491-18edc25cea0d");
		float m_roarVolume = 1.0f;					// 鳴き声の音量(1 で 100%)

		float m_roarBlurStrength       = 0.15f;		// 震えの引きずり量(UV単位。強さ 1 のとき)
		float m_roarBlurStrengthJitter = 0.6f;		// 引きずり量の揺れ幅(引きずり量に対する割合。0〜1)
		float m_roarBlurCenterJitter   = 0.02f;		// ブラーの中心を揺らす幅(UV単位)
		float m_roarBlurFrequency      = 18.0f;		// 揺れの速さ(回/秒)

		// ---- 実行中の状態(保存しない) ----
		Engine::Handle<Engine::Resource::SoundInstance> m_roarSoundHandle = {};	// 借りている鳴き声
		float m_roarWeight = 0.0f;				// 行動から頼まれている震えの強さ(0〜1)
	};
}
