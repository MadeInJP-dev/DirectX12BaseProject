#pragma once

#include "../Data/Model/IO/ModelIO.h"
#include "../Data/Texture/IO/TextureIO.h"
#include "../Data/AnimatorAsset/IO/AnimatorAssetIO.h"
#include "../Data/Shader/IO/ShaderIO.h"
#include "../Data/Particles/IO/ParticlesIO.h"
#include "../Data/Material/IO/MaterialIO.h"
#include "../Data/Mesh/IO/MeshIO.h"
#include "../Data/Animation/IO/AnimationIO.h"
#include "../Data/Prefab/Prefab.h"
#include "../Data/EffectPrefab/EffectPrefab.h"
#include "../Data/Sound/IO/SoundIO.h"
#include "../Data/AudioBehavior/IO/AudioBehaviorIO.h"
#include "../Data/EffectAsset/IO/EffectAssetIO.h"
#include "../Data/Font/IO/FontIO.h"
#include "../Data/UIMaterial/IO/UIMaterialIO.h"
#include "../Data/UIPreset/IO/UIPresetIO.h"

// レンダリングパイプライン : Engine::Resource ではなく Engine::Graphics::Pipeline に居る
#include "Engine/Graphics/RenderingPipeline/IO/RenderingPipelineAssetIO.h"

#include "../Common/ScopedResourceBuild.h"
namespace Engine::Resource
{
	// ロード処理の中間用クラス
	//
	// LoadFromFile は必ずビルドコンテキストを受け取る形にしてある。
	// GPUリソースを作らないアセットは無視してよいが、
	// 作るアセット(Model/Mesh/Texture)は必ずコンテキスト側のコマンドリストへ積むこと。
	template<typename T>
	struct DefaultLoader
	{
		static T LoadFromFile(const std::string& a_path, const ResourceBuildContext* a_pContext)
		{
			// 特殊化されていない型で LoadImmediate<T> が呼ばれたらコンパイルを止める
			static_assert(sizeof(T) == 0, "特殊化されていない型のLoaderが呼ばれました。DefaultLoaderを特殊化してください。");
			return T();
		}
	};

	// -----------------------------------------------------
	// テンプレート特殊化
	// -----------------------------------------------------
	// レンダリングパイプライン
	//
	// 保存されているのはパスの型IDだけなので、実体を作り直すのにレジストリが要る。
	// レジストリはビルドコンテキスト経由で受け取る(GraphicsEngine が持ち主)
	template<>
	struct DefaultLoader<Graphics::Pipeline::RenderingPipelineAsset>
	{
		static Graphics::Pipeline::RenderingPipelineAsset LoadFromFile(const std::string& a_path, const ResourceBuildContext* a_pContext)
		{
			// コンテキストを持たない経路から来ることがある(エディタの単体ロードなど。
			// ResourceManager::LoadImmediate は既定が nullptr)。
			// レジストリが無いとパスを1つも作り直せず、中身が空のまま読み終わってしまうので、
			// 渡されていなければここで開いて受け取る
			ResourceBuildScope _scope(a_pContext);

			return Graphics::Pipeline::RenderingPipelineAssetIO::LoadFromFile(
				a_path, _scope.GetContext().pPassMetaRegistry);
		}
	};

	// モデル
	template<>
	struct DefaultLoader<Model>
	{
		static Model LoadFromFile(const std::string& a_path, const ResourceBuildContext* a_pContext)
		{
			// ModelLoaderクラスの専用処理
			return ModelIO::Import(a_path, a_pContext);
		}
	};
	// テクスチャ
	template<>
	struct DefaultLoader<Texture>
	{
		static Texture LoadFromFile(const std::string& a_path, const ResourceBuildContext* a_pContext)
		{
			return TextureIO::LoadFromFile(a_path, a_pContext);
		}
	};
	// アニメーター
	template<>
	struct DefaultLoader<AnimatorAsset>
	{
		static AnimatorAsset LoadFromFile(const std::string& a_path, const ResourceBuildContext* a_pContext)
		{
			return AnimatorAssetIO::LoadFromFile(a_path, *a_pContext->pResourceManager);
		}
	};
	// シェーダー
	template<>
	struct DefaultLoader<Shader>
	{
		static Shader LoadFromFile(const std::string& a_path, const ResourceBuildContext* /*a_pContext*/)
		{
			return ShaderIO::LoadShaderFromFile(a_path);
		}
	};
	// パーティクル
	template<>
	struct DefaultLoader<ParticlesAsset>
	{
		static ParticlesAsset LoadFromFile(const std::string& a_path, const ResourceBuildContext* a_pContext)
		{
			return ParticlesAssetIO::LoadFromFile(a_path, *a_pContext->pResourceManager);
		}
	};
	// マテリアル
	template<>
	struct DefaultLoader<Material>
	{
		static Material LoadFromFile(const std::string& a_path, const ResourceBuildContext* a_pContext)
		{
			return MaterialIO::LoadFromFile(a_path, a_pContext);
		}
	};
	// メッシュ
	template<>
	struct DefaultLoader<Mesh>
	{
		static Mesh LoadFromFile(const std::string& a_path, const ResourceBuildContext* a_pContext)
		{
			return MeshIO::LoadFromFile(a_path, a_pContext);
		}
	};
	// アニメーション
	template<>
	struct DefaultLoader<AnimationData>
	{
		static AnimationData LoadFromFile(const std::string& a_path, const ResourceBuildContext* /*a_pContext*/)
		{
			return AnimationIO::LoadFromFile(a_path);
		}
	};
	// プレハブ
	template<>
	struct DefaultLoader<Prefab>
	{
		static Prefab LoadFromFile(const std::string& a_path, const ResourceBuildContext* a_pContext)
		{
			return Prefab::LoadFromFile(a_path, a_pContext);
		}
	};
	// エフェクトプレハブ
	template<>
	struct DefaultLoader<EffectPrefab>
	{
		static EffectPrefab LoadFromFile(const std::string& a_path, const ResourceBuildContext* a_pContext)
		{
			return EffectPrefab::LoadFromFile(a_path, a_pContext);
		}
	};
	// サウンド
	template<>
	struct DefaultLoader<Sound>
	{
		static Sound LoadFromFile(const std::string& a_path, const ResourceBuildContext* a_pContext)
		{
			return SoundIO::Load(a_path, a_pContext ? a_pContext->pAudioEngine : nullptr);
		}
	};
	// オーディオビヘイビア
	template<>
	struct DefaultLoader<AudioBehavior>
	{
		static AudioBehavior LoadFromFile(const std::string& a_path, const ResourceBuildContext* /*a_pContext*/)
		{
			return AudioBehaviorIO::LoadFromFile(a_path);
		}
	};
	// エフェクト
	template<>
	struct DefaultLoader<EffectAsset>
	{
		static EffectAsset LoadFromFile(const std::string& a_path, const ResourceBuildContext* a_pContext)
		{
			return EffectAssetIO::LoadFromFile(a_path, *a_pContext->pResourceManager);
		}
	};
	// フォント
	template<>
	struct DefaultLoader<Font>
	{
		static Font LoadFromFile(const std::string& a_path, const ResourceBuildContext* a_pContext)
		{
			// アトラステクスチャをGPUへ転送するため、コンテキストをそのまま渡す
			return FontIO::LoadFromFile(a_path, a_pContext);
		}
	};
	// UIマテリアル
	template<>
	struct DefaultLoader<UIMaterial>
	{
		static UIMaterial LoadFromFile(const std::string& a_path, const ResourceBuildContext* a_pContext)
		{
			// ノイズのテクスチャを引き当てるので、マネージャーを渡す
			return UIMaterialIO::LoadFromFile(a_path, *a_pContext->pResourceManager);
		}
	};
	// UIプリセット : 中身は文字列のまま預かるだけなので、他のリソースは引かない
	template<>
	struct DefaultLoader<UIPreset>
	{
		static UIPreset LoadFromFile(const std::string& a_path, const ResourceBuildContext* /*a_pContext*/)
		{
			return UIPresetIO::LoadFromFile(a_path);
		}
	};
}
