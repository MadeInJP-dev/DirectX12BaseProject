#pragma once
//==========================================================================================
//
// Core
//
// Engine にも App にも依存しない道具箱(数学・文字列・ファイル・型情報・GUID・ログなど)。
// 依存の向きは Editor → App → Engine → Core。Core から上の層は見ない。
//
// Engine / App からは Core:: を付けずに使えるよう、それぞれの namespace へ取り込んである
// (Engine/EngineCommon.h・Application/AppCommon.h)。
// JSONHelper は nlohmann/json を要求するので、ここでは読まず使う側で読む
//
//==========================================================================================

// ---- 文字列・ファイル・型情報 ----
#include "Core/String/StringUtility.h"					// 文字列
#include "Core/File/FileUtility.h"						// ファイルパス
#include "Core/TypeInfo/TypeInfo.h"						// 型キー・型名(RTTIの代わり)

// ---- アルゴリズム ----
#include "Core/Algorithm/Graph/TopologicalSort.h"		// トポロジカルソート
#include "Core/Algorithm/Graph/GroupTopologicalSort.h"	// グループ分けトポロジカルソート

// ---- enum class のフラグ演算 ----
#include "Core/EnumFlags/EnumFlags.h"

// ---- デバッグ ----
#include "Core/Debug/DebugLog.h"						// ログ出力
#include "Core/Debug/Profile/Hitch/HitchReport.h"		// 遅いフレームの記録へ送る(ENGINE_HITCH_SCOPE)
#include "Core/Debug/Profile/Time/TimeProfileScope.h"	// スコープ計測(ENGINE_PROFILE_SCOPE)

// ---- 数学 ----
// 自作の数学型。ECSのコンポーネントはこちらで持つ(XMFLOAT系は使わない)。
// DirectXMath / SimpleMath とは暗黙に相互変換できるので、GPUへ渡す境界はそのまま書ける
#include "Core/Math/Alignment.h"						// アライメント
#include "Core/Math/Random.h"							// ランダム
#include "Core/Math/Vector/Vector2.h"					// Vector2
#include "Core/Math/Vector/Vector3.h"					// Vector3
#include "Core/Math/Vector/Vector4.h"					// Vector4
#include "Core/Math/Quaternion.h"						// クォータニオン
#include "Core/Math/Matrix.h"							// 行列
#include "Core/Math/Color.h"							// 色
#include "Core/Math/TRS.h"								// 行列の分解結果
#include "Core/Math/Ray.h"								// レイ
#include "Core/Math/DirectX/Math_DirectX.h"				// DirectXMath との橋渡し

// ---- 共通の型 ----
#include "Core/GUID/GUID.h"								// GUID
#include "Core/BinaryHelper/BinaryHelper.h"				// バイナリ読み書き
