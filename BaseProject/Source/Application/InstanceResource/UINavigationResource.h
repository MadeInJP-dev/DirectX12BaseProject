#pragma once

#include "Engine/Input/Core/InputDevice.h"

namespace App::InstanceResource
{
	//==========================================================================================
	//
	// UI の項目移動(キー・パッドで UI を順に選ぶ)を決めるワールドリソース。
	//
	// ・操作の切り替え
	//   いまキー・パッドで選んでいるのか(isNavMode)、カーソル(マウス)で触っているのかを持つ。
	//   矢印・スティック・十字キー・決定を押すと項目移動へ、マウスを動かす・クリックするとカーソルへ戻る。
	//   項目移動の間はカーソルがどの UI にも乗らない(選んでいる UI とカーソルの下の UI が両方光らないように)。
	//
	// ・グループの取り合い
	//   同じワールドに UINavigationGroup が複数あって同時に出ているときは、
	//   priority のいちばん大きい1つだけが選択を見せる(同じ値なら後から名乗ったほう)。
	//     PreUpdate … 各グループが、選んでいる UI と一緒に名乗る(Claim)
	//     Update    … UI が自分が選ばれているかを見る(IsFocused)
	//   入力で選択を動かすのは、前のフレームに勝ったグループ(WasOwner)。
	//   動かすのは名乗りより前なので、このフレームの結果はまだ出ていないため。
	//
	// ・操作の切り替えと移動量は、そのフレームで最初に PreUpdate を通ったグループが1回だけ作る。
	//   どのグループから見ても同じ値になるように、ここへ置いてある。
	//
	// 書くのも読むのも UI(GameObject)で、ECS のシステムは触らない(UICursorResource と同じ)。
	// アドレスは比較にしか使わないので、名乗った相手がこの後に消えても安全
	//
	//==========================================================================================
	struct UINavigationResource
	{
		//--------------------------------------------------------------------------------------
		// 操作の切り替え(フレームをまたいで持つ)
		//--------------------------------------------------------------------------------------
		bool isInitialized = false;					// 最初のフレームの値を入れたか
		bool isNavMode = false;						// キー・パッドで選んでいるか(false ならカーソル)
		uint64_t inputFrameIndex = UINT64_MAX;		// 操作の切り替えと移動量を作ったフレーム

		Math::Vector2 lastCursorPos = {};			// 前のフレームのカーソル(UI の px)。動いたかを見る
		bool hasLastCursorPos = false;				// 前のフレームにカーソルの位置が取れたか
		Engine::Input::EInputDevice lastActiveDevice = Engine::Input::EInputDevice::None;	// 前のフレームに最後に触られたデバイス

		//--------------------------------------------------------------------------------------
		// 移動量
		//--------------------------------------------------------------------------------------
		int step = 0;					// このフレームに選択を動かす向き(-1 で前、+1 で次、0 で動かさない)
		int holdDir = 0;				// 倒している向き(押しっぱなしで繰り返すため)
		float repeatTimer = 0.0f;		// 次に繰り返すまでの残り時間(秒)

		// 移動を止めるよう頼まれた最後のフレーム(UINT64_MAX で頼まれていない)。
		// キー割り当ての待ち受けのように、スティック・十字キー・LB/RB を別の用途に使っている間に立てる
		uint64_t moveLockFrameIndex = UINT64_MAX;

		//--------------------------------------------------------------------------------------
		// グループの取り合い
		//--------------------------------------------------------------------------------------
		uint64_t claimFrameIndex = UINT64_MAX;		// 集計しているフレーム
		const void* pOwner = nullptr;				// 勝っているグループ
		int priority = 0;							// 勝っているグループの優先度
		const void* pFocused = nullptr;				// 勝っているグループが選んでいる UI

		uint64_t lastClaimFrameIndex = UINT64_MAX;	// 1つ前に集計したフレーム
		const void* pLastOwner = nullptr;			// そのときに勝ったグループ

		/// <summary>
		/// グループが名乗る(PreUpdate から)
		/// </summary>
		/// <param name="a_frameIndex">今のフレーム(ObjectContext::frameIndex)</param>
		/// <param name="a_pOwner">名乗るグループ</param>
		/// <param name="a_priority">優先度(大きいほうが勝つ)</param>
		/// <param name="a_pFocused">そのグループが選んでいる UI</param>
		void Claim(uint64_t a_frameIndex, const void* a_pOwner, int a_priority, const void* a_pFocused)
		{
			if (a_pOwner == nullptr) return;

			// 新しいフレームの最初の名乗り : 前のフレームの結果を控えてから捨てる
			if (claimFrameIndex != a_frameIndex)
			{
				lastClaimFrameIndex = claimFrameIndex;
				pLastOwner = pOwner;

				claimFrameIndex = a_frameIndex;
				pOwner = nullptr;
				priority = 0;
				pFocused = nullptr;
			}

			// 同じ値なら後から名乗ったほう
			if (pOwner != nullptr && a_priority < priority) return;

			pOwner = a_pOwner;
			priority = a_priority;
			pFocused = a_pFocused;
		}

		/// <summary>前のフレームに勝ったグループか(PreUpdate から。入力で選択を動かしてよいか)</summary>
		/// <remarks>前のフレームに誰も名乗っていなければ、誰でもない</remarks>
		bool WasOwner(uint64_t a_frameIndex, const void* a_pOwner) const
		{
			if (a_pOwner == nullptr) return false;

			// このフレームの名乗りがもう始まっていれば、控えたほうが前のフレーム
			const bool _isClaimStarted = (claimFrameIndex == a_frameIndex);
			const uint64_t _prevFrameIndex = _isClaimStarted ? lastClaimFrameIndex : claimFrameIndex;
			const void* _pPrevOwner = _isClaimStarted ? pLastOwner : pOwner;

			if (_prevFrameIndex == UINT64_MAX) return false;
			return (_prevFrameIndex + 1 == a_frameIndex) && (_pPrevOwner == a_pOwner);
		}

		/// <summary>
		/// 項目の移動を止める(毎フレーム頼む)
		/// </summary>
		/// <remarks>
		/// 頼んだフレームと次のフレームの移動量を 0 にする。移動量は次のフレームの頭(PreUpdate)で作るので、
		/// Update から頼んでも次のフレームに効くように1つ先まで止める。選んでいる UI はそのまま
		/// </remarks>
		void LockMove(uint64_t a_frameIndex) { moveLockFrameIndex = a_frameIndex; }

		// 移動を止めているか
		bool IsMoveLocked(uint64_t a_frameIndex) const
		{
			return moveLockFrameIndex != UINT64_MAX && a_frameIndex <= moveLockFrameIndex + 1;
		}

		/// <summary>その UI が選ばれていて、選択を見せるか(Update から)</summary>
		bool IsFocused(uint64_t a_frameIndex, const void* a_pUI) const
		{
			return isNavMode && a_pUI != nullptr && claimFrameIndex == a_frameIndex && pFocused == a_pUI;
		}

		/// <summary>カーソルを UI に乗せないか(Update から)</summary>
		/// <remarks>キー・パッドで選んでいて、選んでいるグループが出ている間だけ</remarks>
		bool IsCursorBlocked(uint64_t a_frameIndex) const
		{
			return isNavMode && claimFrameIndex == a_frameIndex && pOwner != nullptr;
		}
	};
}
