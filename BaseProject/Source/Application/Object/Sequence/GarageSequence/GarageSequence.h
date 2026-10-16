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
	// ・全体の箱と、寄りの一覧はそれぞれ UIPanel にまとめ、そのパネルの表示を切り替える。
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

		// 一覧に、寄っている取り付け口へ付けられる武器を並べる
		void RefreshList(Engine::GameObject::ObjectContext& a_context);

		// UI の飾り(名前で指す)へ画像 / 文字を入れる。無ければ何もしない
		void SetDecorationImage(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_uiGUID, const char* a_decoName, const Core::GUID& a_texGUID);
		void SetDecorationText(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_uiGUID, const char* a_decoName, const std::string& a_text);
		void SetDecorationAlpha(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_uiGUID, const char* a_decoName, float a_alpha);

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
		Core::GUID m_overviewPanelGUID = {};	// 全体で出すもの(箱)をまとめた UIPanel
		Core::GUID m_listPanelGUID = {};		// 寄りで出すもの(一覧)をまとめた UIPanel

		// 取り付け口ごとの箱(UIButton)。EWeaponMount の順
		std::array<Core::GUID, MOUNT_COUNT> m_boxGUIDs = {};

		Core::GUID m_listTitleGUID = {};					// 一覧の見出し("Title" に取り付け口の名前を出す)
		std::vector<Core::GUID> m_listItemGUIDVec = {};		// 一覧の項目(UIButton)。並べる数の上限

		// 選べる武器
		std::vector<GarageWeaponEntry> m_catalogVec = {};

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

		//-------------------------------------------------------------------
		// BGM
		//-------------------------------------------------------------------
		SequenceBgm m_bgm = {};
	};
}
