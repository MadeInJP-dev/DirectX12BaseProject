#include "PlayerSpawner.h"

#include <cstring>

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/ECS/World/World.h"
#include "Engine/ECS/Component/GUIDComponent.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"
#include "Engine/Graphics/DebugDraw/DebugDraw.h"
#include "Engine/EditorField/EditorField.h"
#include "Engine/Common/Color.h"

#include "Engine/Resource/Data/Prefab/Prefab.h"

#include "Application/Components/Movement/LookAngleComponent.h"
#include "Application/Components/Input/PlayerControllTag.h"
#include "Application/Components/Camera/FollowTargetComponent.h"
#include "Application/Components/Attachment/AttachmentSlotsComponent.h"
#include "Application/Components/Attachment/WeaponMountComponent.h"
#include "Application/Components/Weapon/Hand/WeaponStateComponent.h"
#include "Application/Components/Weapon/Shoulder/MissileStateComponent.h"
#include "Application/InstanceResource/GameDataResource.h"
#include "Application/Utility/PrefabSpawnHelper.h"

namespace App::Object
{
	namespace
	{
		// 目印の大きさ(m)
		constexpr float MARKER_SIZE = 1.0f;
	}

	//======================================================================================
	// 開始 : シーンの始まりに1回だけ出す
	//======================================================================================
	void PlayerSpawner::Start(Engine::GameObject::ObjectContext& a_context)
	{
		if (!ResolveBodyPrefabGUID(a_context).IsValid())
		{
			ENGINE_WARNING("PlayerSpawner : 出撃構成にもスポナーにもプレハブが設定されていないのでプレイヤーを出しません");
			return;
		}

		m_isSpawnTried = true;
		SpawnPlayer(a_context);
	}

	//======================================================================================
	// 更新
	//--------------------------------------------------------------------------------------
	// エディターで置いた直後はプレハブが未設定なので Start では出せない。
	// 設定されたところで出す(Start で一度試していれば、ここでは出し直さない)
	//======================================================================================
	void PlayerSpawner::Update(Engine::GameObject::ObjectContext& a_context)
	{
		if (!m_isSpawnTried && ResolveBodyPrefabGUID(a_context).IsValid())
		{
			m_isSpawnTried = true;
			SpawnPlayer(a_context);
		}

		DrawSpawnMarker(a_context);
	}

	//======================================================================================
	// プレイヤーを出す
	//--------------------------------------------------------------------------------------
	// 体の向き(LocalTransform)だけでなく視線(LookAngleComponent)の Yaw もそろえる。
	// プレイヤーの体は LockOnRotationSystem が視線へ寄せていくので、
	// 体だけ回しても最初の数フレームでプレハブの視線の向きへ戻ってしまう。
	//======================================================================================
	bool PlayerSpawner::SpawnPlayer(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld || !a_context.pServices || !a_context.pServices->pResourceManager) return false;

		auto& _world = *a_context.pWorld;
		auto& _rm    = *a_context.pServices->pResourceManager;

		// 出撃構成があればその本体、無ければ自分のプレハブ(武器込みの一体物)
		const Game::PlayerLoadout* _pLoadout = FindLoadout(a_context);
		const Core::GUID _bodyGUID = _pLoadout ? _pLoadout->bodyPrefabGUID : m_prefabGUID;

		// 生成に使うので実体ができるまで待つ
		m_prefabRef = _rm.LoadImmediate<Engine::Resource::Prefab>(_bodyGUID);

		const auto* _pPrefab = _rm.Get(m_prefabRef);
		if (!_pPrefab || _pPrefab->GetSignature().none())
		{
			// 空のまま実体化すると、コンポーネントを持たないエンティティが出る
			ENGINE_WARNING("PlayerSpawner : プレハブを読めませんでした : %s", _bodyGUID.String().c_str());
			return false;
		}

		App::Utility::SpawnParams _params = {};
		_params.pos  = m_pos;
		_params.quat = Math::Quaternion::CreateFromYawPitchRoll(DirectX::XMConvertToRadians(m_yaw), 0.0f, 0.0f);
		_params.isOverrideRotation = true;

		std::vector<Engine::Resource::PrefabInstanceData> _instanceVec = {};
		if (!App::Utility::BuildSpawnInstanceData(_world, *_pPrefab, _params, _instanceVec))
		{
			ENGINE_WARNING("PlayerSpawner : プレハブから生成の材料を作れませんでした");
			return false;
		}

		// 視線の向き。持っていないプレハブには足さない(視線を使わない機体もあるため)
		Engine::Resource::PrefabInstanceData& _root = _instanceVec[0];
		const Engine::ECS::ComponentTypeID _lookTypeID = _world.GetCompTypeID<Component::LookAngleComponent>();

		auto _lookIt = _root.dataMap.find(_lookTypeID);
		if (_lookIt != _root.dataMap.end() && _lookIt->second.size() >= sizeof(Component::LookAngleComponent))
		{
			Component::LookAngleComponent _look = {};
			std::memcpy(&_look, _lookIt->second.data(), sizeof(_look));
			_look.Yaw = m_yaw;
			std::memcpy(_lookIt->second.data(), &_look, sizeof(_look));
		}

		std::vector<Engine::ECS::Entity> _bodyEntityVec = {};
		m_playerEntity = App::Utility::CreateInstanceNow(_world, _instanceVec, &_bodyEntityVec);
		if (m_playerEntity == Engine::ECS::Limits::INVALID_ENTITY)
		{
			ENGINE_WARNING("PlayerSpawner : プレイヤーのエンティティを作れませんでした");
			return false;
		}

		// 出撃構成の武器を取り付け口へ
		if (_pLoadout) AttachLoadoutWeapons(a_context, *_pLoadout, _bodyEntityVec);

		// 見せるだけの機体は入力で動かさない(外れるのは次のフレームの頭)
		if (!m_isControllable && _world.HasComponent<Component::PlayerControllTag>(m_playerEntity))
		{
			_world.ReserveRemoveComponent(_world.GetCompTypeID<Component::PlayerControllTag>(), m_playerEntity);
		}

		LinkCamera(a_context);
		return true;
	}

	//======================================================================================
	// 出撃構成
	//======================================================================================
	const Game::PlayerLoadout* PlayerSpawner::FindLoadout(Engine::GameObject::ObjectContext& a_context) const
	{
		// エディターの確認用ワールドなど、記録の入口が無いワールドもある
		const Game::GlobalGameContext* _pGameData = InstanceResource::GameDataResource::Find(a_context.pWorld);
		if (!_pGameData || !_pGameData->loadout.IsValid()) return nullptr;

		return &_pGameData->loadout;
	}

	Core::GUID PlayerSpawner::ResolveBodyPrefabGUID(Engine::GameObject::ObjectContext& a_context) const
	{
		const Game::PlayerLoadout* _pLoadout = FindLoadout(a_context);
		return _pLoadout ? _pLoadout->bodyPrefabGUID : m_prefabGUID;
	}

	//--------------------------------------------------------------------------------------
	// 出撃構成の武器を付ける
	//
	// 本体の子の取り付け口(WeaponMountComponent)を探し、その子として武器を出す。
	// 付けた武器は本体のスロットへ ID で入れる(武器は GUID を持たないので ID でしかつながらない)。
	// スロットの GUID は空にしておく。残っていると AttachmentSlotLinkSystem が
	// GUID から引き直して、入れた ID を消してしまう。
	//
	// 取り付け口と武器の種類が合わないもの(手に肩の武器など)は付けない。
	// 手持ちの武器は WeaponStateComponent、肩の武器は MissileStateComponent を持つかで見分ける。
	//--------------------------------------------------------------------------------------
	void PlayerSpawner::AttachLoadoutWeapons(
		Engine::GameObject::ObjectContext& a_context,
		const Game::PlayerLoadout& a_loadout,
		const std::vector<Engine::ECS::Entity>& a_bodyEntityVec)
	{
		auto& _world = *a_context.pWorld;

		// 取り付け口の種類 → 取り付け口のエンティティ(あとから付け替えるときにも使うので覚えておく)
		m_mountEntityArray.fill(Engine::ECS::Limits::INVALID_ENTITY);
		m_weaponEntityArray.fill(Engine::ECS::Limits::INVALID_ENTITY);

		for (const Engine::ECS::Entity _entity : a_bodyEntityVec)
		{
			const auto* _pMount = _world.RefData<Component::WeaponMountComponent>(_entity);
			if (!_pMount) continue;

			const size_t _index = static_cast<size_t>(_pMount->mount);
			if (_index < WEAPON_MOUNT_COUNT) m_mountEntityArray[_index] = _entity;
		}

		if (!_world.HasComponent<Component::AttachmentSlotsComponent>(m_playerEntity))
		{
			ENGINE_WARNING("PlayerSpawner : 本体が AttachmentSlotsComponent を持たないので、武器を撃てるようにつなげません");
		}

		struct MountTarget
		{
			Component::EWeaponMount mount;
			Core::GUID prefabGUID;
		};
		const MountTarget _targetArray[] =
		{
			{ Component::EWeaponMount::RightHand,     a_loadout.rightHandWeaponPrefabGUID },
			{ Component::EWeaponMount::LeftHand,      a_loadout.leftHandWeaponPrefabGUID },
			{ Component::EWeaponMount::RightShoulder, a_loadout.shoulderWeaponPrefabGUID },
			{ Component::EWeaponMount::LeftShoulder,  a_loadout.leftShoulderWeaponPrefabGUID },
		};

		for (const MountTarget& _target : _targetArray)
		{
			if (!_target.prefabGUID.IsValid()) continue;
			AttachWeapon(a_context, _target.mount, _target.prefabGUID);
		}
	}

	//--------------------------------------------------------------------------------------
	// 武器を1つ付ける
	//
	// 取り付け口と武器の種類が合わないもの(手に肩の武器など)は付けない。
	// 手持ちの武器は WeaponStateComponent、肩の武器は MissileStateComponent を持つかで見分ける。
	// 付けた武器は本体のスロットへ ID で入れる(武器は GUID を持たないので ID でしかつながらない)。
	// スロットの GUID は空にしておく。残っていると AttachmentSlotLinkSystem が
	// GUID から引き直して、入れた ID を消してしまう。
	//--------------------------------------------------------------------------------------
	Engine::ECS::Entity PlayerSpawner::AttachWeapon(
		Engine::GameObject::ObjectContext& a_context,
		Component::EWeaponMount a_mount,
		const Core::GUID& a_prefabGUID)
	{
		auto& _world = *a_context.pWorld;
		auto& _rm = *a_context.pServices->pResourceManager;

		const size_t _index = static_cast<size_t>(a_mount);
		if (_index >= WEAPON_MOUNT_COUNT) return Engine::ECS::Limits::INVALID_ENTITY;

		const Engine::ECS::Entity _mountEntity = m_mountEntityArray[_index];
		if (_mountEntity == Engine::ECS::Limits::INVALID_ENTITY || !_world.IsAliveEntity(_mountEntity))
		{
			ENGINE_WARNING("PlayerSpawner : 本体に取り付け口がありません(%u)。武器を付けません", static_cast<uint32_t>(a_mount));
			return Engine::ECS::Limits::INVALID_ENTITY;
		}

		m_weaponPrefabRefArray[_index] = _rm.LoadImmediate<Engine::Resource::Prefab>(a_prefabGUID);
		const auto* _pWeaponPrefab = _rm.Get(m_weaponPrefabRefArray[_index]);
		if (!_pWeaponPrefab || _pWeaponPrefab->GetSignature().none())
		{
			ENGINE_WARNING("PlayerSpawner : 武器のプレハブを読めませんでした : %s", a_prefabGUID.String().c_str());
			return Engine::ECS::Limits::INVALID_ENTITY;
		}

		// 取り付け口と武器の種類が合っているか
		const bool _isShoulder =
			(a_mount == Component::EWeaponMount::RightShoulder || a_mount == Component::EWeaponMount::LeftShoulder);
		const Engine::ECS::ComponentTypeID _needTypeID = _isShoulder
			? _world.GetCompTypeID<Component::MissileStateComponent>()
			: _world.GetCompTypeID<Component::WeaponStateComponent>();
		if (!_pWeaponPrefab->Has(_needTypeID))
		{
			ENGINE_WARNING("PlayerSpawner : 取り付け口と武器の種類が合いません(%s には %s が要ります) : %s",
				_isShoulder ? "肩" : "手",
				_isShoulder ? "MissileStateComponent" : "WeaponStateComponent",
				a_prefabGUID.String().c_str());
			return Engine::ECS::Limits::INVALID_ENTITY;
		}

		const Engine::ECS::Entity _weapon = App::Utility::CreateChildInstanceNow(_world, *_pWeaponPrefab, _mountEntity);
		if (_weapon == Engine::ECS::Limits::INVALID_ENTITY)
		{
			ENGINE_WARNING("PlayerSpawner : 武器のエンティティを作れませんでした : %s", a_prefabGUID.String().c_str());
			return Engine::ECS::Limits::INVALID_ENTITY;
		}

		// スロットは作った後に引く(作る途中で本体のデータが動いても古い場所を指さないように)
		if (Component::AttachmentSlot* _pSlot = RefMountSlot(_world, a_mount))
		{
			_pSlot->id = _weapon;
			_pSlot->guid = Core::DEFAULT_GUID;
		}

		m_weaponEntityArray[_index] = _weapon;
		return _weapon;
	}

	//======================================================================================
	// 出した機体の取り付け口と武器
	//======================================================================================
	Engine::ECS::Entity PlayerSpawner::GetMountEntity(Component::EWeaponMount a_mount) const
	{
		const size_t _index = static_cast<size_t>(a_mount);
		return (_index < WEAPON_MOUNT_COUNT) ? m_mountEntityArray[_index] : Engine::ECS::Limits::INVALID_ENTITY;
	}

	Engine::ECS::Entity PlayerSpawner::GetWeaponEntity(Component::EWeaponMount a_mount) const
	{
		const size_t _index = static_cast<size_t>(a_mount);
		return (_index < WEAPON_MOUNT_COUNT) ? m_weaponEntityArray[_index] : Engine::ECS::Limits::INVALID_ENTITY;
	}

	Component::AttachmentSlot* PlayerSpawner::RefMountSlot(Engine::ECS::World& a_world, Component::EWeaponMount a_mount) const
	{
		if (!a_world.IsAliveEntity(m_playerEntity)) return nullptr;

		auto* _pSlots = a_world.RefData<Component::AttachmentSlotsComponent>(m_playerEntity);
		if (!_pSlots) return nullptr;

		switch (a_mount)
		{
		case Component::EWeaponMount::RightHand:		return &_pSlots->rightWeapon;
		case Component::EWeaponMount::LeftHand:			return &_pSlots->leftWeapon;
		case Component::EWeaponMount::RightShoulder:	return &_pSlots->missile;
		case Component::EWeaponMount::LeftShoulder:		return &_pSlots->leftMissile;
		default:										return nullptr;
		}
	}

	//======================================================================================
	// 付け替え : 今の武器を解放予約して、新しい武器をその場で付ける
	//======================================================================================
	bool PlayerSpawner::ReplaceWeapon(
		Engine::GameObject::ObjectContext& a_context,
		Component::EWeaponMount a_mount,
		const Core::GUID& a_prefabGUID)
	{
		if (!a_context.pWorld || !a_context.pServices || !a_context.pServices->pResourceManager) return false;

		auto& _world = *a_context.pWorld;
		const size_t _index = static_cast<size_t>(a_mount);
		if (_index >= WEAPON_MOUNT_COUNT) return false;
		if (!_world.IsAliveEntity(m_playerEntity)) return false;

		// 今の武器を外す(消えるのは次のフレームの頭)
		const Engine::ECS::Entity _old = m_weaponEntityArray[_index];
		if (_old != Engine::ECS::Limits::INVALID_ENTITY && _world.IsAliveEntity(_old))
		{
			_world.ReserveReleaseEntity(_old);
		}
		m_weaponEntityArray[_index] = Engine::ECS::Limits::INVALID_ENTITY;

		if (Component::AttachmentSlot* _pSlot = RefMountSlot(_world, a_mount))
		{
			_pSlot->id = Engine::ECS::Limits::INVALID_ENTITY;
			_pSlot->guid = Core::DEFAULT_GUID;
		}

		if (!a_prefabGUID.IsValid()) return true;

		return AttachWeapon(a_context, a_mount, a_prefabGUID) != Engine::ECS::Limits::INVALID_ENTITY;
	}

	//======================================================================================
	// カメラの追従先を出したプレイヤーへ付ける(エンティティIDで持つ)
	//======================================================================================
	void PlayerSpawner::LinkCamera(Engine::GameObject::ObjectContext& a_context)
	{
		if (!m_cameraGUID.IsValid()) return;
		if (!a_context.pWorld) return;

		auto& _world = *a_context.pWorld;

		const Engine::ECS::Entity _camera = _world.GetEntity(m_cameraGUID);
		if (!_world.IsAliveEntity(_camera))
		{
			ENGINE_WARNING("PlayerSpawner : 追従させるカメラが見つかりません : %s", m_cameraGUID.String().c_str());
			return;
		}
		if (!_world.HasComponent<Component::FollowTargetComponent>(_camera))
		{
			ENGINE_WARNING("PlayerSpawner : カメラが FollowTargetComponent を持っていません");
			return;
		}
		_world.RefData<Component::FollowTargetComponent>(_camera)->target = m_playerEntity;
	}

	//======================================================================================
	// 目印 : 出現位置の十字と、向いている方向
	//======================================================================================
	void PlayerSpawner::DrawSpawnMarker(Engine::GameObject::ObjectContext& a_context) const
	{
		if (!a_context.pServices) return;

		auto* _pDebugDraw = a_context.pServices->pDebugDraw;
		if (!_pDebugDraw) return;

		const Math::Color _color = Engine::Color::GREEN;

		_pDebugDraw->DrawLine(
			m_pos - Math::Vector3(MARKER_SIZE, 0.0f, 0.0f),
			m_pos + Math::Vector3(MARKER_SIZE, 0.0f, 0.0f), _color);
		_pDebugDraw->DrawLine(
			m_pos - Math::Vector3(0.0f, 0.0f, MARKER_SIZE),
			m_pos + Math::Vector3(0.0f, 0.0f, MARKER_SIZE), _color);
		_pDebugDraw->DrawLine(
			m_pos, m_pos + Math::Vector3(0.0f, MARKER_SIZE * 2.0f, 0.0f), _color);

		// 向き(LookAngleComponent と同じ規約 : Yaw 0 = +Z)
		const float _yawRad = DirectX::XMConvertToRadians(m_yaw);
		const Math::Vector3 _forward = { std::sin(_yawRad), 0.0f, std::cos(_yawRad) };
		_pDebugDraw->DrawLine(m_pos, m_pos + _forward * (MARKER_SIZE * 3.0f), Engine::Color::BLUE);
	}

	//======================================================================================
	// ギズモ : 出現位置を動かす(選んでいる間だけ呼ばれる)
	//======================================================================================
	bool PlayerSpawner::DrawGizmo(
		const Engine::GameObject::ObjectGizmoContext& a_ctx,
		Engine::GameObject::ObjectContext& /*a_context*/)
	{
		Math::Matrix _mat = Math::Matrix::CreateTranslation(m_pos);

		// Ctrl を押している間だけスナップ(エンティティ用ギズモと同じ操作感)
		const float _snap = Engine::EditorField::IsCtrlDown() ? 1.0f : 0.0f;

		if (Engine::EditorField::TranslateGizmo(a_ctx.viewMat, a_ctx.projMat, _mat, _snap))
		{
			m_pos = Math::Vector3(_mat._41, _mat._42, _mat._43);
		}

		return true;
	}

	//======================================================================================
	// シリアライズ : 設定だけを保存する(出したプレイヤーは保存しない)
	//======================================================================================
	void PlayerSpawner::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& /*a_context*/)
	{
		a_ar.GUIDField("PrefabGUID", m_prefabGUID);
		a_ar.Field("Pos", m_pos);
		a_ar.Field("Yaw", m_yaw);
		a_ar.GUIDField("CameraGUID", m_cameraGUID);

		// ---- ここから下は後から足したもの : 追加は必ず末尾へ ----
		// 区切りを持たない古い .ob* には無いので読まない
		if (!a_ar.IsLegacyLayout())
		{
			a_ar.Field("IsControllable", m_isControllable);
		}
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void PlayerSpawner::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pServices || !a_context.pWorld) return;

		auto& _world = *a_context.pWorld;

		Engine::EditorField::Header("プレイヤー");
		Engine::EditorField::AssetField(*a_context.pServices, "Player Prefab", "Prefab", m_prefabGUID);
		Engine::EditorField::Tooltip("シーンの始まりに1回だけ出す。差し替えはシーンを開き直すと反映される");

		// 出撃構成があれば、上のプレハブではなくそちらを出す
		if (const Game::PlayerLoadout* _pLoadout = FindLoadout(a_context))
		{
			auto& _assetDB = *a_context.pServices->pAssetDatabase;
			Engine::EditorField::HelpText("出撃構成(GlobalGameContext)を使って出す");
			Engine::EditorField::Value("Body", "%s", _assetDB.GetFileNameFromGUID(_pLoadout->bodyPrefabGUID).c_str());
			Engine::EditorField::Value("Right Hand", "%s", _assetDB.GetFileNameFromGUID(_pLoadout->rightHandWeaponPrefabGUID).c_str());
			Engine::EditorField::Value("Left Hand", "%s", _assetDB.GetFileNameFromGUID(_pLoadout->leftHandWeaponPrefabGUID).c_str());
			Engine::EditorField::Value("Right Shoulder", "%s", _assetDB.GetFileNameFromGUID(_pLoadout->shoulderWeaponPrefabGUID).c_str());
			Engine::EditorField::Value("Left Shoulder", "%s", _assetDB.GetFileNameFromGUID(_pLoadout->leftShoulderWeaponPrefabGUID).c_str());
		}
		else
		{
			Engine::EditorField::HelpText("出撃構成が無いので、上のプレハブ(武器込みの一体物)を出す");
		}

		Engine::EditorField::Field("Controllable", m_isControllable);
		Engine::EditorField::Tooltip("切ると入力で動かない(ガレージで見せるだけの機体)。反映はシーンを開き直したとき");

		Engine::EditorField::Field("Position", m_pos, 0.1f);
		Engine::EditorField::Field("Yaw (deg)", m_yaw, 0.5f);
		Engine::EditorField::Tooltip("0 = +Z 前方。体の向きと視線(LookAngle)の両方に入れる");

		//----------------------------------------------------------------------
		// カメラ : エンティティ番号で選び、保存は GUID で持つ
		//----------------------------------------------------------------------
		Engine::EditorField::Header("カメラ");

		Engine::ECS::Entity _camera = _world.GetEntity(m_cameraGUID);
		if (Engine::EditorField::Field("Camera Entity", _camera))
		{
			if (_world.IsAliveEntity(_camera) && _world.HasComponent<Engine::ECS::GUIDComponent>(_camera))
			{
				m_cameraGUID = _world.RefData<Engine::ECS::GUIDComponent>(_camera)->guid;
			}
		}
		Engine::EditorField::Text("%s", m_cameraGUID.String().c_str());
		Engine::EditorField::Tooltip("出したプレイヤーをこのカメラの FollowTargetComponent へ入れる");

		if (m_cameraGUID.IsValid() && Engine::EditorField::SmallButton("Clear Camera"))
		{
			m_cameraGUID = Core::DEFAULT_GUID;
		}

		// 実行中の状態は表示のみ
		Engine::EditorField::Line();
		Engine::EditorField::Value("Player Entity", "%llu", static_cast<unsigned long long>(m_playerEntity));
	}
}
