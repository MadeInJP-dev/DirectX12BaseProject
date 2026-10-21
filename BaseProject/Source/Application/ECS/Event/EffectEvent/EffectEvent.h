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
// ・同じエフェクトが近い場所へ短い間に続けて積まれたら、後のものは出さない(間引く)。
//   同じ所で爆発が重なり続けると、大きな煙の板が何百枚も重なって塗りが詰まるため。
//   直近に出したものは recent に残し、出す側(OneShotEffectSystem)だけが読み書きする。
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

	// 直近に出した単発エフェクト1件分(近くで続けて出たものを間引くのに使う)
	struct OneShotEffectRecent
	{
		Core::GUID effectGUID = Core::DEFAULT_GUID;		// 出したエフェクト
		Math::Vector3 pos = { 0.0f, 0.0f, 0.0f };		// 出した位置(ワールド)
		float scale = 1.0f;								// 出したときの大きさ倍率
		float time = 0.0f;								// 出した時刻(OneShotEffectEventResource::time)
	};

	// ワールドに1つだけ置く単発エフェクトの配列
	struct OneShotEffectEventResource
	{
		std::vector<OneShotEffectEvent> events = {};

		// ---- 間引き(出す側だけが触る) ----
		std::vector<OneShotEffectRecent> recent = {};	// 間引く時間のあいだに出したもの
		float time = 0.0f;								// 出す側が毎フレーム進める時刻(秒)

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
			recent.reserve(a_capacity);
		}
	};
}
