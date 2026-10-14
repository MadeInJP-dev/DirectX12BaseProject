#pragma once
namespace App::Component
{
	//================================================================================
	// 肩装甲専用武器のミサイルの発射
	//================================================================================
	struct MissileFireComponent
	{
		uint32_t burstNum = 0;		// バーストする発射数

		// 発射位置
		// ファーストポスはミサイル位置からのオフセットを基準として発射開始時の一発めの発射位置になる
		// そこからエンドポスまで発射数に合わせた等間隔基準で発射する
		// 上下段をつけたければ columNumで指定したら farstPos　と　endPosを段数で割った数でバースト数を割り、等間隔で配置する
		Core::Math::Vector3 farstPos;
		Core::Math::Vector3 endPos;
		uint32_t columnNum;

		float burstTime = 0.0f;		// バースト内の間隔
		float waitTime = 0.0f;		// ミサイルがすべて打ち切った後からの次撃てるまでの時間

		bool isBurst = true;		// バースト撃ち・全部同時発射を選べる

		// ランタイムデータ
		struct Runtime
		{
			uint32_t fireCount;			// バースト中の発射カウント
			float burstTime = 0.0f;
			float waitTime = 0.0f;
		};
		Runtime runtimeData = {};
	};

}