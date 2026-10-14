#pragma once

#include "Editor/EditorCommon.h"
#include "Engine/DevTool/IDevTool.h"

namespace Engine::Graphics::D3D12
{
	class DescriptorHeapManager;
}

namespace Engine::ECS
{
	struct EngineServices;
}

namespace Editor
{
	// 前方宣言
	class LogPanel;
	class ComponentEdit;
	class ImGuiContext;
	class EditorCamera;
	class PanelManager;
	class Profiler;
	class EffectEditor;
	class PrefabEditor;

	//=======================================================================
	// 
	// メインエディタクラス
	// 
	//=======================================================================
	// Engine へは開発ツールの窓口(IDevTool)として差し込まれる(main.cpp)。
	// 実体は main.cpp(WinMain)が unique_ptr で持つ。エディターの中では
	// EditorContext が配るもの(カメラ・エフェクトエディター・プレハブエディター・サービス)を使い、ここを名指ししないこと
	class MainEditor : public Engine::DevTool::IDevTool
	{
	public:

		MainEditor();
		~MainEditor() override;
		NON_COPYABLE_NON_MOVABLE(MainEditor);

		// 初期化
		// a_pServices : アプリ寿命のサービス一式(借り物)。パネルへはここから配る
		bool Init(HWND a_hwnd, Graphics::D3D12::DescriptorHeapManager* a_pHeapManager, ECS::EngineServices* a_pServices) override;

		// 解放
		void Release() override;

		// 更新
		void Update(float a_dt) override;

		// 描画
		void Draw(Graphics::D3D12::GraphicsCommandList* a_pCmdList) override;

		/// <summary>
		/// エディター側に残っている入力を捨てる
		/// </summary>
		/// <remarks>
		/// アプリのモードを切り替えるときに呼ぶ。押しっぱなしのキー・溜まった入力イベント・
		/// フリーカメラの操作中フラグを、切り替えの向こう側へ持ち越さないようにする。
		/// </remarks>
		void ResetInput() override;

		/// <summary>
		/// シーンが切り替わることをエディターへ知らせる
		/// </summary>
		/// <remarks>
		/// シーンを捨てる・積み替える直前に呼ぶ。エディターが覚えている選択
		/// (エンティティID・ゲームオブジェクトのポインタ)は切り替え先のシーンには
		/// 存在しないので、ここで捨てる。持ち越すと解放済みのオブジェクトを
		/// インスペクターが描きにいって落ちる。
		/// </remarks>
		void OnSceneChanged() override;

		// カメラの割り込み : 確認用の画面(エフェクト / プレハブエディター)のカメラが最優先、次にエディターモードのフリーカメラ
		bool TryGetCameraOverride(EAppMode a_mode, Engine::DevTool::CameraOverride& a_outOverride) const override;

		// 確認用の画面(エフェクト / プレハブエディター)のシーン(開いている間はゲームのシーンの代わりに回す)
		bool IsScenePreviewActive() const override;
		void UpdateScenePreview(float a_deltaTime) override;
		void DrawScenePreview() override;

		//=======================================================================
		// ログについて
		//
		// 積む口はエディターではなくマクロ側にある。
		//
		//   ENGINE_LOG      … ふつうの記録
		//   ENGINE_WARNING  … 続行するが不都合なこと
		//   ENGINE_ERROR    … 失敗として残すが、止めはしない
		//   ENGINE_ERRLOG   … 条件を満たさなければ記録して止める(assert)
		//
		// エディターは Init で Debug::SetLogCallback を登録し、届いた行を
		// LogPanel へ流すだけ。エンジンやアプリがエディターを名指しする必要はない。
		//
		// ここに AddLog / ErrorLog を置いていた頃は、Archive・PipelineState・
		// World・リソースの保存処理がエディターを直接呼んでいて依存が逆流していた。
		// 同じことをしないよう、口はマクロ1本に絞ってある
		//=======================================================================

		//=======================================================================
		// 計測関連
		//
		// 計測そのものは ENGINE_PROFILE_SCOPE (Core::Debug::TimeProfileScope) が行い、
		// 結果はコールバックで Profiler へ届く。
		// ここが持つのは、その結果をフレームごとにまとめる区切りだけ
		//=======================================================================
		// フレームの終わり : メインループの末尾で呼ぶ
		// 受け取った結果の集計と表示用データの作成はここで走る
		void EndProfileFrame() override;

		//=======================================================================
		// デバッグ描画について
		//
		// ワイヤーを積む場所はエンジン側(Engine::Graphics::DebugDraw)に移した。
		// ここに Draw～ を置いていた頃は、積みたい側(ECSのシステム・GameObject・
		// コリジョン)がすべてエディターを名指ししていて、依存が逆流していた。
		//
		//   積む   : SystemContext / ObjectContext の pServices->pDebugDraw
		//            エンジン内部なら GraphicsEngine::RefDebugDraw()
		//   出す   : DebugLinePass が RenderContext 経由で読む
		//   オンオフ: DebugDrawOption::drawWire(オプションパネルから触る)
		//
		// エディットはエディターの仕事なので、表示設定はここではなく
		// OptionManager が持っている
		//=======================================================================

		//=======================================================================
		// エディターに出す ImGui の登録
		//
		// 自分の設定をエディターのウィンドウへ出したいものが、その描画処理を預ける。
		// 呼ばれるのは毎フレームのエディター描画中。
		//
		// デバッグ用のワイヤー(線・箱・球)とは関係がない。あちらは
		// Engine::Graphics::DebugDraw なので間違えないこと。
		//
		// 登録は Init のあとに行うこと(Init が登録済みのものを捨てる)
		//=======================================================================
		void RegisterEditFunc(std::function<void()> a_func) override;

		//=======================================================================
		// エディター用フリーカメラ
		//=======================================================================
		EditorCamera* RefEditorCamera() { return m_upEditorCamera.get(); }

		//=======================================================================
		// エフェクトエディター
		//
		// エフェクト1枚をゲームと同じ描画環境で確認するためのモーダル画面。
		// 開いている間はゲームのシーンが止まり、レンダーグラフには
		// あちらの確認用ワールドの描画命令だけが流れる。
		//=======================================================================
		EffectEditor* RefEffectEditor() { return m_upEffectEditor.get(); }

		//=======================================================================
		// プレハブエディター
		//
		// プレハブ1つを、出したところを見ながら親子ごと組むモーダル画面。
		// 作りはエフェクトエディターと同じで、開いている間はゲームのシーンが止まる。
		// 2つが同時に開くことは無い(どちらもモーダルなので、片方の中から他方を開けない)
		//=======================================================================
		PrefabEditor* RefPrefabEditor() { return m_upPrefabEditor.get(); }

		/// <summary>
		/// エディターがモーダルな画面を出しているか
		/// </summary>
		/// <remarks>
		/// true の間は、その画面以外の操作を受け付けてはいけない。
		/// ImGui のウィンドウはモーダル自身が塞いでくれるが、
		/// ImGui を通らない操作(アプリのモード切り替え・フリーカメラ)は
		/// 呼ぶ側でここを見て止めること。
		/// </remarks>
		bool IsModalActive() const override;

	private:

		// ImGuiコンテキスト
		std::unique_ptr<ImGuiContext> m_upImGuiContext = nullptr;

		// パネルマネージャー
		std::unique_ptr<PanelManager> m_upPanelManager = nullptr;

		// ログパネル : 実体は PanelManager が所有する。
		// パネルは登録後に増減しないので、ここでは参照だけを持っておく
		LogPanel* m_pLogPanel = nullptr;

		// 計測機 : 計測と集計はここが持ち、パネルは結果を読むだけ
		std::unique_ptr<Profiler> m_upProfiler = nullptr;

		// エディター用フリーカメラ
		std::unique_ptr<EditorCamera> m_upEditorCamera = nullptr;

		// エフェクト確認用のモーダル画面
		std::unique_ptr<EffectEditor> m_upEffectEditor = nullptr;

		// プレハブ編集用のモーダル画面
		std::unique_ptr<PrefabEditor> m_upPrefabEditor = nullptr;

		// エディター用関数登録
		std::vector<std::function<void()>> m_editFuncVec = {};

		bool m_isInit = false;

	};
}