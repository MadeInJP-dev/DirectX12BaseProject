#pragma once

#include "Core/BinaryHelper/BinaryHelper.h"

// json.hpp ではなく前方宣言だけを読む。
// Archive.h はプリコンパイル済みヘッダーに入っているので、ここで json.hpp を
// 読むと 900KB のヘッダーが全翻訳単位に乗る。
// JSON を実際に触るのは Archive.cpp 側(下の Json〜 関数)
#include "Core/JSONHelper/JSONForward.h"

namespace Engine::Persistence
{
	// テスト時にはjson、本番時にはバイナリでデータを管理できるクラス
	// アーカイブ方式で作成
	class Archive
	{
	public:

		//----------------------------------------------------------------------------------
		// フォーマット指定用の列挙型
		//
		// Auto が起動設定の isLoadJsonData を見る。中身は ShouldLoadJson() を参照。
		//   OFF : 必ずバイナリ(.ob)
		//   ON  : .oj があればそちら(重いデータを除く)
		// 強制指定は起動設定を無視するので、設定に関係なく形式を固定したいとき
		// (エンジン設定のように .ob が出来る前から読む必要があるもの)だけに使う。
		//----------------------------------------------------------------------------------
		enum class EArchiveFormat
		{
			Auto,	// 基本設定（起動設定の isLoadJsonData に依存）
			Binary,	// 強制的にバイナリ(.ob)を使用
			Json	// 強制的にJSON(.oj)を使用
		};

		enum class EMode
		{
			Save,		// 書き込み
			Load		// 読み込み
		};

		// モード、ファイルディレクトリを指定して開く
		Archive(
			EMode a_mode, 
			const std::string& a_fileDir,
			const std::string& a_fileName,
			const std::string& a_ext,
			EArchiveFormat a_format = EArchiveFormat::Auto
		);
		//----------------------------------------------------------------------------------
		// メモリ上のJSONだけを相手にするアーカイブ
		//
		// ファイルには一切触らない。「書いてすぐ読み直す」用途
		// (アセットの複製など)のために用意している。
		//   Save : a_json へ書き出す(デストラクタで反映)
		//   Load : a_json から読み込む
		//----------------------------------------------------------------------------------
		Archive(EMode a_mode, nlohmann::json& a_json);

		// クローズ処理を実行
		~Archive();

		// 現在の実行モード
		bool IsSaving() const { return m_mode == EMode::Save; }
		bool IsLoading() const { return m_mode == EMode::Load; }

		// 基本型のシリアライズ : 構造体はしないように
		template<typename T>
		void Field(const std::string& a_name,T& a_data);

		/// <summary>
		/// char型のシリアライズ関数
		/// </summary>
		template<size_t N>
		void Field(const std::string& a_name, char(&a_value)[N]);

		// 文字列型のシリアライズ
		void StringField(const std::string& a_name, std::string& a_data);

		// ベクターのシリアライズ
		template<typename T>
		void VectorField(const std::string& a_name,std::vector<T>& a_data);

		// 構造体のグループ化（JSON の {} ）
		bool BeginGroup(const std::string& a_name);
		void EndGroup();

		// 配列の管理（JSON の [] ）
		bool BeginArray(const std::string& a_name, size_t& a_size);
		void EndArray();

		// 配列の中の1要素としてのオブジェクト（名前なしの {} ）
		bool BeginObject(size_t a_index = 0);
		void EndObject();

		//----------------------------------------------------------------------------------
		// 区切り(セクション)
		//
		// バイナリは「書いた順に並べるだけ」なので、継承したクラスの基底へフィールドを
		// 1つ足すと、後ろに続く派生のぶんが全部ずれる。
		// 区切りの頭に中身の長さを書いておき、
		//   ・読み手の知らない後ろのフィールドは読み飛ばす(古いコードで新しいデータ)
		//   ・区切りの終わりまで読んだら、それ以上は読まず既定値のまま残す(新しいコードで古いデータ)
		// ようにしてある。区切りの中で末尾へ足していく限り、他の区切りの読み出しは崩れない。
		//
		// ・JSON はキーで引くので何もしない(.oj* の中身は変わらない)
		// ・区切りを持たない古い .ob* は頭の目印で見分け、従来どおり並び順で読む
		// ・入れ子にしてよい。内側の区切りは外側の終わりを越えて読まない
		//
		// 使うときは ArchiveSection(下)で囲む。Begin/End を自分で対にしなくて済む
		//----------------------------------------------------------------------------------
		void BeginSection(std::string_view a_name);
		void EndSection();

		/// <summary>
		/// 区切りを持たない古いバイナリを読んでいる最中か
		/// </summary>
		/// <remarks>
		/// 古いデータには区切りの長さが無いので、終わりがどこか分からない。
		/// 区切りを入れた後に足したフィールドをそこで読むと、後ろのデータを食ってずれる。
		/// 後から足したフィールドは、これが true の間は読まないこと
		///
		///     if (!a_ar.IsLegacyLayout()) a_ar.Field("NewValue", m_newValue);
		///
		/// 保存中・JSON・長さを持つ区切りの中では false
		/// </remarks>
		bool IsLegacyLayout() const
		{
			return IsLoading() && m_ifs.is_open() && !m_sectionVec.empty() && !m_sectionVec.back().hasOwnLength;
		}

		// GUID用
		void GUIDField(const std::string& a_name,Core::GUID& a_guid);
		void GUIDVectorField(const std::string& a_name, std::vector<Core::GUID>& a_guid);

		// モード取得
		EMode GetMode() const { return m_mode; }

		//----------------------------------------------------------------------------------
		// この拡張子が「重いデータ」かどうか
		//
		// モデル・メッシュ・アニメーションは頂点や行列がそのまま並ぶので、
		// JSON にすると桁違いに大きく・遅くなる。中身を手で書き換えることも無いため、
		// Development でもバイナリのままにしておく。
		//----------------------------------------------------------------------------------
		static bool IsHeavyDataExtension(const std::string& a_ext);

	private:

		// Auto のときに、このファイルを JSON で読むかどうかを決める
		bool ShouldLoadJson(const std::string& a_ext) const;

		//----------------------------------------------------------------------------------
		// JSON への1フィールドの読み書き
		//
		// 下のテンプレートから nlohmann::json を直接触ると json.hpp をこのヘッダーへ
		// 持ち込むことになるので、JSON に触る処理はすべて .cpp 側に置いてある。
		//
		// 整数・符号なし整数・実数・真偽を分けているのは、まとめて double で通すと
		// 保存済みの .oj* に書かれる数値の形(10 が 10.0 になる等)が変わってしまうため。
		// Read 系は「読めたら true」で、読めなければ呼び出し側の値をそのまま残す
		//----------------------------------------------------------------------------------
		bool HasJson() const;	// JSON を相手にしているか

		// 現在注目しているノードを取り出す(スタックが空なら根)
		nlohmann::json& CurrentNode();

		void JsonWriteBool  (const std::string& a_name, bool a_value);
		void JsonWriteInt   (const std::string& a_name, int64_t a_value);
		void JsonWriteUInt  (const std::string& a_name, uint64_t a_value);
		void JsonWriteFloat (const std::string& a_name, double a_value);
		void JsonWriteString(const std::string& a_name, const std::string& a_value);
		void JsonWriteFloats(const std::string& a_name, const float* a_pValues, size_t a_count);

		bool JsonReadBool  (const std::string& a_name, bool& a_outValue);
		bool JsonReadInt   (const std::string& a_name, int64_t& a_outValue);
		bool JsonReadUInt  (const std::string& a_name, uint64_t& a_outValue);
		bool JsonReadFloat (const std::string& a_name, double& a_outValue);
		bool JsonReadString(const std::string& a_name, std::string& a_outValue);
		bool JsonReadFloats(const std::string& a_name, float* a_pOutValues, size_t a_count);

		/// <summary>バイナリをまだ読んでよいか</summary>
		/// <remarks>
		/// 開いていて、かつ今の区切りの終わりに達していないときだけ true。
		/// 読み込み側の各フィールドはストリームを直接見ずにここを通すこと
		/// (でないと区切りの終わりを越えて、次の区切りの中身を読んでしまう)
		/// </remarks>
		bool CanReadBinary();

	private:

		//----------------------------------------------------------------------------------
		// 開いている区切り1つぶん
		//----------------------------------------------------------------------------------
		struct SectionState
		{
			std::string name = {};			// ログ用

			std::streamoff lengthPos = 0;	// 保存 : 長さを後から書き戻す位置

			std::streamoff end = 0;			// 読み込み : ここより先は読まない
			bool hasEnd = false;			// end が効いているか(区切りの無い古いデータで、親も無ければ false)
			bool hasOwnLength = false;		// 自分の長さを読めたか(閉じるときに end まで飛ばすのはこのときだけ)
		};

		// 実行モード
		EMode m_mode;

		EArchiveFormat m_format = EArchiveFormat::Auto;

		// 本番用ストリーム
		std::ofstream m_ofs;
		std::ifstream m_ifs;

		// テスト時データ
		// 実体はコンストラクタで作る。json.hpp を読まずに持つためポインタで抱える
		std::unique_ptr<nlohmann::json> m_upJson;

		// パス
		std::string m_fileDir;		// ディレクトリ
		std::string m_binPath;
		std::string m_jsonPath;

		// メモリ上のJSONだけを相手にしているか : true ならファイルへは書かない
		bool m_isMemory = false;
		nlohmann::json* m_pMemoryJson = nullptr;		// Save のときの書き出し先

		// 現在注目しているJSONノードのポインタ（参照）をスタックで管理する
		std::stack<nlohmann::json*> m_jsonNodeStack;

		// 開いている区切り(内側ほど後ろ)
		std::vector<SectionState> m_sectionVec;

	};

	//======================================================================================
	// 区切りを開いて、スコープを抜けるときに閉じる
	//
	//     void Foo::Archive(Archive& a_ar, ...)
	//     {
	//         Base::Archive(a_ar, ...);
	//
	//         Engine::Persistence::ArchiveSection _section(a_ar, "Foo");
	//         a_ar.Field("Value", m_value);	// Foo のぶんは必ずこの区切りの末尾へ足す
	//     }
	//======================================================================================
	class ArchiveSection
	{
	public:

		ArchiveSection(Archive& a_ar, std::string_view a_name) : m_ar(a_ar) { m_ar.BeginSection(a_name); }
		~ArchiveSection() { m_ar.EndSection(); }

		ArchiveSection(const ArchiveSection&) = delete;
		ArchiveSection& operator=(const ArchiveSection&) = delete;

	private:

		Archive& m_ar;
	};

	// =========================================================================
	// フィールド処理
	// =========================================================================
	template<typename T>
	inline void Archive::Field(const std::string& a_name, T& a_data)
	{
		static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>,
			"Only arithmetic or enum types are allowed");

		// enum は下地の型で扱う(保存される数値の形を変えないため)
		using Raw = typename std::conditional_t<std::is_enum_v<T>, std::underlying_type<T>, std::type_identity<T>>::type;

		if (IsSaving())
		{
			if constexpr (std::is_same_v<Raw, bool>)			JsonWriteBool(a_name, static_cast<bool>(a_data));
			else if constexpr (std::is_floating_point_v<Raw>)	JsonWriteFloat(a_name, static_cast<double>(a_data));
			else if constexpr (std::is_signed_v<Raw>)			JsonWriteInt(a_name, static_cast<int64_t>(a_data));
			else												JsonWriteUInt(a_name, static_cast<uint64_t>(a_data));

			if (m_ofs.is_open())
			{
				BinaryHelper::Write(m_ofs, a_data);
			}
		}
		else
		{
			if constexpr (std::is_same_v<Raw, bool>)
			{
				bool _value = false;
				if (JsonReadBool(a_name, _value)) a_data = static_cast<T>(_value);
			}
			else if constexpr (std::is_floating_point_v<Raw>)
			{
				double _value = 0.0;
				if (JsonReadFloat(a_name, _value)) a_data = static_cast<T>(_value);
			}
			else if constexpr (std::is_signed_v<Raw>)
			{
				int64_t _value = 0;
				if (JsonReadInt(a_name, _value)) a_data = static_cast<T>(_value);
			}
			else
			{
				uint64_t _value = 0;
				if (JsonReadUInt(a_name, _value)) a_data = static_cast<T>(_value);
			}

			if (CanReadBinary())
			{
				BinaryHelper::Read(m_ifs, a_data);
			}
		}
	}
	template<size_t N>
	inline void Archive::Field(const std::string& a_name, char(&a_value)[N])
	{
		// セーブ時
		if (IsSaving())
		{
			// char配列から std::string に変換して既存の保存処理に流す
			std::string _tempStr(a_value);
			Field(a_name, _tempStr);
		}
		// ロード時
		else
		{
			// 一旦 std::string として読み込む
			std::string tempStr;
			Field(a_name, tempStr);

			// 安全にコピーし、終端文字を必ず入れる
			size_t copied = tempStr.copy(a_value, N - 1);
			a_value[copied] = '\0';
		}
	}
	//======================================================================================
	// 自作数学型(Math::～)
	//--------------------------------------------------------------------------------------
	// ECS のコンポーネントはこちらを使う。
	// JSON の並びもバイナリのバイト数も XMFLOAT系とまったく同じにしてあるので、
	// XMFLOAT3 → Math::Vector3 のように差し替えても既存の .ob* / .oj* はそのまま読める。
	//======================================================================================
	template<>
	inline void Archive::Field(const std::string& a_name, Math::Vector2& a_data)
	{
		if (IsSaving())
		{
			const float _values[2] = { a_data.x, a_data.y };
			JsonWriteFloats(a_name, _values, 2);
			if (m_ofs.is_open()) BinaryHelper::Write(m_ofs, a_data);
		}
		else
		{
			float _values[2] = { a_data.x, a_data.y };
			if (JsonReadFloats(a_name, _values, 2))
			{
				a_data = { _values[0], _values[1] };
			}
			if (CanReadBinary()) BinaryHelper::Read(m_ifs, a_data);
		}
	}
	template<>
	inline void Archive::Field(const std::string& a_name, Math::Vector3& a_data)
	{
		if (IsSaving())
		{
			const float _values[3] = { a_data.x, a_data.y, a_data.z };
			JsonWriteFloats(a_name, _values, 3);
			if (m_ofs.is_open()) BinaryHelper::Write(m_ofs, a_data);
		}
		else
		{
			float _values[3] = { a_data.x, a_data.y, a_data.z };
			if (JsonReadFloats(a_name, _values, 3))
			{
				a_data = { _values[0], _values[1], _values[2] };
			}
			if (CanReadBinary()) BinaryHelper::Read(m_ifs, a_data);
		}
	}
	//--------------------------------------------------------------------------------------
	// DirectX の境界ボリューム(BoundingBox など)が持つ XMFLOAT3 用。
	//
	// 保存するのは Math 型だけにしたいが、当たり判定の形状はライブラリの型を
	// そのまま使っているのでメンバが XMFLOAT3 のまま残る。
	// ここで Math::Vector3 へ橋渡しする(並びは同じなのでファイルの中身は変わらない)
	//--------------------------------------------------------------------------------------
	template<>
	inline void Archive::Field(const std::string& a_name, DirectX::XMFLOAT3& a_data)
	{
		Math::Vector3 _value = a_data;
		Field(a_name, _value);

		// ロード時に読み取った値を書き戻す(セーブ時は同じ値が入るだけ)
		a_data = _value;
	}

	template<>
	inline void Archive::Field(const std::string& a_name, Math::Vector4& a_data)
	{
		if (IsSaving())
		{
			const float _values[4] = { a_data.x, a_data.y, a_data.z, a_data.w };
			JsonWriteFloats(a_name, _values, 4);
			if (m_ofs.is_open()) BinaryHelper::Write(m_ofs, a_data);
		}
		else
		{
			float _values[4] = { a_data.x, a_data.y, a_data.z, a_data.w };
			if (JsonReadFloats(a_name, _values, 4))
			{
				a_data = { _values[0], _values[1], _values[2], _values[3] };
			}
			if (CanReadBinary()) BinaryHelper::Read(m_ifs, a_data);
		}
	}
	template<>
	inline void Archive::Field(const std::string& a_name, Math::Quaternion& a_data)
	{
		if (IsSaving())
		{
			const float _values[4] = { a_data.x, a_data.y, a_data.z, a_data.w };
			JsonWriteFloats(a_name, _values, 4);
			if (m_ofs.is_open()) BinaryHelper::Write(m_ofs, a_data);
		}
		else
		{
			float _values[4] = { a_data.x, a_data.y, a_data.z, a_data.w };
			if (JsonReadFloats(a_name, _values, 4))
			{
				a_data = { _values[0], _values[1], _values[2], _values[3] };
			}
			if (CanReadBinary()) BinaryHelper::Read(m_ifs, a_data);
		}
	}
	template<>
	inline void Archive::Field(const std::string& a_name, Math::Color& a_data)
	{
		if (IsSaving())
		{
			const float _values[4] = { a_data.r, a_data.g, a_data.b, a_data.a };
			JsonWriteFloats(a_name, _values, 4);
			if (m_ofs.is_open()) BinaryHelper::Write(m_ofs, a_data);
		}
		else
		{
			float _values[4] = { a_data.r, a_data.g, a_data.b, a_data.a };
			if (JsonReadFloats(a_name, _values, 4))
			{
				a_data = { _values[0], _values[1], _values[2], _values[3] };
			}
			if (CanReadBinary()) BinaryHelper::Read(m_ifs, a_data);
		}
	}
	template<>
	inline void Archive::Field(const std::string& a_name, Math::Matrix& a_data)
	{
		if (IsSaving())
		{
			const float _values[16] = { a_data._11, a_data._12, a_data._13, a_data._14, a_data._21, a_data._22, a_data._23, a_data._24, a_data._31, a_data._32, a_data._33, a_data._34, a_data._41, a_data._42, a_data._43, a_data._44 };
			JsonWriteFloats(a_name, _values, 16);
			if (m_ofs.is_open()) BinaryHelper::Write(m_ofs, a_data);
		}
		else
		{
			float _values[16] = { a_data._11, a_data._12, a_data._13, a_data._14, a_data._21, a_data._22, a_data._23, a_data._24, a_data._31, a_data._32, a_data._33, a_data._34, a_data._41, a_data._42, a_data._43, a_data._44 };
			if (JsonReadFloats(a_name, _values, 16))
			{
				a_data = { _values[0], _values[1], _values[2], _values[3], _values[4], _values[5], _values[6], _values[7], _values[8], _values[9], _values[10], _values[11], _values[12], _values[13], _values[14], _values[15] };
			}
			if (CanReadBinary()) BinaryHelper::Read(m_ifs, a_data);
		}
	}

	// 文字列
	template<>
	inline void Archive::Field(const std::string& a_name, std::string& a_data)
	{
		// セーブ時
		if (IsSaving())
		{
			// Json処理
			if (HasJson()) JsonWriteString(a_name, a_data);
			// binary処理
			if (m_ofs.is_open()) BinaryHelper::WriteString(m_ofs, a_data);
		}
		// ロード時
		else
		{
			// Json処理
			JsonReadString(a_name, a_data);
			// binary処理
			if (CanReadBinary()) a_data = BinaryHelper::ReadString(m_ifs);
		}
	}
	// GUID
	template<>
	inline void Archive::Field(const std::string& a_name, Core::GUID& a_data)
	{
		// セーブ時
		if (IsSaving())
		{
			// Json処理
			if (HasJson()) JsonWriteString(a_name, a_data.String());
			// binary処理
			if (m_ofs.is_open()) BinaryHelper::Write(m_ofs, a_data.value);
		}
		// ロード時
		else
		{
			// Json処理
			std::string _guidStr;
			if (JsonReadString(a_name, _guidStr)) a_data.FromString(_guidStr);
			// binary処理
			if (CanReadBinary()) BinaryHelper::Read(m_ifs, a_data.value);
		}
	}

	template<typename T>
	inline void Archive::VectorField(const std::string& a_name, std::vector<T>& a_data)
	{
		size_t _size = a_data.size();

		// 新しい BeginArray と BeginObject を使ったスマートな配列シリアライズ
		if (BeginArray(a_name, _size))
		{
			a_data.resize(_size);
			for (size_t _i = 0; _i < _size; ++_i)
			{
				if (BeginObject(_i))
				{
					// JSONでは [ {"v": 10}, {"v": 20} ] のように綺麗に格納される
					// バイナリモードでは BeginObject は何もしないので、単純に連続したデータとして書き込まれる
					Field("v", a_data[_i]);
					EndObject();
				}
			}
			EndArray();
		}
	}
}