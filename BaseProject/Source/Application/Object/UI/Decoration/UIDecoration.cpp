#include "UIDecoration.h"

#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

#include "DecorationElement/UISpriteElement/UISpriteElement.h"
#include "DecorationElement/UITextElement/UITextElement.h"

namespace App::Object::Decoration
{
	//======================================================================================
	// 複製
	//======================================================================================
	UIDecoration::UIDecoration(const UIDecoration& a_other)
	{
		*this = a_other;
	}

	UIDecoration& UIDecoration::operator=(const UIDecoration& a_other)
	{
		if (this == &a_other) return *this;

		// エレメントは型ごとに Clone で写す(基底のポインタのまま写すと派生の中身が落ちる)
		m_upElements.clear();
		m_upElements.reserve(a_other.m_upElements.size());
		for (const auto& _upElement : a_other.m_upElements)
		{
			m_upElements.push_back(_upElement->Clone());
		}

		m_isVisible = a_other.m_isVisible;
		m_scale = a_other.m_scale;
		m_materialGUID = a_other.m_materialGUID;
		m_materialRef = a_other.m_materialRef;
		m_editElementId = a_other.m_editElementId;
		return *this;
	}

	//======================================================================================
	// 更新
	//======================================================================================
	void UIDecoration::Update(const UpdateContext& a_context)
	{
		// 隠している間も進める : 出した瞬間に止まっていた位置から動き出さないように
		for (auto& _upElement : m_upElements)
		{
			_upElement->Update(a_context);
		}
	}

	//======================================================================================
	// 描画
	//======================================================================================
	void UIDecoration::Draw(const DrawContext& a_context)
	{
		if (!m_isVisible) return;
		if (m_upElements.empty()) return;
		if (a_context.pGraphicsEngine == nullptr || a_context.pResourceManager == nullptr) return;

		// 描くのは更新を回さない場面(エディターで止めている間など)もあるので、ここで写す
		SyncPresets(a_context.pResourceManager);

		// 飾り全体へ掛かる設定を足し込んでから配る
		DrawContext _context = a_context;
		_context.pParent = this;
		_context.parentTr.scale *= m_scale;

		// マテリアルは持ち主が渡していればそちらを優先する(移し替えの間は UIBase が持っているため)
		if (_context.parentOp.pMaterial == nullptr)
		{
			_context.parentOp.pMaterial = FindReadyMaterial(a_context.pResourceManager);
		}

		// 配列の順に積む : 後ろにあるものほど手前に出る
		for (auto& _upElement : m_upElements)
		{
			_upElement->Draw(_context);
		}
	}

	//======================================================================================
	// 参照を引き直す
	//======================================================================================
	void UIDecoration::RequestResources(Engine::Resource::ResourceManager* a_pResourceManager)
	{
		if (a_pResourceManager == nullptr) return;

		RequestMaterial(a_pResourceManager);

		for (auto& _upElement : m_upElements)
		{
			_upElement->RequestResources(a_pResourceManager);
		}
	}

	//======================================================================================
	// 保存
	//======================================================================================
	void UIDecoration::Archive(Engine::Persistence::Archive& a_ar)
	{
		a_ar.Field("IsVisible", m_isVisible);
		a_ar.Field("Scale", m_scale);
		a_ar.GUIDField("MaterialGUID", m_materialGUID);

		size_t _elementCount = m_upElements.size();
		if (!a_ar.BeginArray("Elements", _elementCount)) return;

		if (a_ar.IsLoading())
		{
			m_upElements.clear();
			m_upElements.resize(_elementCount);
		}

		for (size_t _i = 0; _i < _elementCount; ++_i)
		{
			if (!a_ar.BeginObject(_i)) continue;

			// 種類を先に書く : 読むときは種類を見て作ってから中身を読む
			EElementType _type = m_upElements[_i] ? m_upElements[_i]->GetType() : EElementType::Sprite;
			a_ar.Field("Type", _type);

			if (a_ar.IsLoading()) m_upElements[_i] = CreateElement(_type);

			// エレメント1つごとに区切る。知らない種類(新しい版で足されたもの)は中身を読まずに飛ばす
			{
				Engine::Persistence::ArchiveSection _elementSection(a_ar, "Element");
				if (m_upElements[_i]) m_upElements[_i]->Archive(a_ar);
			}

			a_ar.EndObject();
		}
		a_ar.EndArray();

		if (!a_ar.IsLoading()) return;

		// 読めなかったもの(知らない種類・壊れた要素)は外し、番号の欠け・重なりを直す
		std::erase(m_upElements, nullptr);
		AssignElementIds();
	}

	//======================================================================================
	// エレメントの操作
	//======================================================================================
	UIDecorationElementBase& UIDecoration::AddElement(EElementType a_type)
	{
		std::unique_ptr<UIDecorationElementBase> _upElement = CreateElement(a_type);
		if (!_upElement) _upElement = std::make_unique<UISpriteElement>();

		UIDecorationElementBase& _element = *_upElement;
		SetupAddedElement(_element);
		m_upElements.push_back(std::move(_upElement));

		return _element;
	}

	bool UIDecoration::RemoveElement(ElementID a_id)
	{
		const int _index = FindElementIndex(a_id);
		if (_index < 0) return false;

		m_upElements.erase(m_upElements.begin() + _index);
		return true;
	}

	bool UIDecoration::MoveElement(size_t a_from, size_t a_to)
	{
		if (a_from >= m_upElements.size() || a_to >= m_upElements.size()) return false;
		if (a_from == a_to) return true;

		// 間にあるものを1つずつずらして、動かすものを差し込む
		auto _begin = m_upElements.begin();
		if (a_from < a_to)
		{
			std::rotate(_begin + a_from, _begin + a_from + 1, _begin + a_to + 1);
		}
		else
		{
			std::rotate(_begin + a_to, _begin + a_from, _begin + a_from + 1);
		}
		return true;
	}

	UIDecorationElementBase* UIDecoration::FindElement(const std::string& a_name)
	{
		return const_cast<UIDecorationElementBase*>(static_cast<const UIDecoration*>(this)->FindElement(a_name));
	}

	const UIDecorationElementBase* UIDecoration::FindElement(const std::string& a_name) const
	{
		for (const auto& _upElement : m_upElements)
		{
			if (_upElement->GetName() == a_name) return _upElement.get();
		}
		return nullptr;
	}

	UIDecorationElementBase* UIDecoration::FindElementById(ElementID a_id)
	{
		const int _index = FindElementIndex(a_id);
		return (_index >= 0) ? m_upElements[_index].get() : nullptr;
	}

	const UIDecorationElementBase* UIDecoration::FindElementById(ElementID a_id) const
	{
		const int _index = FindElementIndex(a_id);
		return (_index >= 0) ? m_upElements[_index].get() : nullptr;
	}

	int UIDecoration::FindElementIndex(ElementID a_id) const
	{
		if (!a_id.IsValid()) return -1;

		for (size_t _i = 0; _i < m_upElements.size(); ++_i)
		{
			if (m_upElements[_i]->GetId() == a_id) return static_cast<int>(_i);
		}
		return -1;
	}

	//======================================================================================
	// マテリアル
	//--------------------------------------------------------------------------------------
	// 飾り全部に同じ効果を掛ける(ParentOption::pMaterial)。
	// 読み終わるまでは効果なしで描く(届くまで描かないと、出た瞬間に UI が見えない)
	//======================================================================================
	void UIDecoration::SetMaterialGUID(const Core::GUID& a_guid, Engine::Resource::ResourceManager* a_pResourceManager)
	{
		m_materialGUID = a_guid;
		RequestMaterial(a_pResourceManager);
	}

	const Engine::Resource::UIMaterial* UIDecoration::FindReadyMaterial(Engine::Resource::ResourceManager* a_pResourceManager) const
	{
		if (!a_pResourceManager || !m_materialRef.IsValid()) return nullptr;
		if (!a_pResourceManager->IsReady(m_materialRef)) return nullptr;

		return a_pResourceManager->Get(m_materialRef);
	}

	void UIDecoration::RequestMaterial(Engine::Resource::ResourceManager* a_pResourceManager)
	{
		if (a_pResourceManager == nullptr) return;

		m_materialRef = m_materialGUID.IsValid()
			? a_pResourceManager->RequestLoad<Engine::Resource::UIMaterial>(m_materialGUID)
			: Engine::ResourceRef<Engine::Resource::UIMaterial>();
	}

	//======================================================================================
	// 内部処理
	//======================================================================================
	std::unique_ptr<UIDecorationElementBase> UIDecoration::CreateElement(EElementType a_type)
	{
		switch (a_type)
		{
		case EElementType::Sprite: return std::make_unique<UISpriteElement>();
		case EElementType::Text:   return std::make_unique<UITextElement>();
		default:                   return nullptr;
		}
	}

	void UIDecoration::SetupAddedElement(UIDecorationElementBase& a_element)
	{
		a_element.m_id = MakeNewElementId();

		// 名前が全部同じだと一覧で見分けられないので、種類と番号を入れておく
		a_element.m_name = std::string(a_element.GetTypeName()) + std::to_string(m_upElements.size() + 1);
	}

	ElementID UIDecoration::MakeNewElementId() const
	{
		// 今ある番号の最大の次。消したエレメントの番号は使い回さない
		// (消したことに気付かず指したままの側が、別のエレメントを掴んでしまわないように)。
		// 1 から振る : 旧 Decoration は 0 を「未採番」に使っていたので、移し替えたときに紛れないように
		uint32_t _maxId = 0;
		for (const auto& _upElement : m_upElements)
		{
			if (!_upElement || !_upElement->GetId().IsValid()) continue;
			_maxId = std::max(_maxId, _upElement->GetId().value);
		}
		return ElementID(_maxId + 1);
	}

	void UIDecoration::AssignElementIds()
	{
		for (size_t _i = 0; _i < m_upElements.size(); ++_i)
		{
			UIDecorationElementBase& _element = *m_upElements[_i];

			// 前にあるエレメントと同じ番号なら、後ろのほうを振り直す
			bool _isDuplicated = false;
			for (size_t _j = 0; _j < _i; ++_j)
			{
				if (m_upElements[_j]->GetId() == _element.GetId()) { _isDuplicated = true; break; }
			}

			if (!_element.GetId().IsValid() || _element.GetId().value == 0 || _isDuplicated)
			{
				_element.m_id = MakeNewElementId();
			}
		}
	}
}
