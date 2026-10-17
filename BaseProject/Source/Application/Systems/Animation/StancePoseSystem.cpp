#include "StancePoseSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

#include "Application/Components/Render/ModelComponent.h"
#include "Application/Components/Animation/NodePoseComponent.h"
#include "Application/Components/Animation/StancePoseComponent.h"

namespace App::System
{
	//==========================================================================================
	// StancePoseSystem
	//
	// ノードごとに、今のポーズの関節の位置を中心に、モデル空間の回転を掛ける。
	//
	// ・行ベクトル規約なので world = local * parentWorld。
	//   ノードのワールド W を、関節の位置 t を中心に R だけ回したいので
	//     W' = W * K      (K = T(-t) * R * T(t) * T(offset))
	//     L' = W' * P^-1  (P は親のワールド)
	//   子はローカルのままなので、一緒に回る。
	// ・ワールド行列はまだ組まれていない(CalcNodeSystem が後で組む)ので、
	//   親のワールドはこのフレームのローカルを根元まで掛けて求める(LowerBodyTurnSystem と同じ)。
	//   配列の順に書き換えるので、後のノードは前のノードを回した後の位置で回る。
	//==========================================================================================
	namespace
	{
		// 名前のハッシュからノード番号を引く。無ければ -1
		int FindNodeIndex(const std::vector<Engine::Resource::Node>& a_nodeVec, UINT a_nameHash)
		{
			if (a_nameHash == 0) return -1;
			for (size_t _i = 0; _i < a_nodeVec.size(); ++_i)
			{
				if (a_nodeVec[_i].nodeNameHash == a_nameHash) return static_cast<int>(_i);
			}
			return -1;
		}

		// このフレームのローカルを根元まで掛けて、ノードのワールド(モデル空間)を求める
		Math::Matrix CalcWorld(const std::vector<Engine::Resource::Node>& a_nodeVec, std::span<Engine::Resource::NodePoseMatrix> a_poseVec, int a_nodeIdx)
		{
			Math::Matrix _world = Math::Matrix::Identity();
			for (int _idx = a_nodeIdx; _idx >= 0; _idx = a_nodeVec[_idx].parent)
			{
				_world = _world * a_poseVec[_idx].local;
			}
			return _world;
		}
	}

	void StancePoseSystem::Init(App::ECS::APPWorld& a_world)
	{
		// 自分のチャンクとプールの自分の範囲だけを書くので、チャンクを分けてワーカーで回す
		a_world.ActiveJobTask<
			const Component::ModelComponent,
			Component::StancePoseComponent,
			Component::NodePoseComponent>(
			Engine::ECS::ESystemType::Animation,
			"StancePoseSystem",
			[](
				Engine::ECS::Chunk* /*a_pChunk*/,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_tags*/,
				const Component::ModelComponent* a_modelArray,
				Component::StancePoseComponent* a_stanceArray,
				Component::NodePoseComponent* a_nodePoseArray
			)
			{
				auto& _nodePosePool = a_ctx.pWorld->RefResource<Engine::Pool::RangePool<Engine::Resource::NodePoseMatrix>>();

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::StancePoseComponent& _stance = a_stanceArray[_i];

					//==================================================================
					// 効きを寄せる
					//==================================================================
					const float _goal = std::clamp(_stance.weight, 0.0f, 1.0f);
					if (_stance.blendRate > 0.0f && a_ctx.dt > 0.0f)
					{
						const float _step = _stance.blendRate * a_ctx.dt;
						if (_stance.currentWeight < _goal)	_stance.currentWeight = (std::min)(_stance.currentWeight + _step, _goal);
						else								_stance.currentWeight = (std::max)(_stance.currentWeight - _step, _goal);
					}
					else
					{
						_stance.currentWeight = _goal;
					}
					const float _weight = _stance.currentWeight;
					if (!(_weight > 0.0f)) continue;

					//==================================================================
					// ポーズへ適用
					//==================================================================
					const auto* _pModel = a_ctx.pServices->pResourceManager->Get(a_modelArray[_i].handle);
					if (!_pModel) continue;

					auto _poseVec = _nodePosePool.RefRange(a_nodePoseArray[_i].nodePoseHandle);
					if (_poseVec.empty()) continue;

					const auto& _nodeVec = _pModel->GetOriginalNodeVec();
					if (_poseVec.size() < _nodeVec.size()) continue;

					for (uint32_t _b = 0; _b < _stance.GetActiveBoneCount(); ++_b)
					{
						const Component::StancePoseBone& _bone = _stance.boneArray[_b];

						const int _nodeIdx = FindNodeIndex(_nodeVec, _bone.nodeHash);
						if (_nodeIdx < 0) continue;

						// X → Y → Z の順に掛けた回転を、効きの分だけ
						const Math::Vector3 _rad = Math::Vector3(_bone.rotationDeg) * (DirectX::XM_PI / 180.0f) * _weight;
						const Math::Matrix _rot = Math::Matrix::CreateFromQuaternion(
							Math::Quaternion::CreateFromAxisAngle(Math::Vector3(1.0f, 0.0f, 0.0f), _rad.x) *
							Math::Quaternion::CreateFromAxisAngle(Math::Vector3(0.0f, 1.0f, 0.0f), _rad.y) *
							Math::Quaternion::CreateFromAxisAngle(Math::Vector3(0.0f, 0.0f, 1.0f), _rad.z));

						// ノードのワールドと、その親のワールド
						const int _parentIdx = _nodeVec[_nodeIdx].parent;
						const Math::Matrix _parentWorld = (_parentIdx >= 0) ? CalcWorld(_nodeVec, _poseVec, _parentIdx) : Math::Matrix::Identity();
						const Math::Matrix _world = Math::Matrix(_poseVec[_nodeIdx].local) * _parentWorld;

						float _det = 0.0f;
						const Math::Matrix _invParentWorld = _parentWorld.Invert(_det);
						if (!std::isfinite(_det) || _det == 0.0f) continue;

						// 関節の位置を中心に回し、ずらす
						const Math::Vector3 _jointPos = _world.Translation();
						const Math::Vector3 _offset = Math::Vector3(_bone.offset) * _weight;
						const Math::Matrix _turn = Math::Matrix::CreateTranslation(-_jointPos) * _rot * Math::Matrix::CreateTranslation(_jointPos + _offset);

						_poseVec[_nodeIdx].local = _world * _turn * _invParentWorld;
					}
				}
			}
		)
		// 順序 : クリップ・加算ポーズ・脚のひねりを書き終えた後、ワールドを組む前
		.After("LowerBodyTurnSystem")
		.Before("CalcNodeSystem");
	}
}
