#pragma once

namespace Engine::Audio
{
	//======================================================================================
	// 音のグループ
	//
	// 発行したサウンドインスタンスに1つ付ける札。
	// シーンとプレイヤーの音量はこの単位で持つので、「BGMだけ絞る」「SEだけ絞る」が
	// 鳴らしている側を1つも触らずにできる。
	//
	// 実際に鳴る音量は
	//     呼び出し側が指定した音量 × シーンの音量(マスター × グループ)
	//                              × プレイヤーの音量(マスター × グループ)
	// で決まる。呼び出し側は自分の都合(遠い・小さい音にしたい等)だけを指定すればよく、
	// 設定画面のことを知らなくてよい。
	//
	// ※ 並びは保存される(シーンの音量が添え字で並ぶ)ので、
	//    増やすときは Count の手前へ足すこと
	//======================================================================================
	enum class ESoundGroup : uint32_t
	{
		Se = 0,		// 効果音 : 指定しなかったものはここへ入る
		Bgm,		// 音楽
		Ui,			// UIの操作音(カーソルが乗った・押した)
		Voice,		// 声

		Count
	};

	// グループの数
	inline constexpr size_t SOUND_GROUP_COUNT = static_cast<size_t>(ESoundGroup::Count);

	//======================================================================================
	// 音量のつり合い(マスター + グループごと)
	//
	// 作り手が決めるもので、シーンが1つずつ持つ(Scene::SceneAudio)。
	// プレイヤーの音量(設定画面)はこれへさらに掛かる
	//======================================================================================
	struct SoundVolume
	{
		// 全体へ掛かる音量
		float master = 1.0f;

		// グループごとの音量。並びは ESoundGroup と同じ
		std::array<float, SOUND_GROUP_COUNT> groupArray = { 1.0f, 1.0f, 1.0f, 1.0f };

		// そのグループへ掛ける倍率(マスター込み)
		float CalcScale(ESoundGroup a_group) const
		{
			const size_t _index = static_cast<size_t>(a_group);
			if (_index >= groupArray.size()) return master;

			return master * groupArray[_index];
		}

		bool operator==(const SoundVolume&) const = default;
	};

	//======================================================================================
	// 音量の席の番号
	//
	// AudioManager に預けた音量(SoundVolume)を指す。シーンが開くときに1つ取る。
	// 0 はどの席でもない : シーンの外で鳴らした音(エディターの試聴など)は素通しで鳴る
	//======================================================================================
	using VolumeScopeID = uint32_t;
	inline constexpr VolumeScopeID INVALID_VOLUME_SCOPE_ID = 0;
}
