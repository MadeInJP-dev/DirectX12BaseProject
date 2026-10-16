#pragma once

#include "Engine/GameObject/BaseObject/BaseObject.h"

namespace App::Object
{
	/// <summary>
	/// シーンに置いておき、SceneSequence のウェーブから呼び出されて出てくるオブジェクト
	/// </summary>
	/// <remarks>
	/// ウェーブは普段プレハブを出すが、プレハブにできない大物(群れのボスのように、
	/// 自分で何千体も出して動かす持ち主が要るもの)はシーンに直接置く。
	/// そういうものをウェーブの1体として扱うための約束がこれ。
	///
	///   1) 読み込み直後(PostDeserialize)に、ウェーブに載っているものへ HoldAppear が届く。
	///      届いたものは Awake で出ずに待つ(載っていなければ今までどおり Awake で出る)
	///   2) そのウェーブが来たら Appear が届く
	///   3) ウェーブの生存数に IsAlive を数える。倒されたら false を返す
	///
	/// 束ねる側(SceneSequence)は相手の型を知らずに済む。
	/// </remarks>
	class SequenceActor : public Engine::GameObject::BaseObject
	{
	public:

		ENGINE_TYPE_CHAIN_BASE(SequenceActor, Engine::GameObject::BaseObject);

		// 呼ばれるまで出ないようにする(Awake より前に届く)
		virtual void HoldAppear() = 0;

		// 出す。出せた(出し済みも含む)なら true
		virtual bool Appear(Engine::GameObject::ObjectContext& a_context) = 0;

		// 出ていて、まだ倒されていないか(ウェーブの生存数に数える)
		virtual bool IsAlive() const = 0;

		// 出てくる位置(ワールド)。シーケンスの出現位置の印に使う
		virtual Math::Vector3 GetAppearPos() const = 0;
	};
}
