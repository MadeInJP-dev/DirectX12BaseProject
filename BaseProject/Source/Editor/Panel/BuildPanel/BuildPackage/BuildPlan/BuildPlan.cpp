#include "BuildPlan.h"

#include <cwctype>

namespace Editor::Build
{
	namespace
	{
		// 比較用に小文字へ揃える(Windows のパスは大文字小文字を区別しない)
		std::wstring ToLowerWide(std::wstring a_str)
		{
			std::transform(a_str.begin(), a_str.end(), a_str.begin(),
				[](wchar_t a_c) { return static_cast<wchar_t>(std::towlower(a_c)); });
			return a_str;
		}

		// 一覧の中に入っているか(大文字小文字は無視)
		bool ContainsIgnoreCase(const std::vector<std::string>& a_vec, const std::filesystem::path& a_value)
		{
			const std::wstring _value = ToLowerWide(a_value.wstring());
			for (const auto& _item : a_vec)
			{
				if (ToLowerWide(Core::File::FromUTF8(_item).wstring()) == _value) return true;
			}
			return false;
		}
	}

	bool BuildPlan::AddCopyFile(const std::filesystem::path& a_src, const std::filesystem::path& a_dst)
	{
		std::error_code _ec;
		if (!std::filesystem::is_regular_file(a_src, _ec)) return false;

		if (!ReserveDestination(a_dst)) return true;

		BuildPlanEntry _entry = {};
		_entry.srcPath = a_src;
		_entry.dstPath = a_dst;
		_entry.byteSize = std::filesystem::file_size(a_src, _ec);
		if (_ec) _entry.byteSize = 0;

		m_totalBytes += _entry.byteSize;
		m_entryVec.push_back(std::move(_entry));
		return true;
	}

	bool BuildPlan::AddCopyDirectory(
		const std::filesystem::path& a_src,
		const std::filesystem::path& a_dst,
		const std::vector<std::string>& a_extensionVec,
		const std::vector<std::string>& a_excludeDirNameVec
	)
	{
		std::error_code _ec;
		if (!std::filesystem::is_directory(a_src, _ec)) return false;

		auto _it = std::filesystem::recursive_directory_iterator(
			a_src, std::filesystem::directory_options::skip_permission_denied, _ec);
		for (; !_ec && _it != std::filesystem::recursive_directory_iterator(); _it.increment(_ec))
		{
			const auto& _entry = *_it;

			// 1件の失敗で走査を止めないよう、走査とは別のエラーで受ける
			std::error_code _entryEc;

			// 除外するフォルダは中に入らない
			if (_entry.is_directory(_entryEc))
			{
				if (ContainsIgnoreCase(a_excludeDirNameVec, _entry.path().filename()))
				{
					_it.disable_recursion_pending();
				}
				continue;
			}
			if (!_entry.is_regular_file(_entryEc)) continue;

			if (!a_extensionVec.empty() && !ContainsIgnoreCase(a_extensionVec, _entry.path().extension())) continue;

			AddCopyFile(_entry.path(), a_dst / _entry.path().lexically_relative(a_src));
		}

		if (_ec)
		{
			AddWarning("フォルダを最後まで読めませんでした : " + Core::File::ToUTF8(a_src));
		}
		return true;
	}

	void BuildPlan::AddWriteText(const std::filesystem::path& a_dst, std::string a_text)
	{
		if (!ReserveDestination(a_dst)) return;

		BuildPlanEntry _entry = {};
		_entry.dstPath = a_dst;
		_entry.byteSize = a_text.size();
		_entry.text = std::move(a_text);

		m_totalBytes += _entry.byteSize;
		m_entryVec.push_back(std::move(_entry));
	}

	void BuildPlan::AddWarning(std::string a_message)
	{
		m_warningVec.push_back(std::move(a_message));
	}

	bool BuildPlan::ReserveDestination(const std::filesystem::path& a_dst)
	{
		return m_destinationSet.insert(ToLowerWide(a_dst.lexically_normal().wstring())).second;
	}

	//======================================================================================
	// 書き出し
	//--------------------------------------------------------------------------------------
	// ワーカースレッドで回る。アセットデータベースやログには触らず、
	// 結果は引数だけで返す(ログはメインスレッドが終わってから出す)
	//======================================================================================
	bool BuildPlan::Execute(
		std::atomic<size_t>& a_doneCount,
		std::atomic<uintmax_t>& a_doneBytes,
		const std::atomic_bool& a_isCancel,
		std::vector<std::string>& a_outErrorVec
	) const
	{
		for (const auto& _entry : m_entryVec)
		{
			if (a_isCancel.load()) return false;

			// 書き出し先が深いと 260 文字を超える(Source の奥のファイルなど)ので、長いパスで開く
			const std::filesystem::path _dst = Core::File::ToLongPath(_entry.dstPath);

			std::error_code _ec;
			std::filesystem::create_directories(_dst.parent_path(), _ec);

			if (!_ec)
			{
				if (_entry.srcPath.empty())
				{
					std::ofstream _ofs(_dst, std::ios::binary);
					if (_ofs.is_open()) _ofs << _entry.text;
					else _ec = std::make_error_code(std::errc::io_error);
				}
				else
				{
					std::filesystem::copy_file(
						Core::File::ToLongPath(_entry.srcPath), _dst, std::filesystem::copy_options::overwrite_existing, _ec);
				}
			}

			if (_ec)
			{
				// message() は ANSI コードページで返るので UTF-8 へ直す
				a_outErrorVec.push_back(Core::File::ToUTF8(_entry.dstPath) + " : "
					+ Core::String::ToUTF8(Core::String::ToWideString(_ec.message())));
			}

			a_doneBytes += _entry.byteSize;
			++a_doneCount;
		}

		return a_outErrorVec.empty();
	}
}
