#pragma once
namespace App::Component
{
	//================================================================================
	// バースト撃ちする武器のコンポーネント
	// 
	// リロード、オーバーヒートどちらの場合も、バースト数に達する前に上限が来たら途中で打ち切る
	//================================================================================
	struct BurstFireComponent
	{
		uint32_t burstNum = 0;		// バーストする発射数
		float burstTime = 0.0f;		// バースト内の間隔

		float waitTime = 0.0f;		// バースト間の間隔 : 秒

		// ランタイムデータ
		struct Runtime
		{
			float burstTime = 0.0f;
			float waitTime = 0.0f;
		};
		Runtime runtimeData = {};
	};

}