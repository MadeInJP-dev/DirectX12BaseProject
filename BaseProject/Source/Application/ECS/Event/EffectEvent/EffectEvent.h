#pragma once

//==========================================================================================
//
// 単発エフェクトのイベント
//
// 単発エフェクトを出す場合、出したい側はエフェクトと置き方をここへ積むだけにして、
// OneShotEffectSystem が1フレーム分をまとめてエンティティとして出す。
// イベントが出された時点で、出したエンティティからの制御ではなくなる
// (出したエフェクトは出し切ったら自分から消える)。
//
// ・出すエンティティの組み合わせ(シグネチャ)は固定。
//   中身は EffectSpawnHelper.cpp の BuildEffectEntity 1か所に閉じてあり、
//   ReserveSpawnEffectAt と同じものを使う。
//   固有の処理(出したあとも動かす・止める)が要るものは、こちらの単発エフェクトでは出さない。
// ・エフェクトはハンドルではなく GUID で積む。
//   積んだ側がハンドルを返しても(消えても)、出す側が取り直せるようにするため。
//
// 積む : 単発のエフェクトを出したい側(DeathMotionSystem など)
// 出す・消す : OneShotEffectSystem(PostUpdate)
//
//==========================================================================================

namespace App::ECS
{
	// 単発エフェクト1件分
	struct OneShotEffectEvent
	{
		Core::GUID effectGUID = Core::DEFAULT_GUID;		// 出すエフェクト(未設定なら出さない)
		Math::Matrix worldMat = {};						// 出す位置と向き(ワールド。拡縮は見ない)
		float scale = 1.0f;								// エフェクト全体の大きさ倍率
	};

	// ワールドに1つだけ置く単発エフェクトの配列
	struct OneShotEffectEventResource
	{
		std::vector<OneShotEffectEvent> events = {};

		// 単発エフェクトを追加する
		void Push(const OneShotEffectEvent& a_event)
		{
			if (a_event.effectGUID == Core::DEFAULT_GUID) return;
			events.push_back(a_event);
		}

		// capacity は残すので、毎フレームの再確保は起きない
		void Clear()
		{
			events.clear();
		}

		// 事前確保(初期化時に一度だけ呼ぶ想定)
		void Reserve(size_t a_capacity)
		{
			events.reserve(a_capacity);
		}
	};
}
