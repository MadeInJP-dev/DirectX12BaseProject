#include "ObjectMetaRegistry.h"

namespace Engine::GameObject
{
	void ObjectMetaRegistry::MigrateName(const std::string& a_oldName, const std::string& a_currentName, MigrateFunc a_onCreate)
	{
		// 現行名が登録されていなければ何もしない(登録前に呼ばれた場合)
		const ObjectTypeID _currentID = GetTypeID(a_currentName);
		if (_currentID == INVALID_OBJECT_TYPE_ID)
		{
			ENGINE_WARNING("[ObjectMetaRegistry] 旧名の引き継ぎ先が未登録です : %s", a_currentName.c_str());
			return;
		}

		const ObjectTypeID _oldID = MakeObjectTypeID(a_oldName);
		if (_oldID == _currentID) return;	// 名前が実質変わっていない

		// 旧IDが別クラスとして生きている場合は、潰すと事故になるので触らない
		if (auto _it = m_metaMap.find(_oldID); _it != m_metaMap.end())
		{
			ENGINE_WARNING("[ObjectMetaRegistry] 旧名が別クラスとして登録済みです : %s -> %s", a_oldName.c_str(), _it->second.name.c_str());
			return;
		}

		// 旧IDから現行IDへ流す。
		// m_metaMap には入れない(AddObject の一覧に同じクラスが二重に並ぶため)
		m_nameMap.emplace(a_oldName, _currentID);
		m_aliasMap.emplace(_oldID, _currentID);
		if (a_onCreate) m_aliasCreateMap.emplace(_oldID, std::move(a_onCreate));
	}

	std::unique_ptr<BaseObject> ObjectMetaRegistry::CreateFromSaved(uint32_t a_savedValue) const
	{
		auto _upObject = Create(ResolveTypeID(a_savedValue));
		if (!_upObject) return nullptr;

		// 旧名で保存されていたもの : 元のクラスの既定値を入れてから読み込みへ渡す
		if (auto _it = m_aliasCreateMap.find(static_cast<ObjectTypeID>(a_savedValue)); _it != m_aliasCreateMap.end())
		{
			_it->second(*_upObject);
		}
		return _upObject;
	}

	ObjectTypeID ObjectMetaRegistry::GetTypeID(const std::string& a_name) const
	{
		auto _it = m_nameMap.find(a_name);
		if (_it != m_nameMap.end())
		{
			return _it->second;
		}
		return INVALID_OBJECT_TYPE_ID;
	}

	ObjectTypeID ObjectMetaRegistry::GetTypeID(TypeInfo::TypeKey a_key) const
	{
		auto _it = m_typeKeyMap.find(a_key);
		if (_it != m_typeKeyMap.end())
		{
			return _it->second;
		}
		return INVALID_OBJECT_TYPE_ID;
	}

	ObjectTypeID ObjectMetaRegistry::ResolveTypeID(uint32_t a_savedValue) const
	{
		// 保存されている値がそのままタイプID
		if (IsValid(a_savedValue))
		{
			return static_cast<ObjectTypeID>(a_savedValue);
		}

		// 登録名を変えたクラス : 旧名のIDから現行IDへ流す
		if (auto _it = m_aliasMap.find(static_cast<ObjectTypeID>(a_savedValue)); _it != m_aliasMap.end())
		{
			return _it->second;
		}

		return INVALID_OBJECT_TYPE_ID;
	}

	const ObjectMeta& ObjectMetaRegistry::GetMeta(ObjectTypeID a_id) const
	{
		auto _it = m_metaMap.find(a_id);
		if (_it != m_metaMap.end())
		{
			return _it->second;
		}

		// 未登録時は空メタを返す(静的なので寿命は安全)
		static const ObjectMeta EMPTY = {};
		return EMPTY;
	}

	std::unique_ptr<BaseObject> ObjectMetaRegistry::Create(ObjectTypeID a_id) const
	{
		auto _it = m_funcMap.find(a_id);
		if (_it != m_funcMap.end() && _it->second.create)
		{
			return _it->second.create();
		}
		return nullptr;
	}
}
