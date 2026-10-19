#include "CalcMatrixSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Transform/HierarchyComponent.h"

namespace App::System
{
	void CalcMatrixSystem::Init(App::ECS::APPWorld& a_world)
	{
		// ヒエラルキーがついていない単体オブジェクトに対して最終行列を作成する。
		// 自分のチャンクの配列だけを書く(汚れ印も自分の分だけ)ので、チャンクを分けてワーカーで回す
		a_world.ActiveJobTask<const Component::LocalTransformComponent, Component::WorldMatrixComponent>(
			Engine::ECS::ESystemType::PostUpdate,
			"CalcMatrixSystem",
			[](
				Engine::ECS::Chunk* /*a_pChunk*/,
				uint32_t a_count,
				const Engine::ECS::SystemContext& /*a_ctx*/,
				Component::ActiveTag* /*a_tags*/,
				const Component::LocalTransformComponent* a_trsArray,
				Component::WorldMatrixComponent* a_worldMatArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					const Component::LocalTransformComponent& _trsComp = a_trsArray[_i];
					Component::WorldMatrixComponent& _worldMatComp = a_worldMatArray[_i];

					// 組み直したかを子へ伝える(子の行列は CommitHierarchyWorldMatrixSystem が組む)。
					// 階層を持たない親(スポナーが出した敵など)に付けた子も、親が動けば組み直されるようにする
					_worldMatComp.wasUpdatedThisFrame = _trsComp.isDirty;

					// 変更がなければ更新しない
					if (!_trsComp.isDirty) continue;

					// 変換行列計算(スケール→回転→平行移動の順で合成)
					// 回転は正規化してから使う。エディタで直打ちした値が
					// 単位長でないとスケールが混ざるため
					_worldMatComp.worldMat = Math::Matrix::CreateTRS(
						_trsComp.pos,
						_trsComp.quat.Normalized(),
						_trsComp.scale);

					// mutubleでconstを無視している
					_trsComp.isDirty = false;
				}
			},
			Engine::ECS::Exclude<Component::HierarchyComponent>()
		)
		// 汚れ印(LocalTransform::isDirty)を下ろすので書き込みも宣言する。
		// クエリの型は const のまま(姿勢そのものは読むだけ)
		.Writes<Component::LocalTransformComponent>();
	}
}