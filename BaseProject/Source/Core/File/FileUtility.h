#pragma once

//==========================================================================================
// Core::File
//
// ファイルパスの文字列操作と、ディレクトリの走査。
//==========================================================================================
namespace Core::File
{
	//--------------------------------------------------------------------------------------
	// 拡張子
	//--------------------------------------------------------------------------------------

	/// <summary>ファイルパスから拡張子を取得する</summary>
	/// <param name="a_fileName">ファイル名またはパス</param>
	/// <returns>
	/// 拡張子。**先頭の "." は含まない**("a/b/c.png" → "png")。
	/// 拡張子が無ければ空文字を返す。
	/// </returns>
	inline std::string GetFilePathExtension(const std::string& a_fileName)
	{
		const auto _idx = a_fileName.find_last_of('.');
		if (_idx == std::string::npos) return {};

		return a_fileName.substr(_idx + 1);
	}
	/// <summary>ファイルパスから拡張子を取得する(ワイド文字列版)</summary>
	/// <returns>拡張子。**先頭の "." は含まない**。拡張子が無ければ空文字</returns>
	inline std::wstring GetFilePathExtension(const std::wstring& a_fileName)
	{
		const auto _idx = a_fileName.find_last_of(L'.');
		if (_idx == std::wstring::npos) return {};

		return a_fileName.substr(_idx + 1);
	}

	/// <summary>拡張子を置き換える</summary>
	/// <param name="a_ext">新しい拡張子。**"." は付けても付けなくてもよい**("png" / ".png" のどちらでも可)</param>
	/// <returns>置き換え後のパス</returns>
	inline std::string ReplaceFilePathExtension(const std::string& a_fileName, const std::string& a_ext)
	{
		std::filesystem::path _p = a_fileName;
		return _p.replace_extension(a_ext).string();
	}
	/// <summary>拡張子を置き換える(ワイド文字列版)</summary>
	/// <param name="a_ext">新しい拡張子。**"." は付けても付けなくてもよい**</param>
	inline std::wstring ReplaceFilePathExtension(const std::wstring& a_fileName, const std::wstring& a_ext)
	{
		std::filesystem::path _p = a_fileName;
		return _p.replace_extension(a_ext).wstring();
	}

	//--------------------------------------------------------------------------------------
	// ファイル名
	//--------------------------------------------------------------------------------------

	/// <summary>拡張子を除いたファイル名を取得する</summary>
	/// <returns>ファイル名。**拡張子と "." は含まない**("a/b/c.png" → "c")</returns>
	inline std::string GetFileNameWithoutExtension(const std::string& a_fileName)
	{
		std::filesystem::path _p = a_fileName;
		return _p.stem().string();
	}
	/// <summary>拡張子を除いたファイル名を取得する(ワイド文字列版)</summary>
	/// <returns>ファイル名。**拡張子と "." は含まない**</returns>
	inline std::wstring GetFileNameWithoutExtension(const std::wstring& a_fileName)
	{
		std::filesystem::path _p = a_fileName;
		return _p.stem().wstring();
	}

	/// <summary>ファイル名を取得する</summary>
	/// <returns>ファイル名。**拡張子は含む**("a/b/c.png" → "c.png")</returns>
	inline std::string GetFileName(const std::string& a_filePath)
	{
		std::filesystem::path _p = a_filePath;
		return _p.filename().string();
	}
	/// <summary>ファイル名を取得する(ワイド文字列版)</summary>
	/// <returns>ファイル名。**拡張子は含む**</returns>
	inline std::wstring GetFileName(const std::wstring& a_filePath)
	{
		std::filesystem::path _p = a_filePath;
		return _p.filename().wstring();
	}

	//--------------------------------------------------------------------------------------
	// ディレクトリ
	//--------------------------------------------------------------------------------------

	/// <summary>ファイルパスから親ディレクトリまでのパスを取得する</summary>
	/// <returns>
	/// ディレクトリパス。**末尾の区切り文字を含む**("a/b/c.png" → "a/b/")。
	/// 区切りが無ければ空文字を返す。
	/// </returns>
	inline std::string GetDirFromPath(const std::string& a_path)
	{
		const auto _slash    = a_path.find_last_of('/');
		const auto _backSlash = a_path.find_last_of('\\');

		// どちらか後ろにある方を採用する(両方無ければ npos)
		std::string::size_type _pos = std::string::npos;
		if (_slash == std::string::npos)          _pos = _backSlash;
		else if (_backSlash == std::string::npos) _pos = _slash;
		else                                      _pos = (std::max)(_slash, _backSlash);

		if (_pos == std::string::npos) return {};

		return a_path.substr(0, _pos + 1);
	}
	/// <summary>ファイルパスから親ディレクトリまでのパスを取得する(ワイド文字列版)</summary>
	/// <returns>ディレクトリパス。**末尾の区切り文字を含む**。区切りが無ければ空文字</returns>
	inline std::wstring GetDirFromPath(const std::wstring& a_path)
	{
		const auto _slash     = a_path.find_last_of(L'/');
		const auto _backSlash = a_path.find_last_of(L'\\');

		std::wstring::size_type _pos = std::wstring::npos;
		if (_slash == std::wstring::npos)          _pos = _backSlash;
		else if (_backSlash == std::wstring::npos) _pos = _slash;
		else                                       _pos = (std::max)(_slash, _backSlash);

		if (_pos == std::wstring::npos) return {};

		return a_path.substr(0, _pos + 1);
	}

	/// <summary>指定ディレクトリ直下から、指定拡張子のファイルを全て集める</summary>
	/// <param name="a_dirPath">探すディレクトリ(再帰はしない)</param>
	/// <param name="a_ext">拡張子。**"." は付けても付けなくてもよい**("png" / ".png" のどちらでも可)</param>
	/// <returns>見つかったファイルのパス。ディレクトリが無ければ空</returns>
	inline std::vector<std::filesystem::path> FindExtensionInDirectory(
		const std::filesystem::path& a_dirPath,
		const std::string& a_ext
	)
	{
		std::vector<std::filesystem::path> _outPaths;

		if (!std::filesystem::exists(a_dirPath) ||
			!std::filesystem::is_directory(a_dirPath))
		{
			return _outPaths;
		}

		// path::extension() は "." 付きで返すので、比較する側も "." 付きへ揃える
		std::string _ext = a_ext;
		if (!_ext.empty() && _ext[0] != '.')
		{
			_ext = "." + _ext;
		}

		for (const auto& _entry : std::filesystem::directory_iterator(a_dirPath))
		{
			// ファイルだけ対象
			if (!_entry.is_regular_file()) continue;

			if (_entry.path().extension() == _ext)
			{
				_outPaths.push_back(_entry.path());
			}
		}

		return _outPaths;
	}

	//--------------------------------------------------------------------------------------
	// UTF-8 の文字列とパスの変換
	//
	// std::filesystem::path を std::string から作る / .string() で取り出すと、
	// Windows では ANSI コードページ(CP932)として扱われる。
	// ImGui の入力欄や JSON は UTF-8 なので、日本語のフォルダ名を通すときはこちらを使う。
	//--------------------------------------------------------------------------------------

	/// <summary>UTF-8 の文字列からパスを作る</summary>
	inline std::filesystem::path FromUTF8(const std::string& a_utf8)
	{
		return std::filesystem::path(std::u8string(a_utf8.begin(), a_utf8.end()));
	}
	/// <summary>パスを UTF-8 の文字列にする</summary>
	inline std::string ToUTF8(const std::filesystem::path& a_path)
	{
		const std::u8string _u8 = a_path.u8string();
		return std::string(_u8.begin(), _u8.end());
	}

	/// <summary>260文字(MAX_PATH)を超えても開けるパスにする</summary>
	/// <remarks>
	/// Windows は "\\?\" を付けた絶対パスなら長さの制限を受けない。
	/// 深いフォルダへ写すとき(提出用パッケージの書き出しなど)に通す。表示には使わないこと
	/// </remarks>
	inline std::filesystem::path ToLongPath(const std::filesystem::path& a_path)
	{
		std::error_code _ec;
		const std::filesystem::path _absolute = std::filesystem::absolute(a_path, _ec);
		if (_ec) return a_path;

		const std::wstring _str = _absolute.lexically_normal().make_preferred().wstring();
		constexpr std::wstring_view PREFIX = L"\\\\?\\";
		if (_str.starts_with(PREFIX)) return _str;

		// ネットワークのパス(\\server\share)は "\\?\UNC\server\share"
		if (_str.starts_with(L"\\\\")) return std::wstring(L"\\\\?\\UNC\\") + _str.substr(2);

		return std::wstring(PREFIX) + _str;
	}

	//--------------------------------------------------------------------------------------
	// 存在チェック
	//--------------------------------------------------------------------------------------

	/// <summary>ファイルが存在して開けるか</summary>
	inline bool IsExistFile(const std::string& a_filePath)
	{
		std::ifstream _file(a_filePath);
		return _file.is_open();
	}
	/// <summary>ファイルが存在して開けるか(ワイド文字列版)</summary>
	inline bool IsExistFile(const std::wstring& a_filePath)
	{
		std::ifstream _file(a_filePath);
		return _file.is_open();
	}

	//--------------------------------------------------------------------------------------
	// コピー処理
	//--------------------------------------------------------------------------------------

	inline bool CopyFile(
		const std::filesystem::path& a_src,
		const std::filesystem::path& a_destination
	)
	{
		std::error_code _error;

		if (!std::filesystem::is_regular_file(a_src, _error) || _error)
		{
			return false;
		}

		// ディレクトリ作成
		std::filesystem::create_directories(a_destination.parent_path(), _error);

		if (_error)
		{
			return false;
		}

		// コピー
		std::filesystem::copy_file(
			a_src,
			a_destination,
			std::filesystem::copy_options::overwrite_existing,
			_error
		);

		return !_error;
	}

	inline bool CopyDirectory(
		const std::filesystem::path& a_src,
		const std::filesystem::path& a_destination
	)
	{
		std::error_code _error;

		if (!std::filesystem::is_directory(a_src, _error) || _error)
		{
			return false;
		}

		std::filesystem::create_directories(a_destination,_error);

		if (_error)
		{
			return false;
		}

		std::filesystem::copy(
			a_src,
			a_destination,
			std::filesystem::copy_options::recursive |
			std::filesystem::copy_options::overwrite_existing,
			_error
		);

		return !_error;
	}
}
