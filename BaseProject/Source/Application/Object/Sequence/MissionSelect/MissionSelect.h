#pragma once

#include "../../../../Engine/GameObject/BaseObject/BaseObject.h"

namespace Engine::Scene
{
	class SceneManager;
}

namespace App::Object
{
	class UIBase;
	namespace Decoration { struct Decoration; }

	/// <summary>
	/// ミッションセレクト : ホーム画面から呼び出される、出撃先を選ぶ画面
	/// </summary>
	/// <remarks>
	/// 出し入れは HomeSequence が SetVisible で行う。ここは
	/// 「どのミッションがあるか」と「選んだら確認を出して出撃する」だけを持つ。
	///
	/// ・一覧はシーンへ置いた UI オブジェクトを配列で覚える
	///     以前は開始位置・項目サイズ・間隔からその場で並べて描いていたが、
	///     それだと縦一列にしか並べられず、項目ごとに見た目を変えることもできなかった。
	///     いまは1ミッション = 1つの UIButton なので、置き方も飾り(枠・画像・文字)も
	///     エディター上で自由に組める。判定も UIButton のものがそのまま効く。
	///
	/// ・詳細(画像・説明文)はミッションごとの UI 配列
	///     カーソルが乗ったミッションのものだけを出す。乗っていない間は
	///     最後に乗ったものを出しっぱなしにする(切り替えのたびに消えるとちらつくため)。
	///
	/// ・押すと確認ボックスが出る
	///     画面中央に「ミッション名 / 出撃しますか? / Yes / No」を出す想定。
	///     ボックスそのものはシーンへ置いた UI で、ここは出し入れと
	///     ミッション名の流し込みだけを行う。
	///     Yes で出撃、No で閉じる。確認中は裏の一覧を押せなくする。
	///
	/// ・ミッション名は Text の飾りへ流し込む
	///     名前だけのために画像を1枚ずつ用意しなくて済むようにするため。
	///     ボックスの UI に Text の飾りを1つ置き、その名前(既定 "MissionName")を指す。
	/// </remarks>
	class MissionSelect : public Engine::GameObject::BaseObject
	{
	public:

		// 初期化処理 : 出し入れに使うマネージャーを覚える
		void Awake(Engine::GameObject::ObjectContext& a_context) override;

		// 更新処理 : ボタンへの差し込みと、カーソルが乗っているミッションの追跡
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// 出し入れ : HomeSequence から GUID 経由で呼ばれる
		//=======================================================================

		bool IsVisible() const override { return m_isVisible; }

		/// <summary>
		/// 出す / 隠す
		/// </summary>
		/// <remarks>
		/// 配下の UI もその場でまとめて切り替える。
		/// 次の更新まで待つと、ボタンを押してから1フレーム遅れて出てくる
		/// </remarks>
		void SetVisible(bool a_isVisible) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "MissionSelect"; }

		// インスペクター : ミッション一覧と確認ボックスの設定
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		/// <summary>
		/// 一覧に並べる1ミッションぶんの設定
		/// </summary>
		struct MissionEntry
		{
			// 確認ボックスへ出す名前(エディターの見分けにも使う)
			std::string name = "Mission";

			// 一覧に置く UIButton。押すと確認ボックスが出る
			Core::GUID buttonGUID = {};

			// 出撃先
			Core::GUID sceneGUID = {};

			// このミッションを見ている間だけ出す UI(ステージ画像・説明文など)
			std::vector<Core::GUID> detailUIGUIDVec = {};
		};

		//-------------------------------------------------------------------
		// 進行
		//-------------------------------------------------------------------

		// ボタンへ押下時の処理を差し込む(見つかったものだけ。見つからないものは知らせる)
		void TryBind(Engine::GameObject::ObjectContext& a_context);

		// カーソルが乗っているミッションを追う(詳細の出し分け用)
		void UpdateHover();

		// 今の状態に合わせて、配下のUIの表示を切り替える
		void ApplyVisible();

		// 確認ボックスを出す / 閉じる
		void OpenConfirm(int a_index);
		void CloseConfirm();

		// 確認しているミッションへ出撃する
		void ReserveSortie(Engine::Scene::SceneManager& a_sceneManager);

		// 確認ボックスへミッション名を流し込む
		void ApplyMissionName(const MissionEntry& a_mission);

		/// <summary>
		/// ミッション名を流し込む飾りを引く(見つからなければ nullptr)
		/// </summary>
		/// <remarks>
		/// 番号を持たない古いデータは、ここで名前から番号を引き直して覚える。
		/// 相手は別のオブジェクトなので、読み込みの時点(Archive)ではまだ引けない
		/// </remarks>
		Decoration::Decoration* ResolveNameDecoration(UIBase& a_nameUI);

		// 指しているのに見つからないボタンを知らせる
		void WarnMissingButtons() const;

		// 有効な添え字か
		bool IsValidIndex(int a_index) const
		{
			return a_index >= 0 && a_index < static_cast<int>(m_missionVec.size());
		}

	private:

		//-------------------------------------------------------------------
		// 設定(保存される) : ミッション
		//-------------------------------------------------------------------
		std::vector<MissionEntry> m_missionVec = {};

		//-------------------------------------------------------------------
		// 設定(保存される) : 確認ボックス
		//-------------------------------------------------------------------
		// 確認中だけ出すUI(ボックスの枠・見出し・背景の暗幕など)
		std::vector<Core::GUID> m_confirmUIGUIDVec = {};

		Core::GUID m_yesButtonGUID = {};	// 出撃する
		Core::GUID m_noButtonGUID = {};	// やめる

		// ミッション名を流し込む先のUIと、その中の Text 飾りの番号(Decoration::id)
		Core::GUID m_nameUIGUID = {};
		uint32_t m_nameDecorationId = 0;

		// 名前で飾りを指していた頃の名残。保存の並びを保つために読み書きだけ続け、
		// 番号を持たない古いデータでは ResolveNameDecoration がこの名前から番号を引き直す
		std::string m_legacyNameDecorationName = "MissionName";

		//-------------------------------------------------------------------
		// 状態(保存しない)
		//-------------------------------------------------------------------
		// 出し入れに使うマネージャー。
		// SetVisible はコンテキストを受け取れないので、Awake で受け取ったものを覚えておく
		// (シングルトンを名指ししないための経路は保ったまま、押した瞬間に反映できる)
		Engine::GameObject::GameObjectManager* m_pObjectManager = nullptr;

		bool m_isVisible = true;	// HomeSequence から切り替えられる

		int m_showIndex = 0;		// 詳細を出しているミッション(最後にカーソルが乗ったもの)
		int m_confirmIndex = -1;	// 確認中のミッション(-1 で確認していない)

		bool m_isBound = false;			// ボタンへ差し込み済みか
		bool m_isSceneRequested = false;	// 二重に遷移要求を出さないための印
	};
}
