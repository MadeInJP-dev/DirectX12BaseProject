#pragma once
namespace App::Component
{
	//================================================================================
	// 単発撃ちするウェポンに持たせるコンポーネント
	//================================================================================
	struct SemioutFireComponent
	{
		float waitTime = 0.0f;			// 前に撃ってから次に発射できるまでの時間 : 秒

		// ランタイムデータ
		struct Runtime
		{
			float waitTime = 0.0f;
		};
		Runtime runtimeData = {};
	};

}