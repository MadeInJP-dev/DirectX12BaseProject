#include "Font.h"

#include "../../Common/ScopedResourceBuild.h"
#include "../../Manager/ResourceManager/ResourceManager.h"

// CD3DX12_* のヘルパーはここだけで使う。
// プリコンパイル済みヘッダーへ置くと全翻訳単位に広がるため
#pragma warning(push, 0)
#include "d3dx12.h"
#pragma warning(pop)

// stb_truetype はここだけで読む。
// ヘッダーへ出すとプリコンパイル済みヘッダー経由で全翻訳単位に広がるため
#pragma warning(push, 0)
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb-master/stb_truetype.h>
#pragma warning(pop)

namespace Engine::Resource
{
	namespace
	{
		// グリフ同士の余白(px)
		//
		// 縮小して出すと隣のグリフをバイリニアで拾ってしまうので、
		// 1ピクセルでは足りない。2つ空けて完全に切り離す
		constexpr uint32_t GLYPH_PADDING = 2;

		// 使うコンテキストから ResourceManager を引く
		// (コンテキストは ScopedResourceBuild が必ず埋めるが、念のため落とさないようにしておく)
		ResourceManager& PickResourceManager(const ResourceBuildContext* a_pContext)
		{
			ENGINE_ERRLOG(a_pContext && a_pContext->pResourceManager, "ResourceBuildContext.pResourceManager が空です");
			return *a_pContext->pResourceManager;
		}

	}

	//==========================================================================================
	// FontAtlas
	//==========================================================================================
	void FontAtlas::Release()
	{
		// 参照を返すだけ。実体の破棄はスイープに任せる
		m_texRef = {};
		m_texHandle = {};
		m_pTexResource = nullptr;

		m_atlasData.clear();
		m_atlasData.shrink_to_fit();

		m_size = 0;
		m_cursorX = 0;
		m_cursorY = 0;
		m_rowHeight = 0;

		m_isDirty = false;
		m_dirtyMinX = 0;
		m_dirtyMinY = 0;
		m_dirtyMaxX = 0;
		m_dirtyMaxY = 0;

		m_isWarnedFull = false;
	}

	bool FontAtlas::Create(uint32_t a_size, const ResourceBuildContext* a_pContext)
	{
		Release();

		if (a_size == 0)
		{
			ENGINE_WARNING("[Font] アトラスのサイズが0のため作成できません");
			return false;
		}

		m_size = a_size;

		TextureCreateDesc _desc = {};
		_desc.name = "FontAtlas";
		_desc.width = m_size;
		_desc.height = m_size;
		_desc.format = DXGI_FORMAT_R8_UNORM;
		_desc.usage = ETextureUsage::SRV;

		// 1成分しか持たないので、rgba すべてに R を配る。
		// こうしておくと UIシェーダーが「白い文字 × 頂点カラー」として
		// そのまま扱えるので、フォント専用のシェーダー分岐が要らない
		_desc.srvComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 0, 0, 0);

		// ビューの置き場はコンテキストから引く。
		// ローダ経路は省略できるので、渡されていなければその場で開く
		// (転送は要らないが、ヒープの引き先を1箇所にまとめるためこの形にしてある)
		ResourceBuildScope _scope(a_pContext);

		Texture _tex = {};
		_tex.Create(_scope.GetContext().pHeapManager, _desc);
		if (!_tex.GetResource())
		{
			ENGINE_WARNING("[Font] アトラステクスチャの作成に失敗しました");
			m_size = 0;
			return false;
		}

		// 描画命令(SubmitUI)がハンドル受け取りなので、実体はマネージャーへ預ける
		//
		// ここでバッチを開かないのは、テクスチャを作るだけなら転送が要らないため。
		// 中身の転送は Flush 側でまとめて行う
		//
		// GPUリソース自体はムーブしても同じものなので、預ける前に転送先として覚えておく
		m_pTexResource = _tex.GetResource();
		m_texRef = PickResourceManager(a_pContext).Add<Texture>(std::move(_tex));
		m_texHandle = m_texRef.GetRaw();

		// 0(=透明)で初期化
		m_atlasData.assign(static_cast<size_t>(m_size) * m_size, 0);

		// 作った直後のGPU側は未初期化のゴミなので、一度は全面を送って0で埋める。
		// 文字の縁をバイリニアで拾ったときに、隣のゴミが滲み出さないようにするため
		MarkDirty(0, 0, m_size, m_size);

		return true;
	}

	bool FontAtlas::Allocate(const uint8_t* a_pBitMap, uint32_t a_width, uint32_t a_height, uint32_t& a_outX, uint32_t& a_outY)
	{
		a_outX = 0;
		a_outY = 0;

		if (m_size == 0) return false;
		if (a_pBitMap == nullptr || a_width == 0 || a_height == 0) return false;

		// 余白ぶんを足した「占有する大きさ」で場所を探す。
		// 実際に書き込むのは余白を含まない a_width × a_height
		const uint32_t _occupyWidth = a_width + GLYPH_PADDING;
		const uint32_t _occupyHeight = a_height + GLYPH_PADDING;

		// 1文字が丸ごと入らない
		if (_occupyWidth > m_size || _occupyHeight > m_size) return false;

		// 横に入らなければ次の行
		if (m_cursorX + _occupyWidth > m_size)
		{
			m_cursorX = 0;
			m_cursorY += m_rowHeight;
			m_rowHeight = 0;
		}

		// アトラスからはみ出す
		if (m_cursorY + _occupyHeight > m_size)
		{
			// 埋まった後は要求のたびにここへ来るので、警告は1回だけにする
			if (!m_isWarnedFull)
			{
				m_isWarnedFull = true;
				ENGINE_WARNING("[Font] アトラステクスチャからはみ出たため文字が生成できません");
			}
			return false;
		}

		a_outX = m_cursorX;
		a_outY = m_cursorY;

		m_cursorX += _occupyWidth;
		m_rowHeight = std::max(m_rowHeight, _occupyHeight);

		// CPU側へ焼き付ける : 元のビットマップは行間の隙間なしで詰まっている
		for (uint32_t _y = 0; _y < a_height; ++_y)
		{
			uint8_t* _destination = m_atlasData.data() + static_cast<size_t>(a_outY + _y) * m_size + a_outX;
			const uint8_t* _source = a_pBitMap + static_cast<size_t>(_y) * a_width;
			std::memcpy(_destination, _source, a_width);
		}

		MarkDirty(a_outX, a_outY, a_width, a_height);

		return true;
	}

	void FontAtlas::MarkDirty(uint32_t a_x, uint32_t a_y, uint32_t a_width, uint32_t a_height)
	{
		if (a_width == 0 || a_height == 0) return;

		if (!m_isDirty)
		{
			m_isDirty = true;
			m_dirtyMinX = a_x;
			m_dirtyMinY = a_y;
			m_dirtyMaxX = a_x + a_width;
			m_dirtyMaxY = a_y + a_height;
			return;
		}

		// 既にある範囲と合わせて1つの矩形にまとめる。
		// 細かく分けて何度も転送するより、まとめて1回送るほうが安い
		m_dirtyMinX = std::min(m_dirtyMinX, a_x);
		m_dirtyMinY = std::min(m_dirtyMinY, a_y);
		m_dirtyMaxX = std::max(m_dirtyMaxX, a_x + a_width);
		m_dirtyMaxY = std::max(m_dirtyMaxY, a_y + a_height);
	}

	void FontAtlas::Flush(const ResourceBuildContext* a_pContext)
	{
		if (!m_isDirty) return;
		if (m_size == 0 || m_atlasData.empty()) return;
		if (!m_texHandle.IsValid() || m_pTexResource == nullptr) return;

		// グリフは描画の途中で焼かれるので、コンテキスト無しで呼ばれることがある。
		// そのときは登録先を持たない転送だけのバッチを開く
		// (書き換えるのは既にあるテクスチャの中身だけで、マネージャーへ登録するものは無い)
		ResourceBuildScope _scope(a_pContext, ScopedResourceBuild::TransferOnly{});
		const ResourceBuildContext& _context = _scope.GetContext();

		// 積む先が無いときは汚れたままにして、次の機会へ持ち越す
		if (!_context.CanRecordCopy() || _context.pDevice == nullptr) return;

		const uint32_t _regionWidth = m_dirtyMaxX - m_dirtyMinX;
		const uint32_t _regionHeight = m_dirtyMaxY - m_dirtyMinY;
		if (_regionWidth == 0 || _regionHeight == 0)
		{
			m_isDirty = false;
			return;
		}

		// 転送する矩形ぶんのフットプリントを求める
		D3D12_RESOURCE_DESC _regionDesc = m_pTexResource->GetDesc();
		_regionDesc.Width = _regionWidth;
		_regionDesc.Height = _regionHeight;
		_regionDesc.MipLevels = 1;
		_regionDesc.DepthOrArraySize = 1;

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT _layout = {};
		UINT _numRow = 0;
		UINT64 _rowSize = 0;
		UINT64 _uploadSize = 0;
		_context.pDevice->GetCopyableFootprints(
			&_regionDesc, 0, 1, 0, &_layout, &_numRow, &_rowSize, &_uploadSize);

		if (_uploadSize == 0) return;

		// 中間バッファ(Uploadヒープ)
		const auto _heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		const auto _bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(_uploadSize);

		ComPtr<ID3D12Resource> _cpUpload = nullptr;
		HRESULT _hr = _context.pDevice->CreateCommittedResource(
			&_heapProp,
			D3D12_HEAP_FLAG_NONE,
			&_bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(_cpUpload.ReleaseAndGetAddressOf()));
		if (FAILED(_hr) || !_cpUpload)
		{
			ENGINE_WARNING("[Font] アトラス転送用の中間バッファ作成に失敗しました");
			return;
		}
		_cpUpload->SetName(L"FontAtlas_UploadBuffer");
		Graphics::D3D12::VideoMemoryTracker::TrackResource(_cpUpload.Get(), Graphics::D3D12::EVideoMemoryCategory::Upload);

		// 汚れた矩形だけを詰め直す : 行ごとにアトラスの幅ぶん飛ばして読む
		void* _pMapped = nullptr;
		_hr = _cpUpload->Map(0, nullptr, &_pMapped);
		if (FAILED(_hr) || _pMapped == nullptr)
		{
			ENGINE_WARNING("[Font] アトラス転送用の中間バッファのMapに失敗しました");
			return;
		}

		for (UINT _row = 0; _row < _numRow; ++_row)
		{
			uint8_t* _destination =
				static_cast<uint8_t*>(_pMapped) + _layout.Offset + static_cast<size_t>(_row) * _layout.Footprint.RowPitch;
			const uint8_t* _source =
				m_atlasData.data() + static_cast<size_t>(m_dirtyMinY + _row) * m_size + m_dirtyMinX;

			std::memcpy(_destination, _source, static_cast<size_t>(_rowSize));
		}
		_cpUpload->Unmap(0, nullptr);

		// コピー命令を積む : コピー先は汚れた矩形の左上
		D3D12_TEXTURE_COPY_LOCATION _src = {};
		_src.pResource = _cpUpload.Get();
		_src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		_src.PlacedFootprint = _layout;

		D3D12_TEXTURE_COPY_LOCATION _dst = {};
		_dst.pResource = m_pTexResource;
		_dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		_dst.SubresourceIndex = 0;

		_context.pCopyCmdList->CopyTextureRegion(&_dst, m_dirtyMinX, m_dirtyMinY, 0, &_src, nullptr);

		// 転送が終わるまで中間バッファを生かしておく
		_context.KeepAlive(_cpUpload);

		m_isDirty = false;
	}

	//==========================================================================================
	// Font
	//==========================================================================================
	Font::Font() = default;
	Font::~Font() = default;
	Font::Font(Font&&) noexcept = default;
	Font& Font::operator=(Font&&) noexcept = default;

	bool Font::Load(
		const std::string& a_path,
		const ResourceBuildContext* a_pContext,
		float a_fontSize,
		uint32_t a_atlasSize)
	{
		Clear();

		if (a_fontSize <= 0.0f)
		{
			ENGINE_WARNING("[Font] 基準サイズが0以下です : %s", a_path.c_str());
			return false;
		}

		// .ttfをバイナリで開く
		std::ifstream _file(a_path, std::ios::binary);
		if (!_file.is_open())
		{
			ENGINE_WARNING("[Font] フォントファイルを開けません : %s", a_path.c_str());
			return false;
		}

		// ファイルサイズを取得
		_file.seekg(0, std::ios::end);
		const std::streamsize _fileSize = _file.tellg();
		_file.seekg(0, std::ios::beg);
		if (_fileSize <= 0)
		{
			ENGINE_WARNING("[Font] フォントファイルが空です : %s", a_path.c_str());
			return false;
		}

		// フォントデータを確保して読込
		m_fontData.resize(static_cast<size_t>(_fileSize));
		if (!_file.read(reinterpret_cast<char*>(m_fontData.data()), _fileSize))
		{
			ENGINE_WARNING("[Font] フォントファイルの読み込みに失敗しました : %s", a_path.c_str());
			m_fontData.clear();
			return false;
		}

		// stb_truetypeを初期化 : .ttc(複数入り)は先頭のフォントを使う
		const int _fontOffset = stbtt_GetFontOffsetForIndex(m_fontData.data(), 0);
		if (_fontOffset < 0)
		{
			ENGINE_WARNING("[Font] フォントとして解釈できません : %s", a_path.c_str());
			m_fontData.clear();
			return false;
		}

		m_upFontInfo = std::make_unique<stbtt_fontinfo>();
		if (!stbtt_InitFont(m_upFontInfo.get(), m_fontData.data(), _fontOffset))
		{
			ENGINE_WARNING("[Font] フォントの初期化に失敗しました : %s", a_path.c_str());
			m_upFontInfo.reset();
			m_fontData.clear();
			return false;
		}

		// フォント内部単位 -> ピクセル
		m_fontSize = a_fontSize;
		m_scale = stbtt_ScaleForPixelHeight(m_upFontInfo.get(), m_fontSize);

		// フォント全体のメトリクスを取得
		int _ascent = 0;
		int _descent = 0;
		int _lineGap = 0;

		stbtt_GetFontVMetrics(
			m_upFontInfo.get(),
			&_ascent,
			&_descent,
			&_lineGap
		);

		// 内部単位のまま持つと使う側で毎回スケールを掛けることになるので、ピクセルにして持つ
		m_ascent = static_cast<float>(_ascent) * m_scale;
		m_descent = static_cast<float>(_descent) * m_scale;
		m_lineGap = static_cast<float>(_lineGap) * m_scale;

		// アトラステクスチャ作成
		if (!m_fontAtlas.Create(a_atlasSize, a_pContext))
		{
			m_upFontInfo.reset();
			m_fontData.clear();
			return false;
		}

		// ここでは転送しない。
		// 全面0の初期化ぶんは汚れ範囲に残るので、最初の Flush で焼いたグリフとまとめて1回で送られる

		return true;
	}

	void Font::Clear()
	{
		m_fontAtlas.Release();

		m_glyphMap.clear();

		m_upFontInfo.reset();
		m_fontData.clear();
		m_fontData.shrink_to_fit();

		m_fontSize = DEFAULT_FONT_SIZE;
		m_scale = 0.0f;
		m_ascent = 0.0f;
		m_descent = 0.0f;
		m_lineGap = 0.0f;

		m_isBatching = false;
	}

	bool Font::IsValid() const
	{
		return m_upFontInfo != nullptr && m_fontAtlas.IsValid();
	}

	const Glyph* Font::RequestGlyph(uint32_t a_codePoint)
	{
		auto _it = m_glyphMap.find(a_codePoint);
		if (_it != m_glyphMap.end())
		{
			return &_it->second;
		}

		return CreateGlyph(a_codePoint);
	}

	const Glyph* Font::CreateGlyph(uint32_t a_codePoint)
	{
		if (!IsValid()) return nullptr;

		// glyph の advance を取得
		int _advance = 0;
		int _leftSideBearing = 0;

		stbtt_GetCodepointHMetrics(
			m_upFontInfo.get(),
			static_cast<int>(a_codePoint),
			&_advance,
			&_leftSideBearing
		);

		// Glyph情報の作成
		Glyph _glyph = {};
		_glyph.codePoint = a_codePoint;

		// フォントの内部単位からピクセルへ変換
		_glyph.xAdvance = static_cast<float>(_advance) * m_scale;

		// glyph の bitmap を生成
		int _width = 0;
		int _height = 0;

		int _xOffset = 0;
		int _yOffset = 0;

		unsigned char* _pBitmap = stbtt_GetCodepointBitmap(
			m_upFontInfo.get(),
			m_scale,
			m_scale,
			static_cast<int>(a_codePoint),
			&_width,
			&_height,
			&_xOffset,
			&_yOffset
		);

		// 絵を持たない文字(半角スペースなど)は送り量だけ覚えておく。
		// ここで弾いておかないと、毎回焼き直しに来てしまう
		if (_pBitmap != nullptr && _width > 0 && _height > 0)
		{
			// アトラス上の領域を確保
			uint32_t _atlasX = 0;
			uint32_t _atlasY = 0;
			const bool _isAllocated = m_fontAtlas.Allocate(
				_pBitmap,
				static_cast<uint32_t>(_width),
				static_cast<uint32_t>(_height),
				_atlasX,
				_atlasY);

			if (_isAllocated)
			{
				_glyph.x = _atlasX;
				_glyph.y = _atlasY;
				_glyph.width = static_cast<uint32_t>(_width);
				_glyph.height = static_cast<uint32_t>(_height);
				_glyph.xOffset = static_cast<float>(_xOffset);
				_glyph.yOffset = static_cast<float>(_yOffset);
			}
		}

		// bitmapの解放
		if (_pBitmap != nullptr) stbtt_FreeBitmap(_pBitmap, nullptr);

		// 登録
		auto [_it, _isInserted] = m_glyphMap.emplace(a_codePoint, _glyph);

		// GPUテクスチャ更新 : まとめ焼き中は最後に1回だけ送る
		if (!m_isBatching)
		{
			m_fontAtlas.Flush(nullptr);
		}

		return &_it->second;
	}

	void Font::Preload(const std::string& a_utf8Text, const ResourceBuildContext* a_pContext)
	{
		if (!IsValid()) return;

		// 1文字ごとに転送していては本数が増えるので、まとめて焼いてから1回だけ送る
		m_isBatching = true;
		for (const uint32_t _codePoint : Core::String::ToCodePoints(a_utf8Text))
		{
			RequestGlyph(_codePoint);
		}
		m_isBatching = false;

		m_fontAtlas.Flush(a_pContext);
	}

	void Font::PreloadAscii(const ResourceBuildContext* a_pContext)
	{
		if (!IsValid()) return;

		m_isBatching = true;
		for (uint32_t _codePoint = 0x20; _codePoint <= 0x7E; ++_codePoint)
		{
			RequestGlyph(_codePoint);
		}
		m_isBatching = false;

		m_fontAtlas.Flush(a_pContext);
	}

	float Font::GetKerning(uint32_t a_prevCodePoint, uint32_t a_nextCodePoint) const
	{
		if (!m_upFontInfo) return 0.0f;

		const int _kern = stbtt_GetCodepointKernAdvance(
			m_upFontInfo.get(),
			static_cast<int>(a_prevCodePoint),
			static_cast<int>(a_nextCodePoint));

		return static_cast<float>(_kern) * m_scale;
	}

	Math::Vector2 Font::MeasureText(const std::string& a_utf8Text)
	{
		Math::Vector2 _size = {};
		if (!IsValid()) return _size;

		const float _lineHeight = GetLineHeight();

		float _lineWidth = 0.0f;
		int _lineCount = 1;
		uint32_t _prevCodePoint = 0;

		for (const uint32_t _codePoint : Core::String::ToCodePoints(a_utf8Text))
		{
			if (_codePoint == '\n')
			{
				_size.x = std::max(_size.x, _lineWidth);
				_lineWidth = 0.0f;
				_prevCodePoint = 0;
				++_lineCount;
				continue;
			}

			// 復帰は行送りを持たないので読み飛ばす
			if (_codePoint == '\r') continue;

			const Glyph* _pGlyph = RequestGlyph(_codePoint);
			if (_pGlyph == nullptr) continue;

			if (_prevCodePoint != 0) _lineWidth += GetKerning(_prevCodePoint, _codePoint);
			_lineWidth += _pGlyph->xAdvance;

			_prevCodePoint = _codePoint;
		}

		_size.x = std::max(_size.x, _lineWidth);
		_size.y = _lineHeight * static_cast<float>(_lineCount);

		return _size;
	}

	float Font::GetLineHeight() const
	{
		// descent は負なので引く
		return m_ascent - m_descent + m_lineGap;
	}
}
