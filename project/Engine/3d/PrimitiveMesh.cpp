#include "PrimitiveMesh.h"
#include "Calculation.h"
#include <cmath>
#include <numbers>

namespace {
    constexpr float kPi = std::numbers::pi_v<float>;

    VertexData MakeVertex(const Vector3& position, const Vector2& texcoord, const Vector3& normal) {
        // boneWeights / boneIndices は既定値（{1,0,0,0} / 0）のまま = ボーン0（単位行列）で変形しない
        return VertexData{ { position.x, position.y, position.z, 1.0f }, texcoord, normal };
    }

    // 三角形を1枚追加する。描画は「時計回りが表」なので、表が outward（外側）を向くよう必要なら順番を入れ替える
    // （この数学ライブラリの左手座標系では、Cross(b - a, c - a) が表の向きになる）
    void AddTriangle(std::vector<VertexData>& vertices, const VertexData& a, const VertexData& b, const VertexData& c,
                     const Vector3& outward) {
        Vector3 pa = { a.position.x, a.position.y, a.position.z };
        Vector3 pb = { b.position.x, b.position.y, b.position.z };
        Vector3 pc = { c.position.x, c.position.y, c.position.z };
        Vector3 faceNormal = Calculation::Cross(pb - pa, pc - pa);
        vertices.push_back(a);
        if (Calculation::Dot(faceNormal, outward) >= 0.0f) {
            vertices.push_back(b);
            vertices.push_back(c);
        } else {
            vertices.push_back(c);
            vertices.push_back(b);
        }
    }

    // 立方体の1面（normal の向きの面。u, v は面に沿った2方向）
    void AddCubeFace(std::vector<VertexData>& vertices, const Vector3& normal, const Vector3& u, const Vector3& v) {
        const Vector3 center = normal * 0.5f;
        const Vector3 halfU = u * 0.5f;
        const Vector3 halfV = v * 0.5f;
        VertexData p0 = MakeVertex(center - halfU - halfV, { 0.0f, 1.0f }, normal);
        VertexData p1 = MakeVertex(center + halfU - halfV, { 1.0f, 1.0f }, normal);
        VertexData p2 = MakeVertex(center + halfU + halfV, { 1.0f, 0.0f }, normal);
        VertexData p3 = MakeVertex(center - halfU + halfV, { 0.0f, 0.0f }, normal);
        AddTriangle(vertices, p0, p1, p2, normal);
        AddTriangle(vertices, p0, p2, p3, normal);
    }

    // 球面上の点（stack: 上から何段目、slice: 何列目）
    VertexData MakeSphereVertex(int stack, int slice, int stacks, int slices) {
        float theta = kPi * static_cast<float>(stack) / static_cast<float>(stacks);       // 0 = 真上
        float phi = 2.0f * kPi * static_cast<float>(slice) / static_cast<float>(slices);
        Vector3 normal = { std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi) };
        Vector2 texcoord = { static_cast<float>(slice) / slices, static_cast<float>(stack) / stacks };
        return MakeVertex(normal * 0.5f, texcoord, normal);
    }

    Vector3 ToVector3(const VertexData& vertex) {
        return { vertex.position.x, vertex.position.y, vertex.position.z };
    }
}

namespace PrimitiveMesh {

std::vector<VertexData> BuildCube() {
    std::vector<VertexData> vertices;
    vertices.reserve(36);
    AddCubeFace(vertices, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f });
    AddCubeFace(vertices, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f });
    AddCubeFace(vertices, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f });
    AddCubeFace(vertices, { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f });
    AddCubeFace(vertices, { 0.0f, 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
    AddCubeFace(vertices, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
    return vertices;
}

std::vector<VertexData> BuildSphere(int slices, int stacks) {
    std::vector<VertexData> vertices;
    vertices.reserve(static_cast<size_t>(slices) * stacks * 6);
    for (int stack = 0; stack < stacks; ++stack) {
        for (int slice = 0; slice < slices; ++slice) {
            // 4つの角で四角を作り、三角形2枚に分ける（極の近くでは1枚がつぶれるが、描画されないだけで問題ない）
            VertexData topLeft = MakeSphereVertex(stack, slice, stacks, slices);
            VertexData topRight = MakeSphereVertex(stack, slice + 1, stacks, slices);
            VertexData bottomRight = MakeSphereVertex(stack + 1, slice + 1, stacks, slices);
            VertexData bottomLeft = MakeSphereVertex(stack + 1, slice, stacks, slices);
            // 球の中心が原点なので、三角形の重心の向き = 外側
            Vector3 outward1 = ToVector3(topLeft) + ToVector3(topRight) + ToVector3(bottomRight);
            Vector3 outward2 = ToVector3(topLeft) + ToVector3(bottomRight) + ToVector3(bottomLeft);
            AddTriangle(vertices, topLeft, topRight, bottomRight, outward1);
            AddTriangle(vertices, topLeft, bottomRight, bottomLeft, outward2);
        }
    }
    return vertices;
}

}
