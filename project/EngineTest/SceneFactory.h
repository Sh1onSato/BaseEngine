#pragma once
#include "AbstractSceneFactory.h"

/// <summary>
/// 具体的なシーン生成工場
/// </summary>
class SceneFactory : public AbstractSceneFactory {
public:
    /// <summary>
    /// シーン生成
    /// </summary>
    /// <param name="sceneName">シーン名</param>
    /// <returns>生成されたシーン</returns>
    BaseScene* CreateScene(const std::string& sceneName) override;
};
