#pragma once

#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/EffectAsset/EffectAsset.h"
#include "Engine/EditorField/EditorField.h"
#include "Engine/EditorField/EditorField.inl"
#include "Engine/ECS/World/World.h"

namespace App::Component
{
	// 死亡モーションの進み具合 : ランタイムだけの値なので保存しない
	enum class EDeathMotionPhase : uint8_t
	{
		Alive,		// まだ生きている
		Falling,	// 爆発して、黒くなりながら回って落ちている
		Landed,		// 地面について、消えるのを待っている
	};

	//==========================================================================================
	// キャラクターにつける死亡時モーションのコンポーネント
	//
	// これがついたキャラクターは死亡時
	// 爆発
	// ↓
	// モデルが黒くなって煙と破片を出しながら、回転しつつ自由落下
	// ↓
	// 地面についたらまた爆発
	// ↓
	// 時間で消えるといった形
	//
	// ・動かすのは DeathMotionSystem。死亡(HealthComponent.isDead)を見て落ち始める。
	// ・爆発は単発なので、OneShotEffectEventResource へ積むだけ(出すのは OneShotEffectSystem)。
	// ・煙は落ちているあいだ出し続けるので、自分の子のエフェクトとして出して付いて来させる
	//   (印は DeathSmokeTag)。地面についたら止め、本体と一緒に消える。
	// ・これを持つものは、HealthComponent の releaseDelay では消えない
	//   (DeathReleaseSystem の対象外)。消すのはここの destroyTime。
	// ・重力はシーンの設定(ScenePhysics → PhysicsWorld)。接地はここで解く
	//   (BallisticComponent と同じく StaticObject へレイを打つ)。
	//   地面が見つからないまま maxFallTime を過ぎたら、その場で地面についた扱いにする。
	//==========================================================================================
	struct DeathMotionComponent
	{
		// ---- 爆発(保存) ----
		// 死んだ瞬間に出す。地面用の爆発(landExplosion)が無ければ、地面についたときもこれ。
		// 単発なのでイベントとして出す
		Core::GUID explosionGUID = Core::DEFAULT_GUID;
		Engine::Handle<Engine::Resource::EffectAsset> explosionHandle = {};		// ランタイム用(Fixup が取る)
		float explosionScale = 1.0f;

		// ---- 地面についたときの爆発(保存) ----
		// 地面で爆ぜるので、空中の爆発とは別のもの(上へ噴き上がる火・土煙・土くれ)を出せる。
		// 未設定なら explosion を使う
		Core::GUID landExplosionGUID = Core::DEFAULT_GUID;
		Engine::Handle<Engine::Resource::EffectAsset> landExplosionHandle = {};	// ランタイム用(Fixup が取る)
		float landExplosionScale = 1.0f;

		// ---- 煙(保存) ----
		// 落ちているあいだ出し続ける。ループするエフェクトを渡すこと
		Core::GUID smokeGUID = Core::DEFAULT_GUID;
		Engine::Handle<Engine::Resource::EffectAsset> smokeHandle = {};			// ランタイム用(Fixup が取る)
		float smokeScale = 1.0f;

		// ---- 落ち方(保存) ----
		// 回転の強さ。回転自体は上下左右ランダムだが、回転速度を個々で決める
		// (1 で 180 度/秒。個体ごとに ±25% ばらす)
		float rotStrength = 1.0f;
		float gravityScale = 1.0f;		// 重力の倍率(シーンの重力 ScenePhysics に掛ける)
		float blackenTime = 0.3f;		// 死んでから真っ黒になるまでの秒数(0 で即座に黒)
		float groundOffset = 0.5f;		// 中心から接地面までの距離(m)
		float maxFallTime = 10.0f;		// 地面が見つからないときに、地面についた扱いにするまでの秒数

		// ---- 消え方(保存) ----
		// 地面についたのちモデルが消えるまでの時間 : マイナス値なら消えない 0なら即時に消える
		float destroyTime = 0.0f;

		// パッと消えるか、だんだん小さくなって消えるか
		// (不透明のモデルは色のアルファで透けないので、縮めて消す)
		bool isDestroyFade = false;

		// ---- ランタイム(保存しない) ----
		EDeathMotionPhase phase = EDeathMotionPhase::Alive;
		float timer = 0.0f;										// 今の段に入ってからの秒数
		bool hasSmoke = false;									// 煙を出した(同時数の枠を使っている)
		Math::Vector3 velocity = { 0.0f, 0.0f, 0.0f };			// 落ちる速さ
		Math::Vector3 spinAxis = { 0.0f, 1.0f, 0.0f };			// 回転の軸(単位ベクトル)
		float spinSpeedDeg = 0.0f;								// 回転の速さ(度/秒)
		// 向きと着地した位置は自分で持ち、毎フレーム書き込む。
		// 向きを毎フレーム書き戻すもの(LookAngle → RotationSystem)や、惰性で動かし続けるものがあっても、
		// 回転と着地点がそれに上書きされないようにするため
		Math::Quaternion rotation = { 0.0f, 0.0f, 0.0f, 1.0f };	// 今の向き
		Math::Vector3 landedPos = { 0.0f, 0.0f, 0.0f };			// 着地した位置
		Math::Color baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };		// 黒くする前の色
		float baseEmissiveIntensity = 0.0f;						// 黒くする前の発光の強さ
		Math::Vector3 baseScale = { 1.0f, 1.0f, 1.0f };			// 縮める前の大きさ
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::DeathMotionComponent>
{
	// コンポーネントはデストラクタが走らないので、参照を返すのはここの仕事
	static void Release(void* a_pData, const Engine::ECS::EngineServices& a_services)
	{
		App::Component::DeathMotionComponent& _comp = Engine::EditorField::RefValue<App::Component::DeathMotionComponent>(a_pData);
		auto& _resourceManager = *a_services.pResourceManager;

		_resourceManager.ReleaseHandle(_comp.explosionHandle);
		_resourceManager.ReleaseHandle(_comp.landExplosionHandle);
		_resourceManager.ReleaseHandle(_comp.smokeHandle);
	}

	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::DeathMotionComponent& _comp = Engine::EditorField::RefValue<App::Component::DeathMotionComponent>(a_pData);

		// ハンドルと進み具合はランタイム状態なので保存しない
		a_ar.Field("explosionGUID", _comp.explosionGUID);
		a_ar.Field("explosionScale", _comp.explosionScale);
		a_ar.Field("smokeGUID", _comp.smokeGUID);
		a_ar.Field("smokeScale", _comp.smokeScale);
		a_ar.Field("rotStrength", _comp.rotStrength);
		a_ar.Field("gravityScale", _comp.gravityScale);
		a_ar.Field("blackenTime", _comp.blackenTime);
		a_ar.Field("groundOffset", _comp.groundOffset);
		a_ar.Field("maxFallTime", _comp.maxFallTime);
		a_ar.Field("destroyTime", _comp.destroyTime);
		a_ar.Field("isDestroyFade", _comp.isDestroyFade);

		// 後から足したもの : 古いバイナリには無いので読まない(既定の「未設定」のまま = 空中の爆発を使う)
		if (!a_ar.IsLegacyLayout())
		{
			a_ar.Field("landExplosionGUID", _comp.landExplosionGUID);
			a_ar.Field("landExplosionScale", _comp.landExplosionScale);
		}
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::DeathMotionComponent& _comp = Engine::EditorField::RefValue<App::Component::DeathMotionComponent>(a_context.pData);
		auto& _services = *a_context.pWorld->RefEngineServices();

		Engine::EditorField::HelpText("死んだら爆発 → 黒くなって回りながら落下 → 地面で爆発");

		// 爆発と煙は同じ名前の欄が並ぶので、ID を分けておく
		{
			Engine::EditorField::IDScope _id("Explosion");
			Engine::EditorField::Header("Explosion");
			Engine::EditorField::AssetField<Engine::Resource::EffectAsset>(
				_services, "Effect", "EffectAsset", _comp.explosionGUID, _comp.explosionHandle);
			Engine::EditorField::Tooltip("(死んだ瞬間に出す。地面用が無ければ、地面についた瞬間も)");
			Engine::EditorField::Field("Scale", _comp.explosionScale, 0.05f, 0.0f);
		}
		{
			Engine::EditorField::IDScope _id("LandExplosion");
			Engine::EditorField::Header("Land Explosion");
			Engine::EditorField::AssetField<Engine::Resource::EffectAsset>(
				_services, "Effect", "EffectAsset", _comp.landExplosionGUID, _comp.landExplosionHandle);
			Engine::EditorField::Tooltip("(地面についた瞬間に出す。未設定なら Explosion を使う)");
			Engine::EditorField::Field("Scale", _comp.landExplosionScale, 0.05f, 0.0f);
		}
		{
			Engine::EditorField::IDScope _id("Smoke");
			Engine::EditorField::Header("Smoke");
			Engine::EditorField::AssetField<Engine::Resource::EffectAsset>(
				_services, "Effect", "EffectAsset", _comp.smokeGUID, _comp.smokeHandle);
			Engine::EditorField::Tooltip("(落ちているあいだ付いて来る。ループするものを入れる)");
			Engine::EditorField::Field("Scale", _comp.smokeScale, 0.05f, 0.0f);
		}

		Engine::EditorField::Header("Fall");
		Engine::EditorField::Field("RotStrength", _comp.rotStrength, 0.05f, 0.0f);
		Engine::EditorField::Tooltip("(1 で 180 度/秒。軸はランダム)");
		Engine::EditorField::Field("GravityScale", _comp.gravityScale, 0.01f);
		Engine::EditorField::Field("BlackenTime", _comp.blackenTime, 0.01f, 0.0f);
		Engine::EditorField::Tooltip("(死んでから真っ黒になるまでの秒数)");
		Engine::EditorField::Field("GroundOffset", _comp.groundOffset, 0.01f, 0.0f);
		Engine::EditorField::Tooltip("(中心から接地面までの距離)");
		Engine::EditorField::Field("MaxFallTime", _comp.maxFallTime, 0.1f, 0.0f);
		Engine::EditorField::Tooltip("(地面が見つからないとき、この秒数でその場に着地した扱いにする)");

		Engine::EditorField::Header("Destroy");
		Engine::EditorField::Field("DestroyTime", _comp.destroyTime, 0.05f, -1.0f, 60.0f);
		Engine::EditorField::Tooltip("(着地してから消えるまでの秒数。マイナスで消えない / 0 で即座に消える)");
		Engine::EditorField::Field("IsDestroyFade", _comp.isDestroyFade);
		Engine::EditorField::Tooltip("(オンなら DestroyTime をかけて縮んで消える)");

		// 進み具合は表示のみ
		switch (_comp.phase)
		{
		case App::Component::EDeathMotionPhase::Alive:		Engine::EditorField::HelpText("Alive"); break;
		case App::Component::EDeathMotionPhase::Falling:	Engine::EditorField::ErrorText("Falling : %.2f", _comp.timer); break;
		case App::Component::EDeathMotionPhase::Landed:		Engine::EditorField::ErrorText("Landed : %.2f", _comp.timer); break;
		default: break;
		}
	}
};
