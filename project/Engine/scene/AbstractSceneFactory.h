#pragma once
#include "BaseScene.h"
#include <string>

/// <summary>
/// シーン生成の抽象基底クラス
/// </summary>
class AbstractSceneFactory {
public:
    virtual ~AbstractSceneFactory() = default;
    
    /// <summary>
    /// シーン生成
    /// </summary>
    /// <param name="sceneName">シーン名</param>
    /// <returns>生成されたシーン</returns>
    virtual BaseScene* CreateScene(const std::string& sceneName) = 0;
};
