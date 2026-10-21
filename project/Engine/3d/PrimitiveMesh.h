#pragma once
#include "Structs.h"
#include <vector>

/// <summary>
/// 立方体・球など、ファイルを使わずに作れる形の頂点データ。
/// Model::InitializeFromVertices に渡して使う（例: model->InitializeFromVertices(dxCommon, PrimitiveMesh::BuildCube(), "white")）。
/// </summary>
namespace PrimitiveMesh {
    // 中心が原点・1辺1の立方体（36頂点。面ごとの法線とUV付き）
    std::vector<VertexData> BuildCube();
    // 中心が原点・半径0.5の球（slices: 経度方向の分割数、stacks: 緯度方向の分割数）
    std::vector<VertexData> BuildSphere(int slices, int stacks);
}
