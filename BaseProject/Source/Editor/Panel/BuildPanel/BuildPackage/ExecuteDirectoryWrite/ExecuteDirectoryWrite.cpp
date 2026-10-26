#include "ExecuteDirectoryWrite.h"

#include "../../BuildContext.h"
#include "../../BuildSettings.h"
#include "../BuildPlan/BuildPlan.h"
#include "../../Converter/AssetConverterRegistry.h"

#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

#include <chrono>

// .assetmeta とエンジン設定を書き換えるので JSON の実体を触る
#pragma warning(push, 0)
#include <nlohmannJSON/json.hpp>
#pragma warning(pop)

namespace Editor::Build
{
	namespace
	{
		// メタファイルの拡張子(MainEngine::InitializeAssetDatabase で決めているもの)
		constexpr const char* META_EXTENSION = ".assetmeta";

		// エンジン設定のファイル名と、ビルドモードの保存キー(OptionManager / BuildConfig::Archive)
		constexpr const char* ENGINE_DATA_FILE_NAME = "EngineData.ojoptn";
		constexpr const char* BUILD_CONFIG_GROUP = "BuildConfig";
		constexpr const char* BUILD_MODE_KEY = "buildMode";

		// 保存は JSON とバイナリを続けて書くので、書いた順のぶんだけ JSON が新しく見えることがある
		constexpr auto SAVE_TOLERANCE = std::chrono::seconds(2);

		// 参照を拾うソースコード(exe が古くないかの目安)
		constexpr std::array<std::string_view, 3> SOURCE_CODE_EXTENSIONS = { ".h", ".cpp", ".inl" };

		bool StartsWith(std::string_view a_str, std::string_view a_prefix)
		{
			return a_str.size() >= a_prefix.size() && a_str.compare(0, a_prefix.size(), a_prefix) == 0;
		}

		std::string ToLower(std::string a_str)
		{
			std::transform(a_str.begin(), a_str.end(), a_str.begin(),
				[](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
			return a_str;
		}

		bool ContainsIgnoreCase(const std::vector<std::string>& a_vec, const std::string& a_value)
		{
			const std::string _value = ToLower(a_value);
			for (const auto& _item : a_vec)
			{
				if (ToLower(_item) == _value) return true;
			}
			return false;
		}

		// 注意の文に出すアセットの名前
		std::string ToLabel(const Engine::Resource::AssetProperty& a_prop)
		{
			return "[" + a_prop.type + "] " + Core::File::ToUTF8(std::filesystem::path(a_prop.filePath));
		}

		// ディレクトリ以下のソースコードで一番新しい更新時刻
		std::filesystem::file_time_type GetNewestSourceTime(const std::filesystem::path& a_dir)
		{
			auto _newest = std::filesystem::file_time_type::min();

			std::error_code _ec;
			for (const auto& _entry : std::filesystem::recursive_directory_iterator(
				a_dir, std::filesystem::directory_options::skip_permission_denied, _ec))
			{
				if (!_entry.is_regular_file(_ec)) continue;

				const std::string _ext = ToLower(_entry.path().extension().string());
				if (std::find(SOURCE_CODE_EXTENSIONS.begin(), SOURCE_CODE_EXTENSIONS.end(), _ext) == SOURCE_CODE_EXTENSIONS.end()) continue;

				const auto _time = _entry.last_write_time(_ec);
				if (!_ec && _time > _newest) _newest = _time;
			}
			return _newest;
		}
	}

	bool ExecuteDirectoryWrite::Build(const BuildContext& a_context, BuildPlan& a_plan)
	{
		const bool _isSuccess = CopyExe(a_context, a_plan);
		CopyAsset(a_context, a_plan);
		CopyData(a_context, a_plan);
		return _isSuccess;
	}

	bool ExecuteDirectoryWrite::CopyExe(const BuildContext& a_context, BuildPlan& a_plan)
	{
		const BuildSettings& _settings = *a_context.pSettings;

		// ビルド済みのものをコピーするのみなので実行済みエグゼを探す
		std::error_code _ec;
		if (!std::filesystem::is_regular_file(a_context.srcExePath, _ec))
		{
			a_plan.AddWarning("実行ファイルが見つかりません(Release でビルドしてください) : " + Core::File::ToUTF8(a_context.srcExePath));
			return false;
		}

		// 置くときの名前 : 拡張子を書き忘れても exe にする
		std::filesystem::path _exeName = _settings.exeName.empty()
			? a_context.srcExePath.filename()
			: Core::File::FromUTF8(_settings.exeName);
		if (!_exeName.has_extension()) _exeName += ".exe";

		a_plan.AddCopyFile(a_context.srcExePath, a_context.executeDirectory / _exeName);

		// ソースより古い exe は、最後の変更が入っていない
		const auto _exeTime = std::filesystem::last_write_time(a_context.srcExePath, _ec);
		if (!_ec && GetNewestSourceTime(a_context.srcSourceDirectory) > _exeTime)
		{
			a_plan.AddWarning("実行ファイルがソースより古いです。ビルドし直してから書き出してください : " + Core::File::ToUTF8(a_context.srcExePath));
		}

		// exe と同じフォルダの dll(assimp・dxcompiler など)
		if (_settings.isCopyDll)
		{
			for (const auto& _entry : std::filesystem::directory_iterator(a_context.srcExePath.parent_path(), _ec))
			{
				if (!_entry.is_regular_file(_ec)) continue;
				if (ToLower(_entry.path().extension().string()) != ".dll") continue;

				a_plan.AddCopyFile(_entry.path(), a_context.executeDirectory / _entry.path().filename());
			}
		}

		// 一緒に置くもの(説明書など) : フォルダならフォルダごと
		for (const auto& _extra : _settings.extraFileVec)
		{
			const std::filesystem::path _path = Core::File::FromUTF8(_extra);
			const std::filesystem::path _dst = a_context.executeDirectory / _path.filename();

			if (a_plan.AddCopyFile(_path, _dst)) continue;
			if (a_plan.AddCopyDirectory(_path, _dst)) continue;

			a_plan.AddWarning("一緒に置くファイルが見つかりません : " + _extra);
		}

		return true;
	}

	void ExecuteDirectoryWrite::CopyAsset(const BuildContext& a_context, BuildPlan& a_plan)
	{
		if (!a_context.pAssetDatabase) return;

		for (const auto& _guid : a_context.assetGUIDVec)
		{
			const auto* _pProp = a_context.pAssetDatabase->GetAssetProperty(_guid);
			if (!_pProp) continue;

			const auto* _pConverter = AssetConverterRegistry::Find(_pProp->type);
			if (!_pConverter)
			{
				a_plan.AddWarning(ToLabel(*_pProp) + " : 書き出し方の決まっていない種別なので入れません");
				continue;
			}

			const std::vector<std::string> _extVec = SelectRuntimeExtensions(a_context, *_pProp, *_pConverter);
			if (_extVec.empty())
			{
				a_plan.AddWarning(ToLabel(*_pProp) + " : 実行用のファイル(" + _pConverter->GetOutputExtension() + ")が無いので入れません。コンバートしてください");
				continue;
			}

			if (_pConverter->IsOutdated(*_pProp))
			{
				a_plan.AddWarning(ToLabel(*_pProp) + " : JSON の方が新しいです(バイナリが古い)。開いて保存し直してください");
			}

			for (const auto& _ext : _extVec)
			{
				const std::filesystem::path _path = _pProp->filePath + _ext;
				a_plan.AddCopyFile(_path, a_context.executeDirectory / _path);
			}

			WriteMetaFile(a_context, *_pProp, _extVec, a_plan);
		}
	}

	void ExecuteDirectoryWrite::CopyData(const BuildContext& a_context, BuildPlan& a_plan)
	{
		const BuildSettings& _settings = *a_context.pSettings;

		for (const auto& _dir : _settings.dataDirectoryVec)
		{
			const std::filesystem::path _dirPath = Core::File::FromUTF8(_dir);

			// 置き場は作業ディレクトリからの相対位置をそのまま使うので、絶対パスは受けない
			if (_dirPath.is_absolute())
			{
				a_plan.AddWarning("Data のフォルダは作業ディレクトリからの相対パスで書いてください : " + _dir);
				continue;
			}

			std::error_code _ec;
			if (!std::filesystem::is_directory(_dirPath, _ec))
			{
				a_plan.AddWarning("Data のフォルダが見つかりません : " + _dir);
				continue;
			}

			for (const auto& _entry : std::filesystem::recursive_directory_iterator(
				_dirPath, std::filesystem::directory_options::skip_permission_denied, _ec))
			{
				if (!_entry.is_regular_file(_ec)) continue;

				const std::filesystem::path& _path = _entry.path();
				const std::string _ext = ToLower(_path.extension().string());
				const bool _isBinary = StartsWith(_ext, ".ob");
				if (!_isBinary && !ContainsIgnoreCase(_settings.jsonKeepExtensionVec, _ext)) continue;

				// エンジン設定はビルドモードを書き換えて置く
				if (_settings.executeBuildMode >= 0 && _path.filename() == ENGINE_DATA_FILE_NAME)
				{
					WriteEngineData(a_context, _path, a_plan);
					continue;
				}

				a_plan.AddCopyFile(_path, a_context.executeDirectory / _path);

				// 対になる JSON の方が新しければ、バイナリへ反映されていない
				if (_isBinary)
				{
					std::filesystem::path _jsonPath = _path;
					_jsonPath.replace_extension(".oj" + _ext.substr(3));

					std::error_code _timeEc;
					const auto _jsonTime = std::filesystem::last_write_time(_jsonPath, _timeEc);
					const auto _binaryTime = _entry.last_write_time(_ec);
					if (!_timeEc && !_ec && _jsonTime > _binaryTime + SAVE_TOLERANCE)
					{
						a_plan.AddWarning("JSON の方が新しいです(バイナリが古い)。保存し直してください : " + Core::File::ToUTF8(_path));
					}
				}
			}
		}
	}

	std::vector<std::string> ExecuteDirectoryWrite::SelectRuntimeExtensions(
		const BuildContext& a_context,
		const Engine::Resource::AssetProperty& a_prop,
		const IAssetConverter& a_converter
	) const
	{
		std::error_code _ec;

		// 変換するもの : 変換後のファイルだけ
		const std::string _outExt = a_converter.GetOutputExtension();
		if (!_outExt.empty())
		{
			if (std::filesystem::is_regular_file(a_prop.filePath + _outExt, _ec)) return { _outExt };
			return {};
		}

		// そのまま読むもの : 置いてあるもののうち、開発用の JSON 以外(JSON 固定で読むものは置く)
		std::vector<std::string> _extVec = {};
		for (const auto& _ext : a_prop.extensionsVec)
		{
			if (!std::filesystem::is_regular_file(a_prop.filePath + _ext, _ec)) continue;
			if (StartsWith(ToLower(_ext), ".oj") && !ContainsIgnoreCase(a_context.pSettings->jsonKeepExtensionVec, _ext)) continue;

			_extVec.push_back(_ext);
		}
		return _extVec;
	}

	void ExecuteDirectoryWrite::WriteMetaFile(
		const BuildContext& a_context,
		const Engine::Resource::AssetProperty& a_prop,
		const std::vector<std::string>& a_extensionVec,
		BuildPlan& a_plan
	)
	{
		const std::filesystem::path _metaPath = a_prop.filePath + META_EXTENSION;

		// 元のメタファイルを土台にする(GUID と Type はそのまま引き継ぐ)
		nlohmann::json _json = nlohmann::json::object();
		{
			std::ifstream _ifs(_metaPath);
			if (_ifs.is_open())
			{
				try
				{
					_ifs >> _json;
				}
				catch (const nlohmann::json::parse_error&)
				{
					_json = nlohmann::json::object();
				}
			}
		}
		_json["GUID"] = a_prop.guid.String();
		_json["Type"] = a_prop.type;

		// 置いたファイルだけを載せる : 実行時は起動時に実際のファイルから作り直されるが、食い違いを残さない
		auto _array = nlohmann::json::array();
		for (const auto& _ext : a_extensionVec) _array.push_back(_ext);
		_json["Files"] = _array;

		a_plan.AddWriteText(a_context.executeDirectory / _metaPath, _json.dump(4));
	}

	void ExecuteDirectoryWrite::WriteEngineData(const BuildContext& a_context, const std::filesystem::path& a_src, BuildPlan& a_plan)
	{
		const std::filesystem::path _dst = a_context.executeDirectory / a_src;

		nlohmann::json _json;
		{
			std::ifstream _ifs(a_src);
			try
			{
				if (_ifs.is_open()) _ifs >> _json;
			}
			catch (const nlohmann::json::parse_error&)
			{
				_json = nullptr;
			}
		}

		if (!_json.is_object())
		{
			a_plan.AddWarning("エンジン設定が読めないので、ビルドモードを書き換えずに置きます : " + Core::File::ToUTF8(a_src));
			a_plan.AddCopyFile(a_src, _dst);
			return;
		}

		_json[BUILD_CONFIG_GROUP][BUILD_MODE_KEY] = a_context.pSettings->executeBuildMode;
		a_plan.AddWriteText(_dst, _json.dump(4));
	}
}
