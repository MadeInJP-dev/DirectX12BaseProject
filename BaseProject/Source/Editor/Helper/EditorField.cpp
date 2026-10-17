#include "Engine/EditorField/EditorField.h"
#include "EditorHelper.h"

#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

#include "Engine/Resource/Data/Model/Model.h"
#include "Engine/Resource/Data/Texture/Texture.h"
#include "Engine/Resource/Data/Animation/Animation.h"

namespace Engine::EditorField
{
	// 実装はエディターの部品(検索欄など)を使う
	using namespace ::Editor;

	namespace
	{
		ImVec2 ToImVec2(const Math::Vector2& a_value)
		{
			return ImVec2(a_value.x, a_value.y);
		}

		// 小数の既定の書式
		const char* FloatFormat(const char* a_format)
		{
			return a_format ? a_format : "%.3f";
		}

		// 警告・エラーの文字色 : 呼ぶ側で色を決めさせないため、ここだけで持つ
		constexpr ImVec4 WARNING_COLOR = { 1.00f, 0.80f, 0.30f, 1.0f };
		constexpr ImVec4 ERROR_COLOR   = { 1.00f, 0.40f, 0.40f, 1.0f };

		//==================================================================================
		// ラベルの整形
		//
		// コードの上ではメンバ名に合わせて "maxSpeed" / "MaxSpeed" / "max_speed" と
		// 書き方がばらつくので、表示するときに "Max Speed" の形へ揃える。
		// ID には元の文字列を使うので、書き方を変えても ImGui の状態は崩れない。
		//
		//   ・小文字 -> 大文字 の境目で区切る       maxSpeed   -> Max Speed
		//   ・大文字が続いた後の 大文字+小文字 で区切る UVOffset   -> UV Offset
		//   ・単語の先頭の小文字は大文字にする         projInvMat -> Proj Inv Mat
		//   ・'_' は空白にする
		//   ・末尾に1文字だけ付いた大文字は区切らない   DoF        -> DoF
		//   ・英字以外(記号・数字・日本語)には触らない
		//==================================================================================
		bool IsLower(char a_c) { return a_c >= 'a' && a_c <= 'z'; }
		bool IsUpper(char a_c) { return a_c >= 'A' && a_c <= 'Z'; }

		std::string FormatLabelImpl(std::string_view a_src)
		{
			std::string _out;
			_out.reserve(a_src.size() + 8);

			const size_t _size = a_src.size();
			for (size_t _i = 0; _i < _size; ++_i)
			{
				char _c = a_src[_i];
				if (_c == '_') _c = ' ';

				if (_i > 0 && IsUpper(_c))
				{
					const char _prev = a_src[_i - 1];
					const char _next = (_i + 1 < _size) ? a_src[_i + 1] : '\0';

					// 大文字の並び(略語)の長さ : 1文字だけで終わるなら区切らない(DoF)
					size_t _upperRun = 0;
					while (_i + _upperRun < _size && IsUpper(a_src[_i + _upperRun])) ++_upperRun;

					const bool _isLowerToUpper = IsLower(_prev) && (IsLower(_next) || _upperRun >= 2);
					const bool _isAcronymEnd = IsUpper(_prev) && IsLower(_next);
					if (_isLowerToUpper || _isAcronymEnd) _out += ' ';
				}

				// 単語の先頭の小文字は大文字にする
				const bool _isWordHead = _out.empty() || _out.back() == ' ';
				if (_isWordHead && IsLower(_c)) _c = static_cast<char>(_c - 'a' + 'A');

				// 空白は続けない
				if (_c == ' ' && (_out.empty() || _out.back() == ' ')) continue;

				_out += _c;
			}

			while (!_out.empty() && _out.back() == ' ') _out.pop_back();
			return _out;
		}

		// 文字列そのもので引ける辞書にする(引くたびに std::string を作らないため)
		struct StringHash
		{
			using is_transparent = void;
			size_t operator()(std::string_view a_value) const { return std::hash<std::string_view>{}(a_value); }
		};

		/// <summary>
		/// 表示用のラベル : "##" 以降(ID用)を落として整形したもの
		/// 毎フレーム同じラベルが来るので、整形した結果を覚えておく
		/// </summary>
		const std::string& DisplayLabel(const char* a_label)
		{
			static std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> s_cache;

			std::string_view _src = a_label ? a_label : "";
			if (const size_t _pos = _src.find("##"); _pos != std::string_view::npos)
			{
				_src = _src.substr(0, _pos);
			}

			if (auto _it = s_cache.find(_src); _it != s_cache.end()) return _it->second;
			return s_cache.emplace(std::string(_src), FormatLabelImpl(_src)).first->second;
		}

		// "##" で始まるラベルは列を作らない(値だけを出す)
		bool IsHiddenLabel(const char* a_label)
		{
			return a_label == nullptr || a_label[0] == '\0' || (a_label[0] == '#' && a_label[1] == '#');
		}

		/// <summary>
		/// 表示は整形したラベル、ID は元の文字列にした ImGui 用のラベルを作る
		/// ("Display###Original")。見出し・ボタンなど、ラベルを widget 自身が描くものに使う
		/// </summary>
		class DisplayID
		{
		public:
			explicit DisplayID(const char* a_label)
			{
				if (IsHiddenLabel(a_label))
				{
					m_pLabel = a_label;
					return;
				}
				snprintf(m_buff, sizeof(m_buff), "%s###%s", DisplayLabel(a_label).c_str(), a_label);
				m_pLabel = m_buff;
			}
			const char* Get() const { return m_pLabel; }

		private:
			char m_buff[256] = {};
			const char* m_pLabel = nullptr;
		};

		//==================================================================================
		// 行 : 左の列にラベル、右の列に値
		//
		// 値の欄にはラベルを描かせず("##元のラベル" を ID にする)、ラベルはこちらで描く。
		// 列の幅は窓の幅から決めるが、呼ぶ側が幅を決めているとき
		// (ItemWidthScope / SetNextItemWidth。ノードの中など)はそれを値の幅にする
		//==================================================================================

		// 呼ぶ側が決めた幅
		std::vector<float> s_itemWidthStack = {};
		float s_nextItemWidth = 0.0f;
		bool  s_hasNextItemWidth = false;

		// 直前の行 : Tooltip がラベルの上でも出るように覚えておく
		struct LastRow
		{
			int     frame = -1;
			ImGuiID itemID = 0;
			ImVec2  itemMin = {};
			ImVec2  labelMin = {};
			ImVec2  labelMax = {};
		};
		LastRow s_lastRow = {};

		// ラベルの列の幅(文字数ぶん)
		constexpr float LABEL_MIN_CHARS = 7.0f;
		constexpr float LABEL_MAX_CHARS = 14.0f;
		constexpr float LABEL_RATIO = 0.4f;		// 窓の幅に対するラベルの列の割合

		void CalcColumns(float& a_outLabelWidth, float& a_outValueWidth)
		{
			const float _font = ImGui::GetFontSize();
			const float _avail = ImGui::GetContentRegionAvail().x;

			if (s_hasNextItemWidth)
			{
				a_outLabelWidth = _font * LABEL_MIN_CHARS;
				a_outValueWidth = s_nextItemWidth;
				s_hasNextItemWidth = false;
				return;
			}
			if (!s_itemWidthStack.empty())
			{
				a_outLabelWidth = _font * LABEL_MIN_CHARS;
				a_outValueWidth = s_itemWidthStack.back();
				return;
			}

			a_outLabelWidth = std::clamp(_avail * LABEL_RATIO, _font * LABEL_MIN_CHARS, _font * LABEL_MAX_CHARS);
			a_outValueWidth = (std::max)(_avail - a_outLabelWidth, _font * 4.0f);
		}

		class Row
		{
		public:
			explicit Row(const char* a_label)
			{
				if (IsHiddenLabel(a_label))
				{
					// 列を作らないときは ImGui に任せる(呼ぶ側が決めた幅はそのまま効く)
					m_pID = a_label;
					s_hasNextItemWidth = false;
					return;
				}

				snprintf(m_idBuff, sizeof(m_idBuff), "##%s", a_label);
				m_pID = m_idBuff;
				m_hasLabel = true;

				float _labelWidth = 0.0f;
				CalcColumns(_labelWidth, m_valueWidth);
				DrawLabel(DisplayLabel(a_label), _labelWidth);
				ImGui::SetNextItemWidth(m_valueWidth);
			}
			~Row() { Finish(); }
			NON_COPYABLE_NON_MOVABLE(Row);

			// 値の欄を描き終えたら呼ぶ : 直前の行として覚える(デストラクタでも呼ばれる)
			void Finish()
			{
				if (!m_hasLabel || m_isFinished) return;
				m_isFinished = true;

				s_lastRow.frame = ImGui::GetFrameCount();
				s_lastRow.itemID = ImGui::GetItemID();
				s_lastRow.itemMin = ImGui::GetItemRectMin();
				s_lastRow.labelMin = m_labelMin;
				s_lastRow.labelMax = m_labelMax;
			}

			// 値の欄に渡すラベル(ID)
			const char* ID() const { return m_pID; }
			float ValueWidth() const { return m_valueWidth; }

		private:
			void DrawLabel(const std::string& a_text, float a_labelWidth)
			{
				const ImVec2 _start = ImGui::GetCursorScreenPos();
				const float _height = ImGui::GetFrameHeight();
				const float _right = _start.x + a_labelWidth - ImGui::GetStyle().ItemInnerSpacing.x;

				m_labelMin = _start;
				m_labelMax = ImVec2(_right, _start.y + _height);

				// 長いラベルは列で切る(値の欄へはみ出させない)
				ImGui::AlignTextToFramePadding();
				ImGui::PushClipRect(m_labelMin, m_labelMax, true);
				ImGui::TextUnformatted(a_text.c_str());
				ImGui::PopClipRect();

				// 切れているときはカーソルを乗せると全体を出す
				const bool _isClipped = ImGui::CalcTextSize(a_text.c_str()).x > (_right - _start.x);
				if (_isClipped && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(m_labelMin, m_labelMax))
				{
					ImGui::SetTooltip("%s", a_text.c_str());
				}

				// 値の列の頭へ
				ImGui::SameLine(0.0f, 0.0f);
				ImGui::SetCursorScreenPos(ImVec2(_start.x + a_labelWidth, _start.y));
			}

		private:
			char        m_idBuff[256] = {};
			const char* m_pID = nullptr;
			float       m_valueWidth = 0.0f;
			bool        m_hasLabel = false;
			bool        m_isFinished = false;
			ImVec2      m_labelMin = {};
			ImVec2      m_labelMax = {};
		};

		/// <summary>
		/// ラベルの列を空けて、値の列から続きを描く(1つの欄が2行になるとき)
		/// </summary>
		float BeginValueColumn()
		{
			float _labelWidth = 0.0f;
			float _valueWidth = 0.0f;
			CalcColumns(_labelWidth, _valueWidth);

			const ImVec2 _pos = ImGui::GetCursorScreenPos();
			ImGui::SetCursorScreenPos(ImVec2(_pos.x + _labelWidth, _pos.y));
			ImGui::SetNextItemWidth(_valueWidth);
			return _valueWidth;
		}

		// 直前の項目(か、直前の行のラベル)にカーソルが乗っているか
		bool IsLastItemHovered()
		{
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) return true;

			const bool _isLastRow =
				s_lastRow.frame == ImGui::GetFrameCount() &&
				s_lastRow.itemID == ImGui::GetItemID() &&
				s_lastRow.itemMin.x == ImGui::GetItemRectMin().x &&
				s_lastRow.itemMin.y == ImGui::GetItemRectMin().y;

			return _isLastRow &&
				ImGui::IsWindowHovered() &&
				ImGui::IsMouseHoveringRect(s_lastRow.labelMin, s_lastRow.labelMax);
		}

		// 薄い文字・警告は窓の幅で折り返す(狭いインスペクタで切れないように)
		void TextWrappedColoredV(const ImVec4& a_color, const char* a_fmt, va_list a_args)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, a_color);
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextV(a_fmt, a_args);
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}
	}

	//======================================================================================
	// 文字
	//======================================================================================
	void Text(const char* a_fmt, ...)
	{
		va_list _args;
		va_start(_args, a_fmt);
		ImGui::TextV(a_fmt, _args);
		va_end(_args);
	}

	void Value(const char* a_label, const char* a_fmt, ...)
	{
		Row _row(a_label);

		va_list _args;
		va_start(_args, a_fmt);
		ImGui::TextV(a_fmt, _args);
		va_end(_args);
	}

	void HelpText(const char* a_fmt, ...)
	{
		va_list _args;
		va_start(_args, a_fmt);
		TextWrappedColoredV(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), a_fmt, _args);
		va_end(_args);
	}

	void Tooltip(const char* a_fmt, ...)
	{
		if (!IsLastItemHovered()) return;

		va_list _args;
		va_start(_args, a_fmt);
		ImGui::SetTooltipV(a_fmt, _args);
		va_end(_args);
	}

	void WarningText(const char* a_fmt, ...)
	{
		va_list _args;
		va_start(_args, a_fmt);
		TextWrappedColoredV(WARNING_COLOR, a_fmt, _args);
		va_end(_args);
	}

	void ErrorText(const char* a_fmt, ...)
	{
		va_list _args;
		va_start(_args, a_fmt);
		TextWrappedColoredV(ERROR_COLOR, a_fmt, _args);
		va_end(_args);
	}

	void BulletText(const char* a_fmt, ...)
	{
		va_list _args;
		va_start(_args, a_fmt);
		ImGui::BulletTextV(a_fmt, _args);
		va_end(_args);
	}

	//======================================================================================
	// 見出し・区切り・配置
	//======================================================================================
	void Header(const char* a_label)
	{
		// 窓の先頭でなければ、上のまとまりとの間を空ける
		if (ImGui::GetCursorPosY() > ImGui::GetCursorStartPos().y)
		{
			ImGui::Spacing();
		}
		ImGui::SeparatorText(DisplayLabel(a_label).c_str());
	}

	void Line()
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
	}

	void SameLine()
	{
		ImGui::SameLine();
	}

	void SetNextItemWidth(float a_width)
	{
		s_nextItemWidth = a_width;
		s_hasNextItemWidth = true;
		ImGui::SetNextItemWidth(a_width);
	}

	//======================================================================================
	// 値の変更
	//======================================================================================
	bool Field(const char* a_label, bool& a_value)
	{
		Row _row(a_label);
		return ImGui::Checkbox(_row.ID(), &a_value);
	}

	bool Field(const char* a_label, int& a_value, float a_speed, int a_min, int a_max)
	{
		Row _row(a_label);
		return ImGui::DragInt(_row.ID(), &a_value, a_speed, a_min, a_max);
	}

	bool Field(const char* a_label, int (&a_values)[2], float a_speed, int a_min, int a_max)
	{
		Row _row(a_label);
		return ImGui::DragInt2(_row.ID(), a_values, a_speed, a_min, a_max);
	}

	bool Field(const char* a_label, uint32_t& a_value)
	{
		Row _row(a_label);
		return ImGui::InputScalar(_row.ID(), ImGuiDataType_U32, &a_value);
	}

	bool Field(const char* a_label, uint64_t& a_value)
	{
		Row _row(a_label);
		return ImGui::InputScalar(_row.ID(), ImGuiDataType_U64, &a_value);
	}

	bool Field(const char* a_label, float& a_value, float a_speed, float a_min, float a_max, const char* a_format)
	{
		Row _row(a_label);
		return ImGui::DragFloat(_row.ID(), &a_value, a_speed, a_min, a_max, FloatFormat(a_format));
	}

	bool Field(const char* a_label, Math::Vector2& a_value, float a_speed, float a_min, float a_max, const char* a_format)
	{
		Row _row(a_label);
		return ImGui::DragFloat2(_row.ID(), &a_value.x, a_speed, a_min, a_max, FloatFormat(a_format));
	}

	bool Field(const char* a_label, Math::Vector3& a_value, float a_speed, float a_min, float a_max, const char* a_format)
	{
		Row _row(a_label);
		return ImGui::DragFloat3(_row.ID(), &a_value.x, a_speed, a_min, a_max, FloatFormat(a_format));
	}

	//--------------------------------------------------------------------------------------
	// 回転 : 度数法のオイラー角として編集する
	//--------------------------------------------------------------------------------------
	bool Field(const char* a_label, Math::Quaternion& a_value)
	{
		const Math::Vector3 _rotRad = a_value.ToEuler();

		// Degreeへ変換
		Math::Vector3 _rotDeg = {
			DirectX::XMConvertToDegrees(_rotRad.x),
			DirectX::XMConvertToDegrees(_rotRad.y),
			DirectX::XMConvertToDegrees(_rotRad.z)
		};

		if (!Field(a_label, _rotDeg, 0.5f)) return false;

		// Euler(Degree) → Quaternion
		const Math::Quaternion _newQuat =
			Math::Quaternion::CreateFromYawPitchRoll(
				DirectX::XMConvertToRadians(_rotDeg.y),	// Yaw
				DirectX::XMConvertToRadians(_rotDeg.x),	// Pitch
				DirectX::XMConvertToRadians(_rotDeg.z)	// Roll
			);

		a_value = { _newQuat.x, _newQuat.y, _newQuat.z, _newQuat.w };
		return true;
	}

	//--------------------------------------------------------------------------------------
	// 色(RGBA) : ピッカーは 0..1 しか触れないので、1 を超える値のために数値のドラッグも出す
	//--------------------------------------------------------------------------------------
	bool Field(const char* a_label, Math::Color& a_value)
	{
		bool _isChange = false;

		constexpr ImGuiColorEditFlags FLAGS =
			ImGuiColorEditFlags_Float |
			ImGuiColorEditFlags_HDR |
			ImGuiColorEditFlags_AlphaBar |
			ImGuiColorEditFlags_AlphaPreviewHalf;

		{
			Row _row(a_label);
			_isChange |= ImGui::ColorEdit4(_row.ID(), a_value.Data(), FLAGS);
		}

		// 数値は2行目、値の列に揃えて出す
		ImGui::PushID(a_label);
		if (!IsHiddenLabel(a_label)) BeginValueColumn();
		_isChange |= ImGui::DragFloat4("##ColorValue", a_value.Data(), 0.01f, 0.0f, 0.0f, "%.2f");
		ImGui::PopID();

		return _isChange;
	}

	//--------------------------------------------------------------------------------------
	// 行列
	//--------------------------------------------------------------------------------------
	namespace
	{
		// 行列の成分をそのまま並べる
		bool DrawMatrixRaw(Math::Matrix& a_mat)
		{
			float* _m = reinterpret_cast<float*>(a_mat.m);

			bool _isEdit = false;

			static const char* const ROW_LABELS[] = { "M0", "M1", "M2", "M3" };
			for (int _row = 0; _row < 4; ++_row)
			{
				Row _line(ROW_LABELS[_row]);
				_isEdit |= ImGui::DragFloat4(_line.ID(), &_m[_row * 4], 0.01f, -FLT_MAX, FLT_MAX, "%.3f");
			}

			return _isEdit;
		}

		// 行列を位置・回転・スケールに分解して表示する
		// 編集されたら分解した値から組み直して書き戻す
		bool DrawMatrixPosRotScale(Math::Matrix& a_mat)
		{
			// 行列の分解
			Math::Vector3 _scale = {};
			Math::Quaternion _rotQuat = {};
			Math::Vector3 _pos = {};
			if (!a_mat.Decompose(_scale, _rotQuat, _pos))
			{
				ErrorText("Matrix Decompose Failed");
				return false;
			}

			// クォータニオンをEulerに直してDegreeへ変換
			Math::Vector3 _rotRad = _rotQuat.ToEuler();
			Math::Vector3 _rotDeg = {
				DirectX::XMConvertToDegrees(_rotRad.x),
				DirectX::XMConvertToDegrees(_rotRad.y),
				DirectX::XMConvertToDegrees(_rotRad.z)
			};

			bool _isEdit = false;
			_isEdit |= Field("Position", _pos, 0.1f);
			_isEdit |= Field("Rotation", _rotDeg, 0.5f);
			_isEdit |= Field("Scale", _scale, 0.1f);

			// 触られたときだけ組み直す(毎フレーム分解->合成すると誤差が乗るため)
			if (_isEdit)
			{
				Math::Quaternion _newQuat =
					Math::Quaternion::CreateFromYawPitchRoll(
						DirectX::XMConvertToRadians(_rotDeg.y),	// Yaw
						DirectX::XMConvertToRadians(_rotDeg.x),	// Pitch
						DirectX::XMConvertToRadians(_rotDeg.z)	// Roll
					);

				a_mat = Math::Matrix::CreateTRS(_pos, _newQuat, _scale);
			}

			return _isEdit;
		}
	}

	bool Field(const char* a_label, Math::Matrix& a_value, EMatrixViewMode a_defaultMode)
	{
		bool _isEdit = false;

		ImGui::PushID(a_label);

		if (ImGui::TreeNodeEx(DisplayID(a_label).Get(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			// 表示形式はImGuiの状態ストレージにラベル単位で覚えさせる
			ImGuiStorage* _pStorage = ImGui::GetStateStorage();
			const ImGuiID _key = ImGui::GetID("MatrixViewMode");

			auto _mode = static_cast<EMatrixViewMode>(
				_pStorage->GetInt(_key, static_cast<int>(a_defaultMode)));

			// 表示形式の切り替え
			if (Field("View Mode", _mode))
			{
				_pStorage->SetInt(_key, static_cast<int>(_mode));
			}

			switch (_mode)
			{
			case EMatrixViewMode::PosRotScale:
				_isEdit = DrawMatrixPosRotScale(a_value);
				break;

			case EMatrixViewMode::Raw:
			default:
				_isEdit = DrawMatrixRaw(a_value);
				break;
			}

			ImGui::TreePop();
		}

		ImGui::PopID();

		return _isEdit;
	}

	//--------------------------------------------------------------------------------------
	// 文字列
	//--------------------------------------------------------------------------------------
	bool Field(const char* a_label, std::string& a_value)
	{
		Row _row(a_label);
		return ImGui::InputText(_row.ID(), &a_value);
	}

	bool Field(const char* a_label, char* a_buff, size_t a_buffSize)
	{
		Row _row(a_label);
		return ImGui::InputText(_row.ID(), a_buff, a_buffSize);
	}

	bool MultilineField(const char* a_label, std::string& a_value)
	{
		Row _row(a_label);
		return ImGui::InputTextMultiline(_row.ID(), &a_value, ImVec2(_row.ValueWidth(), 0.0f));
	}

	bool ConfirmField(const char* a_label, std::string& a_value)
	{
		Row _row(a_label);
		return ImGui::InputText(_row.ID(), &a_value, ImGuiInputTextFlags_EnterReturnsTrue);
	}

	//--------------------------------------------------------------------------------------
	// そのほかの数値
	//--------------------------------------------------------------------------------------
	bool Slider(const char* a_label, float& a_value, float a_min, float a_max, const char* a_format)
	{
		Row _row(a_label);
		return ImGui::SliderFloat(_row.ID(), &a_value, a_min, a_max, FloatFormat(a_format));
	}

	bool RangeField(const char* a_label, float& a_min, float& a_max, float a_speed, float a_lowerLimit, float a_upperLimit)
	{
		Row _row(a_label);
		return ImGui::DragFloatRange2(_row.ID(), &a_min, &a_max, a_speed, a_lowerLimit, a_upperLimit);
	}

	//--------------------------------------------------------------------------------------
	// 色見本
	//--------------------------------------------------------------------------------------
	bool ColorField(const char* a_label, Math::Color& a_value, bool a_isAlpha)
	{
		Row _row(a_label);

		// アルファなしのときは RGB の3つだけを書き戻す(アルファは触らない)
		return a_isAlpha
			? ImGui::ColorEdit4(_row.ID(), a_value.Data())
			: ImGui::ColorEdit3(_row.ID(), a_value.Data());
	}

	bool ColorField(const char* a_label, Math::Vector3& a_rgb)
	{
		Row _row(a_label);
		return ImGui::ColorEdit3(_row.ID(), &a_rgb.x);
	}

	bool ColorPicker(const char* a_label, Math::Color& a_value)
	{
		Row _row(a_label);
		return ImGui::ColorPicker4(_row.ID(), a_value.Data());
	}

	bool Combo(const char* a_label, int& a_index, const char* const a_items[], int a_count)
	{
		Row _row(a_label);
		return ImGui::Combo(_row.ID(), &a_index, a_items, a_count);
	}

	bool IsItemEditFinished()
	{
		return ImGui::IsItemDeactivatedAfterEdit();
	}

	//--------------------------------------------------------------------------------------
	// Enum
	//--------------------------------------------------------------------------------------
	bool Internal::EnumCombo(const char* a_label, size_t& a_index, std::span<const std::string_view> a_names)
	{
		// magic_enum の名前は終端付きの静的文字列なので data() をそのまま渡せる
		const char* _preview = (a_index < a_names.size()) ? a_names[a_index].data() : "";

		bool _isChange = false;

		if (ComboScope _combo{ a_label, _preview })
		{
			for (size_t _i = 0; _i < a_names.size(); ++_i)
			{
				const bool _isSelect = (_i == a_index);

				if (ImGui::Selectable(a_names[_i].data(), _isSelect))
				{
					a_index = _i;
					_isChange = true;
				}

				if (_isSelect)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
		}

		return _isChange;
	}

	bool Internal::FlagsCombo(const char* a_label, uint64_t& a_raw,
		std::span<const uint64_t> a_bits, std::span<const std::string_view> a_names)
	{
		// 立っているビットの名前を並べてプレビューにする
		std::string _preview = "None";
		if (a_raw != 0)
		{
			_preview.clear();
			for (size_t _i = 0; _i < a_bits.size(); ++_i)
			{
				if (a_bits[_i] == 0 || (a_raw & a_bits[_i]) == 0) continue;

				if (!_preview.empty()) _preview += " | ";
				_preview += a_names[_i];
			}
		}

		bool _isChange = false;

		if (ComboScope _combo{ a_label, _preview.c_str() })
		{
			for (size_t _i = 0; _i < a_bits.size(); ++_i)
			{
				// 0 の項目(None)は選ぶ意味がないので出さない
				if (a_bits[_i] == 0) continue;

				const bool _isCheck = (a_raw & a_bits[_i]) != 0;

				if (ImGui::Selectable(a_names[_i].data(), _isCheck))
				{
					a_raw = _isCheck ? (a_raw & ~a_bits[_i]) : (a_raw | a_bits[_i]);
					_isChange = true;
				}
			}
		}

		return _isChange;
	}

	//======================================================================================
	// ボタン・選択
	//======================================================================================
	bool Button(const char* a_label, const Math::Vector2& a_size)
	{
		return ImGui::Button(DisplayID(a_label).Get(), ToImVec2(a_size));
	}

	bool SmallButton(const char* a_label)
	{
		return ImGui::SmallButton(DisplayID(a_label).Get());
	}

	bool ArrowButton(const char* a_id, EArrowDir a_dir)
	{
		ImGuiDir _dir = ImGuiDir_Up;
		switch (a_dir)
		{
		case EArrowDir::Left:	_dir = ImGuiDir_Left;	break;
		case EArrowDir::Right:	_dir = ImGuiDir_Right;	break;
		case EArrowDir::Up:		_dir = ImGuiDir_Up;		break;
		case EArrowDir::Down:	_dir = ImGuiDir_Down;	break;
		}
		return ImGui::ArrowButton(a_id, _dir);
	}

	//--------------------------------------------------------------------------------------
	// 生成/削除ボタン
	//
	// 色は「通常 / ホバー / 押下」の3つを差し替える。3つとも入れないと、
	// カーソルを乗せた瞬間に既定色へ戻って別のボタンに見えてしまう。
	// 文字色は触らない(テーマ側の可読性設定をそのまま活かす)。
	//--------------------------------------------------------------------------------------
	namespace
	{
		// 生成系(緑)
		constexpr ImVec4 CREATE_COLOR         = { 0.20f, 0.52f, 0.24f, 1.0f };
		constexpr ImVec4 CREATE_HOVERED_COLOR = { 0.26f, 0.66f, 0.30f, 1.0f };
		constexpr ImVec4 CREATE_ACTIVE_COLOR  = { 0.16f, 0.44f, 0.20f, 1.0f };

		// 削除系(赤)
		constexpr ImVec4 DELETE_COLOR         = { 0.58f, 0.20f, 0.20f, 1.0f };
		constexpr ImVec4 DELETE_HOVERED_COLOR = { 0.72f, 0.26f, 0.26f, 1.0f };
		constexpr ImVec4 DELETE_ACTIVE_COLOR  = { 0.48f, 0.16f, 0.16f, 1.0f };

		void PushButtonColor(const ImVec4& a_normal, const ImVec4& a_hovered, const ImVec4& a_active)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, a_normal);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, a_hovered);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, a_active);
		}

		void PopButtonColor()
		{
			ImGui::PopStyleColor(3);
		}
	}

	bool CreateButton(const char* a_label, const Math::Vector2& a_size)
	{
		PushButtonColor(CREATE_COLOR, CREATE_HOVERED_COLOR, CREATE_ACTIVE_COLOR);
		const bool _isPressed = ImGui::Button(DisplayID(a_label).Get(), ToImVec2(a_size));
		PopButtonColor();
		return _isPressed;
	}

	bool CreateSmallButton(const char* a_label)
	{
		PushButtonColor(CREATE_COLOR, CREATE_HOVERED_COLOR, CREATE_ACTIVE_COLOR);
		const bool _isPressed = ImGui::SmallButton(DisplayID(a_label).Get());
		PopButtonColor();
		return _isPressed;
	}

	bool DeleteButton(const char* a_label, const Math::Vector2& a_size)
	{
		PushButtonColor(DELETE_COLOR, DELETE_HOVERED_COLOR, DELETE_ACTIVE_COLOR);
		const bool _isPressed = ImGui::Button(DisplayID(a_label).Get(), ToImVec2(a_size));
		PopButtonColor();
		return _isPressed;
	}

	bool DeleteSmallButton(const char* a_label)
	{
		PushButtonColor(DELETE_COLOR, DELETE_HOVERED_COLOR, DELETE_ACTIVE_COLOR);
		const bool _isPressed = ImGui::SmallButton(DisplayID(a_label).Get());
		PopButtonColor();
		return _isPressed;
	}

	bool RadioButton(const char* a_label, bool a_isActive)
	{
		return ImGui::RadioButton(DisplayID(a_label).Get(), a_isActive);
	}

	bool Selectable(const char* a_label, bool a_isSelected)
	{
		// 一覧の中身(アセット名・オブジェクト名など)なので整形しない
		return ImGui::Selectable(a_label, a_isSelected);
	}

	void SetItemDefaultFocus()
	{
		ImGui::SetItemDefaultFocus();
	}

	bool CollapsingHeader(const char* a_label, bool a_isDefaultOpen)
	{
		return ImGui::CollapsingHeader(
			DisplayID(a_label).Get(),
			a_isDefaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
	}

	//======================================================================================
	// 表示
	//======================================================================================
	void ProgressBar(const char* a_label, float a_fraction, const char* a_overlay)
	{
		Row _row(a_label);
		const float _width = IsHiddenLabel(a_label) ? -FLT_MIN : _row.ValueWidth();
		ImGui::ProgressBar(a_fraction, ImVec2(_width, 0.0f), a_overlay);
	}

	Math::Vector2 Image(
		const ECS::EngineServices& a_services,
		const Handle<Resource::Texture>& a_handle,
		float a_width,
		float a_height
	)
	{
		auto& _resourceManager = *a_services.pResourceManager;

		// 読み込み中はまだ中身が空なので、SRVを引くと不正なディスクリプタを掴む
		if (!_resourceManager.IsReady(a_handle))
		{
			HelpText("Loading...");
			return { 0,0 };
		}

		auto* _pTex = _resourceManager.Ref(a_handle);
		if (!_pTex)
		{
			WarningText("Texture not found");
			return { 0,0 };
		}
		auto _gpuHandle = EditorHelper::GetImGuiTexHandle(_pTex->GetImGuiSRV());

		ImTextureID _imTex = static_cast<ImTextureID>(_gpuHandle.ptr);

		// 横幅だけを取得（縦の残り領域は無視する）
		const float _drawWidth = ImGui::GetContentRegionAvail().x;

		// 横幅に合わせて、指定の比率で高さを逆算する
		const float _aspect = a_width / a_height;
		const float _drawHeight = _drawWidth / _aspect;

		ImGui::Image(_imTex, ImVec2(_drawWidth, _drawHeight));

		// 実際に描画したサイズを返す
		return Math::Vector2(_drawWidth, _drawHeight);
	}

	//======================================================================================
	// アセット選択
	//
	// 1行に収める : コンボには選択中のアセット名を出し、
	// 置き場所と GUID はカーソルを乗せたときだけ出す
	//======================================================================================
	bool AssetPicker(
		const ECS::EngineServices& a_services,
		const char* a_label,
		const char* a_assetTypeName,
		const GUID& a_currentGUID,
		GUID& a_outSelectedGUID
	)
	{
		if (!a_services.pAssetDatabase)
		{
			ErrorText("AssetDatabase is not available");
			return false;
		}
		auto& _assetDB = *a_services.pAssetDatabase;

		// 現在の選択 : ハンドルを持たない前提なのでGUIDから名前を引く
		// 未設定のときは引かない : インスペクターが毎フレーム描くので、引くたびにログが出る
		const auto* _pCurrentProp = a_currentGUID.IsValid() ? _assetDB.GetAssetProperty(a_currentGUID) : nullptr;
		std::string _preview = "None";
		if (_pCurrentProp)
		{
			_preview = _pCurrentProp->fileName;
		}
		else if (a_currentGUID.IsValid())
		{
			// GUID はあるのに一覧に無い : 消されたか、まだ読み込まれていない
			_preview = "(missing) " + a_currentGUID.String().substr(0, 8);
		}

		Row _row(a_label);
		const bool _isOpen = ImGui::BeginCombo(_row.ID(), _preview.c_str());
		_row.Finish();

		if (!_isOpen)
		{
			// 同名のアセットが別フォルダにあり得るので、置き場所まで出す
			if (_pCurrentProp)
			{
				Tooltip("%s\n%s\n%s", a_assetTypeName, _pCurrentProp->filePath.c_str(), a_currentGUID.String().c_str());
			}
			else
			{
				Tooltip("%s", a_assetTypeName);
			}
			return false;
		}

		bool _isChanged = false;

		// 数が増えると探せなくなるので名前で絞り込めるようにする
		const std::string& _search = EditorHelper::DrawSearchBox();

		const auto& _assetList = _assetDB.GetTypeMetaVec(a_assetTypeName);

		// 同名のアセットは名前だけでは選び分けられないので、置き場所を添える対象を先に拾う
		const auto _duplicatedSet = CollectDuplicatedNames(
			_assetList, [](const Resource::AssetProperty& a_prop) { return a_prop.fileName; });

		for (size_t _i = 0; _i < _assetList.size(); ++_i)
		{
			const auto& _prop = _assetList[_i];

			// 同名が並ぶときはフォルダ名で絞り込めたほうが早いので、パスも検索対象にする
			if (!EditorHelper::IsMatchSearch(_search, _prop.fileName) &&
				!EditorHelper::IsMatchSearch(_search, _prop.filePath)) continue;

			const bool _isSelected = (a_currentGUID == _prop.guid);

			// 同名でもImGuiのIDがぶつからないようにする。
			// Selectable のIDはラベル文字列から作られるので、名前が同じだと
			// 別のアセットが同じ項目として扱われてしまう
			ImGui::PushID(static_cast<int>(_i));

			const std::string _label = MakeUniqueLabel(
				_duplicatedSet, _prop.fileName, Core::File::GetDirFromPath(_prop.filePath));

			if (ImGui::Selectable(_label.c_str(), _isSelected))
			{
				a_outSelectedGUID = _prop.guid;
				_isChanged = true;
			}

			// コンボボックスを開いた際、現在の選択アイテムまで自動スクロールする
			// (絞り込み中は検索欄に入力しているので、そちらからフォーカスを奪わない)
			if (_isSelected && _search.empty())
			{
				ImGui::SetItemDefaultFocus();
			}

			ImGui::PopID();
		}
		ImGui::EndCombo();

		return _isChanged;
	}

	bool AssetField(
		const ECS::EngineServices& a_services,
		const char* a_label,
		const char* a_assetTypeName,
		GUID& a_inoutGUID
	)
	{
		Core::GUID _selectedGUID = {};
		if (!AssetPicker(a_services, a_label, a_assetTypeName, a_inoutGUID, _selectedGUID))
		{
			return false;
		}

		a_inoutGUID = _selectedGUID;
		return true;
	}

	//--------------------------------------------------------------------------------------
	// モデルのノード
	//--------------------------------------------------------------------------------------
	bool ModelNodeField(
		const char* a_label,
		const Resource::Model* a_pModel,
		UINT& a_inoutNodeIndex,
		UINT& a_inoutNodeNameHash
	)
	{
		if (!a_pModel)
		{
			WarningText("Model Resource is null.");
			return false;
		}

		// モデルが管理する全ノード配列
		const auto& _nodes = a_pModel->GetOriginalNodeVec();

		// 現在選択されているノード名を表示用として取得。
		// 同名のノードがあるモデルもあるので、番号まで出して区別できるようにする
		std::string _currentNodeName = "None";
		if (a_inoutNodeIndex < _nodes.size())
		{
			_currentNodeName = _nodes[a_inoutNodeIndex].name +
				"   [#" + std::to_string(a_inoutNodeIndex) + "]";
		}

		bool _isChanged = false;

		if (ComboScope _combo{ a_label, _currentNodeName.c_str() })
		{
			// ボーンは数が多いので名前で絞り込めるようにする
			const std::string& _search = EditorHelper::DrawSearchBox();

			// 同名のノードは名前だけでは選び分けられないので、番号を添える対象を先に拾う
			const auto _duplicatedSet = CollectDuplicatedNames(
				_nodes, [](const auto& a_node) { return a_node.name; });

			for (size_t _i = 0; _i < _nodes.size(); ++_i)
			{
				if (!EditorHelper::IsMatchSearch(_search, _nodes[_i].name)) continue;

				bool _isSelected = (a_inoutNodeIndex == _i);

				// 同名でもImGuiのIDがぶつからないようにする
				ImGui::PushID(static_cast<int>(_i));

				const std::string _label = MakeUniqueLabel(
					_duplicatedSet, _nodes[_i].name, "#" + std::to_string(_i));

				if (ImGui::Selectable(_label.c_str(), _isSelected))
				{
					a_inoutNodeNameHash = _nodes[_i].nodeNameHash;
					a_inoutNodeIndex = static_cast<UINT>(_i);
					_isChanged = true;
				}

				// 絞り込み中は検索欄からフォーカスを奪わない
				if (_isSelected && _search.empty())
				{
					ImGui::SetItemDefaultFocus();
				}

				ImGui::PopID();
			}
		}

		return _isChanged;
	}

	bool ModelNodeField(
		const char* a_label,
		const Resource::Model* a_pModel,
		std::string& a_inoutNodeName,
		UINT& a_inoutNodeNameHash
	)
	{
		if (!a_pModel)
		{
			WarningText("Model Resource is null.");
			return false;
		}

		const auto& _nodes = a_pModel->GetOriginalNodeVec();

		// 現在の選択表示
		const std::string _currentNodeName = a_inoutNodeName.empty() ? "None" : a_inoutNodeName;

		bool _isChanged = false;

		if (ComboScope _combo{ a_label, _currentNodeName.c_str() })
		{
			// ボーンは数が多いので名前で絞り込めるようにする
			const std::string& _search = EditorHelper::DrawSearchBox();

			// 同名のノードがあると一覧で見分けが付かないので、番号を添える対象を先に拾う。
			// ただしこの欄が持ち帰るのは名前(とそのハッシュ)なので、同名を選び分けても
			// 保存される値は同じになる。区別が要る場面ではインデックス版を使うこと
			const auto _duplicatedSet = CollectDuplicatedNames(
				_nodes, [](const auto& a_node) { return a_node.name; });

			for (size_t _i = 0; _i < _nodes.size(); ++_i)
			{
				const auto& _node = _nodes[_i];

				if (!EditorHelper::IsMatchSearch(_search, _node.name)) continue;

				bool _isSelected = (a_inoutNodeName == _node.name);

				// 同名でもImGuiのIDがぶつからないようにする
				ImGui::PushID(static_cast<int>(_i));

				const std::string _label = MakeUniqueLabel(
					_duplicatedSet, _node.name, "#" + std::to_string(_i));

				if (ImGui::Selectable(_label.c_str(), _isSelected))
				{
					a_inoutNodeName = _node.name;
					a_inoutNodeNameHash = _node.nodeNameHash;
					_isChanged = true;
				}

				// 絞り込み中は検索欄からフォーカスを奪わない
				if (_isSelected && _search.empty())
				{
					ImGui::SetItemDefaultFocus();
				}

				ImGui::PopID();
			}
		}

		return _isChanged;
	}

	//--------------------------------------------------------------------------------------
	// モデルのボーンレイヤー
	//--------------------------------------------------------------------------------------
	bool ModelBoneMaskField(
		const char* a_label,
		const Resource::Model* a_pModel,
		UINT& a_inoutNameHash
	)
	{
		if (!a_pModel)
		{
			WarningText("Model Resource is null.");
			return false;
		}

		// 現在の選択表示 : モデル側で消された・名前が変わったものは分かるようにする
		const auto* _pCurrent = a_pModel->FindBoneMask(a_inoutNameHash);
		std::string _preview = "None";
		if (_pCurrent)                  { _preview = _pCurrent->name; }
		else if (a_inoutNameHash != 0)  { _preview = "Missing (" + std::to_string(a_inoutNameHash) + ")"; }

		bool _isChanged = false;

		if (ComboScope _combo{ a_label, _preview.c_str() })
		{
			if (ImGui::Selectable("None", a_inoutNameHash == 0))
			{
				a_inoutNameHash = 0;
				_isChanged = true;
			}

			const auto& _maskVec = a_pModel->GetBoneMaskVec();
			for (size_t _i = 0; _i < _maskVec.size(); ++_i)
			{
				const auto& _mask = _maskVec[_i];
				bool _isSelected = (a_inoutNameHash == _mask.nameHash);

				ImGui::PushID(static_cast<int>(_i));
				if (ImGui::Selectable(_mask.name.c_str(), _isSelected))
				{
					a_inoutNameHash = _mask.nameHash;
					_isChanged = true;
				}
				if (_isSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
				ImGui::PopID();
			}
		}

		return _isChanged;
	}

	//--------------------------------------------------------------------------------------
	// モデルのアニメーション
	//--------------------------------------------------------------------------------------
	namespace
	{
		// アニメーション選択コンボの本体
		// ハンドルの持ち方(生ハンドル / 参照カウント付き)だけが違うので、選択結果だけを返す
		bool ModelAnimationFieldImpl(
			const ECS::EngineServices& a_services,
			const char* a_label,
			const Resource::Model* a_pModel,
			const Handle<Resource::AnimationData>& a_currentHandle,
			Handle<Resource::AnimationData>& a_outSelected
		)
		{
			if (!a_pModel) { return false; }

			const auto& _handleVec = a_pModel->GetAnimationHandles();

			// ハンドルから名前を引く。取れなければ空(この後の一覧では飛ばす)
			auto _animName = [&a_services](const auto& a_ref) -> std::string
			{
				const auto* _pAnim = a_services.pResourceManager->Get(a_ref);
				return _pAnim ? _pAnim->name : std::string();
			};

			// 現在の再生アニメ名をプレビューにする。
			// 同名のアニメを持つモデルもあるので、そのときは番号まで出す
			std::string _viewName = "None";
			for (size_t _i = 0; _i < _handleVec.size(); ++_i)
			{
				if (!(_handleVec[_i] == a_currentHandle)) continue;

				_viewName = _animName(_handleVec[_i]) + "   [#" + std::to_string(_i) + "]";
				break;
			}

			bool _isChanged = false;

			if (ComboScope _combo{ a_label, _viewName.c_str() })
			{
				// 名前で絞り込めるようにする
				const std::string& _search = EditorHelper::DrawSearchBox();

				// 同名のアニメは名前だけでは選び分けられないので、番号を添える対象を先に拾う
				const auto _duplicatedSet = CollectDuplicatedNames(_handleVec, _animName);

				for (size_t _i = 0; _i < _handleVec.size(); ++_i)
				{
					const auto& _ref = _handleVec[_i];

					const auto* _pAnim = a_services.pResourceManager->Get(_ref);
					if (!_pAnim) continue;

					if (!EditorHelper::IsMatchSearch(_search, _pAnim->name)) continue;

					bool _isSelected = (_ref == a_currentHandle);

					// 同名でもImGuiのIDがぶつからないようにする
					ImGui::PushID(static_cast<int>(_i));

					const std::string _label = MakeUniqueLabel(
						_duplicatedSet, _pAnim->name, "#" + std::to_string(_i));

					if (ImGui::Selectable(_label.c_str(), _isSelected))
					{
						a_outSelected = _ref;
						_isChanged = true;
					}

					// コンボボックスを開いた際、現在の選択アイテムまで自動スクロールする
					// (絞り込み中は検索欄からフォーカスを奪わない)
					if (_isSelected && _search.empty())
					{
						ImGui::SetItemDefaultFocus();
					}

					ImGui::PopID();
				}
			}

			return _isChanged;
		}
	}

	bool ModelAnimationField(
		const ECS::EngineServices& a_services,
		const char* a_label,
		const Resource::Model* a_pModel,
		Handle<Resource::AnimationData>& a_inoutHandle
	)
	{
		Handle<Resource::AnimationData> _selected = {};
		if (!ModelAnimationFieldImpl(a_services, a_label, a_pModel, a_inoutHandle, _selected))
		{
			return false;
		}

		a_inoutHandle = _selected;
		return true;
	}

	bool ModelAnimationField(
		const ECS::EngineServices& a_services,
		const char* a_label,
		const Resource::Model* a_pModel,
		ResourceRef<Resource::AnimationData>& a_inoutRef
	)
	{
		Handle<Resource::AnimationData> _selected = {};
		if (!ModelAnimationFieldImpl(a_services, a_label, a_pModel, a_inoutRef.GetRaw(), _selected))
		{
			return false;
		}

		// 参照カウントの付け替えはResourceRef側に任せる
		a_inoutRef = ResourceRef<Resource::AnimationData>(_selected);
		return true;
	}

	//======================================================================================
	// 入力の状態
	//======================================================================================
	bool IsCtrlDown()
	{
		return ImGui::GetIO().KeyCtrl;
	}

	bool IsTextInputActive()
	{
		// WantCaptureKeyboard は NavEnableKeyboard 有効時、ImGuiウィンドウに
		// フォーカスがあるだけで常時 true になり得る(ドッキング型エディタでは
		// ほぼ常時になる)。テキスト入力中だけ true になる WantTextInput を見る
		if (ImGui::GetCurrentContext() == nullptr) return false;
		return ImGui::GetIO().WantTextInput;
	}

	//======================================================================================
	// シーンビュー上の操作
	//======================================================================================
	bool ScreenHandle(const char* a_id, const Math::Vector2& a_screenPos, float a_radius, Math::Vector2& a_outDragPos)
	{
		const ImVec2 _center = ToImVec2(a_screenPos);

		// ドラッグ操作用の透明ボタン
		ImGui::SetCursorScreenPos(ImVec2(_center.x - a_radius, _center.y - a_radius));
		ImGui::InvisibleButton(a_id, ImVec2(a_radius * 2.0f, a_radius * 2.0f));
		const bool _isActive = ImGui::IsItemActive();		// 掴まれているか
		const bool _isHovered = ImGui::IsItemHovered();		// カーソルが重なっているか

		// ハンドル描画(十字 + 円)
		ImDrawList* _pDrawList = ImGui::GetWindowDrawList();
		const ImU32 _color = _isActive ? IM_COL32(255, 200, 0, 255)
			: _isHovered ? IM_COL32(255, 255, 255, 255)
			: IM_COL32(0, 200, 255, 255);
		const float _crossLength = a_radius * 1.8f;
		_pDrawList->AddCircle(_center, a_radius, _color, 20, 2.0f);
		_pDrawList->AddLine(ImVec2(_center.x - _crossLength, _center.y), ImVec2(_center.x + _crossLength, _center.y), _color, 1.5f);
		_pDrawList->AddLine(ImVec2(_center.x, _center.y - _crossLength), ImVec2(_center.x, _center.y + _crossLength), _color, 1.5f);

		if (!_isActive) return false;

		const ImVec2 _mouse = ImGui::GetMousePos();
		a_outDragPos = Math::Vector2(_mouse.x, _mouse.y);
		return true;
	}

	void ScreenPolyline(
		const Math::Vector2* a_pPoints,
		size_t a_count,
		const Math::Color& a_color,
		float a_thickness,
		bool a_isClosed)
	{
		if (a_pPoints == nullptr || a_count < 2) return;

		std::vector<ImVec2> _pointVec;
		_pointVec.reserve(a_count);
		for (size_t _i = 0; _i < a_count; ++_i)
		{
			_pointVec.push_back(ToImVec2(a_pPoints[_i]));
		}

		const ImU32 _color = ImGui::ColorConvertFloat4ToU32(ImVec4(a_color.r, a_color.g, a_color.b, a_color.a));
		ImGui::GetWindowDrawList()->AddPolyline(
			_pointVec.data(),
			static_cast<int>(_pointVec.size()),
			_color,
			a_isClosed ? ImDrawFlags_Closed : ImDrawFlags_None,
			a_thickness);
	}

	bool TranslateGizmo(const Math::Matrix& a_viewMat, const Math::Matrix& a_projMat, Math::Matrix& a_inoutMat, float a_snap)
	{
		const float _snapValues[3] = { a_snap, a_snap, a_snap };

		ImGuizmo::Manipulate(
			&a_viewMat._11,
			&a_projMat._11,
			ImGuizmo::OPERATION::TRANSLATE,
			ImGuizmo::MODE::WORLD,
			&a_inoutMat._11,
			nullptr,
			(a_snap > 0.0f) ? _snapValues : nullptr
		);

		return ImGuizmo::IsUsing();
	}

	//======================================================================================
	// ノードエディタ
	//======================================================================================
	void NodeLink(int a_linkID, int a_srcOutPinID, int a_dstInPinID)
	{
		ImNodes::Link(a_linkID, a_srcOutPinID, a_dstInPinID);
	}

	//======================================================================================
	// スコープ
	//======================================================================================
	IDScope::IDScope(int a_id)			{ ImGui::PushID(a_id); }
	IDScope::IDScope(const char* a_id)	{ ImGui::PushID(a_id); }
	IDScope::IDScope(const void* a_id)	{ ImGui::PushID(a_id); }
	IDScope::~IDScope()					{ ImGui::PopID(); }

	DisabledScope::DisabledScope(bool a_isDisabled)	{ ImGui::BeginDisabled(a_isDisabled); }
	DisabledScope::~DisabledScope()					{ ImGui::EndDisabled(); }

	IndentScope::IndentScope()	{ ImGui::Indent(); }
	IndentScope::~IndentScope()	{ ImGui::Unindent(); }

	ItemWidthScope::ItemWidthScope(float a_width)
	{
		// 行の値の幅にも使うので、こちらでも積んでおく
		s_itemWidthStack.push_back(a_width);
		ImGui::PushItemWidth(a_width);
	}
	ItemWidthScope::~ItemWidthScope()
	{
		s_itemWidthStack.pop_back();
		ImGui::PopItemWidth();
	}

	TreeScope::TreeScope(const char* a_label, bool a_isDefaultOpen, bool a_isSpanFullWidth)
	{
		ImGuiTreeNodeFlags _flags = ImGuiTreeNodeFlags_None;
		if (a_isDefaultOpen)	_flags |= ImGuiTreeNodeFlags_DefaultOpen;
		if (a_isSpanFullWidth)	_flags |= ImGuiTreeNodeFlags_SpanFullWidth;

		// ツリーのラベルは一覧の中身(名前など)であることが多いので整形しない
		m_isOpen = ImGui::TreeNodeEx(a_label, _flags);
	}
	TreeScope::~TreeScope()
	{
		// 閉じているときは TreePop しない(開いたときだけ積まれる)
		if (m_isOpen) ImGui::TreePop();
	}

	ComboScope::ComboScope(const char* a_label, const char* a_preview)
	{
		// ラベルは行の左の列に出す
		Row _row(a_label);
		m_isOpen = ImGui::BeginCombo(_row.ID(), a_preview);
		_row.Finish();
	}
	ComboScope::~ComboScope()
	{
		// 開いたときだけ閉じる
		if (m_isOpen) ImGui::EndCombo();
	}

	WindowScope::WindowScope(const char* a_name)
	{
		m_isVisible = ImGui::Begin(a_name);
	}
	WindowScope::~WindowScope()
	{
		// Begin は結果に関わらず End と対にする
		ImGui::End();
	}
}
