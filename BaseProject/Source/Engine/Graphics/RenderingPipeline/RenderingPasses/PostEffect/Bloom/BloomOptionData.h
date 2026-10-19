#pragma once
namespace Engine::Graphics
{
	// 川瀬式ブルームの調整値
	// 抽出パスと合成パスがそれぞれ自分の値を詰めて送る。
	// ※ HLSL 側(Asset/Shader/Common/RootParameters/BloomOptionData.hlsli)と並びを合わせること
	struct BloomOptionCB
	{
		float threshold;	// 高輝度として抽出し始める輝度
		float softKnee;		// しきい値付近をなめらかにつなぐ幅の割合(0でハードカット)
		float intensity;	// 合成時のブルームの強さ
		int   enable;		// 0 ならブルームを掛けない
	};
}
