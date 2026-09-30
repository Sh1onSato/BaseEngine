# C++ / DX12 コーディングルール

## 命名規則
- クラス・構造体・enum: `PascalCase`（例: `DirectXCommon`, `SrvManager`）
- 関数・メソッド: `PascalCase`（例: `CreateDescriptorHeap()`, `PreDraw()`）
- メンバ変数: プレフィックスなし `camelCase`（例: `device`, `commandQueue`）— `m_` は付けない
- 定数: `kPascalCase`（例: `kMaxSrvCount`）— 新規は `k` 小文字始まりに統一
- ローカル変数: `camelCase`
- `using namespace` は `.cpp` 内のみ許可、ヘッダでは使わない

## 基本方針
- C++20。`auto`・構造化束縛・`std::span` を積極活用
- 所有権は `unique_ptr` / `shared_ptr`、DX12オブジェクトは `ComPtr`
- 生の `new` / `delete` を新規コードに書かない
- 1関数50行超えたら分割を検討

## エラーハンドリング
- `HRESULT` は必ず確認。`FAILED(hr)` を無視しない
- `assert(SUCCEEDED(hr))` 単体禁止 — Releaseビルドで消えるため
  - 代わりに `if (FAILED(hr)) { ログ + 終了/エラー処理 }` を必ず併用
- デバイスロスト時は `GetDeviceRemovedReason()` でログを出す

## DX12固有
- コマンドアロケータはフレームまたはスレッドごとに分離、複数フレームで同時 `Reset` しない
- フェンス待ちは `SetEventOnCompletion` + `WaitForSingleObject`（ポーリング禁止）
- フレームレイテンシは定数化し、リソース確保数と一致させる
- ディスクリプタヒープのサイズ定数は用途別に分ける（`kMaxRtvCount` / `kMaxDsvCount` / `kMaxSrvCount`）
- スワップチェインと RTV の Format 不一致は意図的な場合のみコメントで明記
- ルートシグネチャのパラメータを追加する場合、既存インデックス（他クラスのバインド呼び出し箇所）を変更せず末尾に追加する。`Object3d.VS/PS.hlsl`を共用しているクラス（例: SpriteCommon）がないか確認し、あれば入力レイアウト・ルートパラメータの追従漏れがないようにする

## よくある落とし穴（レビュー時チェック）
- 同じ意味のマジックナンバーが複数箇所に散在 → 定数に集約
- メンバ関数が自分自身を引数で受け取る設計 → `this` を直接使うか設計見直し
- `CreatBufferResource`（e抜け）/ `Texure`（t抜け）などの typo
- SRV用サイズ定数を DSV/RTV ヒープに流用していないか
- シェーダー・ルートシグネチャを複数クラスで共用している場合、片方だけ変更して他方（ダミー宣言含む）への追従を忘れていないか
- シャドウパス等、メインの描画経路と別のコードパスがある機能（`DrawShadow()`等）に変更を追従させ忘れていないか
