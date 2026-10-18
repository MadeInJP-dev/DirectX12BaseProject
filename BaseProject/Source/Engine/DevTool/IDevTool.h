#pragma once

#include "Engine/Common/EngineConfigTypes.h"
#include "Engine/Graphics/D3D12/D3D12Types.h"
#include "Core/Math/Matrix.h"

namespace Engine::Graphics::D3D12
{
	class DescriptorHeapManager;
}

namespace Engine::ECS
{
	struct EngineServices;
}

namespace Engine::Scene
{
	class SceneAmbient;
}

namespace Engine::DevTool
{
	//==========================================================================================
	// ツールが割り込ませるカメラ(IDevTool::TryGetCameraOverride の結果)
	//==========================================================================================
	struct CameraOverride
	{
		Math::Matrix worldMat = Math::Matrix::Identity();	// カメラのワールド行列(ビュー行列ではない)
		Math::Matrix projMat = Math::Matrix::Identity();

		// メッシュレットのカリングだけはゲームのカメラで行うか。
		// 描くのは割り込んだカメラからなので、間引かれた様子を外から確かめられる
		bool isCullByGameCamera = false;
	};

	//==========================================================================================
	// エンジンへ差し込む開発ツール(エディター)の窓口
	//
	// 依存の向きは Editor → App → Engine なので、Engine と App はエディターを知らない。
	// エディターはこれを実装し、最上位(main.cpp)が MainEngine::SetDevTool で差し込む。
	// Engine / App はこの窓口だけを呼び、差し込まれていなければツール無しで動く。
	//==========================================================================================
	class IDevTool
	{
	public:

		virtual ~IDevTool() = default;

		//------------------------------------------------------------------------------
		// 寿命と1フレームの流れ(MainEngine が呼ぶ)
		//------------------------------------------------------------------------------

		// 初期化 : デバイス・ウィンドウ・サービスが揃った後に1回
		virtual bool Init(HWND a_hWnd, Graphics::D3D12::DescriptorHeapManager* a_pHeapManager, ECS::EngineServices* a_pServices) = 0;

		// 解放
		virtual void Release() = 0;

		// 更新 : 描画開始の前
		virtual void Update(float a_deltaTime) = 0;

		// 描画 : ゲーム以外のモードで、バックバッファへ重ねて描く
		virtual void Draw(Graphics::D3D12::GraphicsCommandList* a_pCmdList) = 0;

		// モード切り替えをまたいで入力を持ち越さないよう、溜まっている入力を捨てる
		virtual void ResetInput() = 0;

		// カメラの割り込み : ツールが見せたいカメラがあれば中身を返して true
		virtual bool TryGetCameraOverride(EAppMode a_mode, CameraOverride& a_outOverride) const = 0;

		//------------------------------------------------------------------------------
		// シーン(SceneManager が呼ぶ)
		//------------------------------------------------------------------------------

		// シーンが切り替わる・消える : ツールが覚えている選択などを捨てる
		virtual void OnSceneChanged() = 0;

		// ゲームのシーンの代わりに、ツール側の確認用シーンを回しているか(エフェクトの確認など)
		virtual bool IsScenePreviewActive() const = 0;

		// 確認用シーンの更新と描画
		virtual void UpdateScenePreview(float a_deltaTime) = 0;
		virtual void DrawScenePreview() = 0;

		// 確認用シーンで使う環境設定。nullptr ならゲームのシーンのもの(SceneManager が流し込む)
		virtual const Scene::SceneAmbient* GetScenePreviewAmbient() const = 0;

		//------------------------------------------------------------------------------
		// アプリ側から
		//------------------------------------------------------------------------------

		// プロファイラの1フレームを締める
		virtual void EndProfileFrame() = 0;

		// モーダルな画面を出しているか(出している間はモードを切り替えない)
		virtual bool IsModalActive() const = 0;

		// ツールに出す編集UIの関数を登録する(中身は Engine::EditorField で組む)
		virtual void RegisterEditFunc(std::function<void()> a_func) = 0;
	};
}
