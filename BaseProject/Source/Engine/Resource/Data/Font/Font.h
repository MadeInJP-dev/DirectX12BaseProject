#pragma once

// stb_truetype.h はここでは読まない。
// このヘッダーは EngineCommon 経由でプリコンパイル済みヘッダーに入るため、
// ここで外部ライブラリを引き込むと全翻訳単位が巻き添えになる。
// 実装(Font.cpp)側だけで読み、ここでは前方宣言で足りるようにしておく
struct stbtt_fontinfo;

namespace Engine::Resource
{
	//==========================================================================================
	// フォントの1文字ぶんの情報
	//
	// 座標系はすべてピクセル(左上原点/Y下向き)。基準サイズ(Font::GetFontSize())で焼いた値なので、
	// 実際に出したい大きさが違うときは呼び出し側で (出したいサイズ / 基準サイズ) を掛ける。
	//
	// ペン位置を (penX, baselineY) とすると、この文字の描画矩形の左上は
	//   (penX + xOffset, baselineY + yOffset)
	// になり、次の文字のペン位置は penX + xAdvance へ進む。
	// yOffset はベースラインより上が負になる
	//==========================================================================================
	struct Glyph
	{
		uint32_t codePoint = 0;

		// アトラステクスチャ上の位置とサイズ : Pixel
		uint32_t  x = 0;
		uint32_t  y = 0;
		uint32_t  width = 0;
		uint32_t  height = 0;

		// 描画ベースラインに対するオフセット
		float xOffset = 0.0f;
		float yOffset = 0.0f;

		// 次のGlyphを配置するまでの移動量
		float xAdvance = 0.0f;

		// 絵を持たない文字(半角スペースなど)か
		bool IsEmpty() const { return width == 0 || height == 0; }
	};

	//==========================================================================================
	// グリフを1枚のテクスチャへ敷き詰めるアトラス
	//
	// ・CPU側に同じ大きさのグレースケール配列を持ち、そこへ焼いてからGPUへ転送する。
	//   転送は「汚れた矩形」をまとめて1回だけ行うので、まとめ焼き(Font::Preload)なら転送も1回で済む。
	//
	// ・テクスチャは R8_UNORM 1枚。SRVのスウィズルで (r,r,r,r) に配ってあるので、
	//   既存のUIシェーダーがそのまま「白い文字 × 頂点カラー」として扱える
	//   (UIData 側にフォント用のフラグを足さずに済ませるため)。
	//
	// ・並べ方は横一列に詰めて、入らなくなったら次の行へ落とすだけの単純なもの。
	//   一度置いた場所は動かさないので、確定したグリフのUVは後から変わらない
	//==========================================================================================
	class FontAtlas
	{
	public:

		FontAtlas() = default;
		~FontAtlas() = default;
		NON_COPYABLE_MOVABLE(FontAtlas);

		// 解放 : テクスチャの参照を返して、詰め位置を巻き戻す
		void Release();

		/// <summary>
		/// アトラスを作る
		/// </summary>
		/// <param name="a_size">1辺のピクセル数(正方形)</param>
		/// <param name="a_pContext">ビルドコンテキスト : 無ければその場でバッチを開く</param>
		bool Create(uint32_t a_size, const ResourceBuildContext* a_pContext);

		/// <summary>
		/// グリフのビットマップを1つ置く
		/// </summary>
		/// <param name="a_pBitMap">8bitグレースケール、a_width × a_height、行間の隙間なし</param>
		/// <param name="a_outX">置いた位置(px)</param>
		/// <param name="a_outY">置いた位置(px)</param>
		/// <returns>置けたら true。埋まっていたら false</returns>
		bool Allocate(const uint8_t* a_pBitMap, uint32_t a_width, uint32_t a_height, uint32_t& a_outX, uint32_t& a_outY);

		/// <summary>
		/// 前回の転送以降に書き換えた範囲をGPUへ送る
		/// </summary>
		/// <remarks>
		/// 送れなかった(コマンドリストが取れなかった)場合は汚れたままにしておくので、
		/// 次に呼んだときにまとめて送られる。
		/// コンテキストが無ければ、その場で転送だけのバッチを開く(ResourceManager は使わない)
		/// </remarks>
		void Flush(const ResourceBuildContext* a_pContext);

		// 取得
		const Handle<Texture>& GetTextureHandle() const { return m_texHandle; }
		uint32_t GetSize() const { return m_size; }
		bool IsValid() const { return m_texHandle.IsValid(); }

	private:

		// 書き換えた範囲を覚えておく
		void MarkDirty(uint32_t a_x, uint32_t a_y, uint32_t a_width, uint32_t a_height);

	private:

		// アトラステクスチャ : ResourceManager に預けてハンドルで持つ
		//
		// 実体を直接抱えないのは、UIの描画命令(SubmitUI)がハンドル受け取りだから。
		// 参照は m_texRef が握っているので、シーンの切れ目のスイープでは消えない
		ResourceRef<Texture> m_texRef = {};
		Handle<Texture> m_texHandle = {};

		// 転送先のGPUリソース(非所有) : 生かしているのは m_texRef
		//
		// グリフは描画の途中で焼かれ、そのとき呼び出し側はビルドコンテキストを持っていない。
		// Flush でマネージャーからテクスチャを引き直さずに済むよう、作ったときに覚えておく
		ID3D12Resource* m_pTexResource = nullptr;

		// CPU側の焼き付け先 : 1ピクセル1バイト
		std::vector<uint8_t> m_atlasData = {};

		uint32_t m_size = 0;

		// 詰め位置
		uint32_t m_cursorX = 0;
		uint32_t m_cursorY = 0;
		uint32_t m_rowHeight = 0;

		// GPUへ送っていない範囲 [min, max)
		bool m_isDirty = false;
		uint32_t m_dirtyMinX = 0;
		uint32_t m_dirtyMinY = 0;
		uint32_t m_dirtyMaxX = 0;
		uint32_t m_dirtyMaxY = 0;

		// 埋まった警告は1回だけ出す : 毎フレームのログは重い
		bool m_isWarnedFull = false;
	};

	//==========================================================================================
	// フォント(.ttf / .otf / .ttc)
	//
	// ・大きめの基準サイズ(既定 64px)で1回だけ焼き、使う側で縮小して出す前提。
	//   サイズごとにアトラスを持たない代わりに、拡大するとぼやける。
	//
	// ・グリフは要求されたときに焼く(RequestGlyph)。出てくる文字が分かっているなら
	//   Preload でまとめて焼いておくと、GPUへの転送が1回で済む。
	//
	// ・メインスレッドから触る前提でロックを持っていない。
	//   ワーカースレッドから RequestGlyph を呼ばないこと(読むだけでも作りに行くため)
	//==========================================================================================
	class Font
	{
	public:

		// 焼き付ける基準サイズ(px)。大きめに焼いて、使う側で縮小する
		static constexpr float DEFAULT_FONT_SIZE = 64.0f;

		// アトラス1辺の既定サイズ(px)
		static constexpr uint32_t DEFAULT_ATLAS_SIZE = 2048;

		Font();
		~Font();

		// コピー禁止・ムーブ可 : stbtt_fontinfo が不完全型なので実装は cpp 側に置く
		Font(const Font&) = delete;
		Font& operator=(const Font&) = delete;
		Font(Font&&) noexcept;
		Font& operator=(Font&&) noexcept;

		/// <summary>
		/// フォントファイルを読み込む
		/// </summary>
		/// <param name="a_path">.ttf / .otf / .ttc のパス</param>
		/// <param name="a_pContext">ビルドコンテキスト : 無ければその場でバッチを開く</param>
		/// <param name="a_fontSize">焼き付ける基準サイズ(px)</param>
		/// <param name="a_atlasSize">アトラス1辺のサイズ(px)</param>
		bool Load(
			const std::string& a_path,
			const ResourceBuildContext* a_pContext = nullptr,
			float a_fontSize = DEFAULT_FONT_SIZE,
			uint32_t a_atlasSize = DEFAULT_ATLAS_SIZE);

		// 中身を捨てる
		void Clear();

		// 使える状態か
		bool IsValid() const;

		//--------------------------------------------------------------------------------------
		// グリフ
		//--------------------------------------------------------------------------------------

		/// <summary>
		/// グリフの要求 : 無ければその場で焼く
		/// </summary>
		/// <returns>焼けなければ nullptr。返るポインタは Clear / Load まで有効</returns>
		const Glyph* RequestGlyph(uint32_t a_codePoint);

		/// <summary>
		/// 文字列に出てくる文字をまとめて焼く : GPUへの転送は最後に1回だけ
		/// </summary>
		/// <param name="a_utf8Text">UTF-8文字列</param>
		void Preload(const std::string& a_utf8Text, const ResourceBuildContext* a_pContext = nullptr);

		// 半角の英数字・記号(U+0020〜U+007E)をまとめて焼く
		void PreloadAscii(const ResourceBuildContext* a_pContext = nullptr);

		/// <summary>
		/// 隣り合う2文字のカーニング(詰め量, px)
		/// </summary>
		/// <remarks>xAdvance へ足して使う。持っていないフォントなら 0 が返る</remarks>
		float GetKerning(uint32_t a_prevCodePoint, uint32_t a_nextCodePoint) const;

		//--------------------------------------------------------------------------------------
		// 計測 : すべて基準サイズでのピクセル値
		//--------------------------------------------------------------------------------------

		/// <summary>
		/// 文字列を1行で並べたときの大きさ(px)
		/// </summary>
		/// <remarks>
		/// 未生成の文字があれば焼きにいくので const にしていない。
		/// 改行(\n)を含む場合は、一番長い行の幅と、行数ぶんの高さが返る
		/// </remarks>
		Math::Vector2 MeasureText(const std::string& a_utf8Text);

		//--------------------------------------------------------------------------------------
		// 取得
		//--------------------------------------------------------------------------------------

		// 焼き付けた基準サイズ(px)
		float GetFontSize() const { return m_fontSize; }

		// 行送り(px) : ascent - descent + lineGap
		float GetLineHeight() const;

		// ベースラインから上の高さ(px, 正)
		float GetAscent() const { return m_ascent; }

		// ベースラインから下の深さ(px, 負)
		float GetDescent() const { return m_descent; }

		// 出したいピクセルサイズに対する縮小率
		float GetScaleForSize(float a_pixelSize) const { return (m_fontSize > 0.0f) ? (a_pixelSize / m_fontSize) : 1.0f; }

		// アトラス
		const FontAtlas& GetAtlas() const { return m_fontAtlas; }
		const Handle<Texture>& GetAtlasTextureHandle() const { return m_fontAtlas.GetTextureHandle(); }
		uint32_t GetAtlasSize() const { return m_fontAtlas.GetSize(); }

	private:

		// グリフの作成 : 焼いてアトラスへ置き、登録して返す
		const Glyph* CreateGlyph(uint32_t a_codePoint);

	private:

		// .ttfファイルの実データ
		// stbtt_fontinfo がこのバッファを指しているので、読み込んだ後は動かさないこと
		std::vector<uint8_t> m_fontData = {};

		std::unique_ptr<stbtt_fontinfo> m_upFontInfo = nullptr;

		// 生成済みのグリフ
		std::unordered_map<uint32_t, Glyph> m_glyphMap = {};

		// アトラス
		FontAtlas m_fontAtlas = {};

		// フォント全体の情報 : すべて基準サイズでのピクセル値
		float m_fontSize = DEFAULT_FONT_SIZE;
		float m_scale = 0.0f;		// フォント内部単位 -> ピクセル
		float m_ascent = 0.0f;
		float m_descent = 0.0f;
		float m_lineGap = 0.0f;

		// まとめ焼き中は転送しない : Preload が終わったところで1回だけ送る
		bool m_isBatching = false;
	};
}
