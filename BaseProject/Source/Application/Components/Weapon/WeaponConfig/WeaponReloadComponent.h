#pragma once
namespace App::Component
{
	//================================================================================
	// リロード : マガジン数分発射したら入る挙動
	// 
	// オーバーヒートと対になる
	//================================================================================
	struct WeaponRelodComponent
	{
		uint32_t magazineMaxCapacity = 0;	// マガジンの最大容量
		float relodTime = 0.0f;			// リロードにかかる時間

		// ランタイムデータ
		struct Runtime
		{
			uint32_t capacityRemaining = 0;		// マガジン内の残弾数
			bool isRelod = false;				// 現在リロード中か
		};
		Runtime runtimeData = {};
	};

}