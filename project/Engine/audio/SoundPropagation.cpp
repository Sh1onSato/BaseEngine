#include "SoundPropagation.h"
#include "Calculation.h"
#include "DebugDraw.h"
#include "GameTime.h"
#include "NavGrid.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <queue>

namespace {
    constexpr float kSqrt2 = 1.41421356f;
    // これより短いベクトルは向きが決まらないとみなす
    constexpr float kMinLength = 1.0e-4f;

    struct OpenEntry {
        float distance;
        int index;
        bool operator>(const OpenEntry& other) const { return distance > other.distance; }
    };
}

void SoundPropagation::SetGrid(const NavGrid* newGrid) {
    grid = newGrid;
    isDirty = true;
}

void SoundPropagation::SetSettings(const SoundPropagationSettings& newSettings) {
    settings = newSettings;
    isDirty = true; // maxDistance が変わると届くマスが変わる
}

void SoundPropagation::Update() {
    // この前の Update から Apply されなかった音（止まった音）の追いかけ中の値を消す
    for (auto it = smoothed.begin(); it != smoothed.end();) {
        if (!it->second.isUsed) {
            it = smoothed.erase(it);
        } else {
            it->second.isUsed = false;
            ++it;
        }
    }

    listenerPosition = AudioManager::GetInstance()->GetListenerPosition();
    if (!grid) {
        return;
    }
    // 耳のいるマス（壁の中や格子の外なら、近くの歩けるマス）
    int x = 0;
    int y = 0;
    int listenerCell = -1;
    if ((grid->WorldToCell(listenerPosition, x, y) && grid->IsWalkable(x, y)) || grid->FindNearestWalkable(listenerPosition, x, y)) {
        listenerCell = y * grid->GetWidth() + x;
    }
    const size_t cellCount = static_cast<size_t>(grid->GetWidth()) * static_cast<size_t>(grid->GetHeight());
    if (isDirty || listenerCell != builtListenerCell || cells.size() != cellCount) {
        const auto start = std::chrono::steady_clock::now();
        Rebuild(listenerCell);
        const auto end = std::chrono::steady_clock::now();
        lastBuildMilliseconds = std::chrono::duration<float, std::milli>(end - start).count();
        builtListenerCell = listenerCell;
        isDirty = false;
    }
}

void SoundPropagation::Rebuild(int listenerCell) {
    const int width = grid->GetWidth();
    const int height = grid->GetHeight();
    cells.assign(static_cast<size_t>(width) * static_cast<size_t>(height), Cell{});
    if (listenerCell < 0) {
        return;
    }
    const float maxCells = settings.maxDistance / grid->GetCellSize();
    const int listenerX = listenerCell % width;
    const int listenerY = listenerCell / width;

    // 耳のマスから近い順に全部のマスへ広げる（ダイクストラ法）。近い順に確定するので、親のマスは必ず先に確定している
    std::vector<uint8_t> isClosed(cells.size(), 0);
    std::priority_queue<OpenEntry, std::vector<OpenEntry>, std::greater<OpenEntry>> open;
    cells[listenerCell].distance = 0.0f;
    open.push({ 0.0f, listenerCell });

    static constexpr int kOffsets[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
    while (!open.empty()) {
        const int current = open.top().index;
        open.pop();
        if (isClosed[current]) {
            continue; // 同じマスを後から短く入れ直した古い分
        }
        isClosed[current] = 1;
        const int x = current % width;
        const int y = current / width;

        // 耳から見通せるマスは自分が「最後の曲がり角」。見通せなければ親（耳に近い側）の曲がり角を受け継ぐ
        Cell& cell = cells[current];
        cell.isVisible = grid->HasLineOfSight(listenerX, listenerY, x, y);
        cell.corner = (cell.isVisible || cell.parent < 0) ? current : cells[cell.parent].corner;

        for (int i = 0; i < 8; ++i) {
            const int nx = x + kOffsets[i][0];
            const int ny = y + kOffsets[i][1];
            if (!grid->IsWalkable(nx, ny)) {
                continue;
            }
            const bool isDiagonal = i >= 4;
            // 斜めは、横と縦の両方のマスが歩けるときだけ（壁の角をすり抜けない）
            if (isDiagonal && (!grid->IsWalkable(nx, y) || !grid->IsWalkable(x, ny))) {
                continue;
            }
            const int next = ny * width + nx;
            const float distance = cell.distance + (isDiagonal ? kSqrt2 : 1.0f);
            Cell& nextCell = cells[next];
            if (isClosed[next] || distance > maxCells || (nextCell.distance >= 0.0f && distance >= nextCell.distance)) {
                continue;
            }
            nextCell.distance = distance;
            nextCell.parent = current;
            open.push({ distance, next });
        }
    }
}

int SoundPropagation::FindSourceCell(const Vector3& sourcePosition) const {
    int x = 0;
    int y = 0;
    if ((grid->WorldToCell(sourcePosition, x, y) && grid->IsWalkable(x, y)) || grid->FindNearestWalkable(sourcePosition, x, y)) {
        return y * grid->GetWidth() + x;
    }
    return -1;
}

Vector3 SoundPropagation::CellToWorldAt(int cell, const Vector3& reference) const {
    Vector3 position = grid->CellToWorld(cell % grid->GetWidth(), cell / grid->GetWidth());
    // 格子の面の外の座標（3D なら高さ y、2D なら z）は音の物を使う
    if (grid->GetPlane() == NavPlane::kXZ) {
        position.y = reference.y;
    } else {
        position.z = reference.z;
    }
    return position;
}

SoundPathResult SoundPropagation::Evaluate(const Vector3& sourcePosition) const {
    SoundPathResult result;
    result.apparentPosition = sourcePosition;
    result.cornerPosition = sourcePosition;
    result.directDistance = Calculation::Length(sourcePosition - listenerPosition);
    result.pathDistance = result.directDistance;
    const size_t cellCount = grid ? static_cast<size_t>(grid->GetWidth()) * static_cast<size_t>(grid->GetHeight()) : 0;
    if (!grid || cells.size() != cellCount || builtListenerCell < 0) {
        return result; // 格子が無い・耳が格子の外: 普通の 3D の音のまま
    }
    const int sourceCell = FindSourceCell(sourcePosition);
    if (sourceCell < 0) {
        return result;
    }
    const Cell& cell = cells[sourceCell];
    // 壁越し: 向きは真っすぐのまま、壁の分だけ遠くで鳴っているように聞こえる
    const float throughWallDistance = result.directDistance + settings.wallPenaltyDistance;
    auto makeThroughWall = [&]() {
        result.kind = SoundPathResult::Kind::kThroughWall;
        result.cornerPosition = sourcePosition;
        result.pathDistance = throughWallDistance;
        result.occlusion = settings.throughWallOcclusion;
        if (result.directDistance > kMinLength) {
            result.apparentPosition = listenerPosition + (sourcePosition - listenerPosition) * (throughWallDistance / result.directDistance);
        }
        return result;
    };
    if (cell.distance < 0.0f) {
        return makeThroughWall(); // 道が無い（閉じた部屋・遠すぎる）
    }
    if (cell.isVisible) {
        return result; // 見通せる
    }

    // 耳 → 見える最後の曲がり角 は真っすぐ、そこから音までは格子の上の道の長さ
    const Vector3 corner = CellToWorldAt(cell.corner, sourcePosition);
    const float cellSize = grid->GetCellSize();
    const float pathDistance = (std::max)(Calculation::Length(corner - listenerPosition) +
                                          (cell.distance - cells[cell.corner].distance) * cellSize,
                                          result.directDistance);
    // 回り込む道が壁越しより遠いなら、壁越しに聞こえる方にする
    if (pathDistance >= throughWallDistance) {
        return makeThroughWall();
    }
    const float detour = pathDistance - result.directDistance;
    const float occlusion = (std::min)(settings.throughWallOcclusion,
                                       settings.cornerOcclusion + (1.0f - settings.cornerOcclusion) * detour /
                                                                      (std::max)(settings.detourForFullOcclusion, kMinLength));
    result.kind = SoundPathResult::Kind::kAround;
    result.cornerPosition = corner;
    result.pathDistance = pathDistance;
    result.occlusion = occlusion;
    // 曲がり角の方向に、道の長さだけ離れた所から聞こえる（左右は扉の向き、大きさは遠回りの長さで決まる）
    const Vector3 toCorner = corner - listenerPosition;
    const float toCornerLength = Calculation::Length(toCorner);
    if (toCornerLength > kMinLength) {
        result.apparentPosition = listenerPosition + toCorner * (pathDistance / toCornerLength);
    }
    return result;
}

SoundPathResult SoundPropagation::Apply(SoundHandle handle, const Vector3& sourcePosition) {
    const SoundPathResult result = Evaluate(sourcePosition);
    AudioManager* audio = AudioManager::GetInstance();
    if (!audio->IsPlaying(handle)) {
        // 一時停止中・終わった音は、次に鳴ったときに今の値から始める
        smoothed.erase(handle);
        audio->SetSoundPosition(handle, result.apparentPosition);
        audio->SetOcclusion(handle, result.occlusion);
        return result;
    }

    // 聞こえる位置は「耳からの向き」と「距離」に分けて追いかける（位置のまま混ぜると、左右が入れ替わる途中で耳の近くを通って大きくなる）
    const Vector3 offset = result.apparentPosition - listenerPosition;
    const float distance = Calculation::Length(offset);
    auto [it, isNew] = smoothed.try_emplace(handle);
    Smoothed& state = it->second;
    if (isNew) {
        state.position = offset;
        state.occlusion = result.occlusion;
    } else {
        const float t = 1.0f - std::exp(-settings.smoothingSpeed * Time::GetUnscaledDeltaTime());
        const float previousDistance = Calculation::Length(state.position);
        Vector3 direction = previousDistance > kMinLength ? state.position * (1.0f / previousDistance) : Vector3{ 0.0f, 0.0f, 0.0f };
        if (distance > kMinLength) {
            const Vector3 target = offset * (1.0f / distance);
            direction = direction + (target - direction) * t;
            const float length = Calculation::Length(direction);
            direction = length > kMinLength ? direction * (1.0f / length) : target;
        }
        const float smoothedDistance = previousDistance + (distance - previousDistance) * t;
        state.position = direction * smoothedDistance;
        state.occlusion += (result.occlusion - state.occlusion) * t;
    }
    state.isUsed = true;
    audio->SetSoundPosition(handle, listenerPosition + state.position);
    audio->SetOcclusion(handle, state.occlusion);
    return result;
}

void SoundPropagation::DrawDebug(const Vector3& sourcePosition) const {
    if (!DebugDraw::IsEnabled()) {
        return;
    }
    const SoundPathResult result = Evaluate(sourcePosition);
    switch (result.kind) {
    case SoundPathResult::Kind::kDirect:
        DebugDraw::Line(listenerPosition, sourcePosition, DebugDraw::kGreen, 0.0f, false);
        return;
    case SoundPathResult::Kind::kThroughWall:
        DebugDraw::Line(listenerPosition, sourcePosition, DebugDraw::kRed, 0.0f, false);
        return;
    case SoundPathResult::Kind::kAround:
        break;
    }
    // 耳 → 曲がり角 → （格子の道）→ 音
    DebugDraw::Line(listenerPosition, result.cornerPosition, DebugDraw::kYellow, 0.0f, false);
    const int sourceCell = FindSourceCell(sourcePosition);
    const int cornerCell = cells[sourceCell].corner;
    Vector3 previous = sourcePosition;
    for (int index = sourceCell; index >= 0 && index != cornerCell; index = cells[index].parent) {
        const Vector3 point = CellToWorldAt(index, sourcePosition);
        DebugDraw::Line(previous, point, DebugDraw::kYellow, 0.0f, false);
        previous = point;
    }
    DebugDraw::Line(previous, result.cornerPosition, DebugDraw::kYellow, 0.0f, false);
    // 聞こえる位置
    DebugDraw::Cross(result.apparentPosition, 0.5f, DebugDraw::kWhite, 0.0f, false);
}
