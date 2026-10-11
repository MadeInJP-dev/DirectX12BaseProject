#include "PlayerSpawner.h"

#include <cstring>

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/ECS/World/World.h"
#include "Engine/ECS/Component/GUIDComponent.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Graphics/DebugDraw/DebugDraw.h"
#include "Engine/EditorField/EditorField.h"
#include "Engine/Common/Color.h"

#include "Application/Components/Movement/LookAngleComponent.h"
#include "Application/Components/Camera/FollowTargetComponent.h"
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
		if (!m_prefabGUID.IsValid())
		{
			ENGINE_WARNING("PlayerSpawner : プレハブが設定されていないのでプレイヤーを出しません");
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
		if (!m_isSpawnTried && m_prefabGUID.IsValid())
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

		// 生成に使うので実体ができるまで待つ
		m_prefabRef = _rm.LoadImmediate<Engine::Resource::Prefab>(m_prefabGUID);

		const auto* _pPrefab = _rm.Get(m_prefabRef);
		if (!_pPrefab || _pPrefab->GetSignature().none())
		{
			// 空のまま実体化すると、コンポーネントを持たないエンティティが出る
			ENGINE_WARNING("PlayerSpawner : プレハブを読めませんでした : %s", m_prefabGUID.String().c_str());
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

		m_playerEntity = App::Utility::CreateInstanceNow(_world, _instanceVec);
		if (m_playerEntity == Engine::ECS::Limits::INVALID_ENTITY)
		{
			ENGINE_WARNING("PlayerSpawner : プレイヤーのエンティティを作れませんでした");
			return false;
		}

		LinkCamera(a_context);
		return true;
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
