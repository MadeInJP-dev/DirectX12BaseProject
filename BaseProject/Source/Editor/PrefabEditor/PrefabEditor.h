#pragma once

#include "Editor/EditorCommon.h"

namespace Engine
{
	namespace ECS { class World; }
	namespace Resource { class Prefab; }
	namespace Graphics::Pipeline { class RenderingPipelineAsset; }
}

namespace Editor
{
	class EditorCamera;

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
	class PrefabEditor
	{
	public:

		// a_pServices : アプリ寿命のサービス一式(借り物)。アセットの読み込みに使う
		explicit PrefabEditor(ECS::EngineServices* a_pServices);
		~PrefabEditor();
		NON_COPYABLE_NON_MOVABLE(PrefabEditor);

		/// <summary>
		/// 指定のプレハブを開く(次のフレームからプレビューが回りだす)
		/// </summary>
		void Open(const Core::GUID& a_prefabGUID);

		/// <summary>
		/// 確かめずに閉じる。ワールドは残し、中に出したものだけを片付ける
		/// </summary>
		void Close();

		/// <summary>
		/// 開いているか。ここが true の間、ゲームのシーンは止まる
		/// </summary>
		bool IsOpen() const { return m_isOpen; }

		/// <summary>
		/// プレビュー用ワールドの更新(ゲームのシーンの代わりに回る)
		/// </summary>
		void UpdateScene(float a_dt);

		/// <summary>
		/// プレビュー用ワールドの描画命令を積む(ゲームのシーンの代わりに回る)
		/// </summary>
		void DrawScene();

		/// <summary>
		/// カメラの更新。MainEditor がフリーカメラの代わりに呼ぶ
		/// </summary>
		void UpdateCamera(float a_dt);

		/// <summary>
		/// カメラの割り込み行列を取得する
		/// </summary>
		/// <returns>開いていなければ false(割り込まない)</returns>
		bool TryGetCameraOverride(Math::Matrix& a_outWorldMat, Math::Matrix& a_outProjMat) const;

		/// <summary>
		/// ポップアップの描画。MainEditor がパネルの後に呼ぶ
		/// </summary>
		void OnDrawImGui();

		/// <summary>
		/// 終了処理
		/// </summary>
		void Release();

	private:

		//----------------------------------------------------------------------------------
		// プレビュー
		//----------------------------------------------------------------------------------

		// プレビュー用ワールドとカメラを作る(初回に開いたときだけ)
		void EnsureWorld();

		// 編集中のプレハブの実体を原点に出す(その場で生成する)
		void SpawnInstance();

		// プレビュー用ワールドに居るものを全部片付ける
		// (実体が動いて出したもの(弾・エフェクト)も含めて消す)
		void DestroyAllEntities();

		// このプレビューのカメラを GraphicsEngine へ積む
		void SubmitPreviewCamera();

		// 目安になる格子をデバッグ線で描く(大きさの把握用)
		void DrawGrid() const;

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
		void DrawToolbar();
		void DrawNodePane();
		void DrawNodeTreeItem(int a_nodeIndex, const std::vector<std::vector<int>>& a_childIndexMap);
		void DrawViewport();
		void DrawNodeGizmo(const ImVec2& a_imageMin, const ImVec2& a_imageSize);
		void DrawInfo();
		void DrawEditPane();
		void DrawCloseConfirmPopup();

	private:

		//----------------------------------------------------------------------------------
		// 状態
		//----------------------------------------------------------------------------------
		bool m_isOpen = false;					// 開いているか
		bool m_isOpenRequest = false;			// ImGui へポップアップを開かせる要求(開いた最初の1回だけ)
		bool m_isCloseConfirmRequest = false;	// 未保存の確認を開かせる要求
		bool m_isCloseRequest = false;			// 確認が済んで閉じる要求(ポップアップの中で閉じる)

		Core::GUID m_prefabGUID = Core::DEFAULT_GUID;
		Engine::Handle<Resource::Prefab> m_prefabHandle = {};

		// 保存したとき(開いたとき・読み直したときも)の中身の値
		uint64_t m_savedHash = 0;

		// アプリ寿命のサービス一式(借り物)
		ECS::EngineServices* m_pServices = nullptr;

		// プレビュー用のワールド。
		// 閉じても捨てずに使い回す(理由はエフェクトエディターと同じ :
		// World::Release() はリソースのGC掃除まで走り、ゲーム側のモデルまで解放してしまう)
		std::unique_ptr<Engine::ECS::World> m_upWorld = nullptr;

		// シーンビューと同じフリーカメラ(プレビュー専用の実体)
		std::unique_ptr<EditorCamera> m_upCamera = nullptr;

		//----------------------------------------------------------------------------------
		// 描画構成
		//
		// 開いた時点でゲームが使っていたものを借りる(絵作りを本番と揃えるため)
		//----------------------------------------------------------------------------------
		Engine::Handle<Engine::Graphics::Pipeline::RenderingPipelineAsset> m_pipelineHandle = {};

		// このプレビューのカメラを GraphicsEngine 側で識別する鍵(ワールドが違えば別扱い)
		static constexpr uint32_t PREVIEW_CAMERA_ENTITY = 0;

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
		// 再生
		//----------------------------------------------------------------------------------
		bool  m_isPlaying = true;		// 時間を進めるか(止めるとアニメーションなどがその場で固まる)
		float m_playSpeed = 1.0f;		// 再生速度の倍率

		//----------------------------------------------------------------------------------
		// 編集
		//----------------------------------------------------------------------------------
		int   m_selectedNode = -1;		// 選んでいるノード(-1 = ルート / 0 以上 = 子の添え字)
		float m_nodePaneWidth = 220.0f;	// 左のノード欄の幅(px)
		float m_editPaneWidth = 420.0f;	// 右の編集欄の幅(px)

		//----------------------------------------------------------------------------------
		// 表示
		//----------------------------------------------------------------------------------
		bool  m_isDrawGrid = true;
		float m_gridSize = 10.0f;		// 格子の半径(m)
		bool  m_isDrawAxis = true;		// 選んでいるノードの位置に軸を描く
		bool  m_isUseGizmo = true;		// 選んでいる子ノードを移動ギズモで動かす
	};
}
