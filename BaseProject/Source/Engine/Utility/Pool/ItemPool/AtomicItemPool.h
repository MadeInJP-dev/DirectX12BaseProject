#pragma once

#include "Core/Debug/DebugLog.h"

namespace Engine::Pool
{
	//==========================================================================================
	// ItemPool のスレッドセーフ版
	//
	// ItemPool にミューテックスを足しただけでは安全にならない点が2つあるため、
	// そこだけ作りを変えている。
	//
	// 1) 実体を std::vector ではなく std::deque で持つ
	//    vector は要素が増えたときに配列ごと引っ越すため、
	//    Ref()/Get() が返した T* が、別スレッドの Add() 一発で全部ダングリングする。
	//    ポインタはロックを抜けた後に呼び出し元が握っているので、
	//    ミューテックスをいくら足しても防げない。
	//    deque は末尾追加で既存要素への参照が無効にならないので、
	//    「取得済みのポインタは Add() では壊れない」を保証できる。
	//
	// 2) GetAll()/RefAll() を提供しない
	//    コンテナの参照をそのまま返すと、中身がロックの外へ丸ごと漏れる。
	//    件数は Size()、走査は ForEach() に置き換えること。
	//
	// ---- 返ってくるポインタの寿命 ----
	// Ref()/Get()/Access() が返すポインタは Add() に対しては安全だが、
	// 同じスロットへの Remove()/Release() には勝てない(実体が破棄されるため)。
	// Remove()/Release() は「誰もポインタを持っていない」と言えるタイミング
	// (GCのスイープなど、ジョブを流していないところ) で呼ぶこと。
	// ロックの中で読み書きを完結させたい場合は Read()/Write() を使う。
	//
	// ---- 破棄はロックの外で ----
	// Remove()/Release() はロックの中で実体をスロットから取り出すだけにして、
	// デストラクタはロックを外してから走らせる。
	// 実体が同じプールのリソースへの参照(ResourceRef)を持っていると、
	// そのデストラクタが IsValid() でこのプールのロックを取りに来る。
	// std::shared_mutex は同じスレッドでも二重に取れないので、ロックの中で壊すと
	// 自分で自分を待ち続けて止まる(LOD モデルを持つ Model で、終了時に固まった)
	//==========================================================================================
	template<typename T>
	class AtomicItemPool
	{
	public:
		AtomicItemPool() = default;
		~AtomicItemPool() = default;

		// ミューテックスを抱えるのでコピーもムーブも不可
		AtomicItemPool(const AtomicItemPool&) = delete;
		AtomicItemPool& operator=(const AtomicItemPool&) = delete;
		AtomicItemPool(AtomicItemPool&&) = delete;
		AtomicItemPool& operator=(AtomicItemPool&&) = delete;

		/// <summary>
		/// 解放処理
		/// 実体をすべて破棄するため、取得済みのポインタはすべて無効になる
		/// </summary>
		void Release();

		/// <summary>
		/// 領域確保
		/// 実体側(deque)はブロック単位で伸びるため予約できない。
		/// ここで確保できるのは世代・空き番号の管理配列だけになる
		/// </summary>
		/// <param name="a_capacity">確保したい最大サイズ</param>
		void Reserve(size_t a_capacity);

		/// <summary>
		/// プールに追加
		/// </summary>
		/// <param name="a_resource">登録する実態</param>
		/// <returns>登録して生成したハンドルを返す</returns>
		Handle<T> Add(T&& a_resource);

		/// <summary>
		/// プールから削除 : 参照数を無視して削除するため、複数をまたいで管理するようなものには
		/// FreeRefのほうを使用すること
		/// </summary>
		/// <param name="a_handle">削除したいデータのハンドル</param>
		void Remove(const Handle<T>& a_handle);

		/// <summary>
		/// ポインタ参照
		/// 返した後はロックが外れる。Remove()と同時に使わないこと
		/// </summary>
		/// <param name="a_handle">参照したいハンドル</param>
		/// <returns>ポインタで返る</returns>
		T* Ref(const Handle<T>& a_handle);

		/// <summary>
		/// 読み取り専用参照
		/// 返した後はロックが外れる。Remove()と同時に使わないこと
		/// </summary>
		/// <param name="a_handle">ハンドル</param>
		/// <returns>const ポインタで返る</returns>
		const T* Get(const Handle<T>& a_handle) const;

		/// <summary>
		/// スロットに実体が入っているか
		///
		/// 添え字で実体そのものを取り出す口はあえて用意していない。
		/// 世代を見ずに引けてしまうと、スロットが再利用されていたときに
		/// 別のリソースを掴んでもそれに気づけないため。
		/// 実体が要る場合はハンドルを作って Get()/Ref() を通すこと。
		/// これは全スロットを舐めるスイープ処理のための存在確認用
		/// </summary>
		/// <param name="a_index">スロット番号</param>
		/// <returns>範囲内かつ実体があれば true</returns>
		bool IsOccupied(uint16_t a_index) const;

		/// <summary>
		/// インデックスから世代を取得
		/// </summary>
		/// <param name="a_index">ハンドルインデックス</param>
		/// <returns>世代 : 範囲外なら0</returns>
		uint16_t GetGeneration(uint16_t a_index) const;

		/// <summary>
		/// プール内に存在するかのチェック : 実体の内部は考慮しない
		/// </summary>
		/// <param name="a_handle">確認したいハンドル</param>
		/// <returns>存在するのならば true </returns>
		bool IsValid(const Handle<T>& a_handle) const;

		/// <summary>
		/// スロット数 : 空きスロットも含んだ、確保済み領域の大きさ
		/// ItemPool の GetAll().size() に相当する
		/// </summary>
		size_t Size() const;

		//--------------------------------------------------------------------------------------
		// ロックの中で完結させるアクセス
		//
		// ポインタを外へ持ち出さないので、Remove() と同時に走っても実体が消えることはない。
		// 渡す処理の中からこのプールを触るとデッドロックするので注意
		//--------------------------------------------------------------------------------------

		/// <summary>
		/// ロックしたまま読み取る
		/// </summary>
		/// <param name="a_handle">ハンドル</param>
		/// <param name="a_func">const T&amp; を受け取る処理</param>
		/// <returns>実体があって処理を呼べたら true</returns>
		template<typename F>
		bool Read(const Handle<T>& a_handle, F&& a_func) const;

		/// <summary>
		/// ロックしたまま書き換える
		/// </summary>
		/// <param name="a_handle">ハンドル</param>
		/// <param name="a_func">T&amp; を受け取る処理</param>
		/// <returns>実体があって処理を呼べたら true</returns>
		template<typename F>
		bool Write(const Handle<T>& a_handle, F&& a_func);

		/// <summary>
		/// 中身のあるスロットだけを走査する : ItemPool の RefAll() の置き換え
		/// </summary>
		/// <param name="a_func">(uint16_t インデックス, T&amp; 実体) を受け取る処理</param>
		template<typename F>
		void ForEach(F&& a_func);

		/// <summary>
		/// 中身のあるスロットだけを走査する : 読み取り専用
		/// </summary>
		/// <param name="a_func">(uint16_t インデックス, const T&amp; 実体) を受け取る処理</param>
		template<typename F>
		void ForEach(F&& a_func) const;

	private:

		/// <summary>
		/// ロック済み前提の存在チェック
		/// std::shared_mutex は再帰的に取れないため、
		/// ロックを持っている側から IsValid() を呼ぶと自分で自分を止めてしまう。
		/// 内部からはこちらを使うこと
		/// </summary>
		bool IsValidUnlocked(const Handle<T>& a_handle) const;

	private:

		// データ
		std::deque<std::optional<T>> m_data;		// 実体データ : 伸ばしても既存要素の参照が生きる
		std::vector<uint16_t> m_generations;		// 領域の世代
		std::vector<uint16_t> m_freeIndices;		// 使っていない領域

		// 読み取りは同時に通し、書き換えのときだけ独占する
		mutable std::shared_mutex m_mutex;
	};

	template<typename T>
	inline void AtomicItemPool<T>::Release()
	{
		// 実体はロックの中で取り出すだけにして、壊すのはロックを外してから
		std::deque<std::optional<T>> _released;
		{
			std::unique_lock _lock(m_mutex);

			_released.swap(m_data);
			m_generations.clear();
			m_freeIndices.clear();
		}
	}

	template<typename T>
	inline void AtomicItemPool<T>::Reserve(size_t a_capacity)
	{
		std::unique_lock _lock(m_mutex);

		// deque には reserve がないため、管理配列だけ先に伸ばしておく
		m_generations.reserve(a_capacity);
		m_freeIndices.reserve(a_capacity);
	}

	template<typename T>
	inline Handle<T> AtomicItemPool<T>::Add(T&& a_resource)
	{
		std::unique_lock _lock(m_mutex);

		uint16_t _index = 0;
		// 確保領域にまだ空きがあるのなら
		if (!m_freeIndices.empty())
		{
			// 空き領域を再利用
			_index = m_freeIndices.back();
			m_freeIndices.pop_back();
			m_data[_index] = std::move(a_resource);		// 実体の移動
			m_generations[_index]++;					// 世代を進める
		}
		else
		{
			// インデックスが16bitの上限を超えていないかチェック
			ENGINE_ERRLOG(m_data.size() < 0xFFFF, "AtomicItemPoolの最大値が uint16_t のサイズを超えています");

			// assert が消える構成でも、超えたまま進めると別スロットを指すハンドルができる。
			// 無効なハンドルを返して打ち切る
			if (m_data.size() >= 0xFFFF) return Handle<T>();

			// 新規追加
			_index = static_cast<uint16_t>(m_data.size());
			m_data.emplace_back(std::move(a_resource));
			m_generations.push_back(1);
		}

		// ハンドルを返す
		Handle<T> _res(_index, m_generations[_index]);
		return _res;
	}

	template<typename T>
	inline void AtomicItemPool<T>::Remove(const Handle<T>& a_handle)
	{
		// 実体はロックの中で取り出すだけにして、壊すのはロックを外してから
		std::optional<T> _removed;
		{
			std::unique_lock _lock(m_mutex);

			// 存在チェック
			if (IsValidUnlocked(a_handle))
			{
				auto& _slot = m_data[a_handle.GetIndex()];
				_removed = std::move(_slot);
				_slot.reset();

				m_freeIndices.push_back(a_handle.GetIndex());	// インデックスキューに返却
				m_generations[a_handle.GetIndex()]++;			// 無効化のため世代を進める
			}
		}
	}

	template<typename T>
	inline T* AtomicItemPool<T>::Ref(const Handle<T>& a_handle)
	{
		std::shared_lock _lock(m_mutex);

		// 存在チェック
		if (IsValidUnlocked(a_handle))
		{
			return &m_data[a_handle.GetIndex()].value();
		}

		// なければnullptr
		return nullptr;
	}

	template<typename T>
	inline const T* AtomicItemPool<T>::Get(const Handle<T>& a_handle) const
	{
		std::shared_lock _lock(m_mutex);

		// 存在チェック
		if (IsValidUnlocked(a_handle))
		{
			return &m_data[a_handle.GetIndex()].value();
		}

		// なければnullptr
		return nullptr;
	}

	template<typename T>
	inline bool AtomicItemPool<T>::IsOccupied(uint16_t a_index) const
	{
		std::shared_lock _lock(m_mutex);

		// スロット数は他スレッドの Add() で変わるため、必ず範囲を見てから触る
		if (a_index >= m_data.size()) return false;

		return m_data[a_index].has_value();
	}

	template<typename T>
	inline uint16_t AtomicItemPool<T>::GetGeneration(uint16_t a_index) const
	{
		std::shared_lock _lock(m_mutex);

		return (a_index < m_generations.size()) ? m_generations[a_index] : 0;
	}

	template<typename T>
	inline bool AtomicItemPool<T>::IsValid(const Handle<T>& a_handle) const
	{
		std::shared_lock _lock(m_mutex);

		return IsValidUnlocked(a_handle);
	}

	template<typename T>
	inline size_t AtomicItemPool<T>::Size() const
	{
		std::shared_lock _lock(m_mutex);

		return m_data.size();
	}

	template<typename T>
	template<typename F>
	inline bool AtomicItemPool<T>::Read(const Handle<T>& a_handle, F&& a_func) const
	{
		std::shared_lock _lock(m_mutex);

		if (!IsValidUnlocked(a_handle)) return false;

		a_func(static_cast<const T&>(m_data[a_handle.GetIndex()].value()));
		return true;
	}

	template<typename T>
	template<typename F>
	inline bool AtomicItemPool<T>::Write(const Handle<T>& a_handle, F&& a_func)
	{
		std::unique_lock _lock(m_mutex);

		if (!IsValidUnlocked(a_handle)) return false;

		a_func(m_data[a_handle.GetIndex()].value());
		return true;
	}

	template<typename T>
	template<typename F>
	inline void AtomicItemPool<T>::ForEach(F&& a_func)
	{
		std::unique_lock _lock(m_mutex);

		for (size_t _i = 0; _i < m_data.size(); ++_i)
		{
			if (!m_data[_i].has_value()) continue;

			a_func(static_cast<uint16_t>(_i), m_data[_i].value());
		}
	}

	template<typename T>
	template<typename F>
	inline void AtomicItemPool<T>::ForEach(F&& a_func) const
	{
		std::shared_lock _lock(m_mutex);

		for (size_t _i = 0; _i < m_data.size(); ++_i)
		{
			if (!m_data[_i].has_value()) continue;

			a_func(static_cast<uint16_t>(_i), static_cast<const T&>(m_data[_i].value()));
		}
	}

	template<typename T>
	inline bool AtomicItemPool<T>::IsValidUnlocked(const Handle<T>& a_handle) const
	{
		// インデックスが配列サイズ以上かどうか
		if (a_handle.GetIndex() >= m_data.size())
		{
			return false;
		}
		// ジェネレーションが一致するかどうか
		if (m_generations[a_handle.GetIndex()] != a_handle.GetGeneration())
		{
			return false;
		}

		// データの存在チェック
		if (!m_data[a_handle.GetIndex()].has_value())
		{
			return false;
		}

		// 存在する
		return true;
	}
}
