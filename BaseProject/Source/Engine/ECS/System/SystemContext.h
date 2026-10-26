#pragma once

namespace Engine
{
	class MainEngine;
}
namespace Engine::Resource
{
	class ResourceManager;
	class AssetDatabase;
}
namespace Engine::Config
{
	class ConfigRegistry;
}
namespace Engine::Input
{
	class InputManager;
}
namespace Engine::Graphics
{
	class DebugDraw;
}
namespace Engine::Graphics::Raytracing
{
	class RayEngine;
}
namespace Engine::Audio
{
	class AudioManager;
}
namespace Engine::Thread
{
	class JobSystem;
}
namespace Engine::Physics
{
	class PhysicsEngine;
}
namespace Engine::GameObject
{
	class ObjectMetaRegistry;
}
namespace Engine::Scene
{
	class SceneManager;
}

namespace Engine::ECS
{
	class World;

	/// <summary>
	/// エンジン側のアプリケーションを立ち上げたら、終了まで解放されないクラス群
	///
	/// ここに足してよいのは「アプリ寿命」のものだけ。
	/// ワールド寿命のものは World::AddResource / GetResource を使うこと。
	/// </summary>
	struct EngineServices
	{
		MainEngine*					pMainEngine			= nullptr;
		Resource::ResourceManager*	pResourceManager	= nullptr;
		Resource::AssetDatabase*	pAssetDatabase		= nullptr;
		Input::InputManager*		pInputManager		= nullptr;
		Graphics::Raytracing::RayEngine*		pRayEngine			= nullptr;
		Audio::AudioManager*		pAudioManager		= nullptr;
		Thread::JobSystem*			pJobSystem			= nullptr;

		// Jolt 全体の持ち主(アロケータ・JobSystem)。
		// シーンごとの物理空間は World リソースの Physics::PhysicsWorld のほう
		Physics::PhysicsEngine*		pPhysicsEngine		= nullptr;

		// エンジン設定の読み書きと設定画面への一覧。
		// 値は各システムが持つ(ウィンドウなら NativeWindow、入力なら InputManager)ので、読むときは持ち主から引く
		Config::ConfigRegistry*		pConfigRegistry		= nullptr;

		// デバッグ用ワイヤーの置き場(GraphicsEngine が持っている)。
		// 以前はここに MainEditor を入れてシステムから直接描かせていたが、
		// アプリからエディターへの依存になるのでエンジン側の置き場に差し替えた。
		// 表示のオンオフは DebugDraw の設定(DebugDrawConfig)が持ち、エディターはそれを触るだけ
		Graphics::DebugDraw*		pDebugDraw			= nullptr;

		// ECS外オブジェクト(GameObject)のクラス情報。
		// シーンの復元とエディターの AddObject 一覧がここからファクトリを引く
		GameObject::ObjectMetaRegistry*	pObjectRegistry	= nullptr;

		// シーンの積み替え(ReserveChangeScene)と、今のワールドの取得。
		// 積み替えを予約しているのは進行役(Sequence 群)とエディター
		Scene::SceneManager*		pSceneManager		= nullptr;
	};

	/// <summary>
	/// システム関数の引数に渡す構造体
	/// 主にシステム内で参照される、使われるデータを共通で送るためのもの
	///
	/// システムのラムダはこれ以外の状態を持たない(無捕獲)こと。
	/// 捕獲するとシーンをまたいで状態が残り、追いにくい不具合になる。
	/// </summary>
	struct SystemContext
	{
		World*			pWorld		= nullptr;		// ワールドの参照
		EngineServices* pServices	= nullptr;		// アプリ寿命
		float			dt			= 0.0f;			// デルタタイム
	};
}
