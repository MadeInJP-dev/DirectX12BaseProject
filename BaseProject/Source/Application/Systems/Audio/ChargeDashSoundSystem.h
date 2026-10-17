#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	/// <summary>
	/// チャージダッシュの溜めの音を鳴らすシステム
	///
	/// ChargeDashComponent の溜めの状態から、ChargeDashSoundComponent の音を鳴らし分ける。
	/// ・溜めている間       : 溜めの音(単発)
	/// ・溜まりきった瞬間   : 溜まった音(単発)
	/// ・溜まりきって押している間 : 待機の音(ループ。溜めの音とは別の音)
	/// 離した・撃ち出したら、溜めの音と待機の音を止める
	/// </summary>
	class ChargeDashSoundSystem : public App::ECS::APPISystem
	{
	public:

		void Init(App::ECS::APPWorld& a_world) override;
	};
}
