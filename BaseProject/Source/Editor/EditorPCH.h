#pragma once

//==========================================================================================
//
// EditorPCH
//
// エディター配下(Source/Editor)だけが使うプリコンパイル済みヘッダー。
// トップの Pch.h に Engine / App の共通と、エディターだけが使う ImGui 一式・内部の共通部品を足したもの。
// エディター配下の .cpp には、共通の Pch.h の代わりにこれが強制インクルードされる
// (設定は vcxproj。配下に .cpp を足したら、Pch ではなくこちらを使うようにすること。
//  付け忘れはビルド時の CheckEditorPch で止まる)。
//
// ImGui をエディターの外へ漏らさないために、共通の Pch.h からは ImGui を外してある。
// エディターの外から読まれるヘッダー(Editor.h / Editor/EditorCommon.h)は、ここに頼らずに書くこと。
// (EffectEditor.h / PrefabEditor.h はエディターの中だけで読む。ScenePreview と Popup を値で持つ)
// 編集UIの窓口 EditorField は Engine/EditorField に宣言があり、実装だけが Helper/EditorField.cpp にある。
//
//==========================================================================================
#include "Pch.h"
#include "Engine/EngineCommon.h"
#include "Application/AppCommon.h"	// エディターは App の型(コンポーネント等)も扱う
#include "Editor/EditorCommon.h"

//---------------------------------------------------------
// ImGui
//
// バックエンド(imgui_impl_*)は初期化とフレーム開始を行う ImGuiContext.cpp だけで読む
//---------------------------------------------------------
#pragma warning(push, 0)
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_stdlib.h>
#include <imnodes.h>			// ノードエディタ
#include <imGuizmo.h>			// シーンビューのギズモ
#pragma warning(pop)

//---------------------------------------------------------
// エディター内部の共通部品
//---------------------------------------------------------
#include "Helper/EditorHelper.h"						// 検索欄・SRV表示・ノード部品・アセット一覧
#include "Widget/PopUp/Popup.h"							// モーダルなポップアップ
#include "Widget/PopUp/NameInputPopup/NameInputPopup.h"	// 名前を1つ打って決めるポップアップ
#include "Widget/PopUp/ConfirmPopup/ConfirmPopup.h"		// 押す前に確かめるポップアップ
