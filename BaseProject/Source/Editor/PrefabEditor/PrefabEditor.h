#pragma once

#include "Editor/EditorCommon.h"
#include "../Widget/ScenePreview/IPreviewEditor.h"
#include "../Widget/ScenePreview/ScenePreview.h"
#include "../Widget/PopUp/Popup.h"
#include "../Widget/PopUp/ConfirmPopup/ConfirmPopup.h"

namespace Engine
{
	namespace Resource { class Prefab; }
}

namespace Editor
{
	//======================================================================================
	//
	// プレハブエディター
	//
	// プレハブ1つを、ゲームと同じ描画環境で「出したところを見ながら組む」ための画面。
	// アセットインスペクターの「Open Prefab Editor」から開く。
	//
	// ・作りはエフェクトエディターと同じ。確認用の専用ワールドを1つ持ち、
	//   開いている間はゲームのシーンを止めて、このワールドだけをレンダーグラフへ流す。
	//   中身は編集中のプレハブの実体1つだけ(原点に出す)。
	//
	// ・左 : ノードの木(ルートと子)。子の追加と削除もここで行う。
	//   インスペクターではルートのコンポーネントしか触れず、子は一覧が見えるだけだった。
	//   子を持つプレハブ(プレイヤーの機体と武器など)を直すたびに、シーンへ出して
	//   直して Create Prefab で作り直す、という往復をしなくて済むようにする。
	//
	// ・右 : 選んだノードのコンポーネント。中身はアセットインスペクターと同じ関数
	//   (PrefabComponentsEdit)を呼ぶだけで、UIを二重に持っていない。
	//
	// ・編集はメモリ上のプレハブを書き換える。書き換わったら(操作中の欄から手を離した時点で)
	//   実体を出し直すので、見えているものは常に今の値。
	//   ファイルへ書くのは Save を押したときだけ。未保存のまま閉じようとすると確かめる。
	//
	// ・選んだ子ノードは、ビューポート上の移動ギズモでも動かせる(ローカル位置に書く)。
	//   ルートは動かさない。プレハブを出す側(スポナーやヒエラルキー)が位置を必ず上書きするので、
	//   ルートの保存位置はどこにも効かないため。
	//
	// ・カメラはシーンビューと同じ EditorCamera(右クリック中だけ操作するフリーカメラ)。
	//
	// ・開いている間はモーダル。他のパネルもアプリのモード切り替え(O/P)も受け付けない
	//   (理由はエフェクトエディターと同じ)。
	//
	//======================================================================================
	class PrefabEditor : public IPreviewEditor
	{
	public:

		// a_pServices : アプリ寿命のサービス一式(借り物)。アセットの読み込みに使う
		explicit PrefabEditor(ECS::EngineServices* a_pServices);
		~PrefabEditor() override;
		NON_COPYABLE_NON_MOVABLE(PrefabEditor);

		/// <summary>
		/// 指定のプレハブを開く(次のフレームからプレビューが回りだす)
		/// </summary>
		void Open(const Core::GUID& a_prefabGUID);

		/// <summary>
		/// 確かめずに閉じる。ワールドは残し、中に出したものだけを片付ける
		/// </summary>
		void Close();

		//----------------------------------------------------------------------------------
		// IPreviewEditor
		//----------------------------------------------------------------------------------
		bool IsOpen() const override { return m_isOpen; }
		void UpdateScene(float a_dt) override;
		void DrawScene() override;
		void UpdateCamera(float a_dt) override;
		bool TryGetCameraOverride(Math::Matrix& a_outWorldMat, Math::Matrix& a_outProjMat) const override;
		void OnDrawImGui() override;
		void Release() override;

	private:

		//----------------------------------------------------------------------------------
		// プレビュー
		//----------------------------------------------------------------------------------

		// 編集中のプレハブの実体を原点に出す(その場で生成する)
		void SpawnInstance();

		// プレビュー用ワールドに居るものを全部片付ける
		// (実体が動いて出したもの(弾・エフェクト)も含めて消す)
		void DestroyAllEntities();

		// 選んでいるノードの位置に軸を描く
		void DrawSelectedNodeAxis() const;

		// そのノードの実体。出していなければ INVALID_ENTITY
		ECS::Entity GetNodeEntity(int a_nodeIndex) const;

		//----------------------------------------------------------------------------------
		// 編集対象
		//----------------------------------------------------------------------------------

		// 編集対象のプレハブ。読み込めていなければ nullptr
		Resource::Prefab* RefPrefab() const;

		// プレハブの中身から作る値。変わったかどうかの判定だけに使う
		uint64_t ComputePrefabHash() const;

		// 保存したときから変わっているか
		bool IsModified() const;

		// ファイルへ書く / ファイルから読み直す
		void Save();
		void Revert();

		// 閉じる操作の入口。未保存なら確かめる画面を出す
		void RequestClose();

		// 子ノードを足す(既定の構成 : 位置・名前・親子・GUID)
		void AddChildNode(int a_parentIndex);

		// ノードをその下の子孫ごと取り除く(ルートは取り除けない)
		void RemoveNode(int a_nodeIndex);

		// ノードの表示名(NameComponent を持っていればその名前)
		std::string GetNodeName(int a_nodeIndex) const;

		//----------------------------------------------------------------------------------
		// UI
		//----------------------------------------------------------------------------------
		void DrawContent();
		void DrawToolbar();
		void DrawNodePane();
		void DrawNodeTreeItem(int a_nodeIndex, const std::vector<std::vector<int>>& a_childIndexMap);
		void DrawViewport();
		void DrawNodeGizmo(const ImVec2& a_imageMin, const ImVec2& a_imageSize);
		void DrawInfo();
		void DrawEditPane();

	private:

		//----------------------------------------------------------------------------------
		// 状態
		//----------------------------------------------------------------------------------
		bool m_isOpen = false;					// 開いているか
		bool m_isCloseRequest = false;			// 確認が済んで閉じる要求(ポップアップの中で閉じる)

		Core::GUID m_prefabGUID = Core::DEFAULT_GUID;
		Engine::Handle<Resource::Prefab> m_prefabHandle = {};

		// 保存したとき(開いたとき・読み直したときも)の中身の値
		uint64_t m_savedHash = 0;

		// アプリ寿命のサービス一式(借り物)
		ECS::EngineServices* m_pServices = nullptr;

		// 確認用のワールド・カメラ・描画構成・再生と格子
		ScenePreview m_preview;

		// この画面そのもの(画面の大部分を使うモーダル)と、未保存のまま閉じるときの確認
		Popup m_popup;
		ConfirmPopup m_closeConfirmPopup;

		//----------------------------------------------------------------------------------
		// 出している実体
		//----------------------------------------------------------------------------------
		// ノードごとの実体。先頭がルート、以降は子の添え字 + 1(作れなかった所は INVALID_ENTITY)
		std::vector<ECS::Entity> m_nodeEntityVec = {};

		// 出したときの中身の値。今の値と違えば出し直す
		uint64_t m_spawnedHash = 0;

		bool m_isRespawnRequest = false;	// 次の更新で出し直す
		bool m_isAutoRespawn = true;		// 中身が変わったら自動で出し直す

		//----------------------------------------------------------------------------------
		// 編集
		//----------------------------------------------------------------------------------
		int   m_selectedNode = -1;		// 選んでいるノード(-1 = ルート / 0 以上 = 子の添え字)
		float m_nodePaneWidth = 220.0f;	// 左のノード欄の幅(px)
		float m_editPaneWidth = 420.0f;	// 右の編集欄の幅(px)

		//----------------------------------------------------------------------------------
		// 表示
		//----------------------------------------------------------------------------------
		bool  m_isDrawAxis = true;		// 選んでいるノードの位置に軸を描く
		bool  m_isUseGizmo = true;		// 選んでいる子ノードを移動ギズモで動かす
	};
}
