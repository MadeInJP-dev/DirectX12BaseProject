#pragma once

#include "Editor/EditorCommon.h"

namespace Engine
{
	namespace ECS { class World; }
	namespace Resource
	{
		class EffectAsset;
		class EffectPrefab;
		class ParticlesAsset;
	}
	namespace Graphics::Pipeline { class RenderingPipelineAsset; }
	namespace Scene { class SceneAmbient; }
}

namespace Editor
{
	class EditorCamera;

	//======================================================================================
	//
	// エフェクトエディター
	//
	// エフェクトアセット1枚を、ゲームと同じ描画環境で「見ながら組む」ための画面。
	// アセットインスペクターの「Open Effect Editor」から開く。
	//
	// ・確認用の専用ワールド(エディターシーン)を1つ持つ。
	//   中身は編集中のエフェクトの実体1つだけ。周りに何も無いので、
	//   ゲームのシーンに置いて他のものに埋もれる前に、単体で見え方を詰められる。
	//
	// ・描画は「ゲームのシーンと同じレンダーグラフ」をそのまま通す。
	//   別のパスや簡易プレビューを用意していないのは、そうすると
	//   トーンマップやブルームの掛かり方が本番と変わり、ここで合わせた見た目が
	//   ゲームに持っていくとずれるため。開いている間はゲームのシーンを止めて、
	//   このワールドの描画命令だけをレンダーグラフへ流す。
	//
	// ・背景(環境設定)は上段の Background で選ぶ。
	//     Black        : 空・フォグ無しの黒背景に格子だけ(既定)。薄い粒や加算の抜けが
	//                    空の色に紛れないので、エフェクト単体の見え方を詰めるとき用
	//     CurrentScene : 開く前のシーンの環境設定(空・フォグ・光)そのまま。
	//                    置く先の景色の中でどう見えるかを確かめるとき用
	//   流し込むのは SceneManager(GetPreviewAmbient で選んだものを毎フレーム渡す)。
	//
	// ・右側で編集する。中身はアセットインスペクターとまったく同じ関数
	//   (EffectAssetEdit / ParticleEdit)を呼んでいるだけで、UIを二重に持っていない。
	//     Effect   タブ : パーツの足し引き・発生位置・発生数・時間指定
	//     Particle タブ : そのパーツが使っている粒そのもの(初速・寿命・絵)
	//   粒まで同じ画面で触れないと、「もう少し細く」のたびに画面を往復することになる。
	//
	// ・カメラはシーンビューと同じ EditorCamera(右クリック中だけ操作するフリーカメラ)。
	//   操作感を変えると、こちらで合わせた画角をシーンで作り直すことになるため。
	//
	// ・開いている間はモーダル。他のパネルもアプリのモード切り替え(O/P)も受け付けない。
	//   ゲームのシーンが止まっている状態でモードを切り替えられると、
	//   「プレイモードなのに何も動かない」という筋の通らない状態になるため。
	//
	// ・エフェクトプレハブ(EffectPrefab)も開ける(OpenEffectPrefab)。
	//   原点に炊き、全部が寿命で消えたら(Loop なら)炊き直す。
	//   右側はプレハブと同じコンポーネント編集。変更は次に炊いたときから効く(Restart)。
	//   プレビューのワールドには地面が無いので、着地する破片は寿命まで落ち続ける。
	//
	//======================================================================================
	class EffectEditor
	{
	public:

		// a_pServices : アプリ寿命のサービス一式(借り物)。アセットの読み込みに使う
		explicit EffectEditor(ECS::EngineServices* a_pServices);
		~EffectEditor();

		/// <summary>
		/// 指定のエフェクトを開く(次のフレームからプレビューが回りだす)
		/// </summary>
		void Open(const Core::GUID& a_effectGUID);

		/// <summary>
		/// 指定のエフェクトプレハブを開く(原点に炊き、消えたら炊き直す)
		/// </summary>
		void OpenEffectPrefab(const Core::GUID& a_effectPrefabGUID);

		/// <summary>
		/// 閉じる。ワールドは残し、中に出したエフェクトだけを片付ける
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
		/// <remarks>
		/// シーンビューのフリーカメラと同じく、参照する ImGui の入力は前フレームの
		/// NewFrame 時点のもの。ホバー判定も前フレーム基準なのでずれは生じない
		/// </remarks>
		void UpdateCamera(float a_dt);

		// このプレビューのカメラを GraphicsEngine へ積む。
		// このワールドにはカメラのエンティティが居ないので、システム任せにできない
		void SubmitPreviewCamera();

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

		/// <summary>
		/// プレビューで使う環境設定
		/// </summary>
		/// <returns>ゲームのシーンのものを使うなら nullptr</returns>
		const Engine::Scene::SceneAmbient* GetPreviewAmbient() const;

	private:

		// プレビュー用ワールドを作る(初回に開いたときだけ)
		void EnsureWorld();

		// 編集中のエフェクトの実体を出す / 片付ける
		void ReserveSpawn();
		void DestroyEffectEntity();

		// ワールドの中からプレビュー中のエフェクトを引く。まだ実体化していなければ nullptr
		struct EffectRef;
		EffectRef FindEffect() const;

		// 編集対象のアセット。読み込めていなければ nullptr
		Resource::EffectAsset* RefEffectAsset() const;
		Resource::EffectPrefab* RefEffectPrefab() const;

		// エフェクトプレハブを開くときの共通の後半(ワールド・カメラ・描画構成の用意と最初の生成)
		void BeginOpen();

		// エフェクトプレハブで出したもののうち、まだ残っている数(寿命を持つもの)
		int CountPrefabEntities() const;

		// エフェクトプレハブの編集欄
		void DrawEffectPrefabEditPane();

		// 今 Particle タブで開いているパーツの粒。選んでいなければ nullptr
		Resource::ParticlesAsset* RefSelectedParticleAsset() const;

		// UI
		void DrawToolbar();
		void DrawViewport();
		void DrawInfo();
		void DrawEditPane();
		void DrawParticleTab();

		// 目安になる格子をデバッグ線で描く(大きさの把握用)
		void DrawGrid() const;

		// プレビューの持ち主の位置と向きを、上の設定から書き込む(エフェクトのときだけ)
		void ApplyPreviewTransform(ECS::Entity a_entity, float a_dt);

	private:

		//----------------------------------------------------------------------------------
		// 状態
		//----------------------------------------------------------------------------------
		bool m_isOpen = false;			// 開いているか
		bool m_isOpenRequest = false;	// ImGui へポップアップを開かせる要求(開いた最初の1回だけ)

		// 開いているもの。m_effectGUID はどちらのモードでも「開いているアセット」のGUID
		enum class EMode
		{
			Effect,			// EffectAsset
			EffectPrefab,	// EffectPrefab
		};
		EMode m_mode = EMode::Effect;

		Core::GUID m_effectGUID = Core::DEFAULT_GUID;
		Engine::Handle<Resource::EffectAsset> m_effectHandle = {};
		Engine::Handle<Resource::EffectPrefab> m_effectPrefabHandle = {};

		float m_prefabElapsed = 0.0f;	// エフェクトプレハブを炊いてからの経過時間(秒)

		// プレビュー用のエディターシーン。
		// 閉じても捨てずに使い回す。World::Release() はリソースのGC掃除まで走るので、
		// ゲームのシーンが生きている間に呼ぶとゲーム側のモデルまで解放してしまう
		// アプリ寿命のサービス一式(借り物)
		ECS::EngineServices* m_pServices = nullptr;

		std::unique_ptr<Engine::ECS::World> m_upWorld = nullptr;

		// シーンビューと同じフリーカメラ。
		// プレビュー専用の実体を持つ(シーンビュー側の位置を動かさないため)
		std::unique_ptr<EditorCamera> m_upCamera = nullptr;

		//----------------------------------------------------------------------------------
		// 描画構成
		//
		// このワールドを描くのに使うパイプライン。
		// 開いた時点でゲームが使っていたものを借りるので、絵作りは本番と揃う。
		// カメラが一度も積まれていなければ無効のままで、プレビューは出ない
		//----------------------------------------------------------------------------------
		Engine::Handle<Engine::Graphics::Pipeline::RenderingPipelineAsset> m_pipelineHandle = {};

		// このプレビューのカメラを GraphicsEngine 側で識別する鍵。
		// ワールドが違えば別のカメラとして扱われるので、添字は固定でよい
		static constexpr uint32_t PREVIEW_CAMERA_ENTITY = 0;

		//----------------------------------------------------------------------------------
		// 再生
		//----------------------------------------------------------------------------------
		bool  m_isPlaying = true;		// 再生中か(止めると時間を進めない)
		bool  m_isLoop = true;			// 出し切ったら頭から繰り返す
		float m_playSpeed = 1.0f;		// 再生速度の倍率
		bool  m_isRestartRequest = false;

		//----------------------------------------------------------------------------------
		// 編集
		//----------------------------------------------------------------------------------
		int m_selectedParticlePart = 0;	// Particle タブで開いているパーツ番号
		float m_editPaneWidth = 420.0f;	// 右の編集欄の幅(px)

		//----------------------------------------------------------------------------------
		// 表示
		//----------------------------------------------------------------------------------
		bool m_isDrawGrid = true;
		float m_gridSize = 10.0f;		// 格子の半径(m)

		// 背景(環境設定)。閉じても覚えておき、次に開いたときも同じ背景で見る
		enum class EBackground
		{
			Black,			// 空・フォグ無しの黒背景(SceneAmbient::CreatePlain)
			CurrentScene,	// 開く前のシーンの環境設定そのまま
		};
		EBackground m_background = EBackground::Black;

		// 黒背景のときに流し込む環境設定
		std::unique_ptr<Engine::Scene::SceneAmbient> m_upPlainAmbient = nullptr;

		//----------------------------------------------------------------------------------
		// プレビューの持ち主(エフェクトを付けたエンティティ)の動かし方
		//
		// アセットは持ち主の座標系で作るので、持ち主を回したときに向きが付いてくるか、
		// 動かしたときに Local の粒が付いてきて World の粒が置き去りになるかを、ここで確かめる
		//----------------------------------------------------------------------------------
		Math::Vector3 m_previewRotation = { 0.0f, 0.0f, 0.0f };	// 持ち主の向き(度。Pitch / Yaw / Roll)
		bool  m_isPreviewOrbit = false;		// 原点のまわりを回す
		float m_previewOrbitRadius = 2.0f;	// 回す半径(m)
		float m_previewOrbitSpeed = 90.0f;	// 回す速さ(度/秒)
		float m_previewOrbitAngle = 0.0f;	// いまの角度(度)

	};
}
