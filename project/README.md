# GE3 / project（自作エンジン基礎化ブランチ）

## これは何か

本ディレクトリはブランチ分岐版のプロジェクトです。元々は DirectX12 自作エンジンベースの3D FPSゲーム（ボス戦まで実装済み）でしたが、ここから**ゲーム固有の実装を取り除き、他のゲームでも使い回せる「自作エンジンの基礎」として別データに切り出す**ことを目的に整理を進めています。

ゲーム側の進捗管理（`ClaudeLog/project.txt` 等）とは別に、このエンジン基礎化の経緯・状態はこの README にまとめます。

## 現在の状態（2026-09-30時点）

- `Engine\scene\TitleScene.h/.cpp` と `Engine\scene\GameScene.h/.cpp` を、`BaseScene` を継承した空の `Initialize/Finalize/Update/Draw` のみのスケルトンにリセット済み。
- 起動すると画面はクリアカラー（青、`Engine\base\DirectXCommon.cpp` で設定）のみが表示される状態。
- 以前の実装（FPSカメラ、当たり判定、レイキャスト射撃、パーティクル、ポストプロセス、1v1ボス戦AI、ポータル、UI等）は**削除しただけで失われてはおらず**、このブランチのgit履歴から参照・復元可能。

## エンジンとして残っている汎用部分

- **Framework** … WinApp / DirectXCommon / SrvManager / ImGuiManager を所有・一元管理
- **描画** … Object3d(3D) / Sprite(2D) / ParticleManager(インスタンシング) / PostProcess(オフスクリーン)
- **設計パターン** … Manager(保管) / Common(PSO・設定) / Standard Object(実体) の3階層
- **シーン管理** … SceneManager + Abstract Factory (SceneFactory) — `TitleScene`/`GameScene`はこの仕組みに乗った空シーンの実例として残置
- **数学** … Calculation.h に行列・ベクトル・当たり判定を集約
- その他、`SettingsMenu`/`GameSettings`（ImGui非依存の設定UI基盤）、`BitmapText`（内部生成ビットマップフォント）なども汎用部品として利用可能

これらは今回のシーン初期化処理では未使用ですが、クラス自体は無改修で残っています。

## 整理ログ

### 2026-09-30：汎用クラスに残っていたゲーム固有の値・マジックナンバーの整理
シーンを空にしただけでは、汎用クラス側にこのFPSゲーム専用の値・前提が残っていたため、`Explore`サブエージェントによる調査を2回行い、見つかった項目を対応した。

- **光源のデフォルト値を中立化**：`Object3dCommon`のPointLight/SpotLightの初期位置・色がこのゲームのマップ座標に固定されていた（intensityは0で無効化済みだったが「太陽光の影確認のため一時的に無効化」という開発中コメントが残置）ため、位置`{0,0,0}`・白色・intensity0のニュートラルな既定値に変更。呼び出し側が設定して有効化する想定に整理。
- **シャドウマップの正射影範囲をAPI化**：`Object3dCommon::SetShadowOrthoRange(lightDistance, orthoExtent, nearClip, farClip)`を新設。従来マジックナンバー(40/55/0.1/80)でC++・HLSL(`Object3d.PS.hlsl`)双方に手書きされ二重管理だった値を、`ShadowData`定数バッファ（C++/HLSL双方80byte、`static_assert`検証済み）経由で渡すよう変更。デフォルト値は従来と完全一致。
- **ディスクリプタヒープのサイズ定数を用途別に分離**：`DirectXCommon`がSRV用の`kMaxSrvCount`(512)をそのままRTV/DSVヒープにも流用していた（cpp-dx12ルール違反）のを、`kMaxRtvCount`(16)/`kMaxDsvCount`(8)に分離。あわせてSRV上限の二重定義（`DirectXCommon::kMaxSrvCount`と`SrvManager::kMaxSRVCount`）だった`DirectXCommon`側は削除し、`SrvManager::kMaxSRVCount`に一本化。
- **クリアカラーの重複解消**：`DirectXCommon`と`PostProcess`に別々にベタ書きされていた背景色`{0.1,0.25,0.5,1.0}`を、`DirectXCommon::kClearColor`に一本化して両方から参照。
- **円周率・固定デルタタイムの集約**：`Calculation.cpp`/`Object3dCommon.cpp`/`ParticleManager.cpp`/`ParticleEmitter.cpp`にバラバラの桁数で直書きされていたPIを`Calculation::kPi`（`std::numbers::pi_v<float>`）に統一。「暫定60FPS」も同様に2箇所へ重複していたのを`Calculation::kFixedDeltaTime`に集約（実フレーム時間を受け取る設計への変更は今回は見送り、将来の課題）。
- **パーティクルの線パラメータのデフォルト値重複を解消**：`lineWidth`/`lineLengthMultiplier`/`maxLineLength`が`Particle`/`ParticleEmitParams`/`EmitterSetting`の3構造体に同じ値で重複していたのを`ParticleLineDefaults`名前空間に集約。
- **Camera/Skyboxのデフォルト値見直し**：`fovY`が0.45rad(約26度、狭すぎ)だったのを60度に、`farZ`が100だったのをSkyboxの既定スケール(500)より外側になるよう1000に変更。`aspectRatio`・`Sprite::screenResolution`は`WinApp::KclientWidth/KclientHeight`参照に統一（1280/720のベタ書きが4箇所に分散、うち`Calculation`内の`kColumnWidth`/`kRowHeight`は完全な未使用デッドコードだったため削除）。
- **命名の中立化**：`WinApp`のウィンドウタイトル/クラス名（さらに前の課題名"CG2"の残骸）を汎用的な名称に変更。`PostProcess`の`TriggerGrayscaleFlash()`/`TriggerDamageVignette()`を、FPS前提の命名（ワープ・被弾）から`TriggerFlash()`/`TriggerImpactVignette()`に変更（機構自体は無改修）。
- **未使用アセット23ファイルを削除**：`GameScene`/`TitleScene`が空になったことでプロジェクト全体から参照されなくなっていたテクスチャ・モデル(obj/mtl/blend)・音声・孤立シェーダー(`Particle_VS.hlsl`)を削除（`Resources\cube`/`Resources\skybox`の空フォルダも削除）。
- **`BitmapText`をフルアルファベット対応に拡張**：数字＋一部英字のみだったグリフを、`TextureManager::kBitmapFontGlyphs`という1箇所のデータテーブル（外部ファイル化はせずコード内に一覧化）にリファクタリングした上でA-Z全26文字に拡張。既存23グリフのインデックスは後方互換のため不変。
- **対応を見送った項目**（今回は据え置き、必要になったら再検討）：`GameSettings`（マウス感度・目線の高さのみのFPS特化した設定コンテナ）の汎用化、シャドウマップ解像度4096のC++/HLSL手動同期解消（HLSLの制約上cbuffer化が難しく、コメントでの相互参照に留めた）。

### 2026-09-30：球vsAABB当たり判定ロジックをCalculationへ汎用化・復元
エンジンとしての機能棚卸し（後述）の結果、CLAUDE.mdに「球 vs AABB スライディングコリジョン ✅」とあった実装は、実は`Calculation.h`側の汎用ユーティリティではなく**`GameScene.cpp`内のゲーム固有コード**（`CheckCollision()`）だったことが判明。今回のシーンリセットで一度失われかけていたため、コミットされていないgit履歴（`git show HEAD:...`、当時はまだ何もcommitしていなかったため直前のHEADにオリジナルが残っていた）から元のロジックを復元し、`Calculation`クラスに汎用関数として抽出した。

- `Calculation::TestSphereAABB(const Sphere& sphere, const AABB& aabb, Vector3* outPushDir = nullptr)`：最近接点法による球vsAABB判定。元の`GameScene::CheckCollision`内の中核計算（最近接点の算出→距離判定→押し出し方向の正規化）をそのまま抽出し、複数コライダーのループやプレイヤー特有の段差乗り越え・接地判定などゲーム固有の周辺ロジックは含めていない（これらは今後ゲーム側で組む前提）。
- `Calculation::MakeAABBFromTransform(const Transform& transform)`：`Transform.translate`を中心・`Transform.scale`を半辺長とみなしてAABBを作る（元の`GameScene::GetAABB(Object3d&)`と同じ変換だが、`Calculation`（数学レイヤー）が`Object3d`（描画レイヤー）に依存しないよう`Transform`受け取りに一般化）。
- 移動処理のX→Z→Y軸別スライド判定・段差乗り越え・接地スナップ等の「キャラクターコントローラー」的な組み立て方はゲームごとに要件が変わるため、あえて汎用化・復元はしていない。次のゲームでプレイヤー移動を組む際は、この2関数を組み合わせて実装する想定（過去の実装パターンはgit履歴の旧`GameScene.cpp`にも残っている）。

### 2026-09-30：スケルタルアニメーションの実装（フェーズ1〜3完了）
機能棚卸しで「.objのみ対応・ボーン非対応」と判明したため、CPU側でジョイント階層を計算しGPU頂点シェーダーでスキニングする標準方式を新規実装した。`/plan`でフェーズ分割し、各フェーズをユーザーのビルド確認を挟みながら順に実装。

- **フェーズ1（描画パイプラインの土台）**：`VertexData`に`boneWeights`/`boneIndices`を追加（デフォルト値付きで既存初期化箇所は無修正）。`Object3dCommon`/`Object3d`/`SpriteCommon`のルートシグネチャにボーン行列パレット用のSRV(t2)を追加し、`Object3d.VS.hlsl`/`Shadow.VS.hlsl`両方にスキニング処理を実装。非スキニングオブジェクトはデフォルト単位行列パレットにフォールバックするため既存描画は無改修で動作。外部ファイル不要の2ジョイント検証シーン(`SkinningTestScene`)で動作確認。
- **フェーズ2（自前JSON/glTFパーサ）**：外部ライブラリ(nlohmann/json等)を正確に取得する手段が無かったため、`Model::LoadObjFile`と同じ「自前で書く」スタイルで最小限のJSONパーサ(`JsonValue`/`JsonParser`)とglTFローダー(`GltfLoader`)を新規実装（1メッシュ/1スキン/1アニメーションのみ対応、`.glb`非対応と範囲を明確に限定）。テスト用glTFはPowerShellスクリプト(`Resources\gltf\GenerateTestArm.ps1`)で自動生成。
  - **見つけたバグ**：glTF(列優先・列ベクトル規約)→本エンジン(行優先・行ベクトル規約)への逆バインド行列の変換で、転置の方向を誤り平行移動成分が消える不具合をコードレビューで発見・修正（テストデータが単位行列だったため実行時には表面化しない種類のバグだった）。
- **フェーズ3（アニメーション再生システム）**：`Skeleton`(ジョイント階層・毎フレーム再計算)/`Animation`(キーフレーム補間、Slerp/Lerp)/`SkinCluster`(逆バインド行列×現在姿勢のパレット生成)/`SkinnedObject3d`(`Object3d`継承)を実装。`ModelManager::LoadGltfModel()`でキャッシュ。アニメーションされないチャンネルは汎用初期値ではなく「そのノード本来のレストポーズ」にフォールバックする設計（実装時に静的オフセットが失われる不具合を自己修正）。
- ユーザーのビルド・実機確認OK（2ジョイントの棒がglTFアニメーション駆動で滑らかに曲がる、影も同期）。実在するリギング済みモデルでの検証は未実施（テストは自動生成した簡易リグのみ）。

## エンジンとしての機能棚卸し（2026-09-30、他ゲームへの転用可否を確認）

**しっかり揃っている**：シャドウマッピング(PCSSソフトシャドウ、範囲は外部設定可能)、ポストプロセス一式(ブルーム/グレースケール/セピア/ぼかし/ヴィネット)、パーティクル(点/線/面)、3灯ライティング、複数カメラ管理、キーボード・マウス・ゲームパッド(XInput)入力、サウンド基盤(`Engine/audio`、XAudio2+Media Foundation、WAV/MP3対応だが呼び出し実績なし)、**スケルタルアニメーション**（自前glTFローダー+GPUスキニング、簡易リグでのみ検証済み。上記参照）。

**足りない／今後ゲームごとに要検討**：
- glTFローダーの対応範囲が限定的（1メッシュ/1スキン/1アニメーションのみ、`.glb`非対応、morph target非対応）→ 複雑なモデルやマルチアニメーションが必要なら拡張が要る
- アニメーションブレンド・状態遷移（歩く→走る等の遷移）は未実装
- 汎用当たり判定は上記の球vsAABB・レイキャストのみ（AABB-AABB、球vs球等は無し）
- サンプル/テンプレートシーンが無い（`GameScene`/`TitleScene`が空のため、新規ゲームは白紙から組む必要あり）
- UIにクリック判定・汎用レイアウトが無い（`SettingsMenu`は増減式の設定項目専用）
- レベルデータの外部ファイル化なし（配置は全てC++コードにハードコードする前提）
- `TextureManager`/`ModelManager`にリソースのUnload機構がなく、シーン往復が多いゲームではVRAM蓄積に注意
- セーブ/ロード（ファイル永続化）なし
- ウィンドウリサイズ・複数ウィンドウ非対応（優先度低）

## 今後の方針

- 新規実装・整理の際は「エンジン側（汎用機能）」と「ゲーム固有ロジック」の境界を混ぜない。
- ゲーム固有の値・依存を汎用クラス側に埋め込まず、定数化・パラメータ化・インターフェース分離で外から渡す。
- 別ゲームでの再利用を見据えた設計判断を優先する。

## 新しいゲームを始めるときは

`EngineTemplate/`フォルダに、Claude Codeとの運用ルール（`CLAUDE.md`）とコーディング規約（`.claude/rules/cpp-dx12.md`）を汎用化したテンプレート一式を用意してある。新規ゲームのリポジトリを作る際はこのフォルダの中身をコピーして使う（詳細は`EngineTemplate/README.md`参照）。

## 関連ドキュメント

- `ClaudeLog/codeLog.txt` … 各クラスの役割・使い方の解説（初心者向け、追記のみ）
- `ClaudeLog/collisionLog.txt` … 当たり判定の試行錯誤の記録（追記のみ）
- `ClaudeLog/project.txt` … **このリポジトリがゲームだった当時の**進捗管理ファイル（過去の実装経緯の記録として維持。エンジン基礎化に関する記録はこのREADMEに集約し、project.txtへの追記は行わない）
