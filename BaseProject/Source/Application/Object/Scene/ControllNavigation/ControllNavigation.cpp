#include "ControllNavigation.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/EditorField/EditorField.h"

#include "Application/Object/ObjectPicker.h"
#include "Application/Object/UI/UIBase.h"
#include "Application/ECS/World/APPWorld.h"
#include "Application/InstanceResource/UINavigationResource.h"	// 決定は項目移動で選べるときだけ出す

namespace App::Object
{
	//======================================================================================
	// 更新 : 出す項目を決めて、帯の左端から詰めて並べる
	//--------------------------------------------------------------------------------------
	// 並べるのは画面の座標で行う(SetWorldPixelPos)。項目を帯の子に置いても置かなくても同じ形になる。
	// 横の位置は毎フレーム入れ直すので、エディターで項目を横へ動かしても戻る(縦は UI の置き場所のまま)
	//======================================================================================
	void ControllNavigation::Update(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pObjectManager = a_context.pObjectManager;
		if (!_pObjectManager) return;

		m_isShownVec.assign(m_itemVec.size(), false);

		float _cursorX = CalcStartX(a_context);
		bool _isAnyShown = false;

		for (size_t _i = 0; _i < m_itemVec.size(); ++_i)
		{
			UIBase* _pUI = Picker::Find<UIBase>(_pObjectManager, m_itemVec[_i].uiGUID);
			if (!_pUI) continue;

			const bool _isShown = m_isPreviewAll || IsItemShown(a_context, m_itemVec[_i]);
			_pUI->SetVisible(_isShown);
			if (!_isShown) continue;

			// 左端を今の位置へ合わせる。ピボットが中央の UI でも左詰めになるよう、ピボットのぶんだけ右へ置く
			const UIAnchor _anchor = _pUI->CalcWorldAnchor();
			const float _width = _anchor.pixelSize.x * _anchor.scale;
			const float _pivotX = _cursorX + _anchor.pivot.x * _width;

			// 同じ位置なら書かない(エディターで毎フレーム保存値を書き換えないように)
			if (std::abs(_pivotX - _anchor.pixelPos.x) > 0.01f)
			{
				_pUI->SetWorldPixelPos({ _pivotX, _anchor.pixelPos.y });
			}

			_cursorX += _width + m_spacing;
			m_isShownVec[_i] = true;
			_isAnyShown = true;
		}

		// 出せる操作が無ければ、空の帯は出さない
		if (UIBase* _pBand = Picker::Find<UIBase>(_pObjectManager, m_bandGUID))
		{
			_pBand->SetVisible(_isAnyShown);
		}
	}

	bool ControllNavigation::IsActionShown(Game::EGameAction a_action) const
	{
		for (size_t _i = 0; _i < m_itemVec.size() && _i < m_isShownVec.size(); ++_i)
		{
			if (m_itemVec[_i].action == a_action && m_isShownVec[_i]) return true;
		}
		return false;
	}

	bool ControllNavigation::IsItemShown(Engine::GameObject::ObjectContext& a_context, const ControllNavigationItem& a_item) const
	{
		// 進行役の上書きがあればそちら、無ければ保存されている値
		const auto _it = m_enabledOverrideMap.find(a_item.action);
		const bool _isEnabled = (_it != m_enabledOverrideMap.end()) ? _it->second : a_item.isEnabled;
		if (!_isEnabled) return false;

		// 決定はキー・パッドで選べる項目がある(出ている UINavigationGroup が名乗った)ときだけ。
		// グループは PreUpdate で名乗るので、このフレームの結果が見える
		if (a_item.action == Game::EGameAction::Decide)
		{
			if (!a_context.pWorld || !a_context.pWorld->HasResource<InstanceResource::UINavigationResource>()) return false;
			if (!a_context.pWorld->GetResource<InstanceResource::UINavigationResource>().HasOwner(a_context.frameIndex)) return false;
		}

		if (!a_item.conditionGUID.IsValid()) return true;

		return IsObjectShown(a_context, a_item.conditionGUID) != a_item.isConditionInverted;
	}

	bool ControllNavigation::IsObjectShown(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_guid)
	{
		const auto* _pObject = Picker::Find<Engine::GameObject::BaseObject>(a_context.pObjectManager, a_guid);
		if (!_pObject) return false;

		// UI は上の親が隠れていれば出ていない(パネルごと隠した中の UI を指していても効くように)
		if (const UIBase* _pUI = Core::TypeInfo::Cast<const UIBase>(_pObject))
		{
			return _pUI->IsVisibleInHierarchy(a_context);
		}
		return _pObject->IsVisible();
	}

	float ControllNavigation::CalcStartX(Engine::GameObject::ObjectContext& a_context) const
	{
		const UIBase* _pBand = Picker::Find<UIBase>(a_context.pObjectManager, m_bandGUID);
		if (!_pBand) return m_paddingLeft;

		// 帯の左端 : ピボットから、ピボットより左にある幅のぶんだけ戻る(回転は見ない)
		const UIAnchor _anchor = _pBand->CalcWorldAnchor();
		const float _left = _anchor.pixelPos.x - _anchor.pivot.x * _anchor.pixelSize.x * _anchor.scale;
		return _left + m_paddingLeft;
	}

	//======================================================================================
	// アーカイブ
	//======================================================================================
	void ControllNavigation::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& /*a_context*/)
	{
		a_ar.GUIDField("BandGUID", m_bandGUID);
		a_ar.Field("PaddingLeft", m_paddingLeft);
		a_ar.Field("Spacing", m_spacing);

		size_t _count = m_itemVec.size();
		if (a_ar.BeginArray("Items", _count))
		{
			m_itemVec.resize(_count);
			for (size_t _i = 0; _i < _count; ++_i)
			{
				if (!a_ar.BeginObject(_i)) continue;

				// 1項目ごとに区切る : 項目へフィールドを足しても、後ろの項目の読み出しがずれない
				{
					Engine::Persistence::ArchiveSection _section(a_ar, "ControllNavigationItem");

					ControllNavigationItem& _item = m_itemVec[_i];
					Game::ActionField(a_ar, "Action", _item.action);
					a_ar.GUIDField("UIGUID", _item.uiGUID);
					a_ar.Field("IsEnabled", _item.isEnabled);
					a_ar.GUIDField("ConditionGUID", _item.conditionGUID);
					a_ar.Field("IsConditionInverted", _item.isConditionInverted);
				}

				a_ar.EndObject();
			}
			a_ar.EndArray();
		}
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void ControllNavigation::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pObjectManager = a_context.pObjectManager;

		Engine::EditorField::Header("ControllNavigation");
		Engine::EditorField::HelpText("今できる操作の項目だけを、帯の左端から左詰めで横に並べる(出せない項目は詰める)");

		Picker::DrawCombo<UIBase>("Band", _pObjectManager, m_bandGUID);
		Engine::EditorField::Tooltip("画面下部の帯(UIElement)。項目はこの左端から並ぶ。指さなければ画面の左端から。出せる項目が無いときは隠す");

		Engine::EditorField::Field("PaddingLeft", m_paddingLeft, 1.0f, 0.0f, 1920.0f);
		Engine::EditorField::Tooltip("帯の左端から最初の項目までの余白(px)");
		Engine::EditorField::Field("Spacing", m_spacing, 1.0f, 0.0f, 1920.0f);
		Engine::EditorField::Tooltip("項目どうしの間(px)");

		// エディターでは条件がそろわず隠れている項目もあるので、ここで全部出して配置を見る(保存はされない)
		Engine::EditorField::Field("PreviewAll", m_isPreviewAll);
		Engine::EditorField::Tooltip("配置を見るために全部の項目を出しておく(保存はされない)");

		//------------------------------------------------------------------
		// 項目(上から順に左から並ぶ)
		//------------------------------------------------------------------
		Engine::EditorField::Header("Items");
		Engine::EditorField::HelpText("1項目 = アイコンと文字を飾りに持つ UI 1つ。幅はその UI の PixelSize.x(文字の飾りは測れないので、収まる幅を入れる)");

		int _removeIndex = -1;
		int _swapIndex = -1;
		for (size_t _i = 0; _i < m_itemVec.size(); ++_i)
		{
			Engine::EditorField::IDScope _id(static_cast<int>(_i));
			ControllNavigationItem& _item = m_itemVec[_i];

			const bool _isShown = (_i < m_isShownVec.size()) && m_isShownVec[_i];
			Engine::EditorField::Text("#%zu %s", _i, _isShown ? "(shown)" : "(hidden)");

			Engine::EditorField::SameLine();
			if (Engine::EditorField::ArrowButton("##Up", Engine::EditorField::EArrowDir::Up) && _i > 0) _swapIndex = static_cast<int>(_i) - 1;
			Engine::EditorField::SameLine();
			if (Engine::EditorField::ArrowButton("##Down", Engine::EditorField::EArrowDir::Down) && _i + 1 < m_itemVec.size()) _swapIndex = static_cast<int>(_i);
			Engine::EditorField::SameLine();
			if (Engine::EditorField::DeleteSmallButton("Remove")) _removeIndex = static_cast<int>(_i);

			Engine::EditorField::Field("Action", _item.action);
			Engine::EditorField::Tooltip("説明するアクション。進行役は SetActionEnabled でこの名前を使って出し入れする");

			Picker::DrawCombo<UIBase>("UI", _pObjectManager, _item.uiGUID);
			Engine::EditorField::Tooltip("出す UI(キーのアイコンと「決定」などの文字を飾りに持つ UIElement)");

			Engine::EditorField::Field("Enabled", _item.isEnabled);
			Engine::EditorField::Tooltip("最初から出すか。実行中は進行役の SetActionEnabled が優先する");

			Picker::DrawCombo<Engine::GameObject::BaseObject>("Condition", _pObjectManager, _item.conditionGUID);
			Engine::EditorField::Tooltip("指していれば、そのオブジェクトが出ている間だけ出す(None で常に)。UI は上の親の表示も含めて見る");
			if (_item.conditionGUID.IsValid())
			{
				Engine::EditorField::Field("InvertCondition", _item.isConditionInverted);
				Engine::EditorField::Tooltip("Condition が隠れている間だけ出す(サイドバーが閉じている間だけ「開く」を出す、など)");
			}

			// 幅が無いと次の項目と重なるので知らせる
			if (const UIBase* _pUI = Picker::Find<UIBase>(_pObjectManager, _item.uiGUID))
			{
				if (_pUI->GetAnchor().pixelSize.x <= 0.0f)
				{
					Engine::EditorField::WarningText("UI の PixelSize.x が 0 : 次の項目と重なる");
				}
			}

			Engine::EditorField::Line();
		}

		if (_swapIndex >= 0) std::swap(m_itemVec[_swapIndex], m_itemVec[_swapIndex + 1]);
		if (_removeIndex >= 0) m_itemVec.erase(m_itemVec.begin() + _removeIndex);
		if (Engine::EditorField::CreateButton("Add Item")) m_itemVec.push_back({});
	}
}
