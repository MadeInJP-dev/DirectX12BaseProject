#include "UIDecoration.h"
namespace App::Object::Decoration
{
	void UIDecoration::Updaate(const UpdateContext& a_context)
	{
		for (auto& _upElement : m_upElements)
		{
			_upElement->Updaate(a_context);
		}
	}
	void UIDecoration::Draw(const DrawContext& a_context)
	{
		for (auto& _upElement : m_upElements)
		{
			_upElement->Draw(a_context);
		}
	}
}