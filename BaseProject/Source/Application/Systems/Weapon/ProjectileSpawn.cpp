#include "ProjectileSpawn.h"

#include "Engine/Resource/Data/Prefab/Prefab.h"

#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Movement/DesiredVelocityComponent.h"
#include "Application/Components/Weapon/Projectile/HomingComponent.h"
#include "Application/Components/Weapon/Projectile/ProjectileComponent.h"
#include "Application/Components/Transform/HierarchyComponent.h"
#include "Application/Components/Physics/Collider.h"
#include "Application/Components/Enemy/EnemyTag.h"
#include "Application/Components/Render/ModelComponent.h"

namespace App::System::ProjectileSpawn
{
	namespace
	{
		//------------------------------------------------------------------------------
		// この弾がどちら側のものかを決める
		//
		// プレイヤーも敵もまったく同じ弾/ミサイルのプレハブを撃つので、
		// どちら側かはプレハブに書いておけない。撃った本体を見てここで決める。
		//
		// タグが付いているのは本体だけで、銃は子エンティティのことがある。
		// ResolveShooterEntity と同じように親を辿って探す
		// (撃った本体が渡ってくる前提だが、辿っておけば銃を直接渡されても壊れない)。
		// 見つからなければプレイヤー側とみなす。
		//------------------------------------------------------------------------------
		Component::ECollisionLayer ResolveProjectileLayer(Engine::ECS::World& a_world, Engine::ECS::Entity a_shooter)
		{
			constexpr int MAX_DEPTH = 8;

			Engine::ECS::Entity _entity = a_shooter;

			for (int _d = 0; _d < MAX_DEPTH; ++_d)
			{
				if (_entity == Engine::ECS::Limits::INVALID_ENTITY) break;

				if (a_world.HasComponent<Component::EnemyTag>(_entity)) return Component::ECollisionLayer::EnemyProjectile;

				if (!a_world.HasComponent<Component::HierarchyComponent>(_entity)) break;
				const auto* _pHierarchy = a_world.RefData<Component::HierarchyComponent>(_entity);
				if (!_pHierarchy) break;
				_entity = _pHierarchy->parentID;
			}

			return Component::ECollisionLayer::PlayerProjectile;
		}

		//------------------------------------------------------------------------------
		// 敵が撃った弾の発光色
		//
		// 弾のプレハブはプレイヤーと共用なので、絵の違いもここで付けるしかない。
		// 撃ち合っている最中に「今飛んでいるのがどちらの弾か」を色で分ける。
		// 強さ(emissiveIntensity)はプレハブの値をそのまま使う。色だけ差し替えれば
		// 弾ごとの光り方(バレットは強め/ミサイルは弱め)の作り分けが残る。
		//------------------------------------------------------------------------------
		constexpr Math::Vector3 ENEMY_PROJECTILE_EMISSIVE = { 0.60f, 0.15f, 1.00f };	// 紫

		//------------------------------------------------------------------------------
		// その弾が当たりに行く相手
		//
		// 地形(StaticObject)と機体(DiynamicObject)には今までどおり当たる。
		// 弾同士は「相手側の弾」だけ。自分側を入れると、同じ銃口から続けて出た弾や
		// 斉射したミサイルが発射直後にぶつかって消える。
		// 相手側は残してあるので、敵のミサイルは今までどおり撃ち落とせる。
		//------------------------------------------------------------------------------
		Component::ECollisionLayer MakeProjectileCollideLayer(Component::ECollisionLayer a_myLayer)
		{
			const bool _isEnemySide = (a_myLayer == Component::ECollisionLayer::EnemyProjectile);

			const Component::ECollisionLayer _otherSide = _isEnemySide
				? Component::ECollisionLayer::PlayerProjectile
				: Component::ECollisionLayer::EnemyProjectile;

			Component::ECollisionLayer _result = Component::ECollisionLayer::StaticObject | Component::ECollisionLayer::DiynamicObject | _otherSide;

			// Enemy(群れのボスのボイドなど)はプレイヤー側の攻撃でだけ落ちる。
			// 敵の弾にも当てると、敵同士の流れ弾でボスの体力が減ってしまう
			if (!_isEnemySide) _result |= Component::ECollisionLayer::Enemy;

			return _result;
		}
	}

	Engine::ECS::Entity ResolveShooterEntity(
		Engine::ECS::World& a_world,
		Engine::ECS::Entity a_gunEntity)
	{
		// 親を辿る深さの上限。親子が循環していても止まるように付けておく
		constexpr int MAX_DEPTH = 8;

		Engine::ECS::Entity _entity = a_gunEntity;
		Engine::ECS::Entity _last   = a_gunEntity;

		for (int _d = 0; _d < MAX_DEPTH; ++_d)
		{
			if (_entity == Engine::ECS::Limits::INVALID_ENTITY) break;
			_last = _entity;

			// コライダーを持つ = コリジョンワールドに居る本体
			if (a_world.HasComponent<Component::ColliderComponent>(_entity)) return _entity;

			// 親へ
			if (!a_world.HasComponent<Component::HierarchyComponent>(_entity)) break;
			const auto* _pHierarchy = a_world.RefData<Component::HierarchyComponent>(_entity);
			if (!_pHierarchy) break;
			_entity = _pHierarchy->parentID;
		}

		return _last;
	}

	void Spawn(
		Engine::ECS::World&       a_world,
		Engine::Resource::Prefab* a_pPrefab,
		const Math::Vector3&  a_pos,
		const Math::Vector3&  a_velocity,
		Engine::ECS::Entity       a_shooter,
		Engine::ECS::Entity       a_homingTarget,
		float                     a_damage)
	{
		if (!a_pPrefab) return;

		// ---- プレハブのデータをコピーして、位置と速度を上書き ----
		// 子を持つプレハブ(噴煙エフェクト付きの弾など)でも落とさないよう、
		// 材料の組み立てはプレハブ側に任せる。先頭がルート=弾本体。
		std::vector<Engine::Resource::PrefabInstanceData> _instanceVec =
			a_pPrefab->BuildInstanceData(&a_world);
		if (_instanceVec.empty()) return;

		// 子は保存された姿のまま出す(位置と向きは親に追従する)
		for (size_t _c = 1; _c < _instanceVec.size(); ++_c)
		{
			a_world.ReserveCreateEntityWithData(
				_instanceVec[_c].sig, std::move(_instanceVec[_c].dataMap));
		}

		Engine::ECS::Signature _sig = _instanceVec[0].sig;
		auto _data = std::move(_instanceVec[0].dataMap);	// (型ID -> バイト列)

		auto _ltID  = a_world.GetCompTypeID<Component::LocalTransformComponent>();
		auto _velID = a_world.GetCompTypeID<Component::DesiredVelocityComponent>();
		auto _wmID  = a_world.GetCompTypeID<Component::WorldMatrixComponent>();

		// 弾が動く・描画されるために最低限必要なコンポーネントが無ければ足す
		auto _ensure = [&](Engine::ECS::ComponentTypeID _id)
		{
			if (_sig.test(_id)) return;
			_sig.set(_id);
			auto& _buf = _data[_id];
			_buf.assign(a_world.GetComponentMetaData(_id).compAlignSize, 0);
			auto _ctor = a_world.GetCompFunc(_id).construct;
			if (_ctor) _ctor(_buf.data());
		};
		_ensure(_ltID);
		_ensure(_velID);
		_ensure(_wmID);

		// 位置の上書き
		{
			auto& _buf = _data[_ltID];
			Component::LocalTransformComponent _lt = {};
			std::memcpy(&_lt, _buf.data(), sizeof(_lt));
			_lt.pos = a_pos;
			_lt.isDirty = true;
			std::memcpy(_buf.data(), &_lt, sizeof(_lt));
		}
		// 速度の上書き
		{
			auto& _buf = _data[_velID];
			Component::DesiredVelocityComponent _v = {};
			std::memcpy(&_v, _buf.data(), sizeof(_v));
			_v.value = a_velocity;
			std::memcpy(_buf.data(), &_v, sizeof(_v));
		}
		// 発射元を入れる。弾が自分を撃った相手に当たらないようにするため
		// (銃口は体の中にあるので、入れないと発射した瞬間に自分へ当たる)
		{
			auto _projID = a_world.GetCompTypeID<Component::ProjectileComponent>();
			auto _it = _data.find(_projID);
			if (_sig.test(_projID) &&
				_it != _data.end() && _it->second.size() >= sizeof(Component::ProjectileComponent))
			{
				Component::ProjectileComponent _proj = {};
				std::memcpy(&_proj, _it->second.data(), sizeof(_proj));
				_proj.shooterEntity = a_shooter;

				// 威力は武器が決める(0 以下ならプレハブの値のまま)
				if (a_damage > 0.0f) _proj.damage = a_damage;

				// 連続判定の起点。銃口の位置から入れておかないと、
				// 生成された最初のフレームの移動ぶんだけ判定が抜ける
				_proj.prevPos    = a_pos;
				_proj.hasPrevPos = true;

				std::memcpy(_it->second.data(), &_proj, sizeof(_proj));
			}
		}

		// この弾がどちら側のものか。レイヤーと発光色の両方で使う
		const bool _isEnemySide =
			(ResolveProjectileLayer(a_world, a_shooter) == Component::ECollisionLayer::EnemyProjectile);

		// 撃った側でレイヤーを入れ替える。
		// プレハブに書いてあるレイヤーは、どちらが撃ったか分からない状態の値なので
		// ここで必ず上書きする(プレハブ側をいじっても発射された弾には効かない)
		{
			auto _collID = a_world.GetCompTypeID<Component::ColliderComponent>();
			auto _it = _data.find(_collID);
			if (_sig.test(_collID) &&
				_it != _data.end() && _it->second.size() >= sizeof(Component::ColliderComponent))
			{
				Component::ColliderComponent _coll = {};
				std::memcpy(&_coll, _it->second.data(), sizeof(_coll));

				_coll.layer        = _isEnemySide ? Component::ECollisionLayer::EnemyProjectile : Component::ECollisionLayer::PlayerProjectile;
				_coll.collideLayer = MakeProjectileCollideLayer(_coll.layer);

				std::memcpy(_it->second.data(), &_coll, sizeof(_coll));
			}
		}
		// 敵が撃った弾は発光色を紫にする。
		// レイヤーと同じで、どちら側の弾かはここでしか分からない
		{
			auto _modelID = a_world.GetCompTypeID<Component::ModelComponent>();
			auto _it = _data.find(_modelID);
			if (_isEnemySide && _sig.test(_modelID) &&
				_it != _data.end() && _it->second.size() >= sizeof(Component::ModelComponent))
			{
				Component::ModelComponent _model = {};
				std::memcpy(&_model, _it->second.data(), sizeof(_model));

				_model.emissiveColor = ENEMY_PROJECTILE_EMISSIVE;

				std::memcpy(_it->second.data(), &_model, sizeof(_model));
			}
		}

		// 誘導弾なら追う相手を入れる(持っていない弾には足さない)
		{
			auto _homingID = a_world.GetCompTypeID<Component::HomingComponent>();
			auto _it = _data.find(_homingID);
			if (_sig.test(_homingID) &&
				_it != _data.end() && _it->second.size() >= sizeof(Component::HomingComponent))
			{
				Component::HomingComponent _homing = {};
				std::memcpy(&_homing, _it->second.data(), sizeof(_homing));
				_homing.targetEntity = a_homingTarget;
				std::memcpy(_it->second.data(), &_homing, sizeof(_homing));
			}
		}

		// 反復中なので即時生成せず、遅延生成コマンドに積む
		a_world.ReserveCreateEntityWithData(_sig, std::move(_data));
	}
}
