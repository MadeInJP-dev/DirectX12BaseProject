#pragma once

#include "Engine/ECS/World/World.h"
#include "Engine/EditorField/EditorField.h"
#include "Engine/ECS/Component/GUIDComponent.h"
#include "Application/Components/Core/NameComponent.h"

namespace App::Component
{
	// アタッチメント1スロット分。
	// GUID を保存し、Awake で AttachmentSlotLinkSystem が id を解決する。
	// GUID が空で id だけ入っているものは、プレハブの即時生成が付けたもの
	// (相手が GUIDComponent を持たない。プレイヤーの武器など)。
	struct AttachmentSlot
	{
		Core::GUID		 guid = {};												// シリアライズ用
		Engine::ECS::Entity	 id   = Engine::ECS::Limits::INVALID_ENTITY;		// ランタイム用(GUIDから解決)
	};

	struct AttachmentSlotsComponent
	{
		// 肩のブースター
		AttachmentSlot rightShoulderBoost;
		AttachmentSlot leftShoulderBoost;

		// 足のブースター
		AttachmentSlot rightLegBoost;
		AttachmentSlot leftLegBoost;

		// 武器 : 左手・右手。プレイヤーは左クリックで左、右クリックで右を撃つ。
		// スロットが指す先は「引き金の受け口(WeaponTriggerComponent)を持つ武器エンティティ」で、
		// 撃てるかどうかや弾の設定は武器側(Components/Weapon の Hand / WeaponConfig)が持つ
		AttachmentSlot leftWeapon;
		AttachmentSlot rightWeapon;

		// 肩の武器(ミサイル) : 溜めて一斉射なので、引き金を配信するのではなく
		// MissileSalvoSystem がこのスロットの武器の MissileFireComponent へ要求を出す
		AttachmentSlot missile;
	};

	//==========================================================================================
	// 全スロットを順に渡す
	// スロットを足したときに、解決する側(リンク・即時生成)の書き漏れが出ないようにまとめてある
	//==========================================================================================
	template<typename Func>
	inline void ForEachAttachmentSlot(AttachmentSlotsComponent& a_slots, Func&& a_func)
	{
		a_func(a_slots.rightShoulderBoost);
		a_func(a_slots.leftShoulderBoost);
		a_func(a_slots.rightLegBoost);
		a_func(a_slots.leftLegBoost);
		a_func(a_slots.leftWeapon);
		a_func(a_slots.rightWeapon);
		a_func(a_slots.missile);
	}
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::AttachmentSlotsComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::AttachmentSlotsComponent& _comp = Engine::EditorField::RefValue<App::Component::AttachmentSlotsComponent>(a_pData);

		// 各スロットは GUID のみ保存する(id はランタイムで解決)
		a_ar.Field("rightShoulderBoostGUID", _comp.rightShoulderBoost.guid);
		a_ar.Field("leftShoulderBoostGUID",  _comp.leftShoulderBoost.guid);
		a_ar.Field("rightLegBoostGUID",      _comp.rightLegBoost.guid);
		a_ar.Field("leftLegBoostGUID",       _comp.leftLegBoost.guid);
		a_ar.Field("leftWeaponGUID",         _comp.leftWeapon.guid);
		a_ar.Field("rightWeaponGUID",        _comp.rightWeapon.guid);
		a_ar.Field("missileGUID",            _comp.missile.guid);
	}

	static void Edit(CompEditContext& a_context)
	{
		using namespace Engine;
		App::Component::AttachmentSlotsComponent& _comp = Engine::EditorField::RefValue<App::Component::AttachmentSlotsComponent>(a_context.pData);

		auto* _pWorld = a_context.pWorld;
		if (!_pWorld)
		{
			Engine::EditorField::WarningText("World is null");
			return;
		}

		// エンティティのラベル(名前があれば名前、無ければGUID)を返す
		auto _entityLabel = [&](Engine::ECS::Entity a_e, const Core::GUID& a_guid) -> std::string
		{
			if (a_e != Engine::ECS::Limits::INVALID_ENTITY &&
				_pWorld->HasComponent<App::Component::NameComponent>(a_e))
			{
				if (auto* _pName = _pWorld->RefData<App::Component::NameComponent>(a_e))
				{
					return _pName->name;
				}
			}
			return a_guid.String();
		};

		// 候補に出すエンティティ(GUIDを持つもの)を先に集める。
		// 名前は重複し得る(武器を複製すれば同名が並ぶ)ので、
		// 同名のものにだけGUIDの頭を添えて選び分けられるようにする
		struct SlotCandidate
		{
			Engine::ECS::Entity entity = Engine::ECS::Limits::INVALID_ENTITY;
			Core::GUID        guid   = {};
			std::string         name   = {};
		};

		std::vector<SlotCandidate> _candidateVec;
		for (const auto& _loc : _pWorld->GetEntityList())
		{
			Engine::ECS::Entity _e = _pWorld->GetEntity(_loc);
			if (_e == Engine::ECS::Limits::INVALID_ENTITY) continue;
			if (!_pWorld->HasComponent<Engine::ECS::GUIDComponent>(_e)) continue;

			auto* _pGuid = _pWorld->RefData<Engine::ECS::GUIDComponent>(_e);
			if (!_pGuid) continue;

			_candidateVec.push_back({ _e, _pGuid->guid, _entityLabel(_e, _pGuid->guid) });
		}

		const auto _duplicatedSet = Engine::EditorField::CollectDuplicatedNames(
			_candidateVec, [](const SlotCandidate& a_candidate) { return a_candidate.name; });

		// 同名を見分けるための手掛かり。GUIDは長いので頭だけ出す
		auto _guidHint = [](const Core::GUID& a_guid) -> std::string
		{
			return a_guid.String().substr(0, 8);
		};

		// 1スロット分の選択コンボを描画
		auto _drawSlot = [&](const char* a_label, App::Component::AttachmentSlot& a_slot)
		{
			// 現在の選択表示。同名の候補があるならGUIDの頭まで出す。
			// GUID を持たない相手(スポナーが出したプレイヤーの武器など)は id だけ入っている
			std::string _current = "None";
			if (a_slot.guid != Core::DEFAULT_GUID || a_slot.id != Engine::ECS::Limits::INVALID_ENTITY)
			{
				_current = Engine::EditorField::MakeUniqueLabel(
					_duplicatedSet, _entityLabel(a_slot.id, a_slot.guid), _guidHint(a_slot.guid));
			}

			if (Engine::EditorField::ComboScope _combo{ a_label, _current.c_str() })
			{
				// クリア用
				if (Engine::EditorField::Selectable("None", a_slot.guid == Core::DEFAULT_GUID))
				{
					a_slot.guid = {};
					a_slot.id = Engine::ECS::Limits::INVALID_ENTITY;
				}

				// GUIDComponent を持つ全エンティティが候補
				for (const SlotCandidate& _candidate : _candidateVec)
				{
					// 名前が同じでもImGuiのIDがぶつからないようにエンティティIDでPushID
					Engine::EditorField::IDScope _id(static_cast<int>(_candidate.entity));

					bool _selected = (a_slot.guid == _candidate.guid);

					const std::string _label = Engine::EditorField::MakeUniqueLabel(
						_duplicatedSet, _candidate.name, _guidHint(_candidate.guid));

					if (Engine::EditorField::Selectable(_label.c_str(), _selected))
					{
						a_slot.guid = _candidate.guid;
						a_slot.id = _candidate.entity;
					}
					if (_selected) Engine::EditorField::SetItemDefaultFocus();
				}
			}

			// 参考: 解決済みのランタイムID
			Engine::EditorField::SameLine();
			Engine::EditorField::HelpText("id:%llu", static_cast<unsigned long long>(a_slot.id));
		};

		Engine::EditorField::Header("Boosters");
		_drawSlot("R Shoulder", _comp.rightShoulderBoost);
		_drawSlot("L Shoulder", _comp.leftShoulderBoost);
		_drawSlot("R Leg",      _comp.rightLegBoost);
		_drawSlot("L Leg",      _comp.leftLegBoost);

		Engine::EditorField::Header("Weapons");
		_drawSlot("Left Weapon",  _comp.leftWeapon);
		_drawSlot("Right Weapon", _comp.rightWeapon);
		_drawSlot("Missile",      _comp.missile);
	}
};
