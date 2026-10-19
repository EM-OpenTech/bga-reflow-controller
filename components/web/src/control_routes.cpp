/*
 * SPDX-FileCopyrightText: 2026 EM-OpenTech
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file control_routes.cpp
 * @brief Implementation of process control and hardware override REST route handlers.
 *
 * @copyright Copyright (C) 2026 EM-OpenTech, AGPL-3.0-or-later
 * @see https://github.com/EM-OpenTech/bga-reflow-controller
 */

#include "web/control_routes.hpp"
#include "cJSON.h"
#include "esp_log.h"
#include "system_context.hpp"
#include "web/api_context.hpp"
#include "web/fsm_command_queue.hpp"
#include <cmath>
#include <memory>
#include <string>

namespace web {

namespace {

constexpr const char* TAG = "ControlRoutes";

using app::fsmCmdPost;
using app::FsmCommand;
using app::FsmCommandType;

bool isSafetyCheckActive(const ApiContext& ctx) {
    return (ctx.settings == nullptr || (ctx.settings->enableSafetyWatchdog && !ctx.settings->simulationMode));
}

bool postOrFail(httpd_req_t* req, const FsmCommand& cmd, const char* actionName) {
    if (!fsmCmdPost(cmd)) {
        ESP_LOGE(TAG, "CMD queue full – '%s' dropped!", actionName);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Command queue full");
        return false;
    }
    return true;
}

esp_err_t handlePreheat(httpd_req_t* req, cJSON* root, const ApiContext& ctx) {
    if (ctx.fsm != nullptr) {
        auto st = ctx.fsm->getState();
        if (st != fsm::ReflowState::IDLE && st != fsm::ReflowState::DONE && st != fsm::ReflowState::COOLING) {
            ESP_LOGW(TAG, "PREHEAT rejected: not permitted in state %s", ctx.fsm->getStateString());
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden: Preheat only allowed in IDLE, DONE or COOLING");
            return ESP_OK;
        }
    }

    if (isSafetyCheckActive(ctx) && ctx.context != nullptr) {
        auto snap = ctx.context->getSnapshot();
        if (!snap.topSensorOk || !snap.bottomSensorOk) {
            ESP_LOGW(TAG, "PREHEAT rejected: Active sensor fault (top=%d, bot=%d)", snap.topSensorOk,
                     snap.bottomSensorOk);
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                                "Forbidden: Cannot start preheat while sensor fault is active");
            return ESP_OK;
        }
    }

    cJSON* profileItem = cJSON_GetObjectItem(root, "profile");
    std::string profName = (profileItem && profileItem->valuestring) ? profileItem->valuestring : "";
    if (profName.empty() && ctx.context != nullptr) {
        profName = ctx.context->getSnapshot().activeProfileFile;
    }
    if (profName.empty()) {
        ESP_LOGW(TAG, "PREHEAT rejected: No profile selected");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No profile selected");
        return ESP_OK;
    }

    // Load profile on Core 0, transfer ownership to Core 1 via pointer queue
    auto prof = std::make_unique<config::ReflowProfile>();
    if (ctx.storage == nullptr || !ctx.storage->loadProfile(profName, *prof)) {
        ESP_LOGW(TAG, "PREHEAT: profile '%s' not found", profName.c_str());
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Profile not found");
        return ESP_OK;
    }

    FsmCommand cmd{};
    cmd.type = FsmCommandType::PREHEAT;
    cmd.profile = prof.get();
    if (!fsmCmdPost(cmd)) {
        ESP_LOGE(TAG, "PREHEAT command queue full – profile freed");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Command queue full");
        return ESP_OK;
    }
    auto* released = prof.release();
    (void)released;

    ESP_LOGI(TAG, "CMD queued: PREHEAT profile '%s'", profName.c_str());
    return ApiContext::sendJsonSuccess(req);
}

esp_err_t handleReflow(httpd_req_t* req, const ApiContext& ctx) {
    if (ctx.fsm != nullptr) {
        auto st = ctx.fsm->getState();
        if (st != fsm::ReflowState::PREHEAT) {
            ESP_LOGW(TAG, "REFLOW rejected: not permitted in state %s", ctx.fsm->getStateString());
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden: Reflow only permitted during PREHEAT");
            return ESP_OK;
        }
    }

    if (isSafetyCheckActive(ctx) && ctx.context != nullptr) {
        auto snap = ctx.context->getSnapshot();
        if (!snap.topSensorOk || !snap.bottomSensorOk) {
            ESP_LOGW(TAG, "REFLOW rejected: Active sensor fault (top=%d, bot=%d)", snap.topSensorOk,
                     snap.bottomSensorOk);
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                                "Forbidden: Cannot start reflow while sensor fault is active");
            return ESP_OK;
        }
    }

    FsmCommand cmd{};
    cmd.type = FsmCommandType::REFLOW;
    if (!postOrFail(req, cmd, "reflow")) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "CMD queued: REFLOW");
    return ApiContext::sendJsonSuccess(req);
}

esp_err_t handleStop(httpd_req_t* req, const ApiContext& ctx) {
    const bool isFault = (ctx.fsm != nullptr && ctx.fsm->getState() == fsm::ReflowState::FAULT);
    FsmCommand cmd{};
    cmd.type = isFault ? FsmCommandType::RESET_FAULT : FsmCommandType::STOP;
    const char* actionName = isFault ? "resetFault (stop)" : "stop";

    if (!postOrFail(req, cmd, actionName)) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "CMD queued: %s", isFault ? "RESET_FAULT (via Stop in FAULT state)" : "STOP");
    return ApiContext::sendJsonSuccess(req);
}

esp_err_t handleAutotune(httpd_req_t* req, cJSON* root, const ApiContext& ctx) {
    if (ctx.fsm != nullptr) {
        auto st = ctx.fsm->getState();
        if (st != fsm::ReflowState::IDLE && st != fsm::ReflowState::DONE) {
            ESP_LOGW(TAG, "AUTOTUNE rejected: not permitted in state %s", ctx.fsm->getStateString());
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden: Autotune only permitted in IDLE or DONE");
            return ESP_OK;
        }
    }

    std::string channel = "top";
    cJSON* chJson = cJSON_GetObjectItem(root, "channel");
    if (chJson && chJson->valuestring) {
        channel = chJson->valuestring;
    }
    if (channel != "top" && channel != "bottom") {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid autotune channel (must be 'top' or 'bottom')");
        return ESP_OK;
    }
    bool isTop = (channel == "top");

    if (isSafetyCheckActive(ctx) && ctx.context != nullptr) {
        auto snap = ctx.context->getSnapshot();
        bool channelSensorOk = isTop ? snap.topSensorOk : snap.bottomSensorOk;
        if (!channelSensorOk) {
            ESP_LOGW(TAG, "AUTOTUNE rejected: Channel %s sensor fault active", isTop ? "TOP" : "BOTTOM");
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                                "Forbidden: Cannot start autotune with faulted channel sensor");
            return ESP_OK;
        }
    }

    float targetTemp = 150.0f;
    cJSON* tempJson = cJSON_GetObjectItem(root, "targetTemp");
    if ((tempJson != nullptr) && (cJSON_IsNumber(tempJson) != 0)) {
        targetTemp = static_cast<float>(tempJson->valuedouble);
    }
    if (std::isnan(targetTemp) || std::isinf(targetTemp) || targetTemp < config::Limits::MIN_TEMPERATURE ||
        targetTemp > config::Limits::MAX_TEMPERATURE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Autotune target temperature out of range (30..300°C)");
        return ESP_OK;
    }

    FsmCommand cmd{};
    cmd.type = FsmCommandType::AUTOTUNE_START;
    cmd.tuneIsTop = isTop;
    cmd.tuneTargetTemp = targetTemp;
    if (!postOrFail(req, cmd, "autotune")) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "CMD queued: AUTOTUNE_START (channel=%s, targetTemp=%.1f°C)", isTop ? "TOP" : "BOTTOM",
             static_cast<double>(targetTemp));
    return ApiContext::sendJsonSuccess(req);
}

esp_err_t handleSkip(httpd_req_t* req, const ApiContext& ctx) {
    if (ctx.fsm != nullptr) {
        auto st = ctx.fsm->getState();
        if (st != fsm::ReflowState::PREHEAT && st != fsm::ReflowState::SOAK && st != fsm::ReflowState::REFLOW) {
            ESP_LOGW(TAG, "SKIP rejected: not permitted in state %s", ctx.fsm->getStateString());
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden: Skip only permitted during active profile steps");
            return ESP_OK;
        }
    }

    FsmCommand cmd{};
    cmd.type = FsmCommandType::SKIP_STEP;
    if (!postOrFail(req, cmd, "skip")) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "CMD queued: SKIP_STEP");
    return ApiContext::sendJsonSuccess(req);
}

esp_err_t handleResetFault(httpd_req_t* req) {
    FsmCommand cmd{};
    cmd.type = FsmCommandType::RESET_FAULT;
    if (!postOrFail(req, cmd, "resetFault")) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "CMD queued: RESET_FAULT");
    return ApiContext::sendJsonSuccess(req);
}

esp_err_t handleEnterBackup(httpd_req_t* req, const ApiContext& ctx) {
    if (ctx.fsm != nullptr) {
        auto st = ctx.fsm->getState();
        if (st != fsm::ReflowState::IDLE && st != fsm::ReflowState::DONE && st != fsm::ReflowState::BACKUP) {
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                                "Forbidden: Cannot enter backup state while process is active");
            return ESP_OK;
        }
    }

    FsmCommand cmd{};
    cmd.type = FsmCommandType::ENTER_BACKUP;
    if (!postOrFail(req, cmd, "enterBackup")) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "CMD queued: ENTER_BACKUP");
    return ApiContext::sendJsonSuccess(req);
}

esp_err_t handleExitBackup(httpd_req_t* req) {
    FsmCommand cmd{};
    cmd.type = FsmCommandType::EXIT_BACKUP;
    if (!postOrFail(req, cmd, "exitBackup")) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "CMD queued: EXIT_BACKUP");
    return ApiContext::sendJsonSuccess(req);
}

} // anonymous namespace

esp_err_t ControlRoutes::postControlHandler(httpd_req_t* req) {
    const auto& ctx = ApiContext::instance();
    if (ctx.fsm == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "FSM not ready");
        return ESP_OK;
    }

    std::string body = ApiContext::readRequestBody(req);
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    cJSON* actionItem = cJSON_GetObjectItem(root, "action");
    if (!actionItem || !actionItem->valuestring) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing action");
        return ESP_OK;
    }

    std::string action = actionItem->valuestring;
    ESP_LOGI(TAG, "REST API: Control action '%s' received", action.c_str());

    esp_err_t ret = ESP_OK;
    if (action == "preheat") {
        ret = handlePreheat(req, root, ctx);
    } else if (action == "reflow") {
        ret = handleReflow(req, ctx);
    } else if (action == "stop" || action == "stopAutotune") {
        ret = handleStop(req, ctx);
    } else if (action == "autotune") {
        ret = handleAutotune(req, root, ctx);
    } else if (action == "skip") {
        ret = handleSkip(req, ctx);
    } else if (action == "resetFault") {
        ret = handleResetFault(req);
    } else if (action == "enterBackup") {
        ret = handleEnterBackup(req, ctx);
    } else if (action == "exitBackup") {
        ret = handleExitBackup(req);
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown action");
    }

    cJSON_Delete(root);
    return ret;
}

esp_err_t ControlRoutes::postOverridesHandler(httpd_req_t* req) {
    const auto& ctx = ApiContext::instance();
    if (ctx.fsm == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "FSM not ready");
        return ESP_OK;
    }

    std::string body = ApiContext::readRequestBody(req);
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    cJSON* fan = cJSON_GetObjectItem(root, "fan");
    cJSON* lamp = cJSON_GetObjectItem(root, "lamp");
    if ((fan != nullptr) && (cJSON_IsBool(fan) != 0)) {
        bool fanOn = (cJSON_IsTrue(fan) != 0);
        ctx.fsm->setFanOverride(fanOn);
        ESP_LOGI(TAG, "REST Override: Fan set to %s", fanOn ? "ON" : "OFF");
    }
    if ((lamp != nullptr) && (cJSON_IsBool(lamp) != 0)) {
        bool lampOn = (cJSON_IsTrue(lamp) != 0);
        ctx.fsm->setLampOverride(lampOn);
        ESP_LOGI(TAG, "REST Override: Lamp set to %s", lampOn ? "ON" : "OFF");
    }
    cJSON_Delete(root);

    return ApiContext::sendJsonSuccess(req);
}

} // namespace web
