#include "EffectSpawnHelper.h"

#include <cstring>

#include "Engine/ECS/World/World.h"

#include "../../Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Particle/ParticleBufferManager.h"
#include "Engine/Graphics/Particle/GPU/EmitterSlotPool/EmitterSlotPool.h"
#include "Engine/Graphics/LightManager/LightManager.h"
#include "Engine/Effect/Trail/TrailNodePool.h"
#include "../../Engine/Effect/EffectInstance.h"


#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Effect/EffectAssetComponent.h"
#include "Application/Components/Effect/EffectOverrideComponent.h"
#include "Application/Components/Effect/EffectComponent.h"
#include "Application/Components/Transform/HierarchyComponent.h"
#include "Engine/ECS/Component/TransientTag.h"

namespace App::Utility
{
	namespace
	{
		//----------------------------------------------------------------------
		// シグネチャへ1コンポーネント足して、既定構築した書き込み先を返す。
		// ここは何も無いところから組み立てるので「すでに持っているか」は見ない。
		//----------------------------------------------------------------------
		template<typename Comp>
		bool PushComponent(
			Engine::ECS::World& a_world,
			Engine::ECS::Signature& a_sig,
			std::unordered_map<Engine::ECS::ComponentTypeID, std::vector<uint8_t>>& a_data,
			const Comp& a_value)
		{
			const auto _typeID = a_world.GetCompTypeID<Comp>();
			if (_typeID == Engine::ECS::Limits::INVALID_COMPONENTTYPEID) return false;

			a_sig.set(_typeID);

			// チャンク上の1件ぶんの大きさで確保する(アライン込み)
			auto& _buf = a_data[_typeID];
			_buf.assign(a_world.GetComponentMetaData(_typeID).compAlignSize, 0);
			if (_buf.empty()) return false;

			// 既定構築してから値を流し込む
			if (auto _ctor = a_world.GetCompFunc(_typeID).construct) _ctor(_buf.data());
			std::memcpy(_buf.data(), &a_value, sizeof(Comp));

			return true;
		}

		using EffectDataMap = std::unordered_map<Engine::ECS::ComponentTypeID, std::vector<uint8_t>>;

		//----------------------------------------------------------------------
		// エフェクト1体ぶんのシグネチャと初期データを組む。
		// 「エフェクトのエンティティとは何で出来ているか」をここ1か所に閉じておき、
		// 遅延生成(ReserveSpawnEffectAt)と即時生成(SpawnEffectAtNow)で同じものを使う。
		//----------------------------------------------------------------------
		bool BuildEffectEntity(
			Engine::ECS::World& a_world,
			const Core::GUID& a_effectGUID,
			const Math::Vector3& a_pos,
			const Math::Quaternion& a_rot,
			bool a_isDestroyOnFinish,
			const Math::Vector3& a_emitDir,
			float a_scale,
			Engine::ECS::Signature& a_outSig,
			EffectDataMap& a_outData)
		{
			Engine::ECS::Signature& _sig = a_outSig;
			EffectDataMap& _data = a_outData;

			// ---- 位置と向き ----
			Component::LocalTransformComponent _localTrs = {};
			_localTrs.pos = a_pos;
			_localTrs.quat = a_rot;
			_localTrs.isDirty = true;
			if (!PushComponent(a_world, _sig, _data, _localTrs)) return false;

			// ---- ワールド行列 ----
			// CalcMatrixSystem(PostUpdate)が Draw の前に組み直すが、
			// 出た最初のフレームから正しい位置で出したいので、ここで向きと平行移動を入れておく
			Component::WorldMatrixComponent _worldMat = {};
			_worldMat.worldMat = Math::Matrix::CreateFromQuaternion(a_rot) * Math::Matrix::CreateTranslation(a_pos);
			_worldMat.wasUpdatedThisFrame = false;
			if (!PushComponent(a_world, _sig, _data, _worldMat)) return false;

			// ---- 再生するエフェクト ----
			Component::EffectAssetComponent _effect = {};
			_effect.effectGUID = a_effectGUID;
			_effect.playOnStart = true;			// 出た瞬間から再生する
			_effect.destroyOnFinish = a_isDestroyOnFinish;	// 出し切ったら自分から消える

			if (!PushComponent(a_world, _sig, _data, _effect)) return false;

			// ---- 出す側からの上書き ----
			// アセットは共有なので、大きさと向きの違いはここで付ける。
			// 吹き出す向きを渡すのは向きを持たない呼び出し(ReserveSpawnEffectAt / SpawnEffectAtNow)だけなので、
			// 渡された向きはそのままワールドの向きになる
			Component::EffectOverrideComponent _override = {};
			_override.effectScale = (a_scale > 0.0f) ? a_scale : 1.0f;

			Math::Vector3 _emitDir = a_emitDir;
			if (_emitDir.LengthSquared() > 1e-8f)
			{
				// 手で入れた値は長さがまちまちなので揃えておく。
				// 揃えないと受け側(EffectDrawSystem)で行列を掛けたときに長さが効いてしまう
				_emitDir.Normalize();
				_override.isOverrideTransform = true;
				_override.overrideEmitDir = _emitDir;
			}

			if (!PushComponent(a_world, _sig, _data, _override)) return false;

			// ---- エフェクトである印 ----
			// 一括停止などで絞り込めるように、既存のエフェクトと同じ目印を付けておく
			PushComponent(a_world, _sig, _data, Component::EffectComponent{});

			return true;
		}
	}

	bool ReserveSpawnEffectAt(
		Engine::ECS::World& a_world,
		const Core::GUID& a_effectGUID,
		const Math::Vector3& a_pos,
		bool a_isDestroyOnFinish,
		const Math::Vector3& a_emitDir,
		float a_scale)
	{
		if (a_effectGUID == Core::DEFAULT_GUID) return false;

		Engine::ECS::Signature _sig = {};
		EffectDataMap _data = {};

		if (!BuildEffectEntity(
			a_world, a_effectGUID, a_pos, Math::Quaternion::Identity(), a_isDestroyOnFinish, a_emitDir, a_scale, _sig, _data))
		{
			return false;
		}

		// 反復中なので即時生成せず、遅延生成コマンドに積む
		a_world.ReserveCreateEntityWithData(_sig, std::move(_data));
		return true;
	}

	bool ReserveSpawnEffect(
		Engine::ECS::World& a_world,
		const Core::GUID& a_effectGUID,
		const Math::Matrix& a_worldMat,
		bool a_isDestroyOnFinish,
		float a_scale)
	{
		if (a_effectGUID == Core::DEFAULT_GUID) return false;

		// 拡縮は使わない(大きさは a_scale で付ける)。位置と向きだけ取り出す
		Math::Matrix _worldMat = a_worldMat;
		Math::Vector3 _scale;
		Math::Quaternion _rot;
		Math::Vector3 _pos;
		if (!_worldMat.Decompose(_scale, _rot, _pos))
		{
			// 潰れた行列は向きが取れないので、位置だけで出す
			_pos = a_worldMat.Translation();
			_rot = Math::Quaternion::Identity();
		}

		Engine::ECS::Signature _sig = {};
		EffectDataMap _data = {};

		if (!BuildEffectEntity(
			a_world, a_effectGUID, _pos, _rot, a_isDestroyOnFinish, {}, a_scale, _sig, _data))
		{
			return false;
		}

		a_world.ReserveCreateEntityWithData(_sig, std::move(_data));
		return true;
	}

	//==========================================================================================
	// 子として出す
	//
	// 遅延生成なので出したエンティティの ID は返らない。
	// 親は ID で付ける(HierarchyLinkSystem は GUID が入っているときだけ親を引き直すので、ID が残る)。
	// 階層の深さは親の深さ + 1(CreateChildInstanceNow と同じ)。
	//==========================================================================================
	bool ReserveSpawnChildEffect(
		Engine::ECS::World& a_world,
		const Core::GUID& a_effectGUID,
		Engine::ECS::Entity a_parent,
		float a_scale,
		std::initializer_list<Engine::ECS::ComponentTypeID> a_extraTagIDs)
	{
		if (a_effectGUID == Core::DEFAULT_GUID) return false;
		if (a_parent == Engine::ECS::Limits::INVALID_ENTITY || !a_world.IsAliveEntity(a_parent)) return false;

		Engine::ECS::Signature _sig = {};
		EffectDataMap _data = {};

		// 親の原点に置く(ローカル)
		if (!BuildEffectEntity(
			a_world, a_effectGUID, Math::Vector3(0.0f, 0.0f, 0.0f), Math::Quaternion::Identity(), false, {}, a_scale, _sig, _data))
		{
			return false;
		}

		// ---- 位置だけ親に付いて行く ----
		// 回転と拡縮まで継ぐと、親が回ったり縮んだりしたときに煙まで振り回される
		const auto _transTypeID = a_world.GetCompTypeID<Component::LocalTransformComponent>();
		{
			Component::LocalTransformComponent _localTrs = {};
			std::memcpy(&_localTrs, _data[_transTypeID].data(), sizeof(_localTrs));
			_localTrs.inheritance = Component::ETransformInheritance::Translation;
			std::memcpy(_data[_transTypeID].data(), &_localTrs, sizeof(_localTrs));
		}

		// 出た最初のフレームから親の位置に出るよう、ワールド行列も親の位置にしておく
		if (const auto* _pParentMat = a_world.RefData<Component::WorldMatrixComponent>(a_parent))
		{
			const auto _matTypeID = a_world.GetCompTypeID<Component::WorldMatrixComponent>();

			Component::WorldMatrixComponent _worldMat = {};
			std::memcpy(&_worldMat, _data[_matTypeID].data(), sizeof(_worldMat));
			_worldMat.worldMat = Math::Matrix::CreateTranslation(Math::Matrix(_pParentMat->worldMat).Translation());
			std::memcpy(_data[_matTypeID].data(), &_worldMat, sizeof(_worldMat));
		}

		// ---- 親子 ----
		Component::HierarchyComponent _hierarchy = {};
		_hierarchy.parentID = a_parent;
		_hierarchy.depth = 1;
		if (const auto* _pParentHierarchy = a_world.RefData<Component::HierarchyComponent>(a_parent))
		{
			_hierarchy.depth = _pParentHierarchy->depth + 1;
		}
		if (!PushComponent(a_world, _sig, _data, _hierarchy)) return false;

		// ---- 保存しない ----
		PushComponent(a_world, _sig, _data, Engine::ECS::TransientTag{});

		// ---- 呼んだ側の印 ----
		for (const Engine::ECS::ComponentTypeID _tagID : a_extraTagIDs)
		{
			if (_tagID == Engine::ECS::Limits::INVALID_COMPONENTTYPEID) continue;

			_sig.set(_tagID);
			auto& _buf = _data[_tagID];
			_buf.assign(a_world.GetComponentMetaData(_tagID).compAlignSize, 0);
			if (auto _ctor = a_world.GetCompFunc(_tagID).construct; _ctor && !_buf.empty()) _ctor(_buf.data());
		}

		a_world.ReserveCreateEntityWithData(_sig, std::move(_data));
		return true;
	}

	//==========================================================================================
	// 付けたまま出す
	//
	// ReserveSpawnChildEffect との違いは、回転も継ぐ・置き場所を選べる・出し切ったら消える、の3つ。
	// 拡縮を継がないのは、取り付け側の拡縮が粒の大きさと飛距離にまで掛かってしまうため。
	//==========================================================================================
	bool ReserveSpawnAttachedEffect(
		Engine::ECS::World& a_world,
		const Core::GUID& a_effectGUID,
		Engine::ECS::Entity a_parent,
		const Math::Vector3& a_localPos,
		const Math::Vector3& a_localEmitDir,
		float a_scale)
	{
		if (a_effectGUID == Core::DEFAULT_GUID) return false;
		if (a_parent == Engine::ECS::Limits::INVALID_ENTITY || !a_world.IsAliveEntity(a_parent)) return false;

		Engine::ECS::Signature _sig = {};
		EffectDataMap _data = {};

		// 向きは上書き欄へ入れる。エフェクトの行列(= 親の回転)の中で回されるので、親のローカルの向きのまま渡せばよい
		if (!BuildEffectEntity(
			a_world, a_effectGUID, a_localPos, Math::Quaternion::Identity(), true, a_localEmitDir, a_scale, _sig, _data))
		{
			return false;
		}

		// ---- 位置と回転だけ親に付いて行く ----
		const auto _transTypeID = a_world.GetCompTypeID<Component::LocalTransformComponent>();
		{
			Component::LocalTransformComponent _localTrs = {};
			std::memcpy(&_localTrs, _data[_transTypeID].data(), sizeof(_localTrs));
			_localTrs.inheritance = Component::ETransformInheritance::Translation | Component::ETransformInheritance::Rotation;
			std::memcpy(_data[_transTypeID].data(), &_localTrs, sizeof(_localTrs));
		}

		// 出た最初のフレームから噴射口に出るよう、ワールド行列も親の拡縮を外した行列で組んでおく
		if (const auto* _pParentMat = a_world.RefData<Component::WorldMatrixComponent>(a_parent))
		{
			Math::Matrix _parentMat(_pParentMat->worldMat);
			Math::Vector3 _parentScale;
			Math::Quaternion _parentRot;
			Math::Vector3 _parentPos;
			if (!_parentMat.Decompose(_parentScale, _parentRot, _parentPos))
			{
				_parentRot = Math::Quaternion::Identity();
				_parentPos = _parentMat.Translation();
			}

			const auto _matTypeID = a_world.GetCompTypeID<Component::WorldMatrixComponent>();

			Component::WorldMatrixComponent _worldMat = {};
			std::memcpy(&_worldMat, _data[_matTypeID].data(), sizeof(_worldMat));
			_worldMat.worldMat =
				Math::Matrix::CreateTranslation(a_localPos) *
				Math::Matrix::CreateFromQuaternion(_parentRot) *
				Math::Matrix::CreateTranslation(_parentPos);
			std::memcpy(_data[_matTypeID].data(), &_worldMat, sizeof(_worldMat));
		}

		// ---- 親子 ----
		Component::HierarchyComponent _hierarchy = {};
		_hierarchy.parentID = a_parent;
		_hierarchy.depth = 1;
		if (const auto* _pParentHierarchy = a_world.RefData<Component::HierarchyComponent>(a_parent))
		{
			_hierarchy.depth = _pParentHierarchy->depth + 1;
		}
		if (!PushComponent(a_world, _sig, _data, _hierarchy)) return false;

		// ---- 保存しない ----
		PushComponent(a_world, _sig, _data, Engine::ECS::TransientTag{});

		a_world.ReserveCreateEntityWithData(_sig, std::move(_data));
		return true;
	}

	//==========================================================================================
	// 即時生成
	//
	// 遅延生成だと ID が返らないので、出したあとも動かし続けたい相手はこちらで作る。
	// CreateEntity はコンストラクタまで回してくれるので、
	// 組んでおいた初期データを上から流し込めばそれで出来上がり。
	//==========================================================================================
	Engine::ECS::Entity SpawnEffectAtNow(
		Engine::ECS::World& a_world,
		const Core::GUID& a_effectGUID,
		const Math::Vector3& a_pos,
		bool a_isDestroyOnFinish,
		const Math::Vector3& a_emitDir,
		float a_scale)
	{
		if (a_effectGUID == Core::DEFAULT_GUID) return Engine::ECS::Limits::INVALID_ENTITY;

		Engine::ECS::Signature _sig = {};
		EffectDataMap _data = {};

		if (!BuildEffectEntity(
			a_world, a_effectGUID, a_pos, Math::Quaternion::Identity(), a_isDestroyOnFinish, a_emitDir, a_scale, _sig, _data))
		{
			return Engine::ECS::Limits::INVALID_ENTITY;
		}

		const Engine::ECS::Entity _entity = a_world.CreateEntity(_sig);
		if (_entity == Engine::ECS::Limits::INVALID_ENTITY) return _entity;

		// 既定構築済みのところへ、組んでおいた初期値を上書きする
		for (const auto& [_typeID, _buf] : _data)
		{
			uint8_t* _pDst = a_world.NRefData(_entity, _typeID);
			if (!_pDst || _buf.empty()) continue;

			std::memcpy(_pDst, _buf.data(), _buf.size());
		}

		return _entity;
	}
	void WarmupEffectParticles(const Engine::ECS::EngineServices& a_services, Engine::Handle<Engine::Resource::EffectAsset> a_effectHandle)
	{
		if (!a_services.pResourceManager || !a_services.pMainEngine)  return;

		auto* _pGE = a_services.pMainEngine->RefGraphicsEngine();
		if (!_pGE) return;

		auto* _pPM = _pGE->RefParticleManager();
		if (!_pPM) return;

		// エフェクト取得
		auto* _pEffect = a_services.pResourceManager->Get(a_effectHandle);
		if (!_pEffect) return;

		// エフェクトからとれるパーツのプールを作成する。
		// パーティクル未設定のパーツや、アセットがまだ読めていないものは
		// CreateParticleDataAsync の側で弾かれる(後で ReserveEmit が作り直しに来る)
		for (auto& _part : _pEffect->GetParticleParts())
		{
			if (!_part.IsValid()) continue;
			_pPM->CreateParticleDataAsync(_part.particleHandle);
		}
	}
	void ReserveReturnEffectEmitterSlot(
		const Engine::ECS::EngineServices& a_services,
		Engine::Handle<Engine::Resource::EffectAsset> a_effectHandle,
		Engine::Handle<Engine::Graphics::Particle::EmitterTransform>& a_emitterSlot)
	{
		if (!a_emitterSlot.IsValid()) return;

		// 手元は先に空にしておく(どこで抜けても二重に返さない)
		const auto _slot = a_emitterSlot;
		a_emitterSlot = {};

		if (!a_services.pMainEngine) return;
		auto* _pGE = a_services.pMainEngine->RefGraphicsEngine();
		if (!_pGE) return;
		auto* _pPM = _pGE->RefParticleManager();
		if (!_pPM) return;
		auto* _pSlotPool = _pPM->RefEmitterSlotPool();
		if (!_pSlotPool) return;

		//----------------------------------------------------------------------
		// 待つ時間 : このエフェクトでローカル空間で回すパーツ(パーツの上書き込み)の最大寿命
		//
		// 粒の寿命は [LifeTimeMin, LifeTimeMax] の乱数なので、Max だけ待てば全部消えている。
		// エフェクトやパーティクルが引けず寿命が分からないときは長めの代わりの値で待つ
		//----------------------------------------------------------------------
		float _holdSeconds = 0.0f;
		bool _isKnown = false;

		const auto* _pEffect = a_services.pResourceManager ? a_services.pResourceManager->Get(a_effectHandle) : nullptr;
		if (_pEffect)
		{
			_isKnown = true;
			for (const auto& _part : _pEffect->GetParticleParts())
			{
				if (!_part.IsValid()) continue;

				const auto* _pParticle = a_services.pResourceManager->Get(_part.particleHandle);
				if (!_pParticle)
				{
					_isKnown = false;
					continue;
				}
				if (!_part.IsLocalSimulation(_pParticle)) continue;

				_holdSeconds = (std::max)(_holdSeconds, _pParticle->GetLifeTimeMax());
			}
		}
		if (!_isKnown)
		{
			_holdSeconds = (std::max)(_holdSeconds, Engine::Graphics::Particle::EMITTER_SLOT_FALLBACK_HOLD_SECONDS);
		}

		_pSlotPool->ReserveReturn(_slot, _holdSeconds);
	}
	void ReleaseEffectLights(
		const Engine::ECS::EngineServices& a_services,
		Engine::Effect::EffectInstance& a_instance)
	{
		// 終了処理の順によっては、こちらが先に消えていることがある(PointLightComponent と同じ)
		auto* _pGE = a_services.pMainEngine ? a_services.pMainEngine->RefGraphicsEngine() : nullptr;
		if (!_pGE || !_pGE->RefLightManager()) return;

		a_instance.ReleaseLights(*_pGE->RefLightManager());
	}

	void ReleaseEffectTrails(
		const Engine::ECS::EngineServices& a_services,
		Engine::Effect::EffectInstance& a_instance)
	{
		// ライトと同じく、終了処理の順によってはこちらが先に消えている
		auto* _pGE = a_services.pMainEngine ? a_services.pMainEngine->RefGraphicsEngine() : nullptr;
		if (!_pGE || !_pGE->RefTrailNodePool()) return;

		a_instance.ReleaseTrails(*_pGE->RefTrailNodePool());
	}
}
