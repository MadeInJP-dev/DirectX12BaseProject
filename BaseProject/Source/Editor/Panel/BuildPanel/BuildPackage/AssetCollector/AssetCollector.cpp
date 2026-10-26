#include "AssetCollector.h"

#include "../../BuildContext.h"
#include "../../BuildSettings.h"
#include "../../Converter/AssetConverterRegistry.h"

#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

namespace Editor::Build
{
	namespace
	{
		// 中に他のアセットへの参照を持たない種別 : 中身を読まない(テクスチャやメッシュは大きい)
		constexpr std::array<std::string_view, 6> NON_REFERENCE_TYPES =
		{
			"Texture", "Mesh", "Animation", "Shader", "Sound", "Font"
		};

		// 参照を拾うソースコード
		constexpr std::array<std::string_view, 3> SOURCE_CODE_EXTENSIONS = { ".h", ".cpp", ".inl" };

		// これより大きいファイルは参照を持たない生データとみなして読まない
		constexpr uintmax_t MAX_SCAN_FILE_SIZE = 256ull * 1024ull * 1024ull;

		// GUID の文字列の長さ("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx")
		constexpr size_t GUID_STRING_LENGTH = 36;

		// パスの書き始め
		constexpr std::string_view ASSET_PATH_PREFIX = "Asset/";

		bool StartsWith(std::string_view a_str, std::string_view a_prefix)
		{
			return a_str.size() >= a_prefix.size() && a_str.compare(0, a_prefix.size(), a_prefix) == 0;
		}

		bool IsHex(char a_c)
		{
			return (a_c >= '0' && a_c <= '9') || (a_c >= 'a' && a_c <= 'f') || (a_c >= 'A' && a_c <= 'F');
		}

		// a_text の a_pos から GUID の文字列が始まっているか
		bool IsGUIDString(std::string_view a_text, size_t a_pos)
		{
			if (a_pos + GUID_STRING_LENGTH > a_text.size()) return false;

			for (size_t _i = 0; _i < GUID_STRING_LENGTH; ++_i)
			{
				const char _c = a_text[a_pos + _i];
				const bool _isDash = (_i == 8 || _i == 13 || _i == 18 || _i == 23);
				if (_isDash ? (_c != '-') : !IsHex(_c)) return false;
			}
			return true;
		}

		// パスの終わりとみなす文字(JSON・C++ の文字列の区切りとエスケープ)
		bool IsPathTerminator(char a_c)
		{
			return a_c == '"' || a_c == '\'' || a_c == '\\' || a_c == '\r' || a_c == '\n' || a_c == '\t'
				|| a_c == '<' || a_c == '>' || a_c == '|' || a_c == '*' || a_c == '?';
		}

		std::string ToLower(std::string a_str)
		{
			std::transform(a_str.begin(), a_str.end(), a_str.begin(),
				[](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
			return a_str;
		}
	}

	std::vector<Core::GUID> AssetCollector::Collect(const BuildContext& a_context, const std::vector<Core::GUID>& a_recordedGUIDVec)
	{
		m_pContext = &a_context;
		m_pAssetDatabase = a_context.pAssetDatabase;
		m_allGUIDSet.clear();
		m_addedGUIDSet.clear();
		m_collectedGUIDVec.clear();
		m_scanQueue.clear();

		if (!m_pAssetDatabase || !a_context.pSettings) return {};
		const BuildSettings& _settings = *a_context.pSettings;

		for (const auto& [_type, _typeExt] : m_pAssetDatabase->GetAssetTypeExtensionsMap())
		{
			for (const auto& _prop : m_pAssetDatabase->GetTypeMetaVec(_type))
			{
				m_allGUIDSet.insert(_prop.guid);
			}
		}

		//------------------------------------------------------------------
		// 起点
		//------------------------------------------------------------------

		// 起動時に読む設定 : 最初のシーンやカーソルの絵を GUID で持っている
		for (const auto& _dir : _settings.dataDirectoryVec)
		{
			std::error_code _ec;
			const auto _path = Core::File::FromUTF8(_dir);
			if (!std::filesystem::is_directory(_path, _ec)) continue;

			for (const auto& _entry : std::filesystem::recursive_directory_iterator(
				_path, std::filesystem::directory_options::skip_permission_denied, _ec))
			{
				if (_entry.is_regular_file(_ec)) ScanFile(_entry.path());
			}
		}

		// ソースコードに直書きされた GUID とパス
		if (_settings.isScanSourceReferences)
		{
			std::error_code _ec;
			for (const auto& _entry : std::filesystem::recursive_directory_iterator(
				a_context.srcSourceDirectory, std::filesystem::directory_options::skip_permission_denied, _ec))
			{
				if (!_entry.is_regular_file(_ec)) continue;

				const std::string _ext = ToLower(_entry.path().extension().string());
				if (std::find(SOURCE_CODE_EXTENSIONS.begin(), SOURCE_CODE_EXTENSIONS.end(), _ext) == SOURCE_CODE_EXTENSIONS.end()) continue;

				std::string _text;
				if (ReadFile(_entry.path(), _text)) ScanText(_text);
			}
		}

		// このセッションで読み込まれたもの
		if (_settings.isIncludeRecordedAssets)
		{
			for (const auto& _guid : a_recordedGUIDVec) Add(_guid);
		}

		// 中身を全部入れるフォルダ
		for (const auto& _dir : _settings.alwaysIncludeDirectoryVec)
		{
			AddDirectory(Core::File::FromUTF8(_dir));
		}

		if (_settings.isIncludeAllScenes) AddType("Scene");

		//------------------------------------------------------------------
		// 辿る : 読んだ中身から見つかったものも列の後ろへ積まれる
		//------------------------------------------------------------------
		while (!m_scanQueue.empty())
		{
			const Core::GUID _guid = m_scanQueue.back();
			m_scanQueue.pop_back();

			if (const auto* _pProp = m_pAssetDatabase->GetAssetProperty(_guid))
			{
				ScanAsset(*_pProp);
			}
		}

		// 確認の一覧で探しやすいようパス順に
		std::sort(m_collectedGUIDVec.begin(), m_collectedGUIDVec.end(),
			[this](const Core::GUID& a_lhs, const Core::GUID& a_rhs)
			{
				return m_pAssetDatabase->GetBaseFilePathFromGUID(a_lhs) < m_pAssetDatabase->GetBaseFilePathFromGUID(a_rhs);
			});

		return m_collectedGUIDVec;
	}

	void AssetCollector::Add(const Core::GUID& a_guid)
	{
		// データベースに無いもの(消えたアセット・たまたま GUID の形をした値)は相手にしない
		if (!a_guid.IsValid() || !m_allGUIDSet.contains(a_guid)) return;
		if (!m_addedGUIDSet.insert(a_guid).second) return;

		const auto* _pProp = m_pAssetDatabase->GetAssetProperty(a_guid);
		if (!_pProp || IsExcluded(*_pProp)) return;

		m_collectedGUIDVec.push_back(a_guid);
		m_scanQueue.push_back(a_guid);
	}

	void AssetCollector::AddDirectory(const std::filesystem::path& a_dir)
	{
		for (const auto& [_type, _typeExt] : m_pAssetDatabase->GetAssetTypeExtensionsMap())
		{
			for (const auto& _prop : m_pAssetDatabase->GetTypeMetaVec(_type))
			{
				if (IsUnderDirectory(_prop.filePath, a_dir)) Add(_prop.guid);
			}
		}
	}

	void AssetCollector::AddType(const std::string& a_type)
	{
		for (const auto& _prop : m_pAssetDatabase->GetTypeMetaVec(a_type))
		{
			Add(_prop.guid);
		}
	}

	void AssetCollector::ScanAsset(const Engine::Resource::AssetProperty& a_prop)
	{
		if (std::find(NON_REFERENCE_TYPES.begin(), NON_REFERENCE_TYPES.end(), a_prop.type) != NON_REFERENCE_TYPES.end()) return;

		//------------------------------------------------------------------
		// 読むファイルの候補
		//
		// 書き出し直前のコンバートで出来たばかりのファイルは、アセットデータベースの
		// 拡張子一覧にまだ載っていない(監視が拾うのは次のフレーム)。
		// コンバーターの出力と、その対になる JSON も候補に入れて、実際にあるかで見る
		//------------------------------------------------------------------
		std::vector<std::string> _jsonExtVec = {};
		std::vector<std::string> _binaryExtVec = {};
		for (const auto& _ext : a_prop.extensionsVec)
		{
			if (StartsWith(_ext, ".oj")) _jsonExtVec.push_back(_ext);
			else if (StartsWith(_ext, ".ob")) _binaryExtVec.push_back(_ext);
		}
		if (const auto* _pConverter = AssetConverterRegistry::Find(a_prop.type))
		{
			const std::string _outExt = _pConverter->GetOutputExtension();
			if (StartsWith(_outExt, ".ob"))
			{
				_jsonExtVec.push_back(".oj" + _outExt.substr(3));
				_binaryExtVec.push_back(_outExt);
			}
		}

		// JSON を優先する(キー付きで、GUID が文字列で書いてある)
		for (const auto* _pExtVec : { &_jsonExtVec, &_binaryExtVec })
		{
			for (const auto& _ext : *_pExtVec)
			{
				const std::filesystem::path _path = a_prop.filePath + _ext;
				std::error_code _ec;
				if (!std::filesystem::is_regular_file(_path, _ec)) continue;

				ScanFile(_path);
				return;
			}
		}
	}

	void AssetCollector::ScanFile(const std::filesystem::path& a_path)
	{
		std::string _data;
		if (!ReadFile(a_path, _data)) return;

		if (StartsWith(ToLower(a_path.extension().string()), ".oj")) ScanText(_data);
		else ScanBinary(_data);
	}

	void AssetCollector::ScanText(std::string_view a_text)
	{
		// GUID の文字列
		for (size_t _pos = 0; _pos + GUID_STRING_LENGTH <= a_text.size();)
		{
			if (!IsGUIDString(a_text, _pos))
			{
				++_pos;
				continue;
			}

			Core::GUID _guid = {};
			_guid.FromString(std::string(a_text.substr(_pos, GUID_STRING_LENGTH)));
			Add(_guid);
			_pos += GUID_STRING_LENGTH;
		}

		// "Asset/〜.拡張子" のパス : 拡張子を外したパスでアセットデータベースを引く
		for (size_t _pos = a_text.find(ASSET_PATH_PREFIX); _pos != std::string_view::npos;
			_pos = a_text.find(ASSET_PATH_PREFIX, _pos + ASSET_PATH_PREFIX.size()))
		{
			// "MyAsset/" のような語の途中は拾わない
			if (_pos > 0 && (std::isalnum(static_cast<unsigned char>(a_text[_pos - 1])) || a_text[_pos - 1] == '_')) continue;

			size_t _end = _pos;
			while (_end < a_text.size() && !IsPathTerminator(a_text[_end])) ++_end;

			const std::string_view _candidate = a_text.substr(_pos, _end - _pos);

			// フォルダ(拡張子が無いもの)は飛ばす
			const size_t _slash = _candidate.find_last_of('/');
			if (_candidate.find('.', _slash) == std::string_view::npos) continue;

			// 文字列は UTF-8、アセットデータベースのパスは ANSI なので、パスを通して揃える。
			// ANSI で書けない文字のパスはデータベースにも載らないので飛ばす(.string() が投げる)
			try
			{
				const std::filesystem::path _path = Core::File::FromUTF8(std::string(_candidate));
				Add(m_pAssetDatabase->GetGUIDFromFilePath(_path.string()));
			}
			catch (const std::system_error&)
			{
			}
		}
	}

	void AssetCollector::ScanBinary(std::string_view a_data)
	{
		constexpr size_t GUID_SIZE = sizeof(UUID);
		if (a_data.size() < GUID_SIZE) return;

		// 書いてある位置に決まりは無いので、1バイトずつずらして突き合わせる
		Core::GUID _guid = {};
		for (size_t _pos = 0; _pos + GUID_SIZE <= a_data.size(); ++_pos)
		{
			std::memcpy(&_guid.value, a_data.data() + _pos, GUID_SIZE);
			if (m_allGUIDSet.contains(_guid)) Add(_guid);
		}
	}

	bool AssetCollector::ReadFile(const std::filesystem::path& a_path, std::string& a_outData)
	{
		std::error_code _ec;
		const uintmax_t _size = std::filesystem::file_size(a_path, _ec);
		if (_ec || _size > MAX_SCAN_FILE_SIZE) return false;

		std::ifstream _ifs(a_path, std::ios::binary);
		if (!_ifs.is_open()) return false;

		a_outData.resize(static_cast<size_t>(_size));
		_ifs.read(a_outData.data(), static_cast<std::streamsize>(_size));
		a_outData.resize(static_cast<size_t>(_ifs.gcount()));
		return true;
	}

	bool AssetCollector::IsExcluded(const Engine::Resource::AssetProperty& a_prop) const
	{
		const BuildSettings& _settings = *m_pContext->pSettings;

		if (std::find(_settings.excludeTypeVec.begin(), _settings.excludeTypeVec.end(), a_prop.type) != _settings.excludeTypeVec.end())
		{
			return true;
		}

		for (const auto& _dir : _settings.excludeDirectoryVec)
		{
			if (IsUnderDirectory(a_prop.filePath, Core::File::FromUTF8(_dir))) return true;
		}
		return false;
	}

	bool AssetCollector::IsUnderDirectory(const std::string& a_filePath, const std::filesystem::path& a_dir)
	{
		// どちらも "/" 区切りに揃え、フォルダ名の途中で一致しないよう末尾に "/" を付けて比べる
		std::string _dir = ToLower(a_dir.lexically_normal().generic_string());
		if (_dir.empty()) return false;
		if (_dir.back() != '/') _dir += '/';

		const std::string _filePath = ToLower(std::filesystem::path(a_filePath).lexically_normal().generic_string());
		return StartsWith(_filePath, _dir);
	}
}
