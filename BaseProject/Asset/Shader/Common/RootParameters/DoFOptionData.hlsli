// 被写界深度の調整値。アクティブカメラの FocusParamComponent を CoC/DoF 両パスへ送る。
//
//   ピントが合う範囲 : focusDistance ± focusRange * 0.5
//   そこから手前へ nearRange 進むと最大ボケ(CoC = -1)
//   そこから奥へ   farRange  進むと最大ボケ(CoC = +1)
//
// ※ CPU 側 Engine::Graphics::DoFOptionCB と並びを合わせること
#ifndef ROOTPARAM_DOF_OPTION_DATA_HLSLI
#define ROOTPARAM_DOF_OPTION_DATA_HLSLI

struct DoFOptionData
{
	float focusDistance;
	float focusRange;
	float nearRange;
	float farRange;

	float maxBlurRadius;	// 最大ボケ半径(ピクセル)
	int enable;				// 0 でそのまま通す
	float backdropBlur;		// 後ろのぼかしの掛かり具合(0〜1) : 重ねたシーンの後ろを、ピントに関係なく画面全体ぼかす
	float backdropRadius;	// 後ろのぼかしの半径(ピクセル)
};

#endif
