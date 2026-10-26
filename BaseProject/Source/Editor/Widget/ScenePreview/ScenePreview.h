#pragma once

#include "Editor/EditorCommon.h"

namespace Engine
{
	namespace ECS { class World; }
	namespace Graphics::Pipeline { class RenderingPipelineAsset; }
}

namespace Editor
{
	class EditorCamera;

	//======================================================================================
	//
	// 確認用の画面が持つ「小さなシーン」
	//
	// エフェクトエディターとプレハブエディターは、どちらも
	//   ・ゲームと同じ構成の専用ワールドを1つ持ち、開いている間はゲームのシーンの代わりに回す
	//   ・描画構成は開いた瞬間にゲームが使っていたものを借りる(絵作りを本番と揃えるため)
	//   ・カメラはシーンビューと同じ EditorCamera(右ドラッグ中だけ操作)
	//   ・再生(一時停止・速度)・目安の格子・出力をモーダルの中に画像で出す
	// という同じ作りで、同じコードを二重に持っていた。その共通部分をここにまとめる。
	//
	// 何を出すか(エフェクト / プレハブ)と編集欄は、持ち主の画面が受け持つ。
	//
	//======================================================================================
	class ScenePreview
	{
	public:

		// a_pServices : アプリ寿命のサービス一式(借り物)
		explicit ScenePreview(ECS::EngineServices* a_pServices);
		~ScenePreview();
		NON_COPYABLE_NON_MOVABLE(ScenePreview);

		//----------------------------------------------------------------------------------
		// 開閉
		//----------------------------------------------------------------------------------

		// 開くときの用意 : ワールドとカメラを作り(初回だけ)、描画構成を借り、カメラを定位置へ戻す。
		// 再生も頭から(止めていても動かす)
		void BeginOpen();

		// 閉じるとき : 押しっぱなし扱いを閉じたあとへ持ち越さない
		void EndOpen();

		// 終了処理。World::Release() は呼ばない(下の m_upWorld の説明)
		void Release();

		//----------------------------------------------------------------------------------
		// ワールド
		//----------------------------------------------------------------------------------

		// 確認用のワールド(BeginOpen の前は nullptr)
		Engine::ECS::World* RefWorld() const { return m_upWorld.get(); }

		// ゲームのシーンと同じ順でフェーズを回す(BeginFrame から PostUpdate まで)。
		// a_dt は止めている間 0、それ以外は速度を掛けたもの(GetDeltaTime)を渡す
		void RunUpdate(float a_dt);

		// 描画命令を積む : PreDraw〜PostDraw、このプレビューのカメラ、目安の格子
		void RunDraw();

		// 渡したものを片付ける(解放予約をこの場で消化する)
		void DestroyEntities(const std::vector<ECS::Entity>& a_entityVec);

		// 居るものを全部片付ける(出したものが動いて出したもの(弾・エフェクト)も含めて)
		void DestroyAllEntities();

		//----------------------------------------------------------------------------------
		// 再生
		//----------------------------------------------------------------------------------

		// 再生の状態を掛けた経過時間。止めている間は 0(その瞬間で固まる)
		float GetDeltaTime(float a_dt) const { return m_isPlaying ? (a_dt * m_playSpeed) : 0.0f; }

		bool IsPlaying() const { return m_isPlaying; }
		void SetPlaying(bool a_isPlaying) { m_isPlaying = a_isPlaying; }

		//----------------------------------------------------------------------------------
		// カメラ
		//----------------------------------------------------------------------------------

		// フリーカメラの更新(MainEditor がシーンビューのカメラの代わりに呼ぶ)
		void UpdateCamera(float a_dt);

		// カメラの割り込み行列
		bool TryGetCameraOverride(Math::Matrix& a_outWorldMat, Math::Matrix& a_outProjMat) const;

		// カメラ(ギズモを出すのに使う)。BeginOpen の前は nullptr
		EditorCamera* RefCamera() const { return m_upCamera.get(); }

		//----------------------------------------------------------------------------------
		// UI の部品(上段に並べる順は持ち主が決める)
		//----------------------------------------------------------------------------------

		void DrawPlayButton();			// Play / Pause
		void DrawSpeedSlider();			// 再生速度
		void DrawGridToggle();			// 目安の格子
		void DrawResetCameraButton();	// カメラを定位置へ

		// デバッグ線が切られているとき、格子などが出ない理由を添える
		void DrawWireOffNote(bool a_isNeeded) const;

		/// <summary>
		/// このプレビューの出力を、収まる大きさの画像で出す
		/// </summary>
		/// <param name="a_reserveLines">下に残す行数(情報欄のぶん)</param>
		/// <param name="a_outMin">画像の左上(スクリーン座標)。ギズモを重ねるのに使う</param>
		/// <param name="a_outSize">画像の大きさ</param>
		/// <returns>画像を出せたら true(描画構成や出力がまだ無ければ理由を出して false)</returns>
		bool DrawViewport(float a_reserveLines, ImVec2& a_outMin, ImVec2& a_outSize);

	private:

		// このプレビューのカメラを GraphicsEngine へ積む
		// (このワールドにはカメラのエンティティが居ないので、システム任せにできない)
		void SubmitCamera();

		// 目安になる格子をデバッグ線で描く(大きさの把握用。出す位置の原点に敷く)
		void DrawGrid() const;

		// カメラを定位置へ
		void ResetCamera();

	private:

		// アプリ寿命のサービス一式(借り物)
		ECS::EngineServices* m_pServices = nullptr;

		// 確認用のワールド。
		// 閉じても捨てずに使い回す。World::Release() はリソースのGC掃除まで走るので、
		// ゲームのシーンが生きている間に呼ぶとゲーム側のモデルまで解放してしまう
		std::unique_ptr<Engine::ECS::World> m_upWorld = nullptr;

		// シーンビューと同じフリーカメラ。
		// プレビュー専用の実体を持つ(シーンビュー側の位置を動かさないため)
		std::unique_ptr<EditorCamera> m_upCamera = nullptr;

		//----------------------------------------------------------------------------------
		// 描画構成
		//
		// 開いた時点でゲームが使っていたものを借りる。
		// カメラが一度も積まれていなければ無効のままで、プレビューは出ない
		//----------------------------------------------------------------------------------
		Engine::Handle<Engine::Graphics::Pipeline::RenderingPipelineAsset> m_pipelineHandle = {};

		// このプレビューのカメラを GraphicsEngine 側で識別する鍵。
		// ワールドが違えば別のカメラとして扱われるので、添字は固定でよい
		static constexpr uint32_t PREVIEW_CAMERA_ENTITY = 0;

		//----------------------------------------------------------------------------------
		// 再生・表示
		//----------------------------------------------------------------------------------
		bool  m_isPlaying = true;		// 時間を進めるか
		float m_playSpeed = 1.0f;		// 再生速度の倍率
		bool  m_isDrawGrid = true;		// 目安の格子を出すか
		float m_gridSize = 10.0f;		// 格子の半径(m)
	};
}
