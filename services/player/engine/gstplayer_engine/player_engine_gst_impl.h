/*
 * Copyright (C) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PLAYER_ENGINE_GST_IMPL
#define PLAYER_ENGINE_GST_IMPL

#include <mutex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <map>

#include "i_player_engine.h"
#include "player_register.h"
#include "gst_player_build.h"
#include "gst_player_ctrl.h"

namespace OHOS {
namespace Media {
class PlayerEngineGstImpl : public IPlayerEngine {
public:
    PlayerEngineGstImpl();
    ~PlayerEngineGstImpl();

    int32_t SetSource(const std::string &uri) override;
    int32_t SetObs(const std::weak_ptr<IPlayerEngineObs> &obs) override;
    int32_t SetVideoSurface(sptr<Surface> surface) override;
    int32_t Prepare() override;
    int32_t PrepareAsync() override;
    int32_t Play() override;
    int32_t Pause() override;
    int32_t Stop() override;
    int32_t Reset() override;
    int32_t SetVolume(float leftVolume, float rightVolume) override;
    int32_t Seek(uint64_t mSeconds, int32_t mode) override;
    int32_t GetCurrentTime(uint64_t &currentTime) override;
    int32_t GetDuration(uint64_t &duration) override;
    int32_t SetPlaybackSpeed(int32_t mode) override;
    int32_t GetPlaybackSpeed(int32_t &mode) override;
    int32_t SetLooping(bool loop) override;

private:
    double ChangeModeToSpeed(const int32_t &mode) const;
    int32_t ChangeSpeedToMode(double rate) const;
    int32_t GstPlayerInit();
    int32_t GstPlayerPrepare();
    void PlayerLoop();
    void Synchronize();
    void GstPlayerDeInit();

private:
    std::mutex mutex_;
    std::unique_ptr<GstPlayerBuild> playerBuild_ = nullptr;
    std::shared_ptr<GstPlayerCtrl> playerCtrl_ = nullptr;
    std::weak_ptr<IPlayerEngineObs> obs_;
    sptr<Surface> producerSurface_ = nullptr;
    std::string fileUri_ = "";

    std::condition_variable condVarSync_;
    std::mutex mutexSync_;
    bool gstPlayerInit_ = false;
    std::unique_ptr<std::thread> playerThread_;
};

class GstPlayerFactoryMaker : public PlayerFactoryMake {
public:
    GstPlayerFactoryMaker() = default;
    ~GstPlayerFactoryMaker() = default;
    int Score(std::string uri) const override;
    IPlayerEngine *CreatePlayer() const override;
};
} // Media
} // OHOS

#endif // PLAYER_ENGINE_GST_IMPL