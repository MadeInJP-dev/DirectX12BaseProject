#pragma once

#include "Engine/ECS/World/World.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/EditorField/EditorField.inl"

#include "Application/Components/Render/ModelComponent.h"

namespace App::Component
{
	//==========================================================================================
	// StancePoseBone
	//
	// 構えの1本ぶん : どのノードを、モデル空間でどれだけ回すか
	//==========================================================================================
	struct StancePoseBone
	{
		UINT nodeHash = 0;							// 回すノードの名前のハッシュ
		Math::Vector3 rotationDeg = {};				// モデル空間の回転(度)。X → Y → Z の順に掛ける
		Math::Vector3 offset = {};					// 回した後にずらす量(モデル空間・m)。腰を落とすときなど
	};

	//==========================================================================================
	// StancePoseComponent
	//
	// クリップで組んだポーズの上から、決まった構え(仁王立ち・銃を構えるなど)を足す。
	//
	// ・ノードごとに、今のポーズの関節の位置を中心に、モデル空間の回転を掛ける(子も一緒に回る)。
	//   ずらし(offset)を入れると、回した後にモデル空間で平行移動する(脚を開いて浮いた分だけ腰を落とすなど)。
	//   加算ポーズ(AdditivePoseComponent)はバインドポーズの向きで回すので、
	//   クリップで大きく曲がった骨に掛けると向きがずれる。こちらは今のポーズで回すので、
	//   「この骨を体の前へ 60 度」がそのまま見た目どおりに効く。
	// ・配列の順に掛ける。親を先に並べると、子は親を回した後の位置で回る。
	// ・効きは weight へ blendRate で寄せる(付けた瞬間にポーズが飛ばないように)。
	// ・適用は StancePoseSystem(加算ポーズと脚のひねりの後、ワールド行列を組む前)。
	// ・ガレージでは GarageSequence が出した機体へ付けて、毎フレーム中身を書き直す。
	//==========================================================================================
	struct StancePoseComponent
	{
		// 持てるノードの数(コンポーネントは std::vector を持てないので固定長)
		static constexpr uint32_t BONE_MAX = 16;

		// ---- 設定(保存) ----
		StancePoseBone boneArray[BONE_MAX] = {};
		uint32_t boneCount = 0;		// boneArray の有効数

		float weight = 1.0f;		// 効かせる強さ(0〜1)
		float blendRate = 4.0f;		// 効きを寄せる速さ(1秒あたり)。0 なら即座に

		// ---- 実行中の値(保存しない) ----
		float currentWeight = 0.0f;	// 今の効き

		uint32_t GetActiveBoneCount() const { return (std::min)(boneCount, BONE_MAX); }
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::StancePoseComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::StancePoseComponent& _comp = Engine::EditorField::RefValue<App::Component::StancePoseComponent>(a_pData);

		a_ar.Field("weight", _comp.weight);
		a_ar.Field("blendRate", _comp.blendRate);

		size_t _count = _comp.GetActiveBoneCount();
		if (a_ar.BeginArray("bones", _count))
		{
			_comp.boneCount = static_cast<uint32_t>((std::min)(_count, static_cast<size_t>(App::Component::StancePoseComponent::BONE_MAX)));
			for (uint32_t _i = 0; _i < _comp.boneCount; ++_i)
			{
				if (!a_ar.BeginObject(_i)) continue;
				a_ar.Field("nodeHash", _comp.boneArray[_i].nodeHash);
				a_ar.Field("rotationDeg", _comp.boneArray[_i].rotationDeg);
				a_ar.Field("offset", _comp.boneArray[_i].offset);
				a_ar.EndObject();
			}
			a_ar.EndArray();
		}
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::StancePoseComponent& _comp = Engine::EditorField::RefValue<App::Component::StancePoseComponent>(a_context.pData);

		Engine::EditorField::Slider("Weight", _comp.weight, 0.0f, 1.0f, "%.2f");
		Engine::EditorField::Slider("Blend Rate", _comp.blendRate, 0.0f, 30.0f, "%.1f");
		Engine::EditorField::Value("Current Weight", "%.2f", _comp.currentWeight);

		// ノードは自身のモデルが持つものから選ぶ
		const auto* _pModelComp = a_context.pWorld->RefData<App::Component::ModelComponent>(a_context.entity);
		const auto* _pModel = _pModelComp ? a_context.pWorld->RefEngineServices()->pResourceManager->Get(_pModelComp->handle) : nullptr;

		int _removeIndex = -1;
		for (uint32_t _i = 0; _i < _comp.GetActiveBoneCount(); ++_i)
		{
			Engine::EditorField::IDScope _id(static_cast<int>(_i));
			App::Component::StancePoseBone& _bone = _comp.boneArray[_i];

			UINT _nodeIdx = UINT_MAX;
			if (_pModel)
			{
				const auto& _nodes = _pModel->GetOriginalNodeVec();
				for (size_t _n = 0; _n < _nodes.size(); ++_n)
				{
					if (_nodes[_n].nodeNameHash == _bone.nodeHash) { _nodeIdx = static_cast<UINT>(_n); break; }
				}
			}
			Engine::EditorField::ModelNodeField("Node", _pModel, _nodeIdx, _bone.nodeHash);
			Engine::EditorField::Field("Rotation (deg)", _bone.rotationDeg);
			Engine::EditorField::Field("Offset (m)", _bone.offset, 0.005f);
			if (Engine::EditorField::DeleteSmallButton("Remove")) _removeIndex = static_cast<int>(_i);
			Engine::EditorField::Line();
		}

		if (_removeIndex >= 0)
		{
			for (uint32_t _i = static_cast<uint32_t>(_removeIndex); _i + 1 < _comp.GetActiveBoneCount(); ++_i)
			{
				_comp.boneArray[_i] = _comp.boneArray[_i + 1];
			}
			--_comp.boneCount;
		}
		if (_comp.GetActiveBoneCount() < App::Component::StancePoseComponent::BONE_MAX && Engine::EditorField::CreateButton("Add Bone"))
		{
			_comp.boneArray[_comp.boneCount++] = {};
		}
	}
};
