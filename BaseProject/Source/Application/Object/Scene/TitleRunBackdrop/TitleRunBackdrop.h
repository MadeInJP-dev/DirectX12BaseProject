#pragma once

#include "Engine/GameObject/BaseObject/BaseObject.h"

namespace App::Object
{
	/// <summary>
	/// タイトルの背景のカメラの映し方。並び順に切り替わる
	/// </summary>
	enum class ETitleShot : int
	{
		Side = 0,	// 機体の横を一緒に滑る(角度をゆっくり揺らす)
		Shoulder,	// 機体の肩越しに前を見る
		Flyby,		// 先回りした定点から、飛んでくる機体を首を振って追う
		Count,
	};

	//======================================================================================
	// タイトルの背景 : プレイヤーを地面すれすれでチャージダッシュさせ続ける
	//
	// 背景を絵1枚ではなく、機体が砂の地面の上を突っ込み続ける画にするためのもの。
	// タイトルで読み込みに時間を掛けないよう、ステージの地形は丸ごと使わず、
	// 地面のタイルを数枚だけ親にまとめて置き、機体の足元へ付いて来させて「どこまでも続く地面」に見せる。
	//
	// ・機体は同じシーンの PlayerSpawner が出したもの(入力で動かない設定で出す)。
	//   ここは毎フレーム、その機体のチャージダッシュを「出ている」状態に保ち、進む向き(dashDir)を書く。
	//   移動・姿勢(前のめり・空中扱い)・噴射の演出・音はゲーム中と同じシステムがそのまま動かす。
	//   ダッシュは燃料が尽きると止まるので、満タンに戻し続ける。
	//   チャージダッシュを持たない機体なら、ブーストの押しっぱなしで走らせる。
	// ・真っすぐ走らせ続けると原点から離れ続け、座標の精度が落ちて画が震えてくる。
	//   視線(LookAngle)の Yaw を少しずつ回して大きな円を描かせ、原点のまわりに留める。
	//   ダッシュは発動時の向きへ直進する作りなので、進む向き(dashDir)も同じ向きへ書き直す。
	// ・地面は模様の周期(タイル1枚の大きさ)の倍数で足元へ寄せる(模様がずれて見えない)。
	//   地面の当たり判定は静的なボディなので、寄せたときだけ物理空間の位置も合わせる
	//   (地面の親の子も含めて)。
	// ・カメラは ETitleShot の順に、一定の時間ごとに切り替える(カット)。
	//   書くのは GameObject の更新(ECS の後)なので、反映は次のフレームの行列計算。
	//======================================================================================
	class TitleRunBackdrop final : public Engine::GameObject::BaseObject
	{
	public:

		// 更新処理 : 機体を走らせ、地面を足元へ寄せ、カメラを置く
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "TitleRunBackdrop"; }

		// インスペクター : 機体・地面・カメラと、走り方・映し方
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		// 機体をチャージダッシュ(無ければブースト)で走らせ、視線を回す
		void DriveRunner(Engine::GameObject::ObjectContext& a_context, Engine::ECS::Entity a_runner);

		// 地面を機体の足元へ寄せる
		void FollowFloor(Engine::GameObject::ObjectContext& a_context, Engine::ECS::Entity a_runner);

		// 地面(と子)の当たり判定を今の位置へ合わせる。ボディがまだ無ければ false
		bool SyncFloorBodies(Engine::ECS::World& a_world, Engine::ECS::Entity a_floor) const;

		// 映し方を時間で切り替え、カメラを置く
		void PlaceCamera(Engine::GameObject::ObjectContext& a_context, Engine::ECS::Entity a_runner);

		// 映し方ごとのカメラの位置と見る点
		void CalcSideShot(const Math::Vector3& a_runnerPos, Math::Vector3& a_outPos, Math::Vector3& a_outLookAt) const;
		void CalcShoulderShot(const Math::Vector3& a_runnerPos, Math::Vector3& a_outPos, Math::Vector3& a_outLookAt) const;

		// 定点の置き場所を決める : 機体の m_flybyLeadTime 秒先の位置の横
		Math::Vector3 PredictFlybyPos(Engine::ECS::World& a_world, Engine::ECS::Entity a_runner) const;

		// 視線を回す速さ(度/秒)。a_time の時点のもの
		float CalcTurnRate(float a_time) const;

		// 機体の今の速さ(m/秒)。ダッシュ中ならダッシュの速さ
		float CalcRunSpeed(Engine::ECS::World& a_world, Engine::ECS::Entity a_runner) const;

		// 映し方の長さ(秒)
		float GetShotDuration(ETitleShot a_shot) const;

		// 走らせる機体(スポナーが出したもの)。まだ出ていなければ INVALID
		Engine::ECS::Entity FindRunner(Engine::GameObject::ObjectContext& a_context) const;

		// エンティティを番号で選び、GUID で持つ欄
		static void DrawEntityGUIDField(Engine::ECS::World& a_world, const char* a_label, Core::GUID& a_inoutGUID);

	private:

		//-------------------------------------------------------------------
		// 設定(保存される)
		//-------------------------------------------------------------------
		Core::GUID m_spawnerGUID = Core::DEFAULT_GUID;	// 機体を出す PlayerSpawner(同じシーンの GameObject)
		Core::GUID m_floorGUID   = Core::DEFAULT_GUID;	// 足元へ寄せる地面(エンティティ。子のタイルも一緒に動く)
		Core::GUID m_cameraGUID  = Core::DEFAULT_GUID;	// 機体を映すカメラ(エンティティ)

		// 地面を寄せる刻み(m)。タイルを並べた間隔の倍数にする。
		// 砂のタイルは 1 枚 200m を 199m 間隔で 1m 重ねて並べる(継ぎ目のすき間から空が見えないように。デザート00 と同じ)
		float m_floorSnap = 199.0f;

		// 走り方 : 回る速さ(度/秒)。平均がこれで、円の半径は 速さ / 回る速さ で決まる
		float m_turnRate       = 4.0f;
		float m_turnSwayRate   = 10.0f;	// 回る速さを揺らす幅(度/秒)。S 字の強さ
		float m_turnSwayPeriod = 9.0f;	// 揺らす周期(秒)

		// 映し方の共通 : 進む向きへ追従する速さ(大きいほど遅れない)
		float m_cameraFollowRate = 3.0f;

		// 横 : 角度は 0 = 真後ろ、90 = 右、180 = 正面
		float m_sideDuration    = 6.0f;	// この映し方を続ける秒数
		float m_sideAngle       = 140.0f;
		float m_sideSwayAngle   = 25.0f;	// 角度を揺らす幅(度)
		float m_sideSwayPeriod  = 16.0f;	// 揺らす周期(秒)
		float m_sideDistance    = 15.0f;	// 機体からの水平距離(m)
		float m_sideHeight      = 6.0f;	// 足元からの高さ(m)
		float m_sideLookHeight  = 5.4f;	// 見る点の足元からの高さ(m)

		// 肩越し : 機体の後ろ・右・上へずらした位置から、前の遠くを見る
		float m_shoulderDuration   = 5.0f;
		float m_shoulderBack       = 11.0f;	// 後ろへ(m)
		float m_shoulderRight      = 3.0f;	// 右へ(m)
		float m_shoulderHeight     = 6.5f;	// 足元からの高さ(m)
		float m_shoulderLookAhead  = 80.0f;	// 前のどこを見るか(m)
		float m_shoulderLookHeight = 3.0f;	// 見る点の足元からの高さ(m)

		// 定点 : 機体の少し先の進路の横で待ち、通り過ぎるまで首を振って追う
		float m_flybyDuration   = 4.0f;
		float m_flybyLeadTime   = 1.6f;	// 機体が目の前を通るまでの秒数(どれだけ先回りするか)
		float m_flybySide       = 14.0f;	// 進路から横へ離す距離(m)。切り替えるたびに左右を入れ替える
		float m_flybyHeight     = 2.5f;	// 足元からの高さ(m)
		float m_flybyLookHeight = 2.8f;	// 機体のどの高さを見るか(m)
		float m_flybyTrackRate  = 8.0f;	// 首を振る速さ(大きいほど遅れない)

		//-------------------------------------------------------------------
		// 状態(保存しない)
		//-------------------------------------------------------------------
		float m_time = 0.0f;				// 揺らしに使う経過時間
		float m_cameraYaw = 0.0f;			// カメラの基準にしている進む向き(度)
		bool  m_isCameraYawReady = false;	// 最初のフレームで進む向きへ合わせたか

		ETitleShot m_shot = ETitleShot::Side;	// 今の映し方
		float m_shotTimer = 0.0f;				// 今の映し方を続けている秒数
		bool  m_isShotStarted = false;			// 今の映し方の最初のフレームを済ませたか

		// 定点の位置と、今向いている向き
		Math::Vector3 m_flybyPos = {};
		Math::Quaternion m_flybyQuat = {};
		float m_flybySign = 1.0f;			// 進路のどちら側で待つか(+1 = 右)

		// 地面を最後に寄せた位置(物理空間の位置を合わせ直すかの判定)
		Math::Vector3 m_floorPos = {};
		bool m_isFloorPlaced = false;
	};
}
