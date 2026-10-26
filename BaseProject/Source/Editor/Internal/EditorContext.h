#pragma once

#include "Editor/EditorCommon.h"

// 選択中のアセット(AssetProperty)の中身を触るので型だけ読む
#include "Engine/Resource/Manager/AssetDatabase/AssetTypes.h"

namespace Engine::GameObject
{
	class BaseObject;
}

namespace Engine::ECS
{
	struct EngineServices;
}

namespace Editor
{
	class EditorCamera;
	class Profiler;
	class EffectEditor;
	class PrefabEditor;

	/// <summary>
	/// インスペクターのモード
	/// </summary>
	enum class EInspectorType
	{
		None,
		Entity,
		Asset,
		Game,
	};

	/// <summary>
	/// プレハブのコピー&ペースト用クリップボード。
	///
	/// シグネチャだけでなくコンポーネントの初期値バイト列ごと持つので、
	/// 貼り付け先は編集済みの値まで引き継げる。
	/// コンポーネントのタイプIDはワールド生成時に同じ順で振られるため、
	/// 実行中は別シーンのプレハブへ貼っても同じ型を指す。
	/// (アプリを閉じると消える。ファイルには残らない)
	/// </summary>
	struct PrefabClipboard
	{
		bool isValid = false;
		ECS::Signature signature = {};
		std::unordered_map<ECS::ComponentTypeID, std::vector<uint8_t>> dataMap = {};

		// クリップボードに載っているコンポーネント数
		size_t GetCount() const { return signature.count(); }
	};

	/// <summary>
	/// パネル間でやり取りされるメモ帳
	/// </summary>
	struct EditorContext
	{
		float appWindowWidth = 0;
		float appWindowHeight = 0;

		//==============================================================================
		// 現在のインスペクターモード
		//
		// 最後に選んだものの種類になる(SelectAsset / SelectEntity / SelectGameObject が書く)。
		// 以前は一覧のパネルが「描かれている間」毎フレーム書き換えていたので、
		// 一覧を並べて開いていると、最後に描かれたパネルの種類がいつも勝っていた
		// (シーンビューでエンティティを選んでも、インスペクターがオブジェクトのままになる)
		//==============================================================================
		EInspectorType eInspectorType = EInspectorType::None;

		//==============================================================================
		// 選択中のアセット
		//
		// 実体はアセットデータベースが持っているものを指しているだけ。
		// ランタイム中にファイルが消えるとその要素ごと消えるので、ポインタだけを
		// 持っていると解放済みメモリを描きにいってしまう。
		// 選択の本体はGUIDにして、ポインタは毎フレーム引き直したものを入れる
		// (引き直すのは PanelManager::ValidateContext。描画より前に一度だけ通る)。
		// 直接書き換えず SelectAsset / ClearAssetSelection を通すこと。
		//==============================================================================
		Resource::AssetProperty* pAssetProp = nullptr;	// 引き直された実体
		Core::GUID selectedAssetGUID = {};			// 選択の本体

		// アセットを選択する
		void SelectAsset(Resource::AssetProperty* a_pAssetProp)
		{
			if (!a_pAssetProp)
			{
				ClearAssetSelection();
				return;
			}

			pAssetProp = a_pAssetProp;
			selectedAssetGUID = a_pAssetProp->guid;
			eInspectorType = EInspectorType::Asset;
		}

		// アセットの選択を外す
		void ClearAssetSelection()
		{
			pAssetProp = nullptr;
			selectedAssetGUID = {};
		}

		//==============================================================================
		// アセットを辿った履歴(戻る用)
		//
		// インスペクターから参照先のアセットへ飛べるようにしたので、
		// 飛ぶ前に見ていたものを積んでおいて戻れるようにする。
		// 実体ポインタではなくGUIDで持つ : アセットデータベースを作り直すと
		// AssetProperty の置き場所ごと変わるため、後から引き直せる形にしておく。
		// 積む・戻るの操作は AssetLink 側(GUIDから実体を引ける場所)にある。
		//==============================================================================
		std::vector<Core::GUID> assetHistoryVec = {};

		// 履歴の上限。これを超えたら古いものから捨てる
		static constexpr size_t ASSET_HISTORY_MAX = 32;

		// 選択中のエンティティ
		std::vector<ECS::Entity> selectedEntities = { ECS::Limits::INVALID_ENTITY };	// 一番先頭が単体選択、インスペクター表示
		bool m_isSelecting = false;													// LCtr押下中は複数選択できる

		//==============================================================================
		// エンティティ選択の操作
		//
		// selectedEntities は
		//   ・空にしない(未選択のときは INVALID_ENTITY を1つだけ持つ)
		//   ・先頭がプライマリ選択＝インスペクターに出るもの
		// を不変条件にしている。直接書き換えず必ずこれらを通すこと。
		//==============================================================================

		// インスペクター表示・ギズモ操作の対象になるプライマリ選択
		ECS::Entity GetPrimaryEntity() const
		{
			if (selectedEntities.empty()) return ECS::Limits::INVALID_ENTITY;
			return selectedEntities.front();
		}

		// そのエンティティが選択されているか(ヒエラルキーのハイライト用)
		bool IsSelectedEntity(const ECS::Entity& a_entity) const
		{
			if (a_entity == ECS::Limits::INVALID_ENTITY) return false;
			return std::find(selectedEntities.begin(), selectedEntities.end(), a_entity) != selectedEntities.end();
		}

		// 選択解除。空にはせず INVALID を1つだけ残す
		void ClearEntitySelection()
		{
			selectedEntities.assign(1, ECS::Limits::INVALID_ENTITY);
		}

		// エンティティを選択する
		// a_isAdditive : true(LCtrl押下中)なら追加選択。すでに選択済みなら選択から外す(トグル)
		//                false なら単体選択で置き換える
		void SelectEntity(const ECS::Entity& a_entity, bool a_isAdditive)
		{
			if (a_entity == ECS::Limits::INVALID_ENTITY)
			{
				ClearEntitySelection();
				return;
			}

			eInspectorType = EInspectorType::Entity;

			// 単体選択 : 中身を入れ替えるだけ
			if (!a_isAdditive)
			{
				selectedEntities.assign(1, a_entity);
				return;
			}

			// 追加選択 : すでに入っていればトグルで外す
			auto _it = std::find(selectedEntities.begin(), selectedEntities.end(), a_entity);
			if (_it != selectedEntities.end())
			{
				selectedEntities.erase(_it);
				if (selectedEntities.empty()) ClearEntitySelection();
				return;
			}

			// 未選択状態のダミー(INVALID)が残っていたら取り除いてから追加する
			std::erase(selectedEntities, ECS::Limits::INVALID_ENTITY);
			selectedEntities.push_back(a_entity);
		}

		// 選択からエンティティを取り除く(エンティティ削除時など)
		void DeselectEntity(const ECS::Entity& a_entity)
		{
			std::erase(selectedEntities, a_entity);
			if (selectedEntities.empty()) ClearEntitySelection();
		}

		// 選択中のECS外オブジェクト(GameObjectManager管理下)
		// 選ぶときは SelectGameObject を通す(インスペクターの種類も切り替わる)
		GameObject::BaseObject* pGameObject = nullptr;

		// ECS外オブジェクトを選択する。エンティティの選択は外す(インスペクターは1つしか出せないため)
		void SelectGameObject(GameObject::BaseObject* a_pObject)
		{
			pGameObject = a_pObject;
			if (!a_pObject) return;

			ClearEntitySelection();
			eInspectorType = EInspectorType::Game;
		}

		// プレハブのシグネチャ+初期値のコピー先
		PrefabClipboard prefabClipboard = {};

		// エディターカメラポインタ
		EditorCamera* pEditorCamera = nullptr;

		// エフェクトエディター(モーダルの確認画面)。アセットの「Open Effect Editor」から開く
		EffectEditor* pEffectEditor = nullptr;

		// プレハブエディター(モーダルの編集画面)。アセットの「Open Prefab Editor」から開く
		PrefabEditor* pPrefabEditor = nullptr;

		// アプリ寿命のサービス一式(借り物) : 正本は MainEngine が持っている。
		// アセットデータベースやリソースマネージャーはここから引くこと
		ECS::EngineServices* pServices = nullptr;

		// プロファイラポインタ : 計測はプロファイラ側が行い、パネルは結果を読むだけ
		Profiler* pProfiler = nullptr;

		//==============================================================================
		// シーンに紐づくものを捨てる
		//
		// 選択中のエンティティとゲームオブジェクトは、そのシーンのワールド・
		// オブジェクトマネージャーが持っているものを指している。
		// シーンが切り替わると実体ごと消えるため、持ち越すと解放済みメモリを触る。
		// アセット選択(pAssetProp)はアセットデータベースの持ち物でシーンを跨いで
		// 生きているので、ここでは捨てない。
		//==============================================================================
		void ClearSceneContext()
		{
			ClearEntitySelection();
			pGameObject = nullptr;

			// アセットを見ていたならその表示は残す
			if (eInspectorType == EInspectorType::Entity || eInspectorType == EInspectorType::Game)
			{
				eInspectorType = EInspectorType::None;
			}
		}
	};
}