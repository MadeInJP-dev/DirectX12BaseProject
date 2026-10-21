#include "UIPreset.h"

namespace Engine::Resource
{
	namespace
	{
		// アプリ全体で進める番号。プリセットごとに持つと、読み直したときに
		// 前と同じ番号から始まり、指している飾りが変わったことに気付けない
		std::atomic<uint32_t> s_revisionCounter = 0;
	}

	void UIPreset::Archive(Persistence::Archive& a_ar)
	{
		a_ar.StringField("Name", name);
		a_ar.StringField("Decoration", decorationJson);

		if (a_ar.IsLoading())
		{
			Touch();
			isDirty = false;
		}
	}

	void UIPreset::Save(const std::string& a_baseFilePath)
	{
		auto _fileDir = Core::File::GetDirFromPath(a_baseFilePath);
		auto _fileName = Core::File::GetFileNameWithoutExtension(a_baseFilePath);

		{
			Persistence::Archive _ar(Persistence::Archive::EMode::Save, _fileDir, _fileName, "uipreset");
			Archive(_ar);
		}
		isDirty = false;
	}

	void UIPreset::SetDecorationJson(const std::string& a_json)
	{
		if (decorationJson == a_json) return;

		decorationJson = a_json;
		isDirty = true;
		Touch();
	}

	void UIPreset::Touch()
	{
		revision = ++s_revisionCounter;
	}
}
