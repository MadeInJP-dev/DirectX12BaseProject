#pragma once

#include "Editor/EditorCommon.h"

namespace Engine::Scene
{
	class SceneAmbient;
}

namespace Editor
{
	//======================================================================================
	//
	// 確認用の画面(エフェクトエディター / プレハブエディター)の窓口
	//
	// どちらも ScenePreview を持つモーダルで、開いている間はゲームのシーンを止めて
	// こちらのワールドを回す。MainEditor はこの窓口を並べて回すだけにして、
	// 画面の種類ごとに同じ分岐を書かない。
	// 2つ以上が同時に開くことは無い(どちらもモーダルなので、片方の中から他方を開けない)
	//
	//======================================================================================
	class IPreviewEditor
	{
	public:

		virtual ~IPreviewEditor() = default;

		// 開いているか。ここが true の間、ゲームのシーンは止まる
		virtual bool IsOpen() const = 0;

		// 確認用ワールドの更新 / 描画命令(ゲームのシーンの代わりに回る)
		virtual void UpdateScene(float a_dt) = 0;
		virtual void DrawScene() = 0;

		// カメラの更新(シーンビューのフリーカメラの代わりに回る)
		virtual void UpdateCamera(float a_dt) = 0;

		// カメラの割り込み行列。開いていなければ false
		virtual bool TryGetCameraOverride(Math::Matrix& a_outWorldMat, Math::Matrix& a_outProjMat) const = 0;

		// モーダルの描画。MainEditor がパネルの後に呼ぶ
		virtual void OnDrawImGui() = 0;

		// 終了処理
		virtual void Release() = 0;

		// 背景に使う環境設定。ゲームのシーンのものを使うなら nullptr
		virtual const Engine::Scene::SceneAmbient* GetPreviewAmbient() const { return nullptr; }
	};
}
