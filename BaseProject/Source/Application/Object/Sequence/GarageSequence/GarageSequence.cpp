#include "GarageSequence.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/ECS/World/World.h"
#include "Engine/ECS/Component/GUIDComponent.h"
#include "Engine/GameObject/GameObjectManager/GameObjectManager.h"
#include "Engine/Input/InputManager/InputManager.h"
#include "Engine/Option/OptionManager.h"
#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"
#include "Engine/Resource/Data/Texture/IO/TextureIO.h"
#include "Engine/Resource/Data/Prefab/Prefab.h"
#include "Engine/EditorField/EditorField.h"

#include "../../ObjectPicker.h"
#include "Application/Object/UI/UIBase.h"
#include "Application/Object/UI/Widget/UIButton/UIButton.h"
#include "Application/Object/Scene/PlayerSpawner/PlayerSpawner.h"

#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Camera/ProjMatComponent.h"
#include "Application/Components/Camera/CameraParamComponent.h"
#include "Application/Components/Camera/FocusParamComponent.h"
#include "Application/Components/Weapon/Hand/WeaponStateComponent.h"
#include "Application/Components/Weapon/Shoulder/MissileStateComponent.h"
#include "Application/InstanceResource/GameDataResource.h"

//==========================================================================================
// GarageSequence
//
// 出撃構成の武器を見て、付け替える画面の進行役。
//
// ・機体は PlayerSpawner が出撃構成どおりに出す。付け替えもスポナー(ReplaceWeapon)に頼み、
//   ここは出撃構成(GlobalGameContext::loadout)を書き換えるだけ。次のゲームシーンはそれを出す。
// ・ボタンはシーンへ置いたものを GUID で引き、押されて何をするかをここが差し込む(HomeSequence と同じ)。
//   コールバックの中では ObjectContext を持てないので、押されたことだけ覚えて次の更新で処理する。
// ・カメラは GameObject の更新(ECS の後)で LocalTransform を書く。反映は次のフレームの行列計算。
//==========================================================================================
namespace App::Object
{
	namespace
	{
		// 飾りの名前(シーン側の UI はこの名前で飾りを置く)
		constexpr const char* DECO_ICON = "Icon";	// 武器の絵
		constexpr const char* DECO_NAME = "Name";	// 武器の名前
		constexpr const char* DECO_SLOT = "Slot";	// 取り付け口の名前(箱)
		constexpr const char* DECO_TITLE = "Title";	// 一覧の見出し
		constexpr const char* DECO_BG = "Bg";		// 下地(今付いている武器だけ濃くする)

		constexpr float BG_ALPHA_NORMAL = 0.15f;	// 下地の濃さ : ふつう
		constexpr float BG_ALPHA_EQUIPPED = 0.45f;	// 下地の濃さ : 今付いている

		// 指数減衰の補間係数
		float DampFactor(float a_rate, float a_dt)
		{
			if (!(a_rate > 0.0f) || !(a_dt > 0.0f)) return 1.0f;
			return 1.0f - std::exp(-a_rate * a_dt);
		}

		// 板ポリ用の白テクスチャ(引き出し線)
		Engine::Handle<Engine::Resource::Texture> GetWhiteTexture(Engine::Resource::ResourceManager& a_resourceManager)
		{
			const auto _context = Engine::Resource::MakeManagerOnlyContext(&a_resourceManager, &a_resourceManager.RefAssetDatabase());
			return Engine::Resource::TextureIO::LoadTexture(Core::GUID(), Engine::TexColor::WHITE, &_context);
		}

		// 矩形の中心から a_target へ向かう線が、矩形の縁と交わる点
		Math::Vector2 ClipToRectEdge(const Math::Vector2& a_center, const Math::Vector2& a_halfSize, const Math::Vector2& a_target)
		{
			const Math::Vector2 _d = a_target - a_center;
			const float _tx = (std::abs(_d.x) > 1e-4f) ? a_halfSize.x / std::abs(_d.x) : FLT_MAX;
			const float _ty = (std::abs(_d.y) > 1e-4f) ? a_halfSize.y / std::abs(_d.y) : FLT_MAX;
			const float _t = std::min({ _tx, _ty, 1.0f });
			return a_center + _d * _t;
		}
	}

	//======================================================================================
	// 開始 : 一覧の武器が手に付くか肩に付くかを引いておく
	//======================================================================================
	void GarageSequence::Start(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld || !a_context.pServices || !a_context.pServices->pResourceManager) return;

		auto& _world = *a_context.pWorld;
		auto& _rm = *a_context.pServices->pResourceManager;

		const auto _handTypeID = _world.GetCompTypeID<Component::WeaponStateComponent>();
		const auto _shoulderTypeID = _world.GetCompTypeID<Component::MissileStateComponent>();

		for (GarageWeaponEntry& _entry : m_catalogVec)
		{
			_entry.isHand = false;
			_entry.isShoulder = false;
			if (!_entry.prefabGUID.IsValid()) continue;

			_entry.prefabRef = _rm.LoadImmediate<Engine::Resource::Prefab>(_entry.prefabGUID);
			if (const auto* _pPrefab = _rm.Get(_entry.prefabRef))
			{
				_entry.isHand = _pPrefab->Has(_handTypeID);
				_entry.isShoulder = _pPrefab->Has(_shoulderTypeID);
			}
			if (!_entry.isHand && !_entry.isShoulder)
			{
				ENGINE_WARNING("[GarageSequence] 手にも肩にも付けられない武器が一覧にあります : %s", _entry.name.c_str());
			}
		}

		m_isBoxDirty = true;
	}

	//======================================================================================
	// 更新
	//======================================================================================
	void GarageSequence::Update(Engine::GameObject::ObjectContext& a_context)
	{
		// カーソルの固定を切る(ホームと同じ。固定したままだと箱を押せない)
		if (m_isReleaseCursorLock && a_context.pServices && a_context.pServices->pInputManager)
		{
			if (a_context.pServices->pInputManager->IsCursorLockActive())
			{
				a_context.pServices->pInputManager->SetCursorCentered(false);
			}
		}

		TryBindButtons(a_context);

		// 箱の中身(機体が出たあと・付け替えたあと)
		if (m_isBoxDirty)
		{
			RefreshBoxes(a_context);
			m_isBoxDirty = false;
		}

		UpdateCamera(a_context);

		m_bgm.Update(a_context);
	}

	//======================================================================================
	// ボタンへ押下時の処理を差し込む
	//--------------------------------------------------------------------------------------
	// コールバックの中では ObjectContext を持てないので、マネージャーとワールドとサービスを掴ませる。
	// どれも同じシーンの持ち物なので、このオブジェクトと寿命が尽きるのは一緒
	//======================================================================================
	void GarageSequence::TryBindButtons(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_isBound) return;

		auto* _pObjectManager = a_context.pObjectManager;
		if (!_pObjectManager) return;

		constexpr const char* OWNER = "GarageSequence";
		Picker::CheckFound<UIButton>(_pObjectManager, m_backButtonGUID, OWNER, "BackButton");
		for (const Core::GUID& _guid : m_boxGUIDs) Picker::CheckFound<UIButton>(_pObjectManager, _guid, OWNER, "Box");
		for (const Core::GUID& _guid : m_listItemGUIDVec) Picker::CheckFound<UIButton>(_pObjectManager, _guid, OWNER, "ListItem");

		// 押された処理はその場でやる(ObjectContext は呼んだ時点の写しを持たせる)
		Engine::GameObject::ObjectContext _context = a_context;

		if (auto* _pBack = Picker::Find<UIButton>(_pObjectManager, m_backButtonGUID))
		{
			_pBack->SetOnClick([this, _context]() mutable { OnBack(_context); });
		}

		for (size_t _i = 0; _i < MOUNT_COUNT; ++_i)
		{
			if (auto* _pBox = Picker::Find<UIButton>(_pObjectManager, m_boxGUIDs[_i]))
			{
				const auto _mount = static_cast<Component::EWeaponMount>(_i);
				_pBox->SetOnClick([this, _context, _mount]() mutable { EnterZoom(_context, _mount); });
			}
		}

		for (size_t _i = 0; _i < m_listItemGUIDVec.size(); ++_i)
		{
			if (auto* _pItem = Picker::Find<UIButton>(_pObjectManager, m_listItemGUIDVec[_i]))
			{
				_pItem->SetOnClick([this, _context, _i]() mutable { OnSelectItem(_context, _i); });
			}
		}

		m_isBound = true;
		ApplyVisible(a_context);
	}

	//======================================================================================
	// 全体へ戻る / 取り付け口へ寄る
	//======================================================================================
	void GarageSequence::EnterOverview(Engine::GameObject::ObjectContext& a_context)
	{
		m_mode = EGarageMode::Overview;
		ApplyVisible(a_context);
	}

	void GarageSequence::EnterZoom(Engine::GameObject::ObjectContext& a_context, Component::EWeaponMount a_mount)
	{
		m_mode = EGarageMode::Zoom;
		m_zoomMount = a_mount;
		RefreshList(a_context);
		ApplyVisible(a_context);
	}

	//======================================================================================
	// 戻る : 寄りなら全体へ、全体ならホームへ
	//======================================================================================
	void GarageSequence::OnBack(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_mode == EGarageMode::Zoom)
		{
			EnterOverview(a_context);
			return;
		}

		if (m_isLeaving) return;
		if (!m_homeSceneGUID.IsValid())
		{
			ENGINE_WARNING("[GarageSequence] 戻る先(ホームのシーン)が設定されていません");
			return;
		}
		if (!a_context.pServices || !a_context.pServices->pSceneManager) return;

		m_isLeaving = true;
		a_context.pServices->pSceneManager->ReserveChangeScene(m_homeSceneGUID, Engine::Scene::ESceneChangeType::Replace);
	}

	//======================================================================================
	// 一覧の項目を押した : その武器に付け替える
	//--------------------------------------------------------------------------------------
	// 機体の武器はスポナーに付け替えてもらい、出撃構成も書き換える(次のゲームシーンはこれを出す)。
	// 寄ったまま見比べられるよう、全体へは戻らない
	//======================================================================================
	void GarageSequence::OnSelectItem(Engine::GameObject::ObjectContext& a_context, size_t a_itemIndex)
	{
		if (m_mode != EGarageMode::Zoom) return;
		if (a_itemIndex >= m_listEntryIndexVec.size()) return;

		const GarageWeaponEntry& _entry = m_catalogVec[m_listEntryIndexVec[a_itemIndex]];

		// 同じ武器なら付け替えない
		if (GetLoadoutWeapon(a_context, m_zoomMount) == _entry.prefabGUID) return;

		auto* _pSpawner = Picker::Find<PlayerSpawner>(a_context.pObjectManager, m_spawnerGUID);
		if (!_pSpawner)
		{
			ENGINE_WARNING("[GarageSequence] PlayerSpawner が見つからないので付け替えられません");
			return;
		}

		if (!_pSpawner->ReplaceWeapon(a_context, m_zoomMount, _entry.prefabGUID))
		{
			ENGINE_WARNING("[GarageSequence] 武器を付け替えられませんでした : %s", _entry.name.c_str());
			return;
		}

		SetLoadoutWeapon(a_context, m_zoomMount, _entry.prefabGUID);

		RefreshList(a_context);
		m_isBoxDirty = true;
	}

	//======================================================================================
	// 今のモードに合わせてパネルを出し分ける
	//======================================================================================
	void GarageSequence::ApplyVisible(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pObjectManager = a_context.pObjectManager;
		if (!_pObjectManager) return;

		const bool _isOverview = (m_mode == EGarageMode::Overview);

		if (auto* _pPanel = Picker::Find<Engine::GameObject::BaseObject>(_pObjectManager, m_overviewPanelGUID))
		{
			_pPanel->SetVisible(_isOverview);
		}
		if (auto* _pPanel = Picker::Find<Engine::GameObject::BaseObject>(_pObjectManager, m_listPanelGUID))
		{
			_pPanel->SetVisible(!_isOverview);
		}
	}

	//======================================================================================
	// 箱に今付いている武器を出す
	//======================================================================================
	void GarageSequence::RefreshBoxes(Engine::GameObject::ObjectContext& a_context)
	{
		for (size_t _i = 0; _i < MOUNT_COUNT; ++_i)
		{
			const auto _mount = static_cast<Component::EWeaponMount>(_i);
			const Core::GUID _weapon = GetLoadoutWeapon(a_context, _mount);
			const GarageWeaponEntry* _pEntry = FindEntry(_weapon);

			std::string _name = "EMPTY";
			if (_pEntry)
			{
				_name = _pEntry->name;
			}
			else if (_weapon.IsValid() && a_context.pServices && a_context.pServices->pAssetDatabase)
			{
				// 一覧に無い武器は、ファイルの名前で出す
				_name = a_context.pServices->pAssetDatabase->GetFileNameFromGUID(_weapon);
			}

			SetDecorationText(a_context, m_boxGUIDs[_i], DECO_SLOT, GetMountLabel(_mount));
			SetDecorationText(a_context, m_boxGUIDs[_i], DECO_NAME, _name);
			SetDecorationImage(a_context, m_boxGUIDs[_i], DECO_ICON, _pEntry ? _pEntry->iconGUID : Core::GUID());
		}
	}

	//======================================================================================
	// 一覧に、寄っている取り付け口へ付けられる武器を並べる
	//======================================================================================
	void GarageSequence::RefreshList(Engine::GameObject::ObjectContext& a_context)
	{
		const bool _isShoulder = IsShoulderMount(m_zoomMount);
		const Core::GUID _equipped = GetLoadoutWeapon(a_context, m_zoomMount);

		m_listEntryIndexVec.clear();
		for (size_t _i = 0; _i < m_catalogVec.size(); ++_i)
		{
			const GarageWeaponEntry& _entry = m_catalogVec[_i];
			if (_isShoulder ? _entry.isShoulder : _entry.isHand) m_listEntryIndexVec.push_back(_i);
		}

		SetDecorationText(a_context, m_listTitleGUID, DECO_TITLE, GetMountLabel(m_zoomMount));

		for (size_t _i = 0; _i < m_listItemGUIDVec.size(); ++_i)
		{
			auto* _pItem = Picker::Find<Engine::GameObject::BaseObject>(a_context.pObjectManager, m_listItemGUIDVec[_i]);
			if (!_pItem) continue;

			// 並べる武器が足りない項目は隠す
			if (_i >= m_listEntryIndexVec.size())
			{
				_pItem->SetVisible(false);
				continue;
			}

			const GarageWeaponEntry& _entry = m_catalogVec[m_listEntryIndexVec[_i]];
			_pItem->SetVisible(true);
			SetDecorationText(a_context, m_listItemGUIDVec[_i], DECO_NAME, _entry.name);
			SetDecorationImage(a_context, m_listItemGUIDVec[_i], DECO_ICON, _entry.iconGUID);
			SetDecorationAlpha(a_context, m_listItemGUIDVec[_i], DECO_BG,
				(_entry.prefabGUID == _equipped) ? BG_ALPHA_EQUIPPED : BG_ALPHA_NORMAL);
		}

		if (m_listEntryIndexVec.size() > m_listItemGUIDVec.size())
		{
			ENGINE_WARNING("[GarageSequence] 一覧の項目が足りません(武器 %zu 件 / 項目 %zu 個)",
				m_listEntryIndexVec.size(), m_listItemGUIDVec.size());
		}
	}

	//======================================================================================
	// 飾りへ流し込む
	//======================================================================================
	void GarageSequence::SetDecorationImage(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_uiGUID, const char* a_decoName, const Core::GUID& a_texGUID)
	{
		auto* _pUI = Picker::Find<UIBase>(a_context.pObjectManager, a_uiGUID);
		if (!_pUI) return;

		Decoration::Decoration* _pDeco = _pUI->FindDecoration(a_decoName);
		if (!_pDeco) return;

		Decoration::ImageData* _pImage = _pDeco->RefImage();
		if (!_pImage) return;

		// 絵が無ければ飾りごと隠す(白い板が出ないように)
		_pDeco->isVisible = a_texGUID.IsValid();
		if (_pImage->texGUID == a_texGUID && (!a_texGUID.IsValid() || _pImage->texRef.IsValid())) return;

		_pImage->texGUID = a_texGUID;
		_pImage->texRef = (a_texGUID.IsValid() && a_context.pServices && a_context.pServices->pResourceManager)
			? a_context.pServices->pResourceManager->RequestLoad<Engine::Resource::Texture>(a_texGUID)
			: Engine::ResourceRef<Engine::Resource::Texture>();
	}

	void GarageSequence::SetDecorationText(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_uiGUID, const char* a_decoName, const std::string& a_text)
	{
		auto* _pUI = Picker::Find<UIBase>(a_context.pObjectManager, a_uiGUID);
		if (!_pUI) return;

		Decoration::Decoration* _pDeco = _pUI->FindDecoration(a_decoName);
		if (!_pDeco) return;

		if (Decoration::TextData* _pText = _pDeco->RefText()) _pText->text = a_text;
	}

	void GarageSequence::SetDecorationAlpha(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_uiGUID, const char* a_decoName, float a_alpha)
	{
		auto* _pUI = Picker::Find<UIBase>(a_context.pObjectManager, a_uiGUID);
		if (!_pUI) return;

		if (Decoration::Decoration* _pDeco = _pUI->FindDecoration(a_decoName)) _pDeco->color.a = a_alpha;
	}

	//======================================================================================
	// 出撃構成
	//======================================================================================
	Core::GUID GarageSequence::GetLoadoutWeapon(Engine::GameObject::ObjectContext& a_context, Component::EWeaponMount a_mount) const
	{
		const Game::GlobalGameContext* _pGameData = InstanceResource::GameDataResource::Find(a_context.pWorld);
		if (!_pGameData) return {};

		const Game::PlayerLoadout& _loadout = _pGameData->loadout;
		switch (a_mount)
		{
		case Component::EWeaponMount::RightHand:		return _loadout.rightHandWeaponPrefabGUID;
		case Component::EWeaponMount::LeftHand:			return _loadout.leftHandWeaponPrefabGUID;
		case Component::EWeaponMount::RightShoulder:	return _loadout.shoulderWeaponPrefabGUID;
		case Component::EWeaponMount::LeftShoulder:		return _loadout.leftShoulderWeaponPrefabGUID;
		default:										return {};
		}
	}

	void GarageSequence::SetLoadoutWeapon(Engine::GameObject::ObjectContext& a_context, Component::EWeaponMount a_mount, const Core::GUID& a_prefabGUID)
	{
		Game::GlobalGameContext* _pGameData = InstanceResource::GameDataResource::Find(a_context.pWorld);
		if (!_pGameData) return;

		Game::PlayerLoadout& _loadout = _pGameData->loadout;
		switch (a_mount)
		{
		case Component::EWeaponMount::RightHand:		_loadout.rightHandWeaponPrefabGUID = a_prefabGUID; break;
		case Component::EWeaponMount::LeftHand:			_loadout.leftHandWeaponPrefabGUID = a_prefabGUID; break;
		case Component::EWeaponMount::RightShoulder:	_loadout.shoulderWeaponPrefabGUID = a_prefabGUID; break;
		case Component::EWeaponMount::LeftShoulder:		_loadout.leftShoulderWeaponPrefabGUID = a_prefabGUID; break;
		default: break;
		}
	}

	const GarageWeaponEntry* GarageSequence::FindEntry(const Core::GUID& a_prefabGUID) const
	{
		if (!a_prefabGUID.IsValid()) return nullptr;
		for (const GarageWeaponEntry& _entry : m_catalogVec)
		{
			if (_entry.prefabGUID == a_prefabGUID) return &_entry;
		}
		return nullptr;
	}

	//======================================================================================
	// カメラ
	//--------------------------------------------------------------------------------------
	// 全体 : 読み込んだときの位置と向き
	// 寄り : 取り付け口から全体のカメラの方へ m_zoomDistance 下がった位置から見る。
	//        見る点を取り付け口から画面の左へずらすので、武器は画面の右側に映る(左は一覧の場所)
	//======================================================================================
	void GarageSequence::UpdateCamera(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld) return;
		auto& _world = *a_context.pWorld;

		const Engine::ECS::Entity _camera = _world.GetEntity(m_cameraGUID);
		if (!_world.IsAliveEntity(_camera)) return;

		auto* _pTrans = _world.RefData<Component::LocalTransformComponent>(_camera);
		if (!_pTrans) return;
		auto* _pFocus = _world.HasComponent<Component::FocusParamComponent>(_camera)
			? _world.RefData<Component::FocusParamComponent>(_camera) : nullptr;

		// 全体の構図は読み込んだときの位置と向き
		if (!m_isCameraCaptured)
		{
			m_overviewCamPos = _pTrans->pos;
			m_overviewCamQuat = _pTrans->quat;
			m_overviewFocus = _pFocus ? _pFocus->focusDistance : 10.0f;
			m_camPos = m_overviewCamPos;
			m_camQuat = m_overviewCamQuat;
			m_camFocus = m_overviewFocus;
			m_isCameraCaptured = true;
		}

		Math::Vector3 _goalPos = m_overviewCamPos;
		Math::Quaternion _goalQuat = m_overviewCamQuat;
		float _goalFocus = m_overviewFocus;

		Math::Vector3 _target = {};
		if (m_mode == EGarageMode::Zoom && TryGetMountPos(a_context, m_zoomMount, _target))
		{
			_target.y += m_zoomLift;

			// 全体のカメラの方へ下がる(全体で見えている側から見るので、体に隠れない)。
			// 肩は腕の武器の陰になりやすいので、上へ持ち上げて見下ろす
			const float _height = IsShoulderMount(m_zoomMount) ? m_shoulderZoomHeight : m_zoomHeight;
			Math::Vector3 _back = m_overviewCamPos - _target;
			_back.y = 0.0f;
			if (_back.LengthSquared() < 1e-6f) _back = { 0.0f, 0.0f, -1.0f };
			_back.Normalize();
			_back = _back * m_zoomDistance + Math::Vector3(0.0f, _height, 0.0f);
			_goalPos = _target + _back;
			_back.Normalize();

			// 画面の半分の幅ぶんの距離を出して、見る点を左へずらす
			float _tanHalfX = std::tan(DirectX::XMConvertToRadians(30.0f)) * (16.0f / 9.0f);
			if (const auto* _pParam = _world.RefData<Component::CameraParamComponent>(_camera))
			{
				_tanHalfX = std::tan(DirectX::XMConvertToRadians(_pParam->GetFovY()) * 0.5f) * _pParam->aspectRatio;
			}
			const Math::Vector3 _up = { 0.0f, 1.0f, 0.0f };
			Math::Vector3 _forward = -_back;
			Math::Vector3 _right = _up.Cross(_forward);
			if (_right.LengthSquared() < 1e-6f) _right = { 1.0f, 0.0f, 0.0f };
			_right.Normalize();

			const Math::Vector3 _lookAt = _target - _right * (m_zoomDistance * _tanHalfX * m_zoomScreenShift);
			Math::Vector3 _lookDir = _lookAt - _goalPos;
			_lookDir.Normalize();

			_goalQuat = Math::Quaternion::CreateFromRotationMatrix(Math::Matrix::CreateWorld(Math::Vector3(), _lookDir, _up));
			_goalFocus = m_zoomDistance;
		}

		const float _t = DampFactor(m_cameraRate, a_context.dt);
		m_camPos = m_camPos + (_goalPos - m_camPos) * _t;
		m_camQuat = Math::Quaternion::Slerp(m_camQuat, _goalQuat, _t);
		m_camFocus = m_camFocus + (_goalFocus - m_camFocus) * _t;

		_pTrans->pos = m_camPos;
		_pTrans->quat = m_camQuat;
		_pTrans->isDirty = true;
		if (_pFocus) _pFocus->focusDistance = m_camFocus;
	}

	bool GarageSequence::TryGetMountPos(Engine::GameObject::ObjectContext& a_context, Component::EWeaponMount a_mount, Math::Vector3& a_outPos) const
	{
		if (!a_context.pWorld) return false;
		auto& _world = *a_context.pWorld;

		const auto* _pSpawner = Picker::Find<PlayerSpawner>(a_context.pObjectManager, m_spawnerGUID);
		if (!_pSpawner) return false;

		// 武器が付いていれば武器の位置、無ければ取り付け口の位置
		Engine::ECS::Entity _entity = _pSpawner->GetWeaponEntity(a_mount);
		if (_entity == Engine::ECS::Limits::INVALID_ENTITY || !_world.IsAliveEntity(_entity))
		{
			_entity = _pSpawner->GetMountEntity(a_mount);
		}
		if (_entity == Engine::ECS::Limits::INVALID_ENTITY || !_world.IsAliveEntity(_entity)) return false;

		const auto* _pWorldMat = _world.RefData<Component::WorldMatrixComponent>(_entity);
		if (!_pWorldMat) return false;

		a_outPos = Math::Matrix(_pWorldMat->worldMat).Translation();
		return true;
	}

	bool GarageSequence::WorldToScreen(Engine::GameObject::ObjectContext& a_context, const Math::Vector3& a_world, Math::Vector2& a_outScreen) const
	{
		if (!a_context.pWorld || !a_context.pServices || !a_context.pServices->pOptionManager) return false;
		auto& _world = *a_context.pWorld;

		const Engine::ECS::Entity _camera = _world.GetEntity(m_cameraGUID);
		if (!_world.IsAliveEntity(_camera)) return false;

		const auto* _pProj = _world.RefData<Component::ProjMatComponent>(_camera);
		const auto* _pWorldMat = _world.RefData<Component::WorldMatrixComponent>(_camera);
		if (!_pProj || !_pWorldMat) return false;

		// HUD の SubmitUI が座標を解釈する基準(MissileSalvoSystem と同じ)
		const auto& _winOp = a_context.pServices->pOptionManager->GetWindowOption();
		const float _w = static_cast<float>(_winOp.windowWidth);
		const float _h = static_cast<float>(_winOp.windowHeight);

		const Math::Matrix _viewProj = Math::Matrix(_pWorldMat->worldMat).Invert() * Math::Matrix(_pProj->projMat);
		const Math::Vector4 _clip = Math::Vector4::Transform(a_world, _viewProj);
		if (_clip.w <= 1e-4f) return false;

		const float _invW = 1.0f / _clip.w;
		a_outScreen = { (_clip.x * _invW * 0.5f + 0.5f) * _w, (0.5f - _clip.y * _invW * 0.5f) * _h };
		return true;
	}

	//======================================================================================
	// 描画 : 引き出し線(全体のときだけ)
	//--------------------------------------------------------------------------------------
	// 取り付け口の位置(画面)から、箱の縁までを1本の細い板で結ぶ。根元には小さな菱形を置く
	//======================================================================================
	void GarageSequence::Draw(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_mode != EGarageMode::Overview) return;
		if (!a_context.pServices || !a_context.pServices->pMainEngine || !a_context.pServices->pResourceManager) return;

		auto* _pGE = a_context.pServices->pMainEngine->RefGraphicsEngine();
		if (!_pGE) return;

		const auto _white = GetWhiteTexture(*a_context.pServices->pResourceManager);

		for (size_t _i = 0; _i < MOUNT_COUNT; ++_i)
		{
			const auto* _pBox = Picker::Find<UIBase>(a_context.pObjectManager, m_boxGUIDs[_i]);
			if (!_pBox || !_pBox->IsVisibleInHierarchy(a_context)) continue;

			Math::Vector3 _mountPos = {};
			if (!TryGetMountPos(a_context, static_cast<Component::EWeaponMount>(_i), _mountPos)) continue;

			Math::Vector2 _point = {};
			if (!WorldToScreen(a_context, _mountPos, _point)) continue;

			const UIAnchor& _anchor = _pBox->GetAnchor();
			const Math::Vector2 _edge = ClipToRectEdge(_anchor.pixelPos, _anchor.pixelSize * 0.5f * _anchor.scale, _point);

			const Math::Vector2 _d = _edge - _point;
			const float _length = _d.Length();
			if (_length < 1.0f) continue;

			Engine::Graphics::SpriteSubmitDesc _line = {};
			_line.texHandle = _white;
			_line.pixelPos = (_edge + _point) * 0.5f;
			_line.pixelSize = { _length, m_lineWidth };
			_line.rotationDeg = DirectX::XMConvertToDegrees(std::atan2(_d.y, _d.x));
			_line.color = m_lineColor;
			_line.Zlayer = _anchor.layer - 0.5f;	// 箱の下へ
			_pGE->RefDrawSubmitter()->SubmitUI(_line);

			Engine::Graphics::SpriteSubmitDesc _dot = _line;
			_dot.pixelPos = _point;
			_dot.pixelSize = { 10.0f, 10.0f };
			_dot.rotationDeg = 45.0f;
			_pGE->RefDrawSubmitter()->SubmitUI(_dot);
		}
	}

	//======================================================================================
	// 解放 : カーソルの固定を設定値へ戻す
	//======================================================================================
	void GarageSequence::Release(Engine::GameObject::ObjectContext& a_context)
	{
		m_bgm.Release(a_context);

		if (!m_isReleaseCursorLock) return;
		if (!a_context.pServices) return;
		if (!a_context.pServices->pInputManager || !a_context.pServices->pOptionManager) return;

		const bool _isLocked = a_context.pServices->pOptionManager->GetInputOption().isCursorLockedToCenter;
		a_context.pServices->pInputManager->SetCursorCentered(_isLocked);
	}

	//======================================================================================
	// 表示名
	//======================================================================================
	const char* GarageSequence::GetMountLabel(Component::EWeaponMount a_mount)
	{
		switch (a_mount)
		{
		case Component::EWeaponMount::RightHand:		return "R-ARM";
		case Component::EWeaponMount::LeftHand:			return "L-ARM";
		case Component::EWeaponMount::RightShoulder:	return "R-BACK";
		case Component::EWeaponMount::LeftShoulder:		return "L-BACK";
		default:										return "";
		}
	}

	bool GarageSequence::IsShoulderMount(Component::EWeaponMount a_mount)
	{
		return a_mount == Component::EWeaponMount::RightShoulder || a_mount == Component::EWeaponMount::LeftShoulder;
	}

	//======================================================================================
	// シリアライズ
	//======================================================================================
	void GarageSequence::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& /*a_context*/)
	{
		a_ar.GUIDField("SpawnerGUID", m_spawnerGUID);
		a_ar.GUIDField("CameraGUID", m_cameraGUID);
		a_ar.GUIDField("HomeSceneGUID", m_homeSceneGUID);

		a_ar.GUIDField("BackButtonGUID", m_backButtonGUID);
		a_ar.GUIDField("OverviewPanelGUID", m_overviewPanelGUID);
		a_ar.GUIDField("ListPanelGUID", m_listPanelGUID);

		a_ar.GUIDField("RightHandBoxGUID", m_boxGUIDs[static_cast<size_t>(Component::EWeaponMount::RightHand)]);
		a_ar.GUIDField("LeftHandBoxGUID", m_boxGUIDs[static_cast<size_t>(Component::EWeaponMount::LeftHand)]);
		a_ar.GUIDField("RightShoulderBoxGUID", m_boxGUIDs[static_cast<size_t>(Component::EWeaponMount::RightShoulder)]);
		a_ar.GUIDField("LeftShoulderBoxGUID", m_boxGUIDs[static_cast<size_t>(Component::EWeaponMount::LeftShoulder)]);

		a_ar.GUIDField("ListTitleGUID", m_listTitleGUID);
		a_ar.GUIDVectorField("ListItemGUIDs", m_listItemGUIDVec);

		//----------------------------------------------------------------------
		// 選べる武器
		//----------------------------------------------------------------------
		size_t _count = m_catalogVec.size();
		if (a_ar.BeginArray("Catalog", _count))
		{
			m_catalogVec.resize(_count);
			for (size_t _i = 0; _i < _count; ++_i)
			{
				if (!a_ar.BeginObject(_i)) continue;
				a_ar.GUIDField("PrefabGUID", m_catalogVec[_i].prefabGUID);
				a_ar.StringField("Name", m_catalogVec[_i].name);
				a_ar.GUIDField("IconGUID", m_catalogVec[_i].iconGUID);
				a_ar.EndObject();
			}
			a_ar.EndArray();
		}

		a_ar.Field("ZoomDistance", m_zoomDistance);
		a_ar.Field("ZoomScreenShift", m_zoomScreenShift);
		a_ar.Field("ZoomLift", m_zoomLift);
		a_ar.Field("CameraRate", m_cameraRate);
		a_ar.Field("ZoomHeight", m_zoomHeight);
		a_ar.Field("ShoulderZoomHeight", m_shoulderZoomHeight);
		a_ar.Field("LineWidth", m_lineWidth);
		a_ar.Field("LineColor", m_lineColor);
		a_ar.Field("IsReleaseCursorLock", m_isReleaseCursorLock);

		m_bgm.Archive(a_ar);

		if (a_ar.IsLoading())
		{
			// 指し先が変わったので差し込み直す。開くのは全体から
			m_isBound = false;
			m_mode = EGarageMode::Overview;
			m_isBoxDirty = true;
		}
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void GarageSequence::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pObjectManager = a_context.pObjectManager;
		if (!a_context.pServices || !a_context.pWorld) return;
		auto& _world = *a_context.pWorld;

		Engine::EditorField::Header("Mode");
		Engine::EditorField::Value("Mode", "%s", m_mode == EGarageMode::Overview ? "Overview" : "Zoom");
		for (size_t _i = 0; _i < MOUNT_COUNT; ++_i)
		{
			const auto _mount = static_cast<Component::EWeaponMount>(_i);
			Engine::EditorField::IDScope _id(static_cast<int>(_i));
			if (Engine::EditorField::SmallButton(GetMountLabel(_mount))) EnterZoom(a_context, _mount);
			Engine::EditorField::SameLine();
		}
		if (Engine::EditorField::SmallButton("Overview")) EnterOverview(a_context);
		Engine::EditorField::Tooltip("構図を見るときはここで切り替える(保存はされない)");

		Engine::EditorField::Header("Scene");
		if (Picker::DrawCombo<PlayerSpawner>("Spawner", _pObjectManager, m_spawnerGUID)) m_isBound = false;
		Engine::EditorField::Tooltip("機体を出す PlayerSpawner。Controllable は切っておくこと");

		Engine::ECS::Entity _camera = _world.GetEntity(m_cameraGUID);
		if (Engine::EditorField::Field("Camera Entity", _camera))
		{
			if (_world.IsAliveEntity(_camera) && _world.HasComponent<Engine::ECS::GUIDComponent>(_camera))
			{
				m_cameraGUID = _world.RefData<Engine::ECS::GUIDComponent>(_camera)->guid;
				m_isCameraCaptured = false;
			}
		}
		Engine::EditorField::Text("%s", m_cameraGUID.String().c_str());
		Engine::EditorField::AssetField(*a_context.pServices, "Scene", "HomeScene", m_homeSceneGUID);

		Engine::EditorField::Header("UI");
		if (Picker::DrawCombo<UIButton>("Back", _pObjectManager, m_backButtonGUID)) m_isBound = false;
		if (Picker::DrawCombo<Engine::GameObject::BaseObject>("OverviewPanel", _pObjectManager, m_overviewPanelGUID)) m_isBound = false;
		if (Picker::DrawCombo<Engine::GameObject::BaseObject>("ListPanel", _pObjectManager, m_listPanelGUID)) m_isBound = false;
		for (size_t _i = 0; _i < MOUNT_COUNT; ++_i)
		{
			const std::string _label = std::string("Box ") + GetMountLabel(static_cast<Component::EWeaponMount>(_i));
			if (Picker::DrawCombo<UIButton>(_label.c_str(), _pObjectManager, m_boxGUIDs[_i])) m_isBound = false;
		}
		Picker::DrawCombo<UIBase>("ListTitle", _pObjectManager, m_listTitleGUID);
		Engine::EditorField::Value("List Items", "%zu", m_listItemGUIDVec.size());
		Engine::EditorField::HelpText("飾りの名前 : Icon(画像) / Name(武器名) / Slot(箱の取り付け口) / Title(一覧の見出し) / Bg(下地)");

		Engine::EditorField::Header("Catalog");
		int _removeIndex = -1;
		for (size_t _i = 0; _i < m_catalogVec.size(); ++_i)
		{
			Engine::EditorField::IDScope _id(static_cast<int>(_i));
			GarageWeaponEntry& _entry = m_catalogVec[_i];
			Engine::EditorField::Field("Name", _entry.name);
			Engine::EditorField::AssetField(*a_context.pServices, "Prefab", "Prefab", _entry.prefabGUID);
			Engine::EditorField::AssetField(*a_context.pServices, "Icon", "Texture", _entry.iconGUID);
			Engine::EditorField::Value("Kind", "%s", _entry.isShoulder ? "Shoulder" : (_entry.isHand ? "Hand" : "?"));
			if (Engine::EditorField::DeleteSmallButton("Remove")) _removeIndex = static_cast<int>(_i);
			Engine::EditorField::Line();
		}
		if (_removeIndex >= 0) m_catalogVec.erase(m_catalogVec.begin() + _removeIndex);
		if (Engine::EditorField::CreateButton("Add Weapon")) m_catalogVec.push_back({});
		Engine::EditorField::HelpText("種類(手 / 肩)は次に開いたときにプレハブから引き直す");

		Engine::EditorField::Header("Camera");
		Engine::EditorField::Field("ZoomDistance (m)", m_zoomDistance, 0.1f, 0.5f, 100.0f);
		Engine::EditorField::Field("ZoomScreenShift", m_zoomScreenShift, 0.01f, -1.0f, 1.0f);
		Engine::EditorField::Tooltip("寄ったときに武器を右へずらす量(画面の半分の幅に対する割合)");
		Engine::EditorField::Field("ZoomLift (m)", m_zoomLift, 0.05f, -10.0f, 10.0f);
		Engine::EditorField::Field("ZoomHeight (m)", m_zoomHeight, 0.05f, -10.0f, 10.0f);
		Engine::EditorField::Field("ShoulderZoomHeight (m)", m_shoulderZoomHeight, 0.05f, -10.0f, 10.0f);
		Engine::EditorField::Tooltip("寄ったときにカメラを見る位置より上げる量。肩は腕の武器に隠れないよう上から見る");
		Engine::EditorField::Field("CameraRate", m_cameraRate, 0.1f, 0.0f, 60.0f);
		if (Engine::EditorField::Button("Recapture Overview")) m_isCameraCaptured = false;
		Engine::EditorField::Tooltip("今のカメラの位置と向きを、全体の構図として覚え直す");

		Engine::EditorField::Header("Line");
		Engine::EditorField::Field("LineWidth", m_lineWidth, 0.1f, 0.5f, 20.0f);
		Engine::EditorField::Field("LineColor", m_lineColor);

		Engine::EditorField::Header("Behavior");
		Engine::EditorField::Field("ReleaseCursorLock", m_isReleaseCursorLock);
		m_bgm.DrawInspector(a_context);
	}
}
