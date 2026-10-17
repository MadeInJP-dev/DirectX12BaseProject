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
#include "Application/Components/Weapon/Shoulder/MissileFireComponent.h"
#include "Application/Components/Weapon/WeaponConfig/AutoFireComponent.h"
#include "Application/Components/Weapon/WeaponConfig/BurstFireComponent.h"
#include "Application/Components/Weapon/WeaponConfig/SemiAutoFireComponent.h"
#include "Application/Components/Weapon/WeaponConfig/WeaponOverHeatComponent.h"
#include "Application/Components/Weapon/WeaponConfig/WeaponReloadComponent.h"
#include "Application/Components/Weapon/Projectile/ProjectileComponent.h"
#include "Application/Components/Animation/StancePoseComponent.h"
#include "Application/Components/Movement/LookAngleComponent.h"
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

		// 性能の板の飾り(項目ごとのものは後ろに番号を付ける)
		constexpr const char* DECO_STAT_NAME = "StatName";		// 武器の名前
		constexpr const char* DECO_STAT_MODE = "StatMode";		// 撃ち方
		constexpr const char* DECO_STAT_LABEL = "StatLabel";	// 項目名
		constexpr const char* DECO_STAT_VALUE = "StatValue";	// 値
		constexpr const char* DECO_STAT_BAR_BG = "StatBarBg";	// 棒の枠(この幅が満タン)
		constexpr const char* DECO_STAT_BAR = "StatBar";		// 棒
		constexpr const char* DECO_STAT_EQUIP = "StatEquip";	// 今付いている武器の棒

		constexpr float STAT_BAR_MIN_RATIO = 0.06f;	// 一番低い値でも棒が見えるように
		constexpr float STAT_NO_LIMIT = -1.0f;		// 上限の無い値(熱も弾倉も無い)。棒は満タン

		// 項目の飾りの名前
		std::string StatDecoName(const char* a_base, size_t a_row)
		{
			return std::string(a_base) + std::to_string(a_row);
		}

		// 数値を文字にする
		std::string FormatStat(const char* a_format, float a_value)
		{
			char _buf[64] = {};
			std::snprintf(_buf, sizeof(_buf), a_format, a_value);
			return _buf;
		}

		// プレハブのルートが持つコンポーネントの中身。持っていなければ nullptr
		template<typename T>
		const T* FindPrefabData(Engine::ECS::World& a_world, const Engine::Resource::Prefab& a_prefab)
		{
			const Engine::ECS::ComponentTypeID _typeID = a_world.GetCompTypeID<T>();
			if (_typeID == Engine::ECS::Limits::INVALID_COMPONENTTYPEID) return nullptr;

			const auto& _dataMap = a_prefab.GetDataMap();
			const auto _it = _dataMap.find(_typeID);
			if (_it == _dataMap.end() || _it->second.size() < sizeof(T)) return nullptr;
			return reinterpret_cast<const T*>(_it->second.data());
		}

		//----------------------------------------------------------------------------------
		// 構えの既定 : 仁王立ちで、両手の銃を体の前へ構える(LightTacticalMech に合わせた値)
		//----------------------------------------------------------------------------------
		std::vector<GarageStanceBone> MakeDefaultStance()
		{
			// モデル空間は X = 機体の右、Y = 上、Z = 前。
			// 腕は元のクリップ(DefaultState)が前腕を正面へ出した構えなので、脚だけを足す
			return {
				// 腿を外へ開いて浮いた分だけ腰を落とす(足裏が地面から離れないように)
				{ "pelvis",		{ 0.0f, 0.0f, 0.0f },	{ 0.0f, -0.06f, 0.0f } },
				// 脚を開いて踏ん張る(腿を外へ、足首で戻して足裏を地面と平らにする)
				{ "thigh_l",	{ 0.0f, 0.0f, -12.0f },	{} },
				{ "thigh_r",	{ 0.0f, 0.0f, 12.0f },	{} },
				{ "foot_l",		{ 0.0f, 0.0f, 12.0f },	{} },
				{ "foot_r",		{ 0.0f, 0.0f, -12.0f },	{} },
			};
		}

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

			_entry.stats = BuildStats(a_context, _entry);
		}
		RebuildStatMax();

		m_isBoxDirty = true;
		m_statEntryIndex = SIZE_MAX;
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
		UpdateStance(a_context);
		RefreshStats(a_context);

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
		m_statEntryIndex = SIZE_MAX;	// 性能の板を出し直す
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
	// 性能の板に、カーソルが乗っている武器(無ければ今付いている武器)を出す
	//--------------------------------------------------------------------------------------
	// 棒は一覧の中で一番よい値を満タンにした長さ。今付いている武器の長さも薄く重ねて、
	// 乗せた武器と見比べられるようにする。出している武器が変わったときだけ書き直す
	//======================================================================================
	void GarageSequence::RefreshStats(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_mode != EGarageMode::Zoom) return;
		if (!m_statPanelGUID.IsValid()) return;

		// カーソルが乗っている一覧の項目
		size_t _entryIndex = SIZE_MAX;
		for (size_t _i = 0; _i < m_listItemGUIDVec.size() && _i < m_listEntryIndexVec.size(); ++_i)
		{
			const auto* _pItem = Picker::Find<UIBase>(a_context.pObjectManager, m_listItemGUIDVec[_i]);
			if (_pItem && _pItem->IsHovered())
			{
				_entryIndex = m_listEntryIndexVec[_i];
				break;
			}
		}

		// 今付いている武器
		const Core::GUID _equipped = GetLoadoutWeapon(a_context, m_zoomMount);
		size_t _equippedIndex = SIZE_MAX;
		for (size_t _i = 0; _i < m_catalogVec.size(); ++_i)
		{
			if (_equipped.IsValid() && m_catalogVec[_i].prefabGUID == _equipped) { _equippedIndex = _i; break; }
		}
		if (_entryIndex == SIZE_MAX) _entryIndex = _equippedIndex;

		if (_entryIndex == m_statEntryIndex && _equipped == m_statEquipped) return;
		m_statEntryIndex = _entryIndex;
		m_statEquipped = _equipped;

		const GarageWeaponEntry* _pEntry = (_entryIndex < m_catalogVec.size()) ? &m_catalogVec[_entryIndex] : nullptr;
		const GarageWeaponEntry* _pEquipped = (_equippedIndex < m_catalogVec.size()) ? &m_catalogVec[_equippedIndex] : nullptr;
		const bool _isShoulder = IsShoulderMount(m_zoomMount);

		const bool _isValid = _pEntry && _pEntry->stats.isValid;
		const bool _hasEquipped = _pEquipped && _pEquipped->stats.isValid;
		SetDecorationText(a_context, m_statPanelGUID, DECO_STAT_NAME, _pEntry ? _pEntry->name : "EMPTY");
		SetDecorationText(a_context, m_statPanelGUID, DECO_STAT_MODE, _isValid ? _pEntry->stats.mode : "");

		for (size_t _row = 0; _row < GarageWeaponStats::ROW_COUNT; ++_row)
		{
			const std::string _bgName = StatDecoName(DECO_STAT_BAR_BG, _row);
			SetDecorationText(a_context, m_statPanelGUID, StatDecoName(DECO_STAT_LABEL, _row).c_str(), GetStatLabel(_isShoulder, _row));
			SetDecorationText(a_context, m_statPanelGUID, StatDecoName(DECO_STAT_VALUE, _row).c_str(), _isValid ? _pEntry->stats.textArray[_row] : "---");
			SetDecorationBar(a_context, m_statPanelGUID, StatDecoName(DECO_STAT_BAR, _row), _bgName,
				_isValid ? CalcStatRatio(_isShoulder, _row, _pEntry->stats.valueArray[_row]) : 0.0f);

			// 今付いている武器の長さ(乗せた武器が付いている武器なら重なる)
			SetDecorationBar(a_context, m_statPanelGUID, StatDecoName(DECO_STAT_EQUIP, _row), _bgName,
				_hasEquipped ? CalcStatRatio(_isShoulder, _row, _pEquipped->stats.valueArray[_row]) : 0.0f);
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

	void GarageSequence::SetDecorationBar(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_uiGUID, const std::string& a_barName, const std::string& a_bgName, float a_ratio)
	{
		auto* _pUI = Picker::Find<UIBase>(a_context.pObjectManager, a_uiGUID);
		if (!_pUI) return;

		Decoration::Decoration* _pBar = _pUI->FindDecoration(a_barName);
		const Decoration::Decoration* _pBg = _pUI->FindDecoration(a_bgName);
		if (!_pBar || !_pBg) return;

		// 枠の左端から、割合の長さだけ伸ばす(どちらのピボットでも左端が揃うように置く)
		const float _width = _pBg->pixelSize.x * std::clamp(a_ratio, 0.0f, 1.0f);
		const float _left = _pBg->offsetPos.x - _pBg->pixelSize.x * _pBg->pivot.x;
		_pBar->pixelSize.x = _width;
		_pBar->offsetPos.x = _left + _width * _pBar->pivot.x;
		_pBar->isVisible = (_width > 0.5f);
	}

	//======================================================================================
	// 性能 : プレハブの中身から引く
	//--------------------------------------------------------------------------------------
	// 手 : 攻撃力 / 毎秒の発射数 / 毎秒のダメージ / 弾速 / 熱(弾倉)が尽きるまでの発射数
	// 肩 : 1発の攻撃力 / 一斉射の数 / 一斉射のダメージ / 弾速 / 次の一斉射までの時間
	//======================================================================================
	GarageWeaponStats GarageSequence::BuildStats(Engine::GameObject::ObjectContext& a_context, const GarageWeaponEntry& a_entry)
	{
		GarageWeaponStats _stats = {};
		if (!a_context.pWorld || !a_context.pServices || !a_context.pServices->pResourceManager) return _stats;

		auto& _world = *a_context.pWorld;
		auto& _rm = *a_context.pServices->pResourceManager;
		const auto* _pPrefab = _rm.Get(a_entry.prefabRef);
		if (!_pPrefab) return _stats;

		if (a_entry.isShoulder && !a_entry.isHand)
		{
			//----------------------------------------------------------------------
			// 肩(ミサイル)
			//----------------------------------------------------------------------
			const auto* _pState = FindPrefabData<Component::MissileStateComponent>(_world, *_pPrefab);
			const auto* _pFire = FindPrefabData<Component::MissileFireComponent>(_world, *_pPrefab);
			if (!_pState || !_pFire) return _stats;

			const float _attack = _pState->missileAttack;
			const float _count = static_cast<float>(_pFire->GetActiveBurstNum());
			const float _cycle = (_pFire->isBurst ? _pFire->burstTime * (std::max)(_count - 1.0f, 0.0f) : 0.0f) + _pFire->waitTime;

			_stats.mode = "HOMING MISSILE";
			_stats.valueArray = { _attack, _count, _attack * _count, _pState->missileSpeed, _cycle };
			_stats.textArray = {
				FormatStat("%.0f", _attack),
				FormatStat("%.0f", _count),
				FormatStat("%.0f", _attack * _count),
				FormatStat("%.0f MPS", _pState->missileSpeed),
				FormatStat("%.2f SEC", _cycle),
			};
			_stats.isValid = true;
			return _stats;
		}

		//--------------------------------------------------------------------------
		// 手
		//--------------------------------------------------------------------------
		const auto* _pState = FindPrefabData<Component::WeaponStateComponent>(_world, *_pPrefab);
		if (!_pState) return _stats;

		// 攻撃力 : 0 以下なら弾のプレハブの値(WeaponProjectileSpawnSystem と同じ)
		float _attack = _pState->bulletAttack;
		if (!(_attack > 0.0f) && _pState->bulletPrefabGUID.IsValid())
		{
			const auto _bulletRef = _rm.LoadImmediate<Engine::Resource::Prefab>(_pState->bulletPrefabGUID);
			if (const auto* _pBullet = _rm.Get(_bulletRef))
			{
				if (const auto* _pProjectile = FindPrefabData<Component::ProjectileComponent>(_world, *_pBullet))
				{
					_attack = _pProjectile->damage;
				}
			}
		}

		// 毎秒の発射数と撃ち方
		float _rate = 0.0f;
		if (const auto* _pAuto = FindPrefabData<Component::AutoFireComponent>(_world, *_pPrefab))
		{
			_rate = _pAuto->fireRate;
			_stats.mode = "FULL AUTO";
		}
		else if (const auto* _pBurst = FindPrefabData<Component::BurstFireComponent>(_world, *_pPrefab))
		{
			// 1バースト = 間隔 (発数 - 1) 回 + 撃ち切ってからの待ち
			const float _num = static_cast<float>(_pBurst->burstNum);
			const float _cycle = _pBurst->burstTime * (std::max)(_num - 1.0f, 0.0f) + _pBurst->waitTime;
			_rate = (_cycle > 0.0f) ? _num / _cycle : 0.0f;
			_stats.mode = std::to_string(_pBurst->burstNum) + "-ROUND BURST";
		}
		else if (const auto* _pSemi = FindPrefabData<Component::SemiAutoFireComponent>(_world, *_pPrefab))
		{
			_rate = (_pSemi->waitTime > 0.0f) ? 1.0f / _pSemi->waitTime : 0.0f;
			_stats.mode = "SEMI AUTO";
		}

		// 熱(弾倉)が尽きるまでの発射数。どちらも無ければ上限なし
		float _sustain = STAT_NO_LIMIT;
		std::string _sustainText = "NO LIMIT";
		if (const auto* _pHeat = FindPrefabData<Component::WeaponOverHeatComponent>(_world, *_pPrefab))
		{
			if (_pHeat->perShot > 0.0f)
			{
				_sustain = std::ceil(_pHeat->limit / _pHeat->perShot);
				_sustainText = FormatStat("%.0f RDS", _sustain);
			}
		}
		else if (const auto* _pReload = FindPrefabData<Component::WeaponReloadComponent>(_world, *_pPrefab))
		{
			_sustain = static_cast<float>(_pReload->magazineMaxCapacity);
			_sustainText = FormatStat("%.0f RDS", _sustain);
		}

		_stats.valueArray = { _attack, _rate, _attack * _rate, _pState->bulletSpeed, _sustain };
		_stats.textArray = {
			FormatStat("%.0f", _attack),
			FormatStat("%.1f RPS", _rate),
			FormatStat("%.0f", _attack * _rate),
			FormatStat("%.0f MPS", _pState->bulletSpeed),
			_sustainText,
		};
		_stats.isValid = true;
		return _stats;
	}

	void GarageSequence::RebuildStatMax()
	{
		for (size_t _kind = 0; _kind < 2; ++_kind)
		{
			m_statMaxArray[_kind].fill(0.0f);
			m_statMinArray[_kind].fill(FLT_MAX);
		}

		for (const GarageWeaponEntry& _entry : m_catalogVec)
		{
			if (!_entry.stats.isValid) continue;
			const size_t _kind = (_entry.isShoulder && !_entry.isHand) ? 1 : 0;
			for (size_t _row = 0; _row < GarageWeaponStats::ROW_COUNT; ++_row)
			{
				const float _value = _entry.stats.valueArray[_row];
				if (_value < 0.0f) continue;	// 上限なし
				m_statMaxArray[_kind][_row] = (std::max)(m_statMaxArray[_kind][_row], _value);
				m_statMinArray[_kind][_row] = (std::min)(m_statMinArray[_kind][_row], _value);
			}
		}
	}

	const char* GarageSequence::GetStatLabel(bool a_isShoulder, size_t a_row)
	{
		static constexpr const char* HAND_LABELS[GarageWeaponStats::ROW_COUNT] = { "ATTACK", "FIRE RATE", "DPS", "BULLET SPEED", "SUSTAIN" };
		static constexpr const char* SHOULDER_LABELS[GarageWeaponStats::ROW_COUNT] = { "ATTACK", "MISSILES", "SALVO DAMAGE", "MISSILE SPEED", "RELOAD" };
		if (a_row >= GarageWeaponStats::ROW_COUNT) return "";
		return a_isShoulder ? SHOULDER_LABELS[a_row] : HAND_LABELS[a_row];
	}

	bool GarageSequence::IsLowerBetterStat(bool a_isShoulder, size_t a_row)
	{
		return a_isShoulder && a_row == 4;	// 肩の再装填
	}

	float GarageSequence::CalcStatRatio(bool a_isShoulder, size_t a_row, float a_value) const
	{
		if (a_row >= GarageWeaponStats::ROW_COUNT) return 0.0f;
		if (a_value < 0.0f) return 1.0f;	// 上限なし

		const size_t _kind = a_isShoulder ? 1 : 0;
		float _ratio = 0.0f;
		if (IsLowerBetterStat(a_isShoulder, a_row))
		{
			// 小さいほどよい : 一番小さい値を満タンにする
			const float _min = m_statMinArray[_kind][a_row];
			_ratio = (a_value > 0.0f && _min < FLT_MAX) ? _min / a_value : 1.0f;
		}
		else
		{
			const float _max = m_statMaxArray[_kind][a_row];
			_ratio = (_max > 0.0f) ? a_value / _max : 0.0f;
		}
		return std::clamp(_ratio, STAT_BAR_MIN_RATIO, 1.0f);
	}

	//======================================================================================
	// 構え : 出した機体に StancePoseComponent を付け、中身を書き直す
	//--------------------------------------------------------------------------------------
	// 付けるのは予約なので、付くのは次のフレームの頭。付いたら毎フレーム中身を書き直す
	// (インスペクターで角度をいじると、その場で見た目に出る)。
	// 機体は TransientTag 付きなので、付けたものがシーンへ保存されることはない
	//======================================================================================
	void GarageSequence::UpdateStance(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld) return;
		auto& _world = *a_context.pWorld;

		const auto* _pSpawner = Picker::Find<PlayerSpawner>(a_context.pObjectManager, m_spawnerGUID);
		if (!_pSpawner) return;

		const Engine::ECS::Entity _player = _pSpawner->GetPlayerEntity();
		if (_player == Engine::ECS::Limits::INVALID_ENTITY || !_world.IsAliveEntity(_player)) return;

		// 視線を機体の正面へ合わせる。
		// 入力で動かさない機体は視線(LookAngle)が 0 のまま残り、機体の向きとの差の分だけ
		// 加算ポーズ(AdditivePoseSystem)が上半身をねじってしまうため
		if (_world.HasComponent<Component::LookAngleComponent>(_player) && _world.HasComponent<Component::LocalTransformComponent>(_player))
		{
			const auto* _pTrans = _world.RefData<Component::LocalTransformComponent>(_player);
			auto* _pLook = _world.RefData<Component::LookAngleComponent>(_player);
			const Math::Vector3 _forward = _pTrans ? Math::Matrix::CreateFromQuaternion(_pTrans->quat).Forward() : Math::Vector3();
			if (_pLook && (_forward.x * _forward.x + _forward.z * _forward.z) > 1e-6f)
			{
				_pLook->Yaw = DirectX::XMConvertToDegrees(std::atan2(_forward.x, _forward.z));
				_pLook->Pitch = 0.0f;
			}
		}

		if (!_world.HasComponent<Component::StancePoseComponent>(_player))
		{
			if (!m_isStanceEnabled || m_stanceBoneVec.empty()) return;
			if (m_stanceRequestedEntity == _player) return;

			_world.ReserveAddComponent(_world.GetCompTypeID<Component::StancePoseComponent>(), _player);
			m_stanceRequestedEntity = _player;
			return;
		}

		auto* _pStance = _world.RefData<Component::StancePoseComponent>(_player);
		if (!_pStance) return;

		_pStance->weight = m_isStanceEnabled ? 1.0f : 0.0f;
		_pStance->boneCount = static_cast<uint32_t>((std::min)(m_stanceBoneVec.size(), static_cast<size_t>(Component::StancePoseComponent::BONE_MAX)));
		for (uint32_t _i = 0; _i < _pStance->boneCount; ++_i)
		{
			_pStance->boneArray[_i].nodeHash = Core::String::ToHash(m_stanceBoneVec[_i].nodeName);
			_pStance->boneArray[_i].rotationDeg = m_stanceBoneVec[_i].rotationDeg;
			_pStance->boneArray[_i].offset = m_stanceBoneVec[_i].offset;
		}
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

		//----------------------------------------------------------------------
		// 性能の板・構え(後から足したもの。バイナリは順番読みなので末尾へ足す)
		//----------------------------------------------------------------------
		a_ar.GUIDField("StatPanelGUID", m_statPanelGUID);
		a_ar.Field("IsStanceEnabled", m_isStanceEnabled);

		size_t _stanceCount = m_stanceBoneVec.size();
		if (a_ar.BeginArray("Stance", _stanceCount))
		{
			m_stanceBoneVec.resize(_stanceCount);
			for (size_t _i = 0; _i < _stanceCount; ++_i)
			{
				if (!a_ar.BeginObject(_i)) continue;
				a_ar.StringField("Node", m_stanceBoneVec[_i].nodeName);
				a_ar.Field("RotationDeg", m_stanceBoneVec[_i].rotationDeg);
				a_ar.Field("Offset", m_stanceBoneVec[_i].offset);
				a_ar.EndObject();
			}
			a_ar.EndArray();
		}
		else if (a_ar.IsLoading())
		{
			// 構えを持たない古いシーンは既定の構えにする
			m_stanceBoneVec = MakeDefaultStance();
		}

		if (a_ar.IsLoading())
		{
			// 指し先が変わったので差し込み直す。開くのは全体から
			m_isBound = false;
			m_mode = EGarageMode::Overview;
			m_isBoxDirty = true;
			m_statEntryIndex = SIZE_MAX;
			m_stanceRequestedEntity = Engine::ECS::Limits::INVALID_ENTITY;
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
		if (Picker::DrawCombo<UIBase>("StatPanel", _pObjectManager, m_statPanelGUID)) m_statEntryIndex = SIZE_MAX;
		Engine::EditorField::HelpText("性能の板の飾り : StatName / StatMode / StatLabel0〜4 / StatValue0〜4 / StatBarBg0〜4(満タンの幅)/ StatBar0〜4 / StatEquip0〜4");

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

		Engine::EditorField::Header("Stance");
		Engine::EditorField::Field("Enabled", m_isStanceEnabled);
		Engine::EditorField::Tooltip("機体に構え(仁王立ちで銃を構える)を足すか");
		int _removeStance = -1;
		for (size_t _i = 0; _i < m_stanceBoneVec.size(); ++_i)
		{
			Engine::EditorField::IDScope _id(static_cast<int>(1000 + _i));
			GarageStanceBone& _bone = m_stanceBoneVec[_i];
			Engine::EditorField::Field("Node", _bone.nodeName);
			Engine::EditorField::Field("Rotation (deg)", _bone.rotationDeg, 0.5f, -180.0f, 180.0f);
			Engine::EditorField::Field("Offset (m)", _bone.offset, 0.005f);
			if (Engine::EditorField::DeleteSmallButton("Remove")) _removeStance = static_cast<int>(_i);
		}
		if (_removeStance >= 0) m_stanceBoneVec.erase(m_stanceBoneVec.begin() + _removeStance);
		if (m_stanceBoneVec.size() < Component::StancePoseComponent::BONE_MAX && Engine::EditorField::CreateButton("Add Bone")) m_stanceBoneVec.push_back({});
		Engine::EditorField::SameLine();
		if (Engine::EditorField::Button("Reset Default")) m_stanceBoneVec = MakeDefaultStance();
		Engine::EditorField::HelpText("回転はモデル空間(X → Y → Z)。上から順に掛ける(親を先に)");

		Engine::EditorField::Header("Line");
		Engine::EditorField::Field("LineWidth", m_lineWidth, 0.1f, 0.5f, 20.0f);
		Engine::EditorField::Field("LineColor", m_lineColor);

		Engine::EditorField::Header("Behavior");
		Engine::EditorField::Field("ReleaseCursorLock", m_isReleaseCursorLock);
		m_bgm.DrawInspector(a_context);
	}
}
