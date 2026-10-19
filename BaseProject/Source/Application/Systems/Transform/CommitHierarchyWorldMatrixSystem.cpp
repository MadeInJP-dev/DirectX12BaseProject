#include "CommitHierarchyWorldMatrixSystem.h"
#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Transform/HierarchyComponent.h"

#include "Application/InstanceResource/HierarchyResource.h"

namespace App::System
{
	void CommitHierarchyWorldMatrixSystem::Init(App::ECS::APPWorld& a_world)
	{
		// ヒエラルキーがついていない単体オブジェクトに対して最終行列を作成する
		a_world.ActiveCustomTask(
			Engine::ECS::ESystemType::PostUpdate,
			"CommitHierarchyWorldMatrixSystem",
			Engine::ECS::ReadList<Component::LocalTransformComponent, Component::HierarchyComponent>{},
			Engine::ECS::WriteList<Component::WorldMatrixComponent>{},
			[](const Engine::ECS::SystemContext& a_ctx)
			{
				// 更新前にワールド行列のフラグを消しておく。
				// 階層を持たないものの印は CalcMatrixSystem が組み直したかどうかで立てているので消さない
				// (消すと、階層を持たない親に付けた子が親の移動に付いて来なくなる)
				a_ctx.pWorld->ForEach<Component::WorldMatrixComponent, const Component::HierarchyComponent>(
					[]
					(
						Engine::ECS::Chunk* /*a_pChunk*/,
						uint32_t a_count,
						Component::WorldMatrixComponent* a_worldMatArray,
						const Component::HierarchyComponent* /*a_hArray*/
						)
					{
						for (size_t _i = 0; _i < a_count; ++_i)
						{
							auto& _comp = a_worldMatArray[_i];
							_comp.wasUpdatedThisFrame = false;
						}
					}
				);

				// 深度ごとに親子階層の更新をする
				auto& _hRes = a_ctx.pWorld->RefResource<InstanceResource::HierarchyResource>();
				for (UINT _depth = 0; _depth <= _hRes.maxDepth; ++_depth)
				{
					a_ctx.pWorld->ForEach<Component::LocalTransformComponent, Component::WorldMatrixComponent, Component::HierarchyComponent>(
						[_depth, &a_ctx]
						(
							Engine::ECS::Chunk* /*a_pChunk*/,
							uint32_t a_count,
							Component::LocalTransformComponent* a_trsArray,
							Component::WorldMatrixComponent* a_worldMatArray,
							Component::HierarchyComponent* a_hArray
							)
						{

							for (size_t _i = 0; _i < a_count; ++_i)
							{
								const Component::HierarchyComponent& _hComp = a_hArray[_i];
								if (_hComp.depth != _depth) continue;	// 深度値チェック

								bool _isParentUpdated = false;
								if (_hComp.parentID != Engine::ECS::Limits::INVALID_ENTITY) {
									if (!a_ctx.pWorld->HasComponent<Component::WorldMatrixComponent>(_hComp.parentID)) continue;
									auto* _parentMatComp = a_ctx.pWorld->RefData<Component::WorldMatrixComponent>(_hComp.parentID);
									if (_parentMatComp) {
										_isParentUpdated = _parentMatComp->wasUpdatedThisFrame;
									}
								}

								const Component::LocalTransformComponent& _trsComp = a_trsArray[_i];
								Component::WorldMatrixComponent& _worldMatComp = a_worldMatArray[_i];
								if (!_trsComp.isDirty && !_isParentUpdated) {
									_worldMatComp.wasUpdatedThisFrame = false; // 自分も更新なし
									continue;
								}

								// ローカル上で行列を作成
								Math::Matrix _myLocalMat = Math::Matrix::CreateTRS(
									_trsComp.pos,
									_trsComp.quat.Normalized(),
									_trsComp.scale);

								// 親の行列を掛け合わせる
								if (_hComp.parentID != Engine::ECS::Limits::INVALID_ENTITY) {
									// 親のワールド行列を取得
									auto* _parentMatComp = a_ctx.pWorld->RefData<Component::WorldMatrixComponent>(_hComp.parentID);
									if (!_parentMatComp) continue;


									if (_trsComp.inheritance == Component::ETransformInheritance::All)
									{
										// 子 * 親 の順(自分のローカルを先に適用してから親へ乗せる)
										_worldMatComp.worldMat = _myLocalMat * _parentMatComp->worldMat;
									}
									else
									{
										// 分解
										Math::Matrix _parentMat = Math::Matrix::Identity();
										Math::Vector3 _pos = {};
										Math::Quaternion _quat = {};
										Math::Vector3 _scale = {};
										_parentMatComp->worldMat.Decompose(_scale,_quat, _pos);

										if (Core::HasFlag(_trsComp.inheritance, Component::ETransformInheritance::Scale))
										{
											_parentMat *= Math::Matrix::CreateScale(_scale);
										}
										if (Core::HasFlag(_trsComp.inheritance, Component::ETransformInheritance::Rotation))
										{
											_parentMat *= Math::Matrix::CreateFromQuaternion(_quat);
										}
										if (Core::HasFlag(_trsComp.inheritance, Component::ETransformInheritance::Translation))
										{
											_parentMat *= Math::Matrix::CreateTranslation(_pos);
										}
									
										_worldMatComp.worldMat = _myLocalMat * _parentMat;
									}
								}
								else {
									// 親がいない場合はそのまま（ルート）
									_worldMatComp.worldMat = _myLocalMat;
								}

								_worldMatComp.wasUpdatedThisFrame = true;

								_trsComp.isDirty = false;
							}

						}
					);
				}
			}
		)
		// 階層の深さの上限を読む
		.ReadsResource<InstanceResource::HierarchyResource>()
		// 汚れ印(LocalTransform::isDirty)を下ろすので書き込みも宣言する
		.Writes<Component::LocalTransformComponent>()
		// 順序 : CalcMatrixSystem と互いに LocalTransform を読んで書く(対象は階層の有無で重ならない)。
		// 読み書きだけでは循環するので、階層なしを先に組む
		.After("CalcMatrixSystem");
	}
}
