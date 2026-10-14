#pragma once
namespace App::Component
{
	//================================================================================
	// オーバーヒート : 武器が一定間隔打ったら一定時間打てなくなる挙動
	// 
	// リロードと対になる
	//================================================================================
	struct WeaponOverHeatComponent
	{
		float perShot = 0.0f;		// 一発あたりに溜まる熱量
		float limt = 0.0f;			// ここまでたまるとオーバーヒート

		float coolRate = 0.0f;				// 毎秒冷える量
		float overheatCoolScale = 0.0f;		// オーバーヒート中の冷却倍率

		float restertHeapRatio = 0.0f;		// 復帰する熱の割合 : ０の場合完全に冷えるまで打てなくなる

		struct Effect
		{
			Core::GUID guid = Core::DEFAULT_GUID;
			Engine::Handle<Engine::Resource::EffectAsset> handle = {};
		};

		Effect heatEffect = {};			// オーバーヒートに達したときに出るエフェクト : 単発出し用

		// ランタイムデータ
		struct Runtime
		{
			float heat = 0.0f;				// 現在の熱量
			bool isOverheat = false;		// 現在オーバーヒート中か
		};
		Runtime runtimeData = {};
	};

}