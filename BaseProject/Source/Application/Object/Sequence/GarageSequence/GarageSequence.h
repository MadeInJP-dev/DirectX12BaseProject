#pragma once

#include "Engine/GameObject/BaseObject/BaseObject.h"

#include "Application/Components/Attachment/WeaponMountComponent.h"
#include "../../SequenceBgm.h"

namespace App::Object
{
	/// <summary>
	/// ガレージで今どこを見ているか
	/// </summary>
	enum class EGarageMode : int
	{
		Overview = 0,	// 機体の全体 : 取り付け口から引き出し線を出し、付いている武器を箱で見せる
		Zoom,			// 1つの取り付け口へ寄った : 右に武器、左に武器の一覧
	};

	/// <summary>
	/// 武器の性能(一覧で選んでいる武器の横に出す)。プレハブの中身から引く
	/// </summary>
	struct GarageWeaponStats
	{
		static constexpr size_t ROW_COUNT = 5;	// 出す項目の数

		bool isValid = false;
		std::string mode = "";					// 撃ち方("FULL AUTO" など)
		// 項目ごとの値(棒の長さの元)と、出す文字。並びは手と肩で別(GarageSequence::GetStatLabel)
		std::array<float, ROW_COUNT> valueArray = {};
		std::array<std::string, ROW_COUNT> textArray = {};
	};

	/// <summary>
	/// 構えの1本ぶん(ガレージの機体に足すポーズ)。中身は StancePoseComponent へ流し込む
	/// </summary>
	struct GarageStanceBone
	{
		std::string nodeName = "";		// 回すノードの名前
		Math::Vector3 rotationDeg = {};	// モデル空間の回転(度)。X → Y → Z の順
		Math::Vector3 offset = {};		// 回した後にずらす量(モデル空間・m)
	};

	/// <summary>
	/// 武器の一覧の1件(ガレージで選べる武器)
	/// </summary>
	struct GarageWeaponEntry
	{
		Core::GUID prefabGUID = {};		// 武器のプレハブ
		std::string name = "Weapon";	// 出す名前
		Core::GUID iconGUID = {};		// 一覧と箱に出す絵(斜めから撮った武器のスクリーンショット)

		// ---- 実行中(保存しない) ----
		// 手に付くか肩に付くか。プレハブの中身(WeaponState / MissileState)から引く
		bool isHand = false;
		bool isShoulder = false;
		Engine::ResourceRef<Engine::Resource::Prefab> prefabRef = {};
		GarageWeaponStats stats = {};
	};

	//======================================================================================
	// ガレージの進行役 : ガレージのシーンに一つ置く
	//
	// ホームの GARAGE ボタンから来る画面。出撃構成(GlobalGameContext::loadout)の武器を見て、付け替える。
	//
	//   全体 : 機体(PlayerSpawner が出撃構成どおりに出す)の4つの取り付け口から引き出し線を出し、
	//          付いている武器を箱(画像と名前)で見せる。箱を押すとその取り付け口へ寄る
	//   寄り : カメラがその武器へ寄り、武器を画面の右側に映す。左には武器の一覧を出し、
	//          押した武器にその場で付け替える(出撃構成も書き換える)
	//
	// ・箱・一覧の項目・戻るボタンはシーンへ置いた UIButton を GUID で引く(HomeSequence と同じ)。
	//   見た目はシーン側で直せる。中身はここが飾りへ流し込む : 画像は "Icon"、名前は "Name"、
	//   取り付け口の名前は "Slot"(箱)/ 一覧の見出しは "Title"、今付いている印は "Bg" の色。
	// ・全体の箱と、寄りの一覧はそれぞれ入れ物の UI(UIElement)にまとめ、その入れ物の表示を切り替える。
	// ・寄りでは性能の板(UI 1枚)に、カーソルが乗っている武器(乗っていなければ今付いている武器)の
	//   性能を出す。飾り : "StatName"(武器名)/ "StatMode"(撃ち方)/ 項目ごとに
	//   "StatLabel0〜4"(項目名)/ "StatValue0〜4"(値)/ "StatBarBg0〜4"(棒の枠。この幅が満タン)/
	//   "StatBar0〜4"(棒。一覧の中の一番よい値を満タンにした長さ)/ "StatEquip0〜4"(今付いている武器の長さ)。
	// ・機体には構え(仁王立ちで銃を構える)を StancePoseComponent で足す。角度はここで持つ。
	// ・引き出し線は飾りではなく、ここが毎フレーム描く(端が機体の動きについていくため)。
	// ・カメラはシーンのカメラ(エンティティの GUID)を直接動かす。全体の構図は読み込んだときの位置と向き。
	// ・機体は入力で動かないよう、PlayerSpawner の Controllable を切って置くこと。
	//======================================================================================
	class GarageSequence : public Engine::GameObject::BaseObject
	{
	public:

		static constexpr size_t MOUNT_COUNT = 4;	// EWeaponMount の数

		// 開始処理 : 一覧の武器の種類を引く
		void Start(Engine::GameObject::ObjectContext& a_context) override;

		// 更新処理 : ボタンへの差し込み、カメラ、箱の中身
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// 描画処理 : 引き出し線
		void Draw(Engine::GameObject::ObjectContext& a_context) override;

		// 解放処理 : カーソル固定を設定値へ戻す
		void Release(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		const char* GetEditorName() const override { return "GarageSequence"; }

		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		//-------------------------------------------------------------------
		// 進行
		//-------------------------------------------------------------------

		// ボタンへ押下時の処理を差し込む
		void TryBindButtons(Engine::GameObject::ObjectContext& a_context);

		// 全体へ戻る / 取り付け口へ寄る
		void EnterOverview(Engine::GameObject::ObjectContext& a_context);
		void EnterZoom(Engine::GameObject::ObjectContext& a_context, Component::EWeaponMount a_mount);

		// 戻るボタン : 寄りなら全体へ、全体ならホームへ
		void OnBack(Engine::GameObject::ObjectContext& a_context);

		// 一覧の項目を押した : その武器に付け替える
		void OnSelectItem(Engine::GameObject::ObjectContext& a_context, size_t a_itemIndex);

		// 今のモードに合わせてパネルを出し分ける
		void ApplyVisible(Engine::GameObject::ObjectContext& a_context);

		//-------------------------------------------------------------------
		// 中身の流し込み
		//-------------------------------------------------------------------

		// 箱に今付いている武器を出す
		void RefreshBoxes(Engine::GameObject::ObjectContext& a_context);

		// 性能の板に、カーソルが乗っている武器(無ければ今付いている武器)を出す
		void RefreshStats(Engine::GameObject::ObjectContext& a_context);

		// 一覧に、寄っている取り付け口へ付けられる武器を並べる
		void RefreshList(Engine::GameObject::ObjectContext& a_context);

		// UI の飾り(名前で指す)へ画像 / 文字を入れる。無ければ何もしない
		void SetDecorationImage(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_uiGUID, const char* a_decoName, const Core::GUID& a_texGUID);
		void SetDecorationText(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_uiGUID, const char* a_decoName, const std::string& a_text);
		void SetDecorationAlpha(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_uiGUID, const char* a_decoName, float a_alpha);

		// 棒の飾り(a_barName)を、枠の飾り(a_bgName)の幅に対する割合の長さにする。左端は枠に揃える
		void SetDecorationBar(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_uiGUID, const std::string& a_barName, const std::string& a_bgName, float a_ratio);

		//-------------------------------------------------------------------
		// 性能
		//-------------------------------------------------------------------

		// プレハブの中身から性能を引く
		static GarageWeaponStats BuildStats(Engine::GameObject::ObjectContext& a_context, const GarageWeaponEntry& a_entry);

		// 一覧の中で一番よい値(棒の満タン)を、手と肩で別に出し直す
		void RebuildStatMax();

		// 項目名。手と肩で並びが違う
		static const char* GetStatLabel(bool a_isShoulder, size_t a_row);

		// 値が小さいほどよい項目か(肩の再装填)
		static bool IsLowerBetterStat(bool a_isShoulder, size_t a_row);

		// 棒の長さ(0〜1)
		float CalcStatRatio(bool a_isShoulder, size_t a_row, float a_value) const;

		//-------------------------------------------------------------------
		// 構え
		//-------------------------------------------------------------------

		// 出した機体に構え(StancePoseComponent)を付け、中身を書き直す
		void UpdateStance(Engine::GameObject::ObjectContext& a_context);

		//-------------------------------------------------------------------
		// 出撃構成
		//-------------------------------------------------------------------

		// 取り付け口に付いている武器のプレハブ(出撃構成から)
		Core::GUID GetLoadoutWeapon(Engine::GameObject::ObjectContext& a_context, Component::EWeaponMount a_mount) const;

		// 出撃構成の取り付け口の武器を書き換える
		void SetLoadoutWeapon(Engine::GameObject::ObjectContext& a_context, Component::EWeaponMount a_mount, const Core::GUID& a_prefabGUID);

		// 一覧の中からプレハブで引く(無ければ nullptr)
		const GarageWeaponEntry* FindEntry(const Core::GUID& a_prefabGUID) const;

		//-------------------------------------------------------------------
		// カメラと画面
		//-------------------------------------------------------------------

		// カメラを今のモードの構図へ寄せる
		void UpdateCamera(Engine::GameObject::ObjectContext& a_context);

		// 取り付け口のワールド座標。取れなければ false
		bool TryGetMountPos(Engine::GameObject::ObjectContext& a_context, Component::EWeaponMount a_mount, Math::Vector3& a_outPos) const;

		// ワールド座標 → スクリーン(px, 左上原点)。カメラの後ろなら false
		bool WorldToScreen(Engine::GameObject::ObjectContext& a_context, const Math::Vector3& a_world, Math::Vector2& a_outScreen) const;

		// 取り付け口の表示名
		static const char* GetMountLabel(Component::EWeaponMount a_mount);

		// 肩の取り付け口か
		static bool IsShoulderMount(Component::EWeaponMount a_mount);

	private:

		//-------------------------------------------------------------------
		// 設定(保存される)
		//-------------------------------------------------------------------
		Core::GUID m_spawnerGUID = {};		// 機体を出す PlayerSpawner(オブジェクトの GUID)
		Core::GUID m_cameraGUID = {};		// 動かすカメラ(エンティティの GUID)
		Core::GUID m_homeSceneGUID = {};	// 戻る先(ホームのシーン)

		Core::GUID m_backButtonGUID = {};	// 戻る(寄り → 全体、全体 → ホーム)
		Core::GUID m_overviewPanelGUID = {};	// 全体で出すもの(箱)をまとめた入れ物の UI
		Core::GUID m_listPanelGUID = {};		// 寄りで出すもの(一覧)をまとめた入れ物の UI

		// 取り付け口ごとの箱(UIButton)。EWeaponMount の順
		std::array<Core::GUID, MOUNT_COUNT> m_boxGUIDs = {};

		Core::GUID m_listTitleGUID = {};					// 一覧の見出し("Title" に取り付け口の名前を出す)
		std::vector<Core::GUID> m_listItemGUIDVec = {};		// 一覧の項目(UIButton)。並べる数の上限

		// 選べる武器
		std::vector<GarageWeaponEntry> m_catalogVec = {};

		// 性能の板(UI 1枚。寄りの一覧のパネルの下に置くと、寄りのときだけ出る)
		Core::GUID m_statPanelGUID = {};

		// 構え(仁王立ちで銃を構える)。機体の StancePoseComponent へ流し込む
		bool m_isStanceEnabled = true;
		std::vector<GarageStanceBone> m_stanceBoneVec = {};

		// 寄りの構図
		float m_zoomDistance = 9.0f;		// 取り付け口からカメラまで(m)
		float m_zoomScreenShift = 0.45f;	// 武器を画面中央から右へずらす量(画面の半分の幅に対する割合)
		float m_zoomLift = 1.0f;			// 見る位置を取り付け口から上へずらす量(m)
		float m_zoomHeight = 0.0f;			// カメラを見る位置より上げる量(m)。手の取り付け口用
		float m_shoulderZoomHeight = 1.5f;	// 同じく肩の取り付け口用(腕の武器に隠れないよう上から見下ろす)
		float m_cameraRate = 5.0f;			// カメラの寄り・引きの速さ(指数減衰の係数)

		// 引き出し線
		float m_lineWidth = 2.0f;			// 太さ(px)
		Math::Color m_lineColor = { 0.4f, 0.85f, 1.0f, 0.9f };

		// ホームの間と同じく、カーソルの中央固定を切るか
		bool m_isReleaseCursorLock = true;

		//-------------------------------------------------------------------
		// 状態(保存しない)
		//-------------------------------------------------------------------
		EGarageMode m_mode = EGarageMode::Overview;
		Component::EWeaponMount m_zoomMount = Component::EWeaponMount::RightHand;

		bool m_isBound = false;		// ボタンへ差し込み済みか
		bool m_isLeaving = false;	// ホームへ戻るのを頼んだか

		// 一覧の項目 → 武器(m_catalogVec の添え字)
		std::vector<size_t> m_listEntryIndexVec = {};

		// カメラ
		bool m_isCameraCaptured = false;	// 全体の構図(読み込んだときの位置と向き)を覚えたか
		Math::Vector3 m_overviewCamPos = {};
		Math::Quaternion m_overviewCamQuat = {};
		float m_overviewFocus = 10.0f;		// 全体のときのピントの距離
		Math::Vector3 m_camPos = {};
		Math::Quaternion m_camQuat = {};
		float m_camFocus = 10.0f;

		// 箱の中身を出し直すか(付け替えた・読み込んだ)
		bool m_isBoxDirty = true;

		// 性能の板に今出している武器(m_catalogVec の添え字)。SIZE_MAX なら何も出していない
		size_t m_statEntryIndex = SIZE_MAX;
		Core::GUID m_statEquipped = {};		// そのときに付いていた武器(付け替えたら出し直す)

		// 棒の満タン(一覧の中で一番よい値)。[0] が手、[1] が肩
		std::array<std::array<float, GarageWeaponStats::ROW_COUNT>, 2> m_statMaxArray = {};
		std::array<std::array<float, GarageWeaponStats::ROW_COUNT>, 2> m_statMinArray = {};

		// 構えを付けるのを頼んだ機体(付くのは次のフレームの頭)
		Engine::ECS::Entity m_stanceRequestedEntity = Engine::ECS::Limits::INVALID_ENTITY;

		//-------------------------------------------------------------------
		// BGM
		//-------------------------------------------------------------------
		SequenceBgm m_bgm = {};
	};
}
