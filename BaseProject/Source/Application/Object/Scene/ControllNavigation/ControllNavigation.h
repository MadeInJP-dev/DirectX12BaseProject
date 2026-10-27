#pragma once

#include "../../../../Engine/GameObject/GameObjectManager/GameObjectManager.h"
#include "../../../Game/Core/InputSettings.h"

namespace App::Object
{
	//==========================================================================================
	// 操作説明の項目1つ
	//
	// 見た目(キーのアイコンと「決定」などの文字)はシーンに置いた UI が持つ。
	// ここが持つのは、どの UI を・どのアクションの説明として・いつ出すかだけ
	//==========================================================================================
	struct ControllNavigationItem
	{
		Game::EGameAction action = Game::EGameAction::Decide;	// 説明するアクション。進行役が出し入れするときの名指しに使う
		Core::GUID uiGUID = {};									// 出す UI(アイコンと文字を飾りに持つ UIElement)
		bool isEnabled = true;									// 最初から出すか(進行役が SetActionEnabled で上書きできる)

		// 出す条件 : 指していれば、そのオブジェクトが出ている間だけ出す(反転なら隠れている間だけ)
		Core::GUID conditionGUID = {};
		bool isConditionInverted = false;
	};

	//==========================================================================================
	// 操作説明
	//
	// 画面上UIには表示されていないボタン操作を示す
	// (Esc で戻る・Enter で決定・Tab でサイドバー、のように押すボタンが画面に無い操作)。
	//
	// 画面下部の帯(Band)の左端から、今できる操作の項目だけを左詰めで横に並べる。
	// 出せない項目は隠して詰めるので、「戻る」が無い場面では「決定」が左端に来る。
	//
	//   Band  … 帯の UI(UIElement)。項目はこの左端から並ぶ。指さなければ画面の左端から
	//   Items … 項目の並び(左から順)。1つが「アイコン + 文字」を飾りに持つ UI 1つ
	//
	// ・項目を出すのは次の全部を満たすとき : 有効(Enabled / SetActionEnabled)・条件を満たす・UI がシーンにある。
	//   決定(Decide)の項目は、さらにキー・パッドで選べる項目がある(出ている UINavigationGroup がある)ときだけ。
	// ・項目の幅は UI のアンカーの PixelSize.x(×倍率)。アイコンと文字が収まる大きさを入れておくこと
	//   (文字の飾りは大きさを測れないため)。並ぶ位置は横だけ書き換え、縦は UI の置き場所のまま。
	// ・帯は出せる項目が1つも無ければ隠す。帯ごと隠したいときは、帯の上に親を置いてそちらを隠す
	//   (ここが毎フレーム帯の表示を入れ直すため)。
	// ・並べるのはエディターでも行う。隠れている項目を触りたいときはインスペクターの PreviewAll を使う。
	//==========================================================================================
	class ControllNavigation final : public Engine::GameObject::BaseObject
	{
	public:

		// 更新処理 : 出す項目を決めて、帯の左端から並べ直す
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		/// <summary>
		/// アクションの項目を出すか、実行中だけ上書きする
		/// </summary>
		/// <remarks>
		/// 進行役が場面に合わせて切り替える用(確認の箱を開いている間は「戻る」を出す、など)。
		/// 同じアクションの項目が複数あれば全部に効く。保存はされない。
		/// 条件(Condition)を持つ項目は、有効にしても条件を満たさなければ出ない
		/// </remarks>
		void SetActionEnabled(Game::EGameAction a_action, bool a_isEnabled) { m_enabledOverrideMap[a_action] = a_isEnabled; }

		// 上書きをやめて、保存されている Enabled へ戻す
		void ResetActionEnabled(Game::EGameAction a_action) { m_enabledOverrideMap.erase(a_action); }

		// そのアクションの項目を今出しているか(前の更新の結果)
		bool IsActionShown(Game::EGameAction a_action) const;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "ControllNavigation"; }

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		// 項目を今出すか : 有効か(上書き込み)と条件
		bool IsItemShown(Engine::GameObject::ObjectContext& a_context, const ControllNavigationItem& a_item) const;

		// オブジェクトが出ているか : UI なら上の親も含めて見る。シーンに無ければ出ていない扱い
		static bool IsObjectShown(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_guid);

		// 並べ始める画面の X(px) : 帯の左端 + 余白。帯が無ければ画面の左端 + 余白
		float CalcStartX(Engine::GameObject::ObjectContext& a_context) const;

	private:

		//------------------------------------------------------------------------------
		// 設定(保存される)
		//------------------------------------------------------------------------------
		Core::GUID m_bandGUID = {};							// 帯の UI。項目はこの左端から並ぶ
		std::vector<ControllNavigationItem> m_itemVec = {};	// 項目(左から並ぶ順)

		float m_paddingLeft = 32.0f;	// 帯の左端から最初の項目までの余白(px)
		float m_spacing = 40.0f;		// 項目どうしの間(px)

		//------------------------------------------------------------------------------
		// 状態(保存しない)
		//------------------------------------------------------------------------------
		std::unordered_map<Game::EGameAction, bool> m_enabledOverrideMap = {};	// 進行役が上書きした有効/無効
		std::vector<bool> m_isShownVec = {};									// 前の更新で出した項目(m_itemVec と同じ並び)
		bool m_isPreviewAll = false;											// エディターで配置を見るために全部出しておくか
	};
}
