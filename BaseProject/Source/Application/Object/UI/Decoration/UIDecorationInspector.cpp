#include "UIDecoration.h"

#include "Engine/ECS/System/SystemContext.h"	// EngineServices
#include "Engine/EditorField/EditorField.h"
#include "Engine/GameObject/BaseObject/BaseObject.h"	// ObjectGizmoContext
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

#include "Application/Object/UI/Decoration/Internal/DecorationInternal.h"

//==========================================================================================
// UIDecoration : インスペクターとギズモ(エディター用)
//==========================================================================================
namespace App::Object::Decoration
{
	//======================================================================================
	// インスペクター
	//--------------------------------------------------------------------------------------
	// 描く順は配列順なので、並べ替えがそのまま重なり順になる。
	// 開いている1つだけ中身を出す形にしてあるのは、エレメントが増えると
	// 全部展開したときにインスペクターが縦に流れて使えなくなるため
	//======================================================================================
	bool UIDecoration::DrawInspector(const Engine::ECS::EngineServices& a_services)
	{
		bool _isChanged = false;

		//----------------------------------------------------------------------------------
		// 全体の設定
		//----------------------------------------------------------------------------------
		Engine::EditorField::Header("Decoration");

		if (Engine::EditorField::Field("Visible", m_isVisible)) _isChanged = true;
		if (Engine::EditorField::Field("Scale", m_scale, 0.01f, 0.0f, 64.0f)) _isChanged = true;
		Engine::EditorField::Tooltip("全エレメントへ掛ける倍率(位置のずれ・枠の太さ・文字の大きさにも掛かる)");

		Core::GUID _materialGUID = m_materialGUID;
		if (Engine::EditorField::AssetField(a_services, "UIMaterial", "UIMaterial", _materialGUID))
		{
			SetMaterialGUID(_materialGUID, a_services.pResourceManager);
			_isChanged = true;
		}
		Engine::EditorField::Tooltip("全エレメントに同じ効果(ディゾルブなど)を掛ける。持ち主がマテリアルを渡していればそちらが優先");

		//----------------------------------------------------------------------------------
		// エレメントの一覧
		//----------------------------------------------------------------------------------
		Engine::EditorField::Header("Elements");
		Engine::EditorField::HelpText("配列の順に描きます(下にあるものほど手前)");

		// ---- 追加 ----
		if (Engine::EditorField::CreateButton("Add Sprite"))
		{
			m_editElementId = AddElement(EElementType::Sprite).GetId();
			_isChanged = true;
		}
		Engine::EditorField::SameLine();
		if (Engine::EditorField::CreateButton("Add Text"))
		{
			UIDecorationElementBase& _element = AddElement(EElementType::Text);
			_element.RequestResources(a_services.pResourceManager);
			m_editElementId = _element.GetId();
			_isChanged = true;
		}

		// ---- 全削除 ----
		// 戻せないので Ctrl を押している間だけ効かせる
		if (!m_upElements.empty())
		{
			Engine::EditorField::SameLine();
			if (Engine::EditorField::DeleteButton("Clear All") && Engine::EditorField::IsCtrlDown())
			{
				ClearElements();
				m_editElementId = {};
				_isChanged = true;
			}
			Engine::EditorField::Tooltip("Ctrl+クリックで全部消す");
		}

		// 一覧を回している間に配列を触ると足元が崩れるので、操作は覚えておいて後でまとめて行う
		ElementID _removeId = {};
		int _swapIndex = -1;		// この番号と次の番号を入れ替える

		const int _count = static_cast<int>(m_upElements.size());
		for (int _i = 0; _i < _count; ++_i)
		{
			UIDecorationElementBase& _element = *m_upElements[_i];

			Engine::EditorField::IDScope _id(_i);

			//----------------------------------------------------------------------
			// 1行ぶん : [X][↑][↓] 名前
			//
			// ボタンを先に置くこと。
			// Selectable は残りの幅を全部使うので、後ろへ並べると
			// ボタンが行の外まで押し出されて押せなくなる
			//----------------------------------------------------------------------
			if (Engine::EditorField::DeleteSmallButton("X")) _removeId = _element.GetId();
			Engine::EditorField::Tooltip("このエレメントを消す");

			Engine::EditorField::SameLine();
			if (Engine::EditorField::ArrowButton("##Up", Engine::EditorField::EArrowDir::Up) && _i > 0) _swapIndex = _i - 1;

			Engine::EditorField::SameLine();
			if (Engine::EditorField::ArrowButton("##Down", Engine::EditorField::EArrowDir::Down) && _i + 1 < _count)
			{
				_swapIndex = _i;
			}

			// 開閉 : 開いているものだけ中身を出す
			Engine::EditorField::SameLine();
			const bool _isOpen = (m_editElementId == _element.GetId());
			const std::string _label =
				std::to_string(_i) + " : " + (_element.GetName().empty() ? "(no name)" : _element.GetName()) +
				" [" + _element.GetTypeName() + "]";

			if (Engine::EditorField::Selectable(_label.c_str(), _isOpen))
			{
				m_editElementId = _isOpen ? ElementID{} : _element.GetId();
			}

			if (!_isOpen) continue;

			{
				Engine::EditorField::IndentScope _indent;

				// プリセット(付いていく部分は、EditPreset を立てない限り下で触れない)
				if (DrawElementPresetInspector(_element, a_services)) _isChanged = true;

				if (_element.DrawInspector(a_services))
				{
					// 付いていく部分を直したら、プリセットへ書き戻す(同じプリセットを指すものが全部変わる)
					PushElementToPreset(_element, a_services.pResourceManager);
					_isChanged = true;
				}
			}
			Engine::EditorField::Line();
		}

		// 開いていたものは番号で追いかけるので、並べ替え・削除の後も開いたまま
		if (_swapIndex >= 0)
		{
			MoveElement(static_cast<size_t>(_swapIndex), static_cast<size_t>(_swapIndex) + 1);
			_isChanged = true;
		}

		if (_removeId.IsValid())
		{
			RemoveElement(_removeId);
			if (m_editElementId == _removeId) m_editElementId = {};
			_isChanged = true;
		}

		return _isChanged;
	}

	//======================================================================================
	// ギズモ : 開いているエレメントの位置
	//--------------------------------------------------------------------------------------
	// エレメントの OffsetPos は親のピボットからのずれ(親の回転・倍率を掛ける前)なので、
	// 画面上の位置からは回転を戻し、倍率で割って求める。
	// 倍率は描くときと同じく 親 × デコレーション全体 × エレメント自身 で掛かる。
	// 親のハンドルと見分けられるよう、小さく出す
	//======================================================================================
	bool UIDecoration::DrawGizmo(
		const Engine::GameObject::ObjectGizmoContext& a_ctx,
		const ParentTransform& a_parentTr,
		float a_screenW,
		float a_screenH)
	{
		if (a_ctx.viewportSize.x <= 0.0f || a_ctx.viewportSize.y <= 0.0f) return false;
		if (a_screenW <= 0.0f || a_screenH <= 0.0f) return false;

		UIDecorationElementBase* _pElement = FindElementById(m_editElementId);
		if (_pElement == nullptr) return false;

		const float _scale = a_parentTr.scale * m_scale * _pElement->GetScale();

		// 画面上の位置(ゲーム内ピクセル) → シーンビュー上ピクセル
		const Math::Vector2 _screenPos =
			a_parentTr.pixelPos + Internal::RotateDeg(_pElement->GetOffsetPos() * _scale, a_parentTr.rotation);

		Math::Vector2 _handle = {};
		_handle.x = a_ctx.viewportPos.x + (_screenPos.x / a_screenW) * a_ctx.viewportSize.x;
		_handle.y = a_ctx.viewportPos.y + (_screenPos.y / a_screenH) * a_ctx.viewportSize.y;

		static const float HANDLE_RADIUS = 5.0f;

		Math::Vector2 _mouse = {};
		if (!Engine::EditorField::ScreenHandle("##UIDecorationElementGizmo", _handle, HANDLE_RADIUS, _mouse)) return false;

		// シーンビュー上 → ゲーム内ピクセル
		const Math::Vector2 _dragScreen = {
			(_mouse.x - a_ctx.viewportPos.x) / a_ctx.viewportSize.x * a_screenW,
			(_mouse.y - a_ctx.viewportPos.y) / a_ctx.viewportSize.y * a_screenH
		};

		// 親のピボットからのずれへ戻す : 回転を戻してから倍率で割る
		if (_scale > 1e-6f)
		{
			const Math::Vector2 _local = Internal::RotateDeg(_dragScreen - a_parentTr.pixelPos, -a_parentTr.rotation);
			_pElement->SetOffsetPos(_local / _scale);
		}
		return true;
	}
}
